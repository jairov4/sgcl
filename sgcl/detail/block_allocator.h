//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "block.h"
#include "memory_counters.h"
#include "merge_sort.h"
#include "pointer_pool.h"

#include <thread>

namespace sgcl::detail {
    // Wakes the collector when it goes out of scope, if asked to.
    class WakeUpCollectorOnExit {
    public:
        WakeUpCollectorOnExit() noexcept = default;
        WakeUpCollectorOnExit(const WakeUpCollectorOnExit&) = delete;
        WakeUpCollectorOnExit& operator=(const WakeUpCollectorOnExit&) = delete;

        ~WakeUpCollectorOnExit() noexcept {
            if (_armed) {
                waking_up_collector();
            }
        }

        void arm() noexcept {
            _armed = true;
        }

    private:
        bool _armed = false;
    };

    // One per thread: carves data pages out of blocks (SPEC.md §4.1).
    class BlockAllocator {
    public:
        // Runs on the collector thread when it drops a dead thread's record.
        ~BlockAllocator() noexcept {
            DataPage* pages = nullptr;
            while (!_pointer_pool.is_empty()) {
                auto data = (DataPage*)_pointer_pool.alloc();
                data->next = pages;
                pages = data;
            }
            free(pages, true);
        }

        SGCL_API DataPage* alloc() noexcept;

        // Collector only: returns blocks whose pages are all free.
        SGCL_API static void release_empty() noexcept;

        SGCL_API static void free(DataPage* pages, bool destructor = false) noexcept;

    private:
        PointerPool<Block::PageCount, config::PageSize> _pointer_pool;

        SGCL_API static void _push(DataPage* pages) noexcept;

        // Unlinks the pages of fully free blocks from `pages` and returns those blocks.
        SGCL_API static Block* _remove_empty(DataPage*& pages) noexcept;

        SGCL_API static void _release_empty(Block* block) noexcept;
    };
}
