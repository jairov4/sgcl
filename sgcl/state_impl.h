//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// Defines the process state (SPEC.md §3.8, §4.9). Include from exactly one translation unit of the owning image,
// with SGCL_API exporting.

#include "sgcl.h"

#include <utility>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
extern "C" {
#include <mach/thread_state.h>
}
#elif defined(__linux__)
#include <linux/membarrier.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace sgcl::detail {
    // The calling thread's real stack as [low, high), or {0, 0} when the platform does not report it.
    inline std::pair<uintptr_t, uintptr_t> current_stack_bounds() noexcept {
#if defined(_WIN32)
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(&region, &region, sizeof(region))) {
            return {0, 0};
        }
        return {(uintptr_t)region.AllocationBase, (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase};
#elif defined(__APPLE__)
        auto high = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        return {high - pthread_get_stacksize_np(pthread_self()), high};
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) {
            return {0, 0};
        }
        void* addr = nullptr;
        size_t size = 0;
        int rc = pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        return rc == 0 ? std::pair<uintptr_t, uintptr_t>{(uintptr_t)addr, (uintptr_t)addr + size} : std::pair<uintptr_t, uintptr_t>{0, 0};
#endif
    }

#if defined(__linux__)
    // Changing the protection of a page this process has touched makes the kernel interrupt every CPU running one
    // of its threads (TLB shootdown), which serializes them.
    inline void mprotect_fence() noexcept {
        static std::mutex lock;
        static char* page = [] {
            void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                fatal("mmap failed");
            }
            return (char*)p;
        }();
        std::lock_guard<std::mutex> guard(lock);
        mprotect(page, 4096, PROT_READ | PROT_WRITE);
        *(volatile char*)page = 0;
        mprotect(page, 4096, PROT_READ);
    }
#endif

    void SGCL_STATE_SYMBOL(process_fence)() noexcept {
#if defined(_WIN32)
        FlushProcessWriteBuffers();
#elif defined(__APPLE__)
        // Reading another thread's registers interrupts it, which serializes it.
        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
            fatal("task_threads failed");
        }
        for (mach_msg_type_number_t i = 0; i < count; ++i) {
            uintptr_t sp = 0;
            uintptr_t registers[128];
            size_t size = 128;
            thread_get_register_pointer_values(threads[i], &sp, &size, registers);
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(thread_act_t));
#elif defined(__linux__)
        static const bool expedited =
            syscall(__NR_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0;
        if (!expedited || syscall(__NR_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) != 0) {
            mprotect_fence();
        }
#else
#error "no process-wide memory barrier for this platform"
#endif
    }

    constinit ProcessState process_state_instance;

    ProcessState& SGCL_STATE_SYMBOL(process_state)() noexcept {
        return process_state_instance;
    }

    // Immortal: only collector::terminate() stops it, never static destruction, which would run under the Windows
    // loader lock after other threads are gone.
    Collector& SGCL_STATE_SYMBOL(collector_instance)() noexcept {
        static Collector* const collector = checked_new<Collector>();
        return *collector;
    }

    // No thread's exit terminates the collector.
    Thread& SGCL_STATE_SYMBOL(current_thread)() noexcept {
        static thread_local Thread instance = [] {
            auto [low, high] = current_stack_bounds();
            return Thread(low, high);
        }();
        // Constructed after the record, so destroyed before it.
        static thread_local ThreadRetirer retirer{instance};
        return instance;
    }
}

// Out of line: the slow paths and collector-only code of detail/page.h, compiled once, in this TU.
namespace sgcl::detail {
    Page::Page(Block* block, void* data, Metadata* metadata) noexcept
    : metadata(metadata)
    , block(block)
    , data((uintptr_t)data)
    , multiplier((1ull << 32 | 0x10000) / metadata->object_size)
    , object_count(metadata->object_count) {
        assert(data != nullptr);
        auto states = this->states();
        for (unsigned i = 0; i < metadata->object_count; ++i) {
            new (&states[i]) std::atomic<State>(State::Reserved);
        }
        auto flags = this->flags();
        for (unsigned i = 0; i < flags_count(); ++i) {
            new (&flags[i]) Flags();
        }
    }
}

// Out of line: the slow paths and collector-only code of detail/object_pool_allocator_base.h, compiled once, in this TU.
namespace sgcl::detail {
    ObjectPoolAllocator::ObjectPoolAllocator(BlockAllocator& ba, std::atomic<Page*>& pages, Metadata& metadata) noexcept
    : ObjectAllocatorBase(pages)
    , _block_allocator(ba)
    , _metadata(metadata)
    , _pointer_pool((void**)checked_operator_new(sizeof(void*) * metadata.object_count), metadata.object_count,
                    (unsigned)metadata.object_size)
    , _pages_buffer(metadata.pages_buffer) {
    }

    ObjectPoolAllocator::~ObjectPoolAllocator() noexcept {
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
        ::operator delete(_pointer_pool.indexes());
    }

