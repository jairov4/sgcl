//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "object_allocator.h"
#include "object_pool_allocator.h"
#include "stack_pointer_allocator.h"

#include <array>
#include <memory>

namespace sgcl::detail {
    // The part of a thread's record the collector reads; it outlives the thread until the collector drops it.
    struct ThreadData {
        ThreadData(BlockAllocator* b, StackPointerAllocator* s) noexcept
        : block_allocator(b)
        , stack_roots_allocator(s) {
        }
        std::unique_ptr<BlockAllocator> block_allocator;
        std::unique_ptr<StackPointerAllocator> stack_roots_allocator;
        std::atomic<bool> is_deleted = {false};
        bool is_used = {true};
        bool is_last_registered = {false};
        ThreadData* next = {nullptr};
        ThreadData* next_registered = {nullptr};
        std::atomic<Page*> pages = {nullptr};
        Page* last_page_registered = {nullptr};
    };

    // Trivially destructible: it outlives its own retirement so a late use is diagnosed, not a use-after-free.
    class Thread {
    public:
        using Data = ThreadData;

        // The managed allocation whose constructor runs on this thread (SPEC.md §4.3, §4.4).
        struct Construction {
            uintptr_t base = 0;
            size_t size = 0;
            ChildPointers* layout = nullptr;
        };

        class ConstructionGuard {
        public:
            ConstructionGuard(Thread& t, Construction c) noexcept
            : _thread(t)
            , _previous(t.construction) {
                _thread.construction = c;
            }

            ConstructionGuard(const ConstructionGuard&) = delete;
            ConstructionGuard& operator=(const ConstructionGuard&) = delete;

            ~ConstructionGuard() noexcept {
                _thread.construction = _previous;
            }

        private:
            Thread& _thread;
            const Construction _previous;
        };

        Thread(uintptr_t stack_low, uintptr_t stack_base) noexcept
        : stack_allocator(checked_new<StackPointerAllocator>(stack_low, stack_base))
        , _block_allocator(checked_new<BlockAllocator>())
        , _data(checked_new<Data>(_block_allocator, stack_allocator)) {
            auto& threads_data = process_state().threads_data;
            _data->next = threads_data.load(std::memory_order_acquire);
            while(!threads_data.compare_exchange_weak(_data->next, _data, std::memory_order_release, std::memory_order_relaxed));
        }

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        // At thread exit (the object itself stays, trivially destructible): the pool allocators return their
        // unused slots, then the record is published as deleted and the collector frees the rest of it. Any later
        // SGCL use on this thread is fatal (SPEC.md §5.5).
        void retire() noexcept {
            for (auto& a : _allocators) {
                delete a;
                a = nullptr;
            }
            dead = true;
            _data->is_deleted.store(true, std::memory_order_release);
        }

        template<class T>
        auto& alocator() noexcept {
            return _allocator<typename TypeInfo<T>::Allocator>();
        }

        ConstructionGuard use_construction(Construction c) noexcept {
            return {*this, c};
        }

        Construction construction;
        StackPointerAllocator* const stack_allocator;
        bool dead = false;

    private:
        BlockAllocator* const _block_allocator;
        std::array<ObjectAllocatorBase*, config::MaxTypesNumber> _allocators = {};
        Data* const _data;

        template<class Allocator>
        Allocator& _allocator() noexcept {
            auto& alocator = _allocators[_type_index<typename Allocator::ValueType>()];
            if (!alocator) {
                if constexpr(Allocator::IsPoolAllocator::value) {
                    alocator = checked_new<Allocator>(*_block_allocator, _data->pages);
                } else {
                    alocator = checked_new<Allocator>(_data->pages);
                }
            }
            return static_cast<Allocator&>(*alocator);
        }

        // Cached per image; the counter is process-wide, so types of different images never share a slot.
        template<class T>
        inline static unsigned _type_index() noexcept {
            static const unsigned index = _next_type_index();
            return index;
        }

        static unsigned _next_type_index() noexcept {
            auto index = process_state().type_counter.fetch_add(1, std::memory_order_relaxed);
            if (index >= config::MaxTypesNumber) {
                fatal("more managed types than SGCL_MAX_TYPES_NUMBER");
            }
            return index;
        }
    };

    // The Thread lives in the owner's TLS; a pointer to it is cached per image.
    inline Thread& current_thread() noexcept {
        thread_local Thread* thread = nullptr;
        if (!thread) [[unlikely]] {
            thread = &SGCL_STATE_SYMBOL(current_thread)();
        }
        if (thread->dead) [[unlikely]] {
            fatal("SGCL used on a thread after its record was retired at thread exit");
        }
        return *thread;
    }
}

namespace sgcl::detail {
    static_assert(std::is_trivially_destructible_v<Thread>);

    // Retires the thread's record when the thread exits.
    struct ThreadRetirer {
        Thread& thread;
        ~ThreadRetirer() noexcept {
            thread.retire();
        }
    };
}
