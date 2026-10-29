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

        DataPage* alloc() noexcept {
            WakeUpCollectorOnExit wake_up;
            MemoryCounters::update_alloc(1, config::PageSize);
            if (MemoryCounters::alloc_count() * 4 > MemoryCounters::live_count() + 64) {
                wake_up.arm();
            }
            if (_pointer_pool.is_empty()) {
                auto& state = process_state();
                {
                    SpinLockGuard guard(state.empty_pages_lock);
                    if (auto page = state.empty_pages) {
                        state.empty_pages = page->next;
                        return page;
                    }
                }
                auto block = new Block;
                _pointer_pool.fill(block + 1);
            }
            return (DataPage*)_pointer_pool.alloc();
        }

        // Collector only: returns blocks whose pages are all free.
        static void release_empty() noexcept {
            auto& state = process_state();
            DataPage* pages;
            {
                SpinLockGuard guard(state.empty_pages_lock);
                pages = state.empty_pages;
                state.empty_pages = nullptr;
            }
            Block* blocks = _remove_empty(pages);
            pages = merge_sort<&DataPage::next>(pages);
            _push(pages);
            _release_empty(blocks);
        }

        static void free(DataPage* pages, bool destructor = false) noexcept {
            size_t free_count = 0;
            for (auto page = pages; page; page = page->next) {
                ++free_count;
            }
            Block* blocks = _remove_empty(pages);
            _release_empty(blocks);
            _push(merge_sort<&DataPage::next>(pages));
            if (free_count && !destructor) {
                MemoryCounters::update_free(free_count, config::PageSize * free_count);
                if (free_count * 4 > MemoryCounters::last_alloc_count() + 64) {
                    force_short_sleep();
                }
            }
        }

    private:
        PointerPool<Block::PageCount, config::PageSize> _pointer_pool;

        static void _push(DataPage* pages) noexcept {
            if (!pages) {
                return;
            }
            auto last = pages;
            while(last->next) {
                last = last->next;
            }
            auto& state = process_state();
            SpinLockGuard guard(state.empty_pages_lock);
            last->next = state.empty_pages;
            state.empty_pages = pages;
        }

        // Unlinks the pages of fully free blocks from `pages` and returns those blocks.
        static Block* _remove_empty(DataPage*& pages) noexcept {
            Block* blocks = nullptr;
            for (auto page = pages; page; page = page->next) {
                Block* block = page->block;
                if (!block->page_count) {
                    block->next = blocks;
                    blocks = block;
                }
                ++block->page_count;
            }
            DataPage* prev = nullptr;
            auto page = pages;
            while(page) {
                auto next = page->next;
                Block* b = page->block;
                assert(b->page_count <= Block::PageCount);
                if (b->page_count == Block::PageCount) {
                    if (!prev) {
                        pages = next;
                    } else {
                        prev->next = next;
                    }
                } else {
                    prev = page;
                }
                page = next;
            }
            return blocks;
        }

        static void _release_empty(Block* block) noexcept {
            or_fatal([&] {
                std::vector<Block*> blocks_to_delete;
                while(block) {
                    auto next = block->next;
                    if (block->page_count == Block::PageCount) {
                        blocks_to_delete.push_back(block);
                    } else {
                        block->page_count = 0;
                    }
                    block = next;
                }
                if (blocks_to_delete.size()) {
                    destroyer_threads().emplace_back([blocks = std::move(blocks_to_delete)] {
                        for (auto block: blocks) {
                            delete block;
                        }
                    });
                }
            });
        }
    };
}
