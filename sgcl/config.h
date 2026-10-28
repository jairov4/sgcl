//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// logging level [0-3]
#define SGCL_LOG_PRINT_LEVEL 0

//------------------------------------------------------------------------------
// Reduces memory usage on x86-64 platforms by using two highest bits of pointer
// Warning!
// Note: User heap must be allocated in the low half of virtual address space
//------------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
#define SGCL_ARCH_X86_64
#endif

//------------------------------------------------------------------------------
// Process-wide state lives in one image when SGCL_EXTERNAL_STATE is defined.
// Every shared library and executable of a process that uses SGCL then reaches
// the same collector, thread registry, type counter and memory counters through
// the SGCL_API functions declared in detail/state.h, which exactly one
// translation unit of the owning image defines by including
// detail/state_impl.h. SGCL_API marks those functions' linkage: dllexport in the
// owner and dllimport elsewhere on Windows, default visibility on ELF/Mach-O.
// Every image must agree on SGCL_EXTERNAL_STATE and SGCL_MAX_TYPES_NUMBER.
// Without SGCL_EXTERNAL_STATE the library stays header-only and its state is
// private to each image.
//------------------------------------------------------------------------------
#if defined(SGCL_EXTERNAL_STATE)
#if !defined(SGCL_API)
#if defined(_WIN32)
#error "SGCL_EXTERNAL_STATE needs SGCL_API: __declspec(dllexport) in the owner, __declspec(dllimport) elsewhere"
#endif
#define SGCL_API
#endif
// Exported names carry SGCL_MAX_TYPES_NUMBER, which sizes Thread, so an image
// built with another value fails to link or load instead of corrupting memory.
#define SGCL_DETAIL_CAT2(a, b) a##b
#define SGCL_DETAIL_CAT(a, b) SGCL_DETAIL_CAT2(a, b)
#define SGCL_STATE_SYMBOL(name) SGCL_DETAIL_CAT(name##_max_types_, SGCL_MAX_TYPES_NUMBER)
#endif

// Distinct tracked types per process; each thread keeps one allocator slot per
// type. A decimal integer literal: it is pasted into exported names.
#if !defined(SGCL_MAX_TYPES_NUMBER)
#define SGCL_MAX_TYPES_NUMBER 4096
#endif

#include <chrono>
#include <cstddef>

namespace sgcl::config {
    [[maybe_unused]] static constexpr auto   LongSleepTime = std::chrono::seconds(30);
    [[maybe_unused]] static constexpr auto   ShortSleepTime = std::chrono::seconds(3);
                     static constexpr size_t PageSize = 0x10000;
                     static constexpr size_t MaxStackSize = 0x800000;
    [[maybe_unused]] static constexpr size_t MaxOffsetForStackDetection = 0x1000;
    [[maybe_unused]] static constexpr size_t MaxTypesNumber = SGCL_MAX_TYPES_NUMBER;

    static_assert(PageSize <= 0x10000, "PageSize is too large");
    static_assert((PageSize & (PageSize - 1)) == 0, "PageSize is not a power of 2");
    static_assert((MaxStackSize & (MaxStackSize - 1)) == 0, "MaxStackSize is not a power of 2");
}
