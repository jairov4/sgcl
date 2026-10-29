//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

namespace sgcl::detail {
    void delete_unique(const void* p) noexcept;

    // Destroys the object synchronously; its slot is reclaimed by a later cycle (SPEC.md §3.2).
    struct UniqueDeleter {
        template<class T>
        void operator()(T* p) const noexcept {
            delete_unique(p);
        }
    };
}