    void ObjectPoolAllocator::_refill() noexcept {
        {
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
    }

    Page* ObjectPoolAllocator::_create_page_parameters(DataPage* data) noexcept {
        auto mem = checked_operator_new(_metadata.header_size);
        return new(mem) Page(data->block, (char*)data + _metadata.data_offset, &_metadata);
    }

    Page* ObjectPoolAllocator::_alloc_page() noexcept {
        auto data = _block_allocator.alloc();
        auto page = _create_page_parameters(data);
        data->page = page;
        process_state().pages.add(data);
        return page;
    }

    void ObjectPoolAllocator::_remove_empty(Page*& pages, Page*& empty_pages) noexcept {
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

    void ObjectPoolAllocator::_free(Page* pages) noexcept {
        DataPage* empty = nullptr;
        for (Page* page = pages; page; page = page->next_empty) {
            DataPage* data = (DataPage*)(page->data & ~(uintptr_t)(config::PageSize - 1));
            process_state().pages.remove(data);
            data->block = page->block;
            page->is_used = false;
            data->next = empty;
            empty = data;
        }
        BlockAllocator::free(empty);
    }

    void ObjectPoolAllocator::_free(Page* pages, PageBuffer& pages_buffer) noexcept {
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

    void ObjectPoolAllocator::free(Page* pages, Metadata& metadata) noexcept {
        _free(pages, metadata.pages_buffer);
    }
}

// Out of line: the slow paths and collector-only code of detail/pointer_pool_base.h, compiled once, in this TU.
namespace sgcl::detail {
    void PointerPoolBase::fill(void* data) noexcept {
        for(auto i = 0u; i < _size; ++i, data = (void*)((uintptr_t)data + _offset)) {
            _indexes[i] = data;
        }
        _position = 0;
    }

    void PointerPoolBase::fill(Page* page) noexcept {
        auto data = page->data;
        auto object_size = page->metadata->object_size;
        auto states = page->states();
        auto count = page->metadata->object_count;
        [[maybe_unused]] bool unused_occur = false;
        for(int i = count - 1; i >= 0; --i) {
            // acquire: pairs with the collector's (or another thread's dying allocator's)
            // release store of State::Unused, so this thread also sees whatever was
            // sequenced-before that release -- notably the freed object's destructor having
            // already run -- before handing this memory out again for a new object.
            if (states[i].load(std::memory_order_acquire) == State::Unused) {
                _indexes[--_position] = (void*)(data + i * object_size);
                states[i].store(State::Reserved, std::memory_order_relaxed);
                ++page->unused_counter_mutators;
                assert(unused_occur = true);
            }
        }
        assert(unused_occur);
    }
}

// Out of line: the slow paths and collector-only code of detail/block_allocator.h, compiled once, in this TU.
namespace sgcl::detail {
    DataPage* BlockAllocator::alloc() noexcept {
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

    void BlockAllocator::release_empty() noexcept {
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

    void BlockAllocator::free(DataPage* pages, bool destructor) noexcept {
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

    void BlockAllocator::_push(DataPage* pages) noexcept {
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

    Block* BlockAllocator::_remove_empty(DataPage*& pages) noexcept {
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

    void BlockAllocator::_release_empty(Block* block) noexcept {
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
}

// Out of line: the slow paths and collector-only code of detail/page_registry.h, compiled once, in this TU.
namespace sgcl::detail {
    PageRegistry::Location PageRegistry::_locate(const void* p, bool create) noexcept {
        auto page = (uintptr_t)p >> PageBits;
        if (page >> (AddressBits - PageBits)) {
            fatal("managed memory above the 48-bit address space");
        }
        auto& slot = _root[page >> LeafBits];
        auto leaf = slot.load(std::memory_order_acquire);
        if (!leaf && create) {
            auto fresh = (std::atomic<uint64_t>*)checked_operator_new(sizeof(std::atomic<uint64_t>) * LeafWords);
            for (size_t i = 0; i < LeafWords; ++i) {
                new (&fresh[i]) std::atomic<uint64_t>(0);
            }
            if (slot.compare_exchange_strong(leaf, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
                leaf = fresh;
            } else {
                ::operator delete(fresh);
            }
        }
        auto index = page & ((size_t(1) << LeafBits) - 1);
        return {leaf, index / 64, uint64_t(1) << (index % 64)};
    }
}

// Out of line: the slow paths and collector-only code of detail/child_pointers.h, compiled once, in this TU.
namespace sgcl::detail {
    ChildPointers::ChildPointers(bool final_layout, size_t object_size) noexcept
    : final(final_layout)
    , size(final_layout ? 0 : ((object_size + sizeof(RawPointer) - 1) / sizeof(RawPointer) + WordsPerByte - 1) / WordsPerByte)
    , map(size ? static_cast<std::atomic<uint8_t>*>(checked_operator_new(size)) : nullptr) {
        for (size_t i = 0; i < size; ++i) {
            new (&map[i]) std::atomic<uint8_t>(0);
        }
    }

    ChildPointers::ChildPointers(CellTag) noexcept
    : final(true)
    , size(1)
    , map(static_cast<std::atomic<uint8_t>*>(checked_operator_new(1))) {
        new (&map[0]) std::atomic<uint8_t>(1);
    }
}
