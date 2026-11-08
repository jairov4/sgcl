//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "page.h"

namespace sgcl::detail {
    class PointerPoolBase {
    public:
        PointerPoolBase(void** i, unsigned s, unsigned o) noexcept
        : _indexes(i)
        , _size(s)
        , _offset(o)
        , _position(s) {
        }

        SGCL_API void fill(void* data) noexcept;

        SGCL_API void fill(Page* page) noexcept;

        bool is_empty() const noexcept {
            return _position == _size;
        }

        bool is_full() const noexcept {
            return _position == 0;
        }

        void* alloc() noexcept {
            return _indexes[_position++];
        }

        void free(void* p) noexcept {
            _indexes[--_position] = p;
        }

        void** indexes() const noexcept {
            return _indexes;
        }

    private:
        void** const _indexes;
        const unsigned _size;
        const unsigned _offset;
        unsigned _position;
    };
}
