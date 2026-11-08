//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "array_base.h"

namespace sgcl::detail {
    // Describes the element type of a vector's storage.
    struct ArrayMetadata {
        ArrayMetadata(ChildPointers& child_pointers, void (*destroy)(void*, size_t) noexcept, size_t object_size,
                      bool tracked_ptrs_only) noexcept
        : child_pointers(child_pointers)
        , destroy(destroy)
        , object_size(object_size)
        , tracked_ptrs_only(tracked_ptrs_only) {
        }

        ChildPointers& child_pointers;
        void (*const destroy)(void*, size_t) noexcept;
        const size_t object_size;
        // Elements are tracked_ptrs: the collector nulls them and skips their destructors.
        const bool tracked_ptrs_only;
    };
}
