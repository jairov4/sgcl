//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "child_pointers.h"
#include "thread.h"
#include "types.h"

#include <condition_variable>
#include <mutex>
#include <tuple>

namespace sgcl::detail {
    // The collector thread and its handshakes (SPEC.md §4.6, §4.10).
    class Collector {
        struct Resume {
            void operator()(Collector* c) const noexcept {
                c->_resume();
            }
        };

    public:
        // Keeps the collector paused while a get_live_objects() snapshot is in use.
        using PauseGuard = std::unique_ptr<Collector, Resume>;

        Collector() noexcept {
            if (process_state().collector_terminating.load(std::memory_order_acquire)) {
                _stopped = true;
            } else {
                or_fatal([this] {
                    std::thread([this]{ _main_loop(); }).detach();
                });
            }
            process_state().collector_created.store(true, std::memory_order_release);
        }

        Collector(const Collector&) = delete;
        Collector& operator=(const Collector&) = delete;

        void waking_up() noexcept {
            sleep_flag.store(false, std::memory_order_release);
            sleep_cv.notify_one();
        }

        bool force_collect(bool wait) noexcept {
            if (_on_collector_thread()) {
                _request(false);
                return !wait;
            }
            if (wait && _pause_owner_is_current()) {
                fatal("force_collect(true) while this thread holds a pause_guard");
            }
            std::unique_lock<std::mutex> lock(_mutex);
            if (_stopped || process_state().collector_terminating.load(std::memory_order_relaxed)) {
                return false;
            }
            auto target = _request_locked(false);
            if (!wait) {
                return true;
            }
            _cv_done.wait(lock, [&] { return _completed >= target || _stopped; });
            return _completed >= target;
        }

        size_t live_object_count() const noexcept {
            return _last_live_object_count.load(std::memory_order_relaxed);
        }

        std::tuple<PauseGuard, std::vector<void*>> get_live_objects() noexcept {
            if (_on_collector_thread()) {
                fatal("get_live_objects() on the collector thread");
            }
            if (_pause_owner_is_current()) {
                fatal("get_live_objects() while this thread holds a pause_guard");
            }
            std::unique_lock<std::mutex> lock(_mutex);
            std::vector<void*> objects;
            // One snapshot at a time: the paused cycle belongs to exactly one caller.
            _cv_done.wait(lock, [&] { return !_snapshot_busy || _stopped; });
            if (_stopped || process_state().collector_terminating.load(std::memory_order_relaxed)) {
                return {PauseGuard(nullptr), std::move(objects)};
            }
            _snapshot_busy = true;
            auto target = _request_locked(true);
            _cv_done.wait(lock, [&] { return _completed >= target || _stopped; });
            if (_completed >= target && _paused) {
                objects.swap(_live_objects);
                _pause_owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
                return {PauseGuard(this), std::move(objects)};
            }
            _snapshot_busy = false;
            _cv_done.notify_all();
            return {PauseGuard(nullptr), std::move(objects)};
        }

        void force_short_sleep() noexcept {
            _short_sleep = true;
        }

        static void terminate() noexcept {
            if (created()) {
                collector_instance()._terminate();
            } else {
                // Nothing to stop yet; a collector created later never starts (SPEC.md §4.10).
                process_state().collector_terminating.store(true, std::memory_order_release);
            }
        }

        static bool terminated() noexcept {
            return process_state().collector_terminating.load(std::memory_order_acquire);
        }

        static bool created() noexcept {
            return process_state().collector_created.load(std::memory_order_acquire);
        }

        std::vector<std::thread>& destroyer_threads() noexcept {
            return _destroyer_threads;
        }

        // unique_ptr's deleter: destroy now, on the calling thread; a later cycle reclaims the slot.
        static void delete_unique(const void* p) noexcept {
            auto page = Page::page_of(p);
            auto ptr = Page::base_address_of(p);
            _destroy(page, ptr, false);
            Page::set_state<State::Destroyed>(p);
        }

