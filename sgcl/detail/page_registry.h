//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"
#include "fatal.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace sgcl::detail {
    // The set of PageSize-aligned regions that currently start a live page (SPEC.md §4.4): a word read at a child
    // offset is traced only when it points into one, so a non-pointer value there is ignored. Two levels over a
    // 48-bit address space; leaves are created on demand and never freed. Constant-initialized.
    class PageRegistry {
    public:
        static constexpr unsigned AddressBits = 48;
        static constexpr unsigned PageBits = 16;
        static constexpr unsigned LeafBits = 16;
        static constexpr size_t LeafWords = (size_t(1) << LeafBits) / 64;
        static constexpr size_t RootSize = size_t(1) << (AddressBits - PageBits - LeafBits);

        static_assert(config::PageSize == (size_t(1) << PageBits));

        // Before any object of the page is handed out.
        void add(const void* page) noexcept {
            auto [leaf, word, bit] = _locate(page, true);
            leaf[word].fetch_or(bit, std::memory_order_release);
        }

        // Collector only, before the page's memory is reused.
        void remove(const void* page) noexcept {
            auto [leaf, word, bit] = _locate(page, true);
            leaf[word].fetch_and(~bit, std::memory_order_relaxed);
        }

        bool contains(const void* p) const noexcept {
            auto page = (uintptr_t)p >> PageBits;
            if (page >> (AddressBits - PageBits)) {
                return false;
            }
            auto leaf = _root[page >> LeafBits].load(std::memory_order_acquire);
            if (!leaf) {
                return false;
            }
            auto index = page & ((size_t(1) << LeafBits) - 1);
            // acquire: pairs with add(), so the page's header word is visible once its bit is.
            return leaf[index / 64].load(std::memory_order_acquire) & (uint64_t(1) << (index % 64));
        }

    private:
        struct Location {
            std::atomic<uint64_t>* leaf;
            size_t word;
            uint64_t bit;
        };

        Location _locate(const void* p, bool create) noexcept {
            auto page = (uintptr_t)p >> PageBits;
            if (page >> (AddressBits - PageBits)) {
                fatal("managed memory above the 48-bit address space");
            }
            auto& slot = _root[page >> LeafBits];
            auto leaf = slot.load(std::memory_order_acquire);
            if (!leaf && create) {
                auto fresh = (std::atomic<uint64_t>*)checked_operator_new(sizeof(std::atomic<uint64_t>) * LeafWords);
                for (size_t i = 0; i < LeafWords; ++i) {
                    new (&fresh[i]) std::atomic<uint64_t>(0);
                }
                if (slot.compare_exchange_strong(leaf, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    leaf = fresh;
                } else {
                    ::operator delete(fresh);
                }
            }
            auto index = page & ((size_t(1) << LeafBits) - 1);
            return {leaf, index / 64, uint64_t(1) << (index % 64)};
        }

        std::atomic<std::atomic<uint64_t>*> _root[RootSize] = {};
    };
}
