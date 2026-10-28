//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "object_allocator.h"
#include "object_pool_allocator.h"
#include "stack_pointer_allocator.h"

#include <cstdio>
#include <cstdlib>
#include <thread>

#if SGCL_LOG_PRINT_LEVEL >= 3
#include <iostream>
#endif

namespace sgcl::detail {
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
        RawPointer hazard_pointer = {nullptr};
    };

    class Thread {
    public:
        using Data = ThreadData;

        struct ChildPointers {
            uintptr_t base;
            detail::ChildPointers::Map* map;
        };

        struct ChildGuard {
            ChildGuard(const ChildGuard&) = delete;
            void operator=(const ChildGuard&) = delete;

            constexpr ChildGuard(Thread& t, ChildPointers cp) noexcept
            : thread(t)
            , old_pointers(t.child_pointers) {
                thread.child_pointers = cp;
            }

            ~ChildGuard() noexcept {
                thread.child_pointers = old_pointers;
            }

            Thread& thread;
            const ChildPointers old_pointers;
        };

        struct RangeGuard {
            RangeGuard(const RangeGuard&) = delete;
            void operator=(const RangeGuard&) = delete;

            constexpr RangeGuard(Thread& t, std::pair<uintptr_t, uintptr_t> ar) noexcept
            : thread(t)
            , old_alloc_range(t.alloc_range) {
                thread.alloc_range = ar;
            }

            ~RangeGuard() noexcept {
                thread.alloc_range = old_alloc_range;
            }

            Thread& thread;
            const std::pair<uintptr_t, uintptr_t> old_alloc_range;
        };

        Thread()
        : stack_allocator(new StackPointerAllocator)
        , _block_allocator(new BlockAllocator)
        , _data(new Data{_block_allocator, stack_allocator}) {
#if SGCL_LOG_PRINT_LEVEL >= 3
            std::cout << "[sgcl] start thread id: " << std::this_thread::get_id() << std::endl;
#endif
            auto& threads_data = process_state().threads_data;
            _data->next = threads_data.load(std::memory_order_acquire);
            while(!threads_data.compare_exchange_weak(_data->next, _data, std::memory_order_release, std::memory_order_relaxed));
        }

        ~Thread() noexcept {
            _data->is_deleted.store(true, std::memory_order_release);
#if !defined(SGCL_EXTERNAL_STATE)
            if (std::this_thread::get_id() == main_thread_id) {
                for (auto& a : _allocators) {
                    a.reset();
                }
                terminate_collector();
            }
#endif
#if SGCL_LOG_PRINT_LEVEL >= 3
            std::cout << "[sgcl] stop thread id: " << std::this_thread::get_id() << std::endl;
#endif
        }

        template<class T>
        auto& alocator() noexcept {
            return _allocator<typename TypeInfo<T>::Allocator>();
        }

        ChildGuard use_child_pointers(ChildPointers cp) noexcept {
            return {*this, cp};
        }

        RangeGuard use_alloc_range(std::pair<uintptr_t, uintptr_t> ar) noexcept {
            return {*this, ar};
        }

        void set_hazard_pointer(void* p) {
            _data->hazard_pointer.store(p, std::memory_order_release);
        }

        void clear_hazard_pointer() {
            _data->hazard_pointer.store(nullptr, std::memory_order_release);
        }

        ChildPointers child_pointers = {0, nullptr};
        StackPointerAllocator* const stack_allocator;
#if !defined(SGCL_EXTERNAL_STATE)
        inline static std::thread::id main_thread_id = {};
#endif
        std::pair<uintptr_t, uintptr_t> alloc_range = {0, 0};

    private:
        BlockAllocator* const _block_allocator;
        std::array<std::unique_ptr<ObjectAllocatorBase>, config::MaxTypesNumber> _allocators;
        Data* const _data;

        template<class Allocator>
        Allocator& _allocator() {
            auto& alocator = _allocators[_type_index<typename Allocator::ValueType>()];
            if (!alocator) {
                if constexpr(Allocator::IsPoolAllocator::value) {
                    alocator.reset(new Allocator(*_block_allocator, _data->pages));
                } else {
                    alocator.reset(new Allocator(_data->pages));
                }
            }
            return static_cast<Allocator&>(*alocator);
        }
        // Cached per image; the counter is in ProcessState, so under SGCL_EXTERNAL_STATE types of different images
        // never share a slot.
        template<class T>
        inline static unsigned _type_index() {
            static const unsigned index = _next_type_index();
            return index;
        }

        static unsigned _next_type_index() noexcept {
            auto index = process_state().type_counter.fetch_add(1, std::memory_order_relaxed);
            if (index >= config::MaxTypesNumber) {
                std::fprintf(stderr, "[sgcl] fatal: more than %zu tracked types; raise SGCL_MAX_TYPES_NUMBER\n",
                             config::MaxTypesNumber);
                std::abort();
            }
            return index;
        }
    };

#if defined(SGCL_EXTERNAL_STATE)
    // The Thread lives in the owner's TLS; a pointer to it is cached per image, or shared where the loader merges it.
    inline Thread& current_thread() noexcept {
        thread_local Thread* thread = nullptr;
        if (!thread) [[unlikely]] {
            thread = &SGCL_STATE_SYMBOL(current_thread)();
        }
        return *thread;
    }
#else
    struct MainThreadDetector {
        MainThreadDetector() noexcept {
            Thread::main_thread_id = std::this_thread::get_id();
        }
    };

    inline static MainThreadDetector main_thread_detector;

    inline Thread& current_thread() noexcept {
        static thread_local Thread instance;
        return instance;
    }
#endif
}