    private:
        // A word read at a child offset is traced only when it points into a live page (SPEC.md §4.4): zero is
        // null, 1 is the discovery sentinel, a tagged value is the ref of a non-child tracked_ptr, and anything
        // else may be foreign data sharing the offset in type-erased storage.
        static bool _is_referent(const void* p) noexcept {
            return (uintptr_t)p > 1 && process_state().pages.contains(p);
        }

        bool _on_collector_thread() const noexcept {
            return std::this_thread::get_id() == _thread_id.load(std::memory_order_relaxed);
        }

        bool _pause_owner_is_current() const noexcept {
            return std::this_thread::get_id() == _pause_owner.load(std::memory_order_relaxed);
        }

        void _request(bool live_objects) noexcept {
            std::lock_guard<std::mutex> lock(_mutex);
            _request_locked(live_objects);
        }

        uint64_t _request_locked(bool live_objects) noexcept {
            auto target = ++_requested;
            if (live_objects) {
                _snapshot_target = target;
            }
            waking_up();
            return target;
        }

        void _resume() noexcept {
            std::lock_guard<std::mutex> lock(_mutex);
            _pause_owner.store(std::thread::id(), std::memory_order_relaxed);
            _paused = false;
            _snapshot_busy = false;
            _cv_resume.notify_all();
            _cv_done.notify_all();
        }

        void _register_threads() noexcept {
            auto first_thread = process_state().threads_data.load(std::memory_order_acquire);
            auto thread = first_thread;
            while(thread != _last_thread_registered) {
                thread->next_registered = _registered_threads;
                _registered_threads = thread;
                thread = thread->next;
            }
            if (_last_thread_registered != first_thread) {
                // The newest record stays as a sentinel against ABA on the push-only list.
                if (_last_thread_registered) {
                    _last_thread_registered->is_last_registered = false;
                    if (!_last_thread_registered->is_used) {
                        delete _last_thread_registered;
                        BlockAllocator::release_empty();
                    }
                }
                _last_thread_registered = first_thread;
                _last_thread_registered->is_last_registered = true;
            }
        }

        void _register_pages() noexcept {
            bool delete_thread = false;
            Thread::Data* prev = nullptr;
            auto thread = _registered_threads;
            while(thread) {
                auto next = thread->next_registered;
                bool thread_deleted = thread->is_deleted.load(std::memory_order_acquire);
                _register_pages(thread->pages, thread->last_page_registered, thread_deleted);
                if (thread_deleted) {
                    if (!prev) {
                        _registered_threads = next;
                    } else {
                        prev->next_registered = next;
                    }
                    if (!thread->is_last_registered) {
                        delete thread;
                        delete_thread = true;
                    } else {
                        thread->is_used = false;
                    }
                } else {
                    prev = thread;
                }
                thread = next;
            }
            if (delete_thread) {
                BlockAllocator::release_empty();
            }
        }

        void _register_pages(std::atomic<Page*>& pages, Page*& last_page_registered, bool thread_deleted) noexcept {
            auto first_page = pages.load(std::memory_order_acquire);
            auto page = first_page;
            while(page != last_page_registered) {
                page->next_registered = _registered_pages;
                _registered_pages = page;
                page = page->next;
            }
            if (thread_deleted) {
                if (last_page_registered) {
                    last_page_registered->is_last_registered = false;
                    if (!last_page_registered->is_used) {
                        delete last_page_registered;
                    }
                }
            } else if (last_page_registered != first_page) {
                if (last_page_registered) {
                    last_page_registered->is_last_registered = false;
                    if (!last_page_registered->is_used) {
                        delete last_page_registered;
                    }
                }
                last_page_registered = first_page;
                last_page_registered->is_last_registered = true;
            }
        }

