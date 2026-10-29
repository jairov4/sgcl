//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "detail/maker.h"
#include "unique_ptr.h"

namespace sgcl {
    // SPEC.md §3.1.
    template<class T, class... A>
        requires (std::is_object_v<T> && !std::is_array_v<T>)
    unique_ptr<T> make_tracked(A&&... a) {
        static_assert(sizeof(detail::Array<sizeof(T)>) <= detail::PageDataSize, "object is too large (SPEC.md §8)");
        static_assert(alignof(T) <= alignof(void*), "managed types are at most pointer-aligned (SPEC.md §8)");
        return unique_ptr<T>(detail::Maker<T>::make_tracked(std::forward<A>(a)...));
    }
}
