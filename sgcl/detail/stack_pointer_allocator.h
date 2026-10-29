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
    // distance below the thread's stack base. Pages are zeroed on first use.
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

        // `stack_base` is the highest address of the thread's stack, or 0 when the platform did not report it.
        explicit StackPointerAllocator(uintptr_t stack_base) noexcept
        : base(stack_base) {
        }

        Pointer* alloc(void* p) noexcept {
            size_t offset;
            if (base) {
                auto distance = base - (uintptr_t)p;
                if (distance >= config::MaxStackSize) {
                    fatal("a tracked_ptr lies deeper than MaxStackSize below its thread's stack base");
                }
                offset = (distance / Scale) & ~(sizeof(RawPointer) - 1);
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

        const uintptr_t base;
        std::atomic<bool> is_used[PageCount] = {};
        union {
            RawPointer data[ShadowSize / sizeof(RawPointer)];
        };
    };
}