        void _register_objects() noexcept {
            for (auto page = _registered_pages; page; page = page->next_registered) {
                if (!page->is_used || !page->object_created.exchange(false, std::memory_order_acquire)) {
                    continue;
                }
                auto states = page->states();
                auto flags = page->flags();
                auto count = page->flags_count();
                auto object_count = page->metadata->object_count;
                for (unsigned i = 0; i < count; ++i) {
                    auto& flag = flags[i];
                    auto unregistered = ~flag.registered;
                    auto offset = i * Page::FlagBitCount;
                    while(unregistered) {
                        auto countr_zero = std::countr_zero(unregistered);
                        auto index = offset + countr_zero;
                        if (index >= object_count) {
                            break;
                        }
                        auto state = states[index].load(std::memory_order_relaxed);
                        if (state & State::CreatedMask) {
                            flag.registered |= Page::Flag(1) << countr_zero;
                            if (state == State::Reachable) {
                                page->state_updated.store(true, std::memory_order_relaxed);
                            }
                        }
                        unregistered &= unregistered - 1;
                    }
                }
            }
        }

        void _update_states() noexcept {
            for (auto page = _registered_pages; page; page = page->next_registered) {
                page->clear_flags();
                if (!page->state_updated.exchange(false, std::memory_order_acquire)) {
                    continue;
                }
                auto states = page->states();
                auto flags = page->flags();
                auto count = page->flags_count();
                for (unsigned i = 0; i < count; ++i) {
                    auto registered = flags[i].registered;
                    auto offset = i * Page::FlagBitCount;
                    while(registered) {
                        auto index = offset + std::countr_zero(registered);
                        auto& state = states[index];
                        auto expected = State::Reachable;
                        // Never overwrites a handover or a destruction; acq_rel pairs with the barrier's RMW.
                        state.compare_exchange_strong(expected, State::Used, std::memory_order_acq_rel, std::memory_order_relaxed);
                        registered &= registered - 1;
                    }
                }
            }
        }

        void _mark(const void* ptr) noexcept {
            auto page = Page::page_of(ptr);
            auto index = page->index_of(ptr);
            // A foreign value inside a live page may fall past its last slot or on its header word.
            if (index >= page->metadata->object_count) {
                return;
            }
            auto mask = Page::flag_mask_of(index);
            auto& flag = page->flags()[Page::flag_index_of(index)];
            if ((flag.registered & ~flag.marked & ~flag.reachable & mask)) {
                flag.reachable |= mask;
                if (!page->reachable) {
                    page->reachable = true;
                    page->next_reachable = _reachable_pages;
                    _reachable_pages = page;
                }
            }
        }

        void _mark_stack_roots() noexcept {
            for (auto thread = _registered_threads; thread; thread = thread->next_registered) {
                if (thread->is_deleted.load(std::memory_order_acquire)) {
                    continue;
                }
                auto& allocator = *thread->stack_roots_allocator;
                for (size_t i = 0; i < std::size(allocator.is_used); ++i) {
                    if (!allocator.is_used[i].load(std::memory_order_acquire)) {
                        continue;
                    }
                    auto first = i * (StackPointerAllocator::PageSize / sizeof(RawPointer));
                    auto last = first + (StackPointerAllocator::PageSize / sizeof(RawPointer));
                    for (size_t index = first; index < last; ++index) {
                        if (auto p = allocator.data[index].load(std::memory_order_acquire)) {
                            _mark(p);
                        }
                    }
                }
            }
        }

        void _mark_childs(void* ptr, const ChildPointers& layout) noexcept {
            layout.for_each([&](size_t word) {
                auto p = ((RawPointer*)ptr + word)->load(std::memory_order_acquire);
                if (_is_referent(p)) {
                    _mark(p);
                }
            });
        }

        void _mark_array_childs(void* ptr) noexcept {
            auto array = (ArrayBase*)ptr;
            auto metadata = array->metadata.load(std::memory_order_acquire);
            if (!metadata || !metadata->child_pointers.size) {
                return;
            }
            // Every element up to capacity: [size, capacity) is zero (SPEC.md §4.8).
            auto data = (uintptr_t)array + ArrayDataOffset;
            for (size_t i = 0; i < array->capacity; ++i, data += metadata->object_size) {
                _mark_childs((void*)data, metadata->child_pointers);
            }
        }

