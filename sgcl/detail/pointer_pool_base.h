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

        void fill(void* data) noexcept {
            for(auto i = 0u; i < _size; ++i, data = (void*)((uintptr_t)data + _offset)) {
                _indexes[i] = data;
            }
            _position = 0;
        }

        void fill(Page* page) noexcept {
            auto data = page->data;
            auto object_size = page->metadata->object_size;
            auto states = page->states();
            auto count = page->metadata->object_count;
            [[maybe_unused]] bool unused_occur = false;
            for(int i = count - 1; i >= 0; --i) {
                // acquire: pairs with the collector's (or another thread's dying allocator's)
                // release store of State::Unused, so this thread also sees whatever was
                // sequenced-before that release -- notably the freed object's destructor having
                // already run -- before handing this memory out again for a new object.
                if (states[i].load(std::memory_order_acquire) == State::Unused) {
                    _indexes[--_position] = (void*)(data + i * object_size);
                    states[i].store(State::Reserved, std::memory_order_relaxed);
                    ++page->unused_counter_mutators;
                    assert(unused_occur = true);
                }
            }
            assert(unused_occur);
        }

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

    private:
        void** const _indexes;
        const unsigned _size;
        const unsigned _offset;
        unsigned _position;
    };
}
