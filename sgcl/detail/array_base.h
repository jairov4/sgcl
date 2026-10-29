//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "types.h"

#include <memory>

namespace sgcl::detail {
    struct ArrayBase;

    // Header of a vector's storage: elements [0, size) are constructed, [size, capacity) are zero (SPEC.md §4.8).
    struct ArrayBase {
        constexpr ArrayBase(size_t s, size_t c) noexcept
        : size(s)
        , capacity(c) {
        }

        template<typename T>
        static constexpr auto get_destroy_function() -> void(*)(void*, size_t) noexcept {
            static_assert(std::is_nothrow_destructible_v<T>, "managed types must be nothrow-destructible (SPEC.md §3.4)");
            if constexpr (!std::is_trivially_destructible_v<T>) {
                return &_destroy<T>;
            } else {
                return nullptr;
            }
        }

        std::atomic<ArrayMetadata*> metadata = {nullptr};
        size_t size;
        const size_t capacity;

    private:
        template<class T>
        static void _destroy(void* data, size_t count) noexcept {
            for (size_t i = count; i > 0; --i) {
                std::destroy_at((T*)data + i - 1);
            }
        }
    };

    // Where elements start after the header, for any element alignment up to MaxAlign.
    inline constexpr size_t ArrayDataOffset = (sizeof(ArrayBase) + MaxAlign - 1) / MaxAlign * MaxAlign;
}