        void _mark_reachable() noexcept {
            auto page = _reachable_pages;
            _reachable_pages = nullptr;
            while(page) {
                auto metadata = page->metadata;
                const bool is_array = metadata->is_array;
                auto flags = page->flags();
                auto count = page->flags_count();
                bool marked;
                do {
                    marked = false;
                    for (unsigned i = 0; i < count; ++i) {
                        auto& flag = flags[i];
                        while (flag.reachable) {
                            auto reachable = flag.reachable;
                            auto offset = i * Page::FlagBitCount;
                            while(reachable) {
                                auto countr_zero = std::countr_zero(reachable);
                                flag.marked |= Page::Flag(1) << countr_zero;
                                auto ptr = page->pointer_of(offset + countr_zero);
                                if (is_array) {
                                    _mark_array_childs(ptr);
                                } else {
                                    _mark_childs(ptr, metadata->child_pointers);
                                }
                                ++_live_object_count;
                                if (_share_live_objects) {
                                    _live_objects.push_back(ptr);
                                }
                                reachable &= reachable - 1;
                            }
                            flag.reachable &= ~flag.marked;
                            marked = true;
                        }
                    }
                } while(marked);
                page->reachable = false;
                page = page->next_reachable;
                if (!page) {
                    page = _reachable_pages;
                    _reachable_pages = nullptr;
                }
            }
        }

        // Grays registered, unmarked slots whose state is protected. With All, every registered page is scanned
        // and pages keeping unmarked, unprotected slots go on the unreachable list; otherwise only unreachable
        // pages that saw a state change.
        template<bool All>
        void _mark_updated() noexcept {
            auto page = All ? _registered_pages : _unreachable_pages;
            while(page) {
                bool reachable_page = false;
                [[maybe_unused]] bool unreachable_page = false;
                // Always acquire: it orders the state reads below after a mutator's barrier.
                auto state_updated = page->state_updated.load(std::memory_order_acquire);
                if (All || state_updated) {
                    auto states = page->states();
                    auto flags = page->flags();
                    auto count = page->flags_count();
                    for (unsigned i = 0; i < count; ++i) {
                        auto& flag = flags[i];
                        auto unreachable = flag.registered & ~flag.marked;
                        auto offset = i * Page::FlagBitCount;
                        while(unreachable) {
                            auto countr_zero = std::countr_zero(unreachable);
                            auto state = states[offset + countr_zero].load(std::memory_order_relaxed);
                            if (state & State::ReachableMask) {
                                flag.reachable |= Page::Flag(1) << countr_zero;
                                reachable_page = true;
                            } else if constexpr(All) {
                                unreachable_page = true;
                            }
                            unreachable &= unreachable - 1;
                        }
                    }
                }
                if (reachable_page && !page->reachable) {
                    page->reachable = true;
                    page->next_reachable = _reachable_pages;
                    _reachable_pages = page;
                }
                if constexpr(All) {
                    if (unreachable_page && !page->unreachable) {
                        page->unreachable = true;
                        page->next_unreachable = _unreachable_pages;
                        _unreachable_pages = page;
                    }
                    page = page->next_registered;
                } else {
                    page = page->next_unreachable;
                }
            }
        }

        // Only words holding a referent: a foreign value sharing the offset stays as its owner left it.
        static void _clear_childs(void* ptr, const ChildPointers& layout) noexcept {
            layout.for_each([&](size_t word) {
                auto cell = (RawPointer*)ptr + word;
                if (_is_referent(cell->load(std::memory_order_relaxed))) {
                    cell->store(nullptr, std::memory_order_relaxed);
                }
            });
        }

        static void _clear_array_childs(ArrayBase* array, ArrayMetadata* metadata) noexcept {
            if (!metadata->child_pointers.size) {
                return;
            }
            auto data = (uintptr_t)array + ArrayDataOffset;
            for (size_t i = 0; i < array->size; ++i, data += metadata->object_size) {
                _clear_childs((void*)data, metadata->child_pointers);
            }
        }

