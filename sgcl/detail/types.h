//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "fatal.h"
#include "state.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace sgcl::detail {
    using RawPointer = std::atomic<void*>;

    // The strictest alignment a managed type or vector element may have, on every platform (SPEC.md §8): the
    // largest alignof(std::max_align_t) of the supported targets.
    inline constexpr size_t MaxAlign = 16;

    template<size_t>
    struct Array;
    struct ArrayBase;
    struct ArrayMetadata;
    struct Block;
    class  BlockAllocator;
    struct ChildPointers;
    class  Collector;
    struct Counter;
    struct DataPage;
    template<class>
    struct MayContainTracked;
    template<class>
    class  Maker;
    class  MemoryCounters;
    struct Metadata;
    template<class>
    class  ObjectAllocator;
    class  ObjectAllocatorBase;
    template<class>
    class  ObjectPoolAllocator;
    class  ObjectPoolAllocatorBase;
    struct Page;
    template<class>
    struct PageInfo;
    class  Pointer;
    template<unsigned, unsigned>
    class  PointerPool;
    class  PointerPoolBase;
    struct StackPointerAllocator;
    class  Thread;
    // Base of tracked_ptr: lets metadata recognise arrays whose elements are only tracked_ptrs.
    class  Tracked {};
    template<class>
    struct TypeInfo;
    struct UniqueDeleter;

    // SPEC.md §4.2.
    enum State : uint8_t {
        Used = 0,
        Reachable = 1,
        UniqueLock = 2,
        // Reachable | UniqueLock: the constructor is running. A superset of both bits, so every ReachableMask and
        // CreatedMask test treats it as protected and registered; a store of the pointer does not hand it over.
        Constructing = 3,
        Destroyed = 4,
        BadAlloc = 8,
        Reserved = 16,
        Unused = 32,
        Unreachable = Used,
        ReachableMask = 3,
        CreatedMask = 15
    };

    void collector_init() noexcept;
    Collector& collector_instance() noexcept;
    Thread& current_thread() noexcept;
    SGCL_API Collector& SGCL_STATE_SYMBOL(collector_instance)() noexcept;
    SGCL_API Thread& SGCL_STATE_SYMBOL(current_thread)() noexcept;
    void waking_up_collector() noexcept;
    void force_short_sleep() noexcept;
    std::vector<std::thread>& destroyer_threads() noexcept;
}
