//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "array_base.h"
#include "page_info.h"

namespace sgcl::detail {
    // A size class of vector storage: the header, then `Size` bytes of elements.
    template<size_t Size = 1>
    struct Array : ArrayBase {
        constexpr Array(size_t size, size_t capacity) noexcept
        : ArrayBase(size, capacity) {
        }

        ~Array() noexcept {
            auto metadata = this->metadata.load(std::memory_order_acquire);
            if (metadata && metadata->destroy) {
                metadata->destroy(data, size);
            }
        }

        alignas(MaxAlign) char data[Size];
    };

    static_assert(sizeof(Array<MaxAlign>) == ArrayDataOffset + MaxAlign, "elements start at ArrayDataOffset");

    // Storage larger than a page: one large allocation (SPEC.md §4.1).
    template<>
    struct PageInfo<Array<>> : public PageInfo<Array<PageDataSize>> {
    };
}