        // With clear_childs, the children are nulled first so the destructor touches no other dead object.
        static void _destroy(Page* page, void* ptr, bool clear_childs) noexcept {
            auto destroy = page->metadata->destroy;
            if (!destroy) {
                return;
            }
            if (!page->metadata->is_array) {
                if (clear_childs) {
                    _clear_childs(ptr, page->metadata->child_pointers);
                }
                destroy(ptr);
            } else {
                auto array = (ArrayBase*)ptr;
                auto metadata = array->metadata.load(std::memory_order_acquire);
                if (metadata && !metadata->tracked_ptrs_only) {
                    if (clear_childs) {
                        _clear_array_childs(array, metadata);
                    }
                    destroy(ptr);
                }
            }
        }

        // SPEC.md §4.7: every hook of the cycle runs before any child is nulled or destructor runs, at most once
        // per allocation.
        void _finalize_garbage() noexcept {
            for (auto page = _unreachable_pages; page; page = page->next_unreachable) {
                auto finalize = page->metadata->finalize;
                if (!finalize) {
                    continue;
                }
                auto states = page->states();
                auto flags = page->flags();
                auto count = page->flags_count();
                for (unsigned i = 0; i < count; ++i) {
                    auto& flag = flags[i];
                    auto candidates = flag.registered & ~flag.marked & ~flag.finalized;
                    auto offset = i * Page::FlagBitCount;
                    while (candidates) {
                        auto countr_zero = std::countr_zero(candidates);
                        auto index = offset + countr_zero;
                        if (states[index].load(std::memory_order_relaxed) == State::Unreachable) {
                            flag.finalized |= Page::Flag(1) << countr_zero;
                            finalize(page->pointer_of(index));
                        }
                        candidates &= candidates - 1;
                    }
                }
            }
        }

        size_t _remove_garbage() noexcept {
            _finalize_garbage();
            // Keep what the hooks made reachable again, and everything it reaches, for this cycle.
            _mark_updated<false>();
            while (_reachable_pages) {
                _mark_reachable();
                _mark_updated<false>();
            }
            size_t removed = 0;
            auto page = _unreachable_pages;
            _unreachable_pages = nullptr;
            while(page) {
                auto states = page->states();
                auto flags = page->flags();
                auto count = page->flags_count();
                // acquire: the states read below are at least as new as the last barrier on this page.
                (void)page->state_updated.load(std::memory_order_acquire);
                for (unsigned i = 0; i < count; ++i) {
                    auto& flag = flags[i];
                    auto unreachable = flag.registered & ~flag.marked;
                    auto offset = i * Page::FlagBitCount;
                    if (unreachable) {
                        page->unused_occur.store(true, std::memory_order_relaxed);
                    }
                    while (unreachable) {
                        auto countr_zero = std::countr_zero(unreachable);
                        auto index = offset + countr_zero;
                        auto mask = Page::Flag(1) << countr_zero;
                        auto state = states[index].load(std::memory_order_relaxed);
                        unreachable &= unreachable - 1;
                        // Defence in depth: a protected slot is never destroyed; the next cycle decides.
                        if (state & State::ReachableMask) {
                            flag.marked |= mask;
                            continue;
                        }
                        if (state == State::Unreachable) {
                            _destroy(page, page->pointer_of(index), true);
                        }
                        ++removed;
                        flag.finalized &= ~mask;
                        // release: publishes the destruction to the fill() that reuses the slot.
                        states[index].store(State::Unused, std::memory_order_release);
                        ++page->unused_counter_gc;
                    }
                    flag.registered &= flag.marked;
                }
                page->unreachable = false;
                page = page->next_unreachable;
            }
            return removed;
        }

