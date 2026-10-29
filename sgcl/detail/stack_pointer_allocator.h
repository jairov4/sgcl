//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"
#include "types.h"

#include <cstring>

namespace sgcl::detail {
    // A thread's shadow stack (SPEC.md §4.3): one cell per pointer-sized stack word, indexed by the word's
    // distance below the thread's stack base, covering the top MaxStackSize bytes of the thread's real stack.
    // Pages are zeroed on first use.
    struct StackPointerAllocator {
#ifdef SGCL_ARCH_X86_64
        static constexpr size_t Scale = 1;
        static constexpr unsigned PageSize = 4096;
#else
        // A tracked_ptr is two words here, so every other stack word can hold a cell.
        static constexpr size_t Scale = 2;
        static constexpr unsigned PageSize = 2048;
#endif
        static constexpr size_t ShadowSize = config::MaxStackSize / Scale;
        static constexpr unsigned PageCount = ShadowSize / PageSize;

        // [stack_low, stack_base) is the thread's real stack, or both are 0 when the platform did not report it.
        StackPointerAllocator(uintptr_t stack_low, uintptr_t stack_base) noexcept
        : low(stack_low)
        , base(stack_base) {
        }

        // The cell for a tracked_ptr at `p`, or null when `p` is outside the covered part of the real stack: a
        // sanitizer's fake stack, a fiber's stack or a frame deeper than MaxStackSize. Such a tracked_ptr becomes
        // an external root.
        Pointer* alloc(void* p) noexcept {
            size_t offset;
            if (base) {
                auto address = (uintptr_t)p;
                if (address < low || address >= base || base - address >= config::MaxStackSize) {
                    return nullptr;
                }
                offset = ((base - address) / Scale) & ~(sizeof(RawPointer) - 1);
            } else {
                offset = ((uintptr_t)p / Scale) % ShadowSize & ~(sizeof(RawPointer) - 1);
            }
            auto index = offset / PageSize;
            if (!is_used[index].load(std::memory_order_relaxed)) {
                std::memset((char*)data + index * PageSize, 0, PageSize);
                is_used[index].store(true, std::memory_order_release);
            }
            return (Pointer*)((char*)data + offset);
        }

        const uintptr_t low;
        const uintptr_t base;
        std::atomic<bool> is_used[PageCount] = {};
        union {
            RawPointer data[ShadowSize / sizeof(RawPointer)];
        };
    };
}
