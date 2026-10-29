//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "block_allocator.h"
#include "object_allocator_base.h"

namespace sgcl::detail {
    // A type's partly free pages, shared by every thread. Mutators pop, the collector replaces the list; both
    // under the lock (a CAS pop is ABA-prone against the collector's re-sorting).
    struct PageBuffer {
        Page* head = {nullptr};
        SpinLock lock;
    };

    class ObjectPoolAllocatorBase : public ObjectAllocatorBase {
    public:
        ObjectPoolAllocatorBase(BlockAllocator& ba, std::atomic<Page*>& pages, PointerPoolBase& pa, PageBuffer& pb) noexcept
        : ObjectAllocatorBase(pages)
        , _block_allocator(ba)
        , _pointer_pool(pa)
        , _pages_buffer(pb) {
        }

        // At thread exit: return the slots still in the pool.
        ~ObjectPoolAllocatorBase() noexcept override {
            unsigned unused_counter = 0;
            while (!_pointer_pool.is_empty()) {
                auto ptr = _pointer_pool.alloc();
                auto index = _current_page->index_of(ptr);
                // release: pairs with fill()'s acquire load of Unused before reusing the slot.
                _current_page->states()[index].store(State::Unused, std::memory_order_release);
                ++unused_counter;
            }
            if (unused_counter) {
                _current_page->unused_occur.store(true, std::memory_order_relaxed);
                // release: pairs with _release_unused_pages' acquire load of this counter.
                _current_page->unused_atomic.fetch_add(unused_counter, std::memory_order_release);
            }
        }

        void* alloc() noexcept {
            if (_pointer_pool.is_empty()) {
                Page* page;
                {
                    SpinLockGuard guard(_pages_buffer.lock);
                    page = _pages_buffer.head;
                    if (page) {
                        _pages_buffer.head = page->next_empty;
                    }
                }
                if (page) {
                    _pointer_pool.fill(page);
                    page->on_empty_list.store(false, std::memory_order_release);
                } else {
                    page = _alloc_page();
                    _pointer_pool.fill((void*)(page->data));
                    page->next = _pages.load(std::memory_order_relaxed);
                    _pages.store(page, std::memory_order_release);
                }
                _current_page = page;
            }
            assert(!_pointer_pool.is_empty());
            return _pointer_pool.alloc();
        }

    private:
        BlockAllocator& _block_allocator;
        PointerPoolBase& _pointer_pool;
        PageBuffer& _pages_buffer;
        Page* _current_page = {nullptr};

        virtual Page* _create_page_parameters(DataPage*) noexcept = 0;

        Page* _alloc_page() noexcept {
            auto data = _block_allocator.alloc();
            auto page = _create_page_parameters(data);
            data->page = page;
            process_state().pages.add(data);
            return page;
        }

    protected:
        // Collector only: moves the pages whose slots are all free from `pages` to `empty_pages`.
        static void _remove_empty(Page*& pages, Page*& empty_pages) noexcept {
            auto page = pages;
            Page* prev = nullptr;
            while(page) {
                auto next = page->next_empty;
                auto unused_atomic = page->unused_atomic.load(std::memory_order_acquire);
                page->unused_atomic.fetch_sub(unused_atomic, std::memory_order_relaxed);
                page->unused_counter_gc += unused_atomic;
                assert(page->unused_counter_gc >= page->unused_counter_mutators);
                page->unused_counter_gc -= page->unused_counter_mutators;
                page->unused_counter_mutators = 0;
                assert(page->unused_counter_gc <= page->metadata->object_count);
                if (page->unused_counter_gc == page->metadata->object_count) {
                    page->next_empty = empty_pages;
                    empty_pages = page;
                    if (!prev) {
                        pages = next;
                    } else {
                        prev->next_empty = next;
                    }
                } else {
                    prev = page;
                }
                page = next;
            }
        }

        static void _free(Page* pages) noexcept {
            DataPage* empty = nullptr;
            for (Page* page = pages; page; page = page->next_empty) {
                DataPage* data = (DataPage*)(page->data - sizeof(void*));
                process_state().pages.remove(data);
                data->block = page->block;
                page->is_used = false;
                data->next = empty;
                empty = data;
            }
            BlockAllocator::free(empty);
        }

        // Collector only: merges `pages` into the buffer, frees the ones that became entirely free.
        static void _free(Page* pages, PageBuffer& pages_buffer) noexcept {
            Page* buffered;
            {
                SpinLockGuard guard(pages_buffer.lock);
                buffered = pages_buffer.head;
                pages_buffer.head = nullptr;
            }
            if (pages) {
                auto last = pages;
                while(last->next_empty) {
                    last = last->next_empty;
                }
                last->next_empty = buffered;
            } else {
                pages = buffered;
            }
            Page* empty_pages = nullptr;
            _remove_empty(pages, empty_pages);
            pages = merge_sort<&Page::next_empty>(pages);
            if (pages) {
                auto last = pages;
                while(last->next_empty) {
                    last = last->next_empty;
                }
                SpinLockGuard guard(pages_buffer.lock);
                last->next_empty = pages_buffer.head;
                pages_buffer.head = pages;
            }
            if (empty_pages) {
                _free(empty_pages);
            }
        }
    };
}
