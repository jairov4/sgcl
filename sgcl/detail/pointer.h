//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "page.h"

namespace sgcl::detail {
    // A cell (SPEC.md §4.3): the atomic word that holds a referent's address. Storing a non-null address runs
    // the write barrier (§4.5).
    class Pointer {
    public:
        Pointer() noexcept
        : _ptr(nullptr) {
        }

        Pointer(const Pointer&) = delete;
        Pointer& operator=(const Pointer&) = delete;

        void* load() const noexcept {
            return _ptr.load(std::memory_order_relaxed);
        }

        void store(std::nullptr_t) noexcept {
            _ptr.store(nullptr, std::memory_order_release);
        }

        void store(const void* p) noexcept {
            store_no_update(p);
            if (p) {
                Page::set_state<State::Reachable>(p);
            }
        }

        // Takes over an object from its unique_ptr (SPEC.md §4.2).
        void store_handover(const void* p) noexcept {
            store_no_update(p);
            if (p) {
                Page::hand_over(p);
            }
        }

        // Without the barrier: for a value already held by another cell of the same object.
        void store_no_update(const void* p) noexcept {
            _ptr.store(const_cast<void*>(p), std::memory_order_release);
        }

    private:
        RawPointer _ptr;
    };

    static_assert(sizeof(Pointer) == sizeof(RawPointer));
}
