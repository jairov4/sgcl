//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// On x86-64 the two highest address bits tag a tracked_ptr's storage class (SPEC.md §8).
#if defined(__x86_64__) || defined(_M_X64)
#define SGCL_ARCH_X86_64
#endif

// Linkage of the exported process-state functions (SPEC.md §3.8, §4.9).
#if !defined(SGCL_API)
#define SGCL_API
#endif

// Distinct managed types per process. A decimal literal: it is pasted into exported names, so images built with
// different values fail to link or load.
#if !defined(SGCL_MAX_TYPES_NUMBER)
#define SGCL_MAX_TYPES_NUMBER 4096
#endif

// Name of the finalize hook member function (SPEC.md §4.7).
#if !defined(SGCL_FINALIZE_HOOK)
#define SGCL_FINALIZE_HOOK sgcl_finalize
#endif

#define SGCL_DETAIL_CAT2(a, b) a##b
#define SGCL_DETAIL_CAT(a, b) SGCL_DETAIL_CAT2(a, b)
#define SGCL_STATE_SYMBOL(name) SGCL_DETAIL_CAT(name##_max_types_, SGCL_MAX_TYPES_NUMBER)

#include <chrono>
#include <cstddef>

namespace sgcl::config {
    inline constexpr auto   LongSleepTime = std::chrono::seconds(30);
    inline constexpr auto   ShortSleepTime = std::chrono::seconds(3);
    inline constexpr size_t PageSize = 0x10000;
    inline constexpr size_t MaxStackSize = 0x800000;
    inline constexpr size_t MaxOffsetForStackDetection = 0x1000;
    inline constexpr size_t MaxTypesNumber = SGCL_MAX_TYPES_NUMBER;

    static_assert(PageSize <= 0x10000, "PageSize is too large");
    static_assert((PageSize & (PageSize - 1)) == 0, "PageSize is not a power of 2");
    static_assert((MaxStackSize & (MaxStackSize - 1)) == 0, "MaxStackSize is not a power of 2");
}