        void _release_unused_pages() noexcept {
            Metadata* metadata = nullptr;
            for (auto page = _registered_pages; page; page = page->next_registered) {
                if (!page->unused_occur.load(std::memory_order_relaxed)) {
                    continue;
                }
                // acquire: pairs with alloc()'s release store when it took the page off the buffer, which
                // publishes its unused_counter_mutators.
                if (page->on_empty_list.load(std::memory_order_acquire)) {
                    continue;
                }
                auto count = page->metadata->object_count;
                auto unused = page->unused_counter_gc;
                // acquire: pairs with the release fetch_add of slots returned at thread exit.
                unused += page->unused_atomic.load(std::memory_order_acquire);
                unused -= page->unused_counter_mutators;
                if (unused > count / 2) {
                    page->unused_occur.store(false, std::memory_order_relaxed);
                    if (!page->metadata->used) {
                        page->metadata->used = true;
                        page->metadata->next = metadata;
                        metadata = page->metadata;
                    }
                    page->on_empty_list.store(true, std::memory_order_relaxed);
                    page->next_empty = page->metadata->empty_page;
                    page->metadata->empty_page = page;
                }
            }

            _join_destroyer_threads();

            bool possible_empty_blocks = metadata != nullptr;
            while(metadata) {
                metadata->free(metadata->empty_page);
                metadata->used = false;
                metadata->empty_page = nullptr;
                metadata = metadata->next;
            }
            if (possible_empty_blocks) {
                BlockAllocator::release_empty();
            }
            or_fatal([this] {
                std::vector<Page*> pages_to_delete;
                Page* prev = nullptr;
                auto page = _registered_pages;
                while(page) {
                    auto next = page->next_registered;
                    if (!page->is_used) {
                        if (!prev) {
                            _registered_pages = next;
                        } else {
                            prev->next_registered = next;
                        }
                        if (!page->is_last_registered) {
                            pages_to_delete.push_back(page);
                        }
                    } else {
                        prev = page;
                    }
                    page = next;
                }
                if (pages_to_delete.size()) {
                    destroyer_threads().emplace_back([pages = std::move(pages_to_delete)] {
                        for (auto page: pages) {
                            delete page;
                        }
                    });
                }
            });
        }

        void _join_destroyer_threads() noexcept {
            for (auto& thread: destroyer_threads()) {
                thread.join();
            }
            destroyer_threads().clear();
        }

        void _cycle(uint64_t target, bool share_live_objects) noexcept {
            _share_live_objects = share_live_objects;
            // SPEC.md §4.6. The epoch opens first: a barrier that the demotion erases synchronizes with it (both are
            // RMWs on the slot state), so the threads, pages and objects registered next include everything that
            // barrier's thread published, and the stack scan sees its stores. Objects created after registration
            // stay unregistered: never swept this cycle and never traced, the barrier shades what they reference.
            _update_states();
            _register_threads();
            _register_pages();
            _register_objects();
            _mark_stack_roots();
            _live_object_count = 0;
            _mark_reachable();
            _mark_updated<true>();
            while (_reachable_pages) {
                _mark_reachable();
                _mark_updated<false>();
            }
            _last_live_object_count.store(_live_object_count, std::memory_order_relaxed);
            _last_removed = _remove_garbage();
            _share_live_objects = false;
            _release_unused_pages();

            std::unique_lock<std::mutex> lock(_mutex);
            // A request is complete after two cycles started after it: the first may keep objects allocated just
            // before it began (they are registered already shaded), the second reclaims them (SPEC.md §4.10).
            if (_previous_target > _completed) {
                _completed = _previous_target;
            }
            _previous_target = target;
            if (_snapshot_target && _completed >= _snapshot_target) {
                _snapshot_target = 0;
                _paused = true;
            }
            _cv_done.notify_all();
            _cv_resume.wait(lock, [this] { return !_paused; });
        }

