//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"
#include "types.h"

#include <atomic>
#include <bit>
#include <cstring>
#include <vector>

namespace sgcl::detail {
    // The child layout of one managed type (SPEC.md §4.4): one bit per pointer-sized word of the object, set when
    // a child tracked_ptr was constructed at that word. Bits are only ever added, during constructions.
    struct ChildPointers {
        static constexpr size_t WordsPerByte = 8;

        SGCL_API ChildPointers(bool final_layout, size_t object_size) noexcept;

        // The layout of an external-root cell: one child, at offset 0.
        struct CellTag {};
        SGCL_API explicit ChildPointers(CellTag) noexcept;

        ChildPointers(const ChildPointers&) = delete;
        ChildPointers& operator=(const ChildPointers&) = delete;

        // Whether the tracked_ptr cell at `byte_offset` of an object under construction is a child. While the
        // layout is being discovered the object was filled with the sentinel word 1; a cell found on the sentinel
        // is recorded. A cell the layout does not record is not a child; it becomes an external root.
        bool claim(void* cell, size_t byte_offset) noexcept {
            const size_t word = byte_offset / sizeof(RawPointer);
            const uint8_t bit = uint8_t(1u << (word % WordsPerByte));
            auto& byte = map[word / WordsPerByte];
            if (!final.load(std::memory_order_acquire)) {
                size_t value;
                std::memcpy(&value, cell, sizeof(value));
                if (value == 1) {
                    if (!(byte.fetch_or(bit, std::memory_order_relaxed) & bit)) {
                        // release: a collector that sees the new version sees the bit.
                        version.fetch_add(1, std::memory_order_release);
                    }
                    return true;
                }
            }
            return byte.load(std::memory_order_relaxed) & bit;
        }

        // Calls f(word_index) for every recorded word.
        template<class F>
        void for_each(F&& f) const noexcept {
            for (size_t index = 0; index < size; ++index) {
                unsigned flags = map[index].load(std::memory_order_relaxed);
                while (flags) {
                    f(index * WordsPerByte + std::countr_zero(flags));
                    flags &= flags - 1;
                }
            }
        }

        // Collector only: the recorded words as a compact list, rebuilt when the layout gained a bit.
        const std::vector<uint32_t>& words() const {
            auto current = version.load(std::memory_order_acquire);
            if (current != _words_version) {
                _words.clear();
                for_each([&](size_t word) { _words.push_back((uint32_t)word); });
                _words_version = current;
            }
            return _words;
        }

        std::atomic<bool> final;
        const size_t size;
        std::atomic<uint8_t>* const map;
        std::atomic<unsigned> version = {0};

    private:
        mutable std::vector<uint32_t> _words;
        mutable unsigned _words_version = ~0u;
    };
}
