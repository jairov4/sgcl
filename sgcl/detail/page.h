//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "metadata.h"

#include <cassert>
#include <cstring>

namespace sgcl::detail {
    // The header of one data page or large allocation (SPEC.md §4.1).
    struct Page {
        template<class T>
        using Info = TypeInfo<T>;

        using Flag = uint64_t;
        static constexpr unsigned FlagBitCount = sizeof(Flag) * 8;

        // Collector-private bitmaps, one bit per slot.
        struct Flags {
            Flag registered = {0};
            Flag reachable = {0};
            Flag marked = {0};
            Flag finalized = {0};
        };

        template<class T>
        Page(Block* block, T* data) noexcept
        : metadata(&Info<T>::private_metadata())
        , block(block)
        , data((uintptr_t)data)
        , multiplier((1ull << 32 | 0x10000) / metadata->object_size) {
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

        // Placement-constructed in a block of TypeInfo<T>::HeaderSize bytes from the unsized ::operator new, so
        // `delete page` must use the matching unsized form.
        static void operator delete(void* p, size_t) noexcept {
            ::operator delete(p);
        }

        std::atomic<State>* states() const noexcept {
            return (std::atomic<State>*)(this + 1);
        }

        Flags* flags() const noexcept {
            auto states_size = (sizeof(std::atomic<State>) * metadata->object_count + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
            return (Flags*)((uintptr_t)states() + states_size);
        }

        unsigned flags_count() const noexcept {
            return (metadata->object_count + FlagBitCount - 1) / FlagBitCount;
        }

        void clear_flags() noexcept {
            auto flags = this->flags();
            auto count = flags_count();
            for (unsigned i = 0; i < count; ++i) {
                flags[i].reachable = 0;
                flags[i].marked = 0;
            }
        }

        static constexpr unsigned flag_index_of(unsigned i) noexcept {
            return i / FlagBitCount;
        }

        static constexpr Flag flag_mask_of(unsigned i) noexcept {
            return Flag(1) << (i % FlagBitCount);
        }

        unsigned index_of(const void* p) const noexcept {
            return ((uintptr_t)p - data) * multiplier >> 32;
        }

        void* pointer_of(unsigned index) const noexcept {
            return (void*)(data + index * metadata->object_size);
        }

        static Page* page_of(const void* p) noexcept {
            auto page = ((uintptr_t)p & ~(uintptr_t)(config::PageSize - 1));
            return *((Page**)page);
        }

        static void* base_address_of(const void* p) noexcept {
            auto page = page_of(p);
            return page->pointer_of(page->index_of(p));
        }

        static std::atomic<State>& state_of(const void* p) noexcept {
            auto page = page_of(p);
            return page->states()[page->index_of(p)];
        }

        template<State S>
        static void set_state(const void* p) noexcept {
            auto page = Page::page_of(p);
            auto& state = page->states()[page->index_of(p)];
            if constexpr(S == State::Constructing || S == State::UniqueLock || S == State::BadAlloc) {
                state.store(S, std::memory_order_relaxed);
                // state_updated too: a slot that stays Constructing/UniqueLock while its page is already on the
                // unreachable list must keep being rescanned, or the sweep could destroy it.
                page->state_updated.store(true, std::memory_order_release);
                page->object_created.store(true, std::memory_order_release);
            } else if constexpr(S == State::Reachable) {
                // The write barrier (SPEC.md §4.2, §4.5): only Used is promoted. UniqueLock and Constructing keep
                // their protection (a store is an alias, not a handover), and a dead or free slot is never revived.
                // Always an RMW, so it is ordered against the collector's Reachable -> Used demotion and the cell
                // store before it is visible to the cycle that demotes.
                auto current = state.load(std::memory_order_relaxed);
                while ((current == State::Used || current == State::Reachable)
                       && !state.compare_exchange_weak(current, State::Reachable, std::memory_order_acq_rel,
                                                       std::memory_order_relaxed)) {
                }
                page->state_updated.store(true, std::memory_order_release);
            } else {
                state.store(S, std::memory_order_release);
            }
        }

        // A unique_ptr gives its object to a tracked_ptr: UniqueLock -> Reachable.
        static void hand_over(const void* p) noexcept {
            auto page = Page::page_of(p);
            page->states()[page->index_of(p)].exchange(State::Reachable, std::memory_order_acq_rel);
            page->state_updated.store(true, std::memory_order_release);
        }

        Metadata* const metadata;
        Block* const block;
        const uintptr_t data;
        const uint64_t multiplier;
        size_t alloc_size = 0;
        bool reachable = {false};
        bool unreachable = {false};
        bool is_used = {true};
        bool is_last_registered = {false};
        std::atomic_bool object_created = {false};
        std::atomic_bool state_updated = {false};
        std::atomic_bool on_empty_list = {false};
        std::atomic_bool unused_occur = {true};
        // Slots refilled into a pool from this page: written by the mutator that took the page off its type's
        // page buffer, before its release store of on_empty_list = false; read and reset by the collector only
        // after acquiring that store.
        unsigned int unused_counter_mutators = {0};
        unsigned int unused_counter_gc = {0};
        std::atomic_uint unused_atomic = {0};
        Page* next_reachable = {nullptr};
        Page* next_unreachable = {nullptr};
        Page* next_registered = {nullptr};
        Page* next_empty = {nullptr};
        Page* next = {nullptr};
    };
}