        void _main_loop() noexcept {
            _thread_id.store(std::this_thread::get_id(), std::memory_order_relaxed);
            // After terminate() (SPEC.md §4.10): until a cycle reclaims nothing, at most MaxTerminationCycles, and
            // none starting later than TerminationBudget after the request, so threads that keep allocating cannot
            // hold terminate() for long.
            constexpr int MaxTerminationCycles = 5;
            constexpr auto TerminationBudget = std::chrono::seconds(1);
            int termination_cycles = 0;
            std::chrono::steady_clock::time_point termination_start;
            bool stop = false;
            do {
                // Before the cycle, so a wake-up that arrives during it is not overwritten.
                sleep_flag.store(true, std::memory_order_relaxed);
                uint64_t target;
                bool share_live_objects;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    target = _requested;
                    // The cycle that will complete the snapshot's request lists the live objects.
                    share_live_objects = _snapshot_target && _previous_target >= _snapshot_target;
                }
                or_fatal([&] {
                    _live_objects.clear();
                    _cycle(target, share_live_objects);
                });
                bool terminating = process_state().collector_terminating.load(std::memory_order_acquire);
                if (!terminating) {
                    std::unique_lock<std::mutex> lock(_mutex);
                    auto sleep_time = _short_sleep ? config::ShortSleepTime : config::LongSleepTime;
                    _short_sleep = false;
                    sleep_cv.wait_for(lock, sleep_time, [this] {
                        return !sleep_flag.load(std::memory_order_acquire)
                            || _requested > _completed
                            || process_state().collector_terminating.load(std::memory_order_relaxed);
                    });
                }
                MemoryCounters::reset_all();
                if (terminating) {
                    auto now = std::chrono::steady_clock::now();
                    if (termination_cycles++ == 0) {
                        termination_start = now;
                    }
                    stop = !_last_removed || termination_cycles >= MaxTerminationCycles
                        || now - termination_start >= TerminationBudget;
                }
            } while(!stop);
            _join_destroyer_threads();
            std::lock_guard<std::mutex> lock(_mutex);
            _stopped = true;
            _cv_done.notify_all();
            _cv_stopped.notify_all();
        }

        void _terminate() noexcept {
            if (_on_collector_thread()) {
                fatal("collector::terminate() on the collector thread");
            }
            if (_pause_owner_is_current()) {
                fatal("collector::terminate() while this thread holds a pause_guard");
            }
            std::unique_lock<std::mutex> lock(_mutex);
            process_state().collector_terminating.store(true, std::memory_order_release);
            waking_up();
            _cv_stopped.wait(lock, [this] { return _stopped; });
        }

        // Collector-thread state.
        Thread::Data* _registered_threads = {nullptr};
        Thread::Data* _last_thread_registered = {nullptr};
        Page* _reachable_pages = {nullptr};
        Page* _unreachable_pages = {nullptr};
        Page* _registered_pages = {nullptr};
        size_t _live_object_count = {0};
        size_t _last_removed = {0};
        bool _share_live_objects = {false};
        std::vector<std::thread> _destroyer_threads;
        bool _short_sleep = {false};

        // Shared; the plain fields are guarded by _mutex.
        std::atomic<std::thread::id> _thread_id = {};
        std::atomic<std::thread::id> _pause_owner = {};
        std::atomic<size_t> _last_live_object_count = {0};
        std::atomic<bool> sleep_flag = {true};
        std::mutex _mutex;
        std::condition_variable sleep_cv;
        std::condition_variable _cv_done;
        std::condition_variable _cv_resume;
        std::condition_variable _cv_stopped;
        uint64_t _requested = {0};
        uint64_t _completed = {0};
        uint64_t _previous_target = {0};
        bool _paused = {false};
        bool _snapshot_busy = {false};
        uint64_t _snapshot_target = {0};
        bool _stopped = {false};
        std::vector<void*> _live_objects;
    };

    inline Collector& collector_instance() noexcept {
        return SGCL_STATE_SYMBOL(collector_instance)();
    }

    inline void collector_init() noexcept {
        collector_instance();
    }

    inline void delete_unique(const void* p) noexcept {
        Collector::delete_unique(p);
    }

    inline void waking_up_collector() noexcept {
        collector_instance().waking_up();
    }

    inline void force_short_sleep() noexcept {
        collector_instance().force_short_sleep();
    }

    inline std::vector<std::thread>& destroyer_threads() noexcept {
        return collector_instance().destroyer_threads();
    }
}
