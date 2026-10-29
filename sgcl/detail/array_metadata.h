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
        template<class T>
        ArrayMetadata(T*) noexcept
        : child_pointers(TypeInfo<T>::child_pointers())
        , destroy(ArrayBase::get_destroy_function<T>())
        , object_size(TypeInfo<T>::ObjectSize)
        , tracked_ptrs_only(TypeInfo<T>::IsTracked) {
        }

        ChildPointers& child_pointers;
        void (*const destroy)(void*, size_t) noexcept;
        const size_t object_size;
        // Elements are tracked_ptrs: the collector nulls them and skips their destructors.
        const bool tracked_ptrs_only;
    };
}
