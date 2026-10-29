//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "array_metadata.h"
#include "page.h"
#include "types.h"

#include <cstddef>
#include <type_traits>

namespace sgcl::detail {
    // The slot bytes of a page whatever the type's alignment.
    inline constexpr size_t PageDataSize = config::PageSize - MaxAlign;

    // Whether objects of T may hold a tracked_ptr: one always makes T non-trivially-default-constructible.
    template<class T>
    struct MayContainTracked {
#ifdef SGCL_ARCH_X86_64
        static constexpr size_t PointerSize = sizeof(RawPointer);
#else
        static constexpr size_t PointerSize = sizeof(RawPointer) * 2;
#endif
        using Type = std::remove_extent_t<T>;
        static constexpr auto value = std::is_same_v<Type, Pointer>
                                      || (!std::is_trivially_default_constructible_v<Type>
                                          && sizeof(Type) >= PointerSize
                                          && alignof(Type) >= alignof(RawPointer));
    };

    template<class T>
    struct PageInfo {
        using Type = std::remove_cv_t<T>;
        static constexpr size_t ObjectSize = sizeof(std::remove_extent_t<Type>);
        // Word 0 of a page holds its header pointer; slots follow at the type's alignment.
        static constexpr size_t DataOffset = std::max(sizeof(uintptr_t), alignof(std::remove_extent_t<Type>));
        static constexpr size_t PageCapacity = config::PageSize - DataOffset;
        static constexpr size_t ObjectCount = std::max(size_t(1), PageCapacity / ObjectSize);
        static constexpr size_t StatesSize = (sizeof(std::atomic<State>) * ObjectCount + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
        static constexpr size_t FlagsCount = (ObjectCount + Page::FlagBitCount - 1) / Page::FlagBitCount;
        static constexpr size_t FlagsSize = sizeof(Page::Flags) * FlagsCount;
        static constexpr size_t HeaderSize = sizeof(Page) + StatesSize + FlagsSize;
        using Allocator = std::conditional_t<ObjectSize <= PageCapacity, ObjectPoolAllocator<Type>, ObjectAllocator<Type>>;

        static_assert(std::is_nothrow_destructible_v<std::remove_extent_t<Type>>,
                      "managed types must be nothrow-destructible (SPEC.md §3.4)");
        static_assert(alignof(std::remove_extent_t<Type>) <= MaxAlign,
                      "managed types are at most 16-byte aligned (SPEC.md §8)");

        static constexpr auto get_destroy_function() -> void(*)(void*) noexcept {
            if constexpr (!std::is_trivially_destructible_v<Type>) {
                return &_destroy;
            } else {
                return nullptr;
            }
        }

        // SPEC.md §4.7: a class declaring or inheriting `void SGCL_FINALIZE_HOOK() noexcept`.
        static constexpr auto get_finalize_function() -> void(*)(void*) noexcept {
            if constexpr (std::is_class_v<Type> && requires(Type& t) { t.SGCL_FINALIZE_HOOK(); }) {
                static_assert(noexcept(std::declval<Type&>().SGCL_FINALIZE_HOOK()),
                              "the finalize hook must be noexcept (SPEC.md §4.7)");
                return &_finalize;
            } else {
                return nullptr;
            }
        }

        // Leaked, like every per-type singleton: the collector reads them until the process exits, which may be
        // after static destruction. Function-local statics, so a construction during static initialization of
        // another translation unit finds them built.
        inline static Metadata& private_metadata() noexcept {
            static auto metadata = checked_new<Metadata>((std::remove_extent_t<Type>*)0);
            return *metadata;
        }

        inline static ArrayMetadata& array_metadata() noexcept {
            static auto metadata = checked_new<ArrayMetadata>((std::remove_extent_t<Type>*)0);
            return *metadata;
        }

        inline static ChildPointers& child_pointers() noexcept {
            static auto cp = _make_child_pointers();
            return *cp;
        }

    private:
        static ChildPointers* _make_child_pointers() noexcept {
            if constexpr (std::is_same_v<Type, Pointer>) {
                return checked_new<ChildPointers>(ChildPointers::CellTag{});
            } else {
                return checked_new<ChildPointers>(!MayContainTracked<Type>::value, ObjectSize);
            }
        }

        static void _finalize(void* p) noexcept {
            ((Type*)p)->SGCL_FINALIZE_HOOK();
        }

        static void _destroy(void* p) noexcept {
            std::destroy_at((Type*)p);
        }
    };
}
