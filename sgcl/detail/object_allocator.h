//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "block_allocator.h"
#include "memory_counters.h"
#include "object_allocator_base.h"
#include "type_info.h"

#include <thread>

namespace sgcl::detail {
    // Large allocations: one slot each, in its own PageSize-aligned block (SPEC.md §4.1).
    template<class T>
    class ObjectAllocator : public ObjectAllocatorBase {
    public:
        using ValueType = typename TypeInfo<T>::Type;
        using IsPoolAllocator = std::false_type;

        explicit ObjectAllocator(std::atomic<Page*>& pages) noexcept
        : ObjectAllocatorBase(pages) {
        }

        // `size` is the byte count past sizeof(ValueType); the caller checked it for overflow.
        ValueType* alloc(size_t size) const noexcept {
            WakeUpCollectorOnExit wake_up;
            size += sizeof(ValueType) + sizeof(uintptr_t);
            MemoryCounters::update_alloc(1, size);
            auto alloc_counter = MemoryCounters::alloc_counter();
            auto live_counter = MemoryCounters::live_counter();
            if (alloc_counter * 4 > live_counter + Counter(64, config::PageSize * 4 * 64)) {
                wake_up.arm();
            }
            auto mem = checked_operator_new(size, std::align_val_t(config::PageSize));
            auto data = (ValueType*)((uintptr_t)mem + sizeof(uintptr_t));
            auto hmem = checked_operator_new(TypeInfo<T>::HeaderSize);
            auto page = new(hmem) Page(nullptr, data);
            page->alloc_size = size;
            *((Page**)mem) = page;
            process_state().pages.add(mem);
            page->next = _pages.load(std::memory_order_relaxed);
            _pages.store(page, std::memory_order_release);
            return data;
        }

        // Collector only.
        static void free(Page* pages) noexcept {
            or_fatal([&] {
                size_t size = 0;
                std::vector<void*> mems_to_delete;
                for (Page* page = pages; page; page = page->next_empty) {
                    mems_to_delete.push_back((void*)(page->data - sizeof(uintptr_t)));
                    process_state().pages.remove(mems_to_delete.back());
                    size += page->alloc_size;
                    page->is_used = false;
                }
                if (mems_to_delete.size()) {
                    MemoryCounters::update_free(mems_to_delete.size(), size);
                    auto alloc_counter = MemoryCounters::last_alloc_counter();
                    auto free_counter = Counter(mems_to_delete.size(), size);
                    if (free_counter * 4 > alloc_counter + Counter(64, config::PageSize * 4 * 64)) {
                        force_short_sleep();
                    }
                    destroyer_threads().emplace_back([mems = std::move(mems_to_delete)] {
                        for (auto mem: mems) {
                            ::operator delete(mem, std::align_val_t(config::PageSize));
                        }
                    });
                }
            });
        }
    };
}
