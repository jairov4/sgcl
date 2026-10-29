//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "array.h"
#include "thread.h"
#include "type_info.h"
#include "unique_ptr.h"

#include <cstring>
#include <type_traits>

namespace sgcl::detail {
    template<class T>
    class Maker {
        using Info = TypeInfo<T>;
        using Type = typename Info::Type;

    public:
        // Constructs a T in storage that is already a managed slot or a vector element (SPEC.md §4.4). On a throw
        // the storage is zero again, as unconstructed managed storage must be.
        template<class... A>
        static void construct(void* p, A&&... a) {
            if constexpr(Info::MayContainTracked) {
                auto& layout = Info::child_pointers();
                const bool discover = !layout.final.load(std::memory_order_acquire);
                _prepare(p, discover);
                auto& thread = current_thread();
                auto guard = thread.use_construction({(uintptr_t)p, sizeof(Type), &layout});
                try {
                    _construct(p, std::forward<A>(a)...);
                } catch (...) {
                    std::memset(p, 0, sizeof(Type));
                    throw;
                }
                if (discover) {
                    layout.final.store(true, std::memory_order_release);
                }
            } else {
                _construct(p, std::forward<A>(a)...);
            }
        }

        static void destroy(T* p) noexcept {
            if constexpr(!std::is_trivially_destructible_v<T>) {
                std::destroy_at(p);
            }
        }

        // SPEC.md §3.1, §5.1.
        template<class... A>
        static UniquePtr<T> make_tracked(A&&... a) {
            auto& thread = current_thread();
            void* mem = thread.alocator<Type>().alloc();
            if constexpr(Info::MayContainTracked) {
                auto& layout = Info::child_pointers();
                const bool discover = !layout.final.load(std::memory_order_acquire);
                _prepare(mem, discover);
                auto guard = thread.use_construction({(uintptr_t)mem, sizeof(Type), &layout});
                _construct_and_register(mem, std::forward<A>(a)...);
                if (discover) {
                    layout.final.store(true, std::memory_order_release);
                }
            } else {
                _construct_and_register(mem, std::forward<A>(a)...);
            }
            return UniquePtr<T>((Type*)mem);
        }

    private:
        // Discovery fills the storage with the sentinel word 1; otherwise it is zeroed.
        static void _prepare(void* p, bool discover) noexcept {
            if (discover) {
                std::fill_n((size_t*)p, sizeof(Type) / sizeof(size_t), size_t(1));
            } else {
                std::memset(p, 0, sizeof(Type));
            }
        }

        template<class... A>
        static void _construct(void* p, A&&... a) {
            if constexpr(sizeof...(A)) {
                new(p) Type(std::forward<A>(a)...);
            } else if constexpr(std::is_trivially_default_constructible_v<Type>) {
                // Zeroed, as std::vector::resize does; such a type holds no tracked_ptr.
                new(p) Type();
            } else {
                // Default-initialized: value-initialization would zero the storage first and erase the discovery
                // sentinel before the members are constructed (SPEC.md §4.4).
                new(p) Type;
            }
        }

        // The slot is Constructing while the constructor runs and BadAlloc if it throws (SPEC.md §4.2).
        template<class... A>
        static void _construct_and_register(void* p, A&&... a) {
            Page::set_state<State::Constructing>(p);
            try {
                _construct(p, std::forward<A>(a)...);
            } catch (...) {
                Page::set_state<State::BadAlloc>(p);
                throw;
            }
            Page::set_state<State::UniqueLock>(p);
        }
    };

    // The storage of a vector: `capacity` zeroed elements of T, none constructed (SPEC.md §4.8).
    template<class T>
    class ArrayMaker {
        using Info = TypeInfo<T>;
        using Type = typename Info::Type;

    public:
        static constexpr size_t max_capacity() noexcept {
            return (SIZE_MAX - ArrayDataOffset - 2 * config::PageSize) / sizeof(Type);
        }

        // Returns the address of element 0; the slot is UniqueLock. `capacity` may be rounded up to the size
        // class. A capacity beyond max_capacity() is fatal.
        static UniquePtr<T> make(size_t capacity) noexcept {
            assert(capacity > 0);
            if (capacity > max_capacity()) {
                fatal("container size overflow");
            }
            ArrayBase* array = _make_array(capacity * sizeof(Type));
            if constexpr(Info::MayContainTracked) {
                std::memset(data_of(array), 0, sizeof(Type) * array->capacity);
            }
            // release: the zeroed elements are published before the collector traces them.
            array->metadata.store(&Info::array_metadata(), std::memory_order_release);
            return UniquePtr<T>((T*)data_of(array));
        }

        static void* data_of(ArrayBase* array) noexcept {
            return (char*)array + ArrayDataOffset;
        }

        static ArrayBase* header_of(const void* data) noexcept {
            return (ArrayBase*)((const char*)data - ArrayDataOffset);
        }

    private:
        template<class A>
        static ArrayBase* _make(size_t extra, size_t capacity) noexcept {
            auto& thread = current_thread();
            void* mem;
            if constexpr(std::is_same_v<A, Array<>>) {
                mem = thread.alocator<A>().alloc(extra);
            } else {
                mem = thread.alocator<A>().alloc();
            }
            Page::set_state<State::Constructing>(mem);
            new(mem) A(0, capacity);
            Page::set_state<State::UniqueLock>(mem);
            return (ArrayBase*)mem;
        }

        // Size classes grow from one word by 3/2 up to a page; beyond that the storage is a large allocation.
        template<size_t N = sizeof(uintptr_t)>
        static ArrayBase* _make_array(size_t bytes) noexcept {
            if constexpr(sizeof(Array<N>) <= PageInfo<Array<N>>::PageCapacity) {
                if (bytes <= N) {
                    return _make<Array<N>>(0, N / sizeof(Type));
                }
            }
            if constexpr(sizeof(Array<N>) < PageInfo<Array<N>>::PageCapacity) {
                constexpr auto Next = (N * 3 / 2 + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
                return _make_array<Next>(bytes);
            } else {
                constexpr size_t PageArray = PageDataSize - ArrayDataOffset;
                if (bytes <= PageArray) {
                    return _make<Array<PageArray>>(0, PageArray / sizeof(Type));
                }
                // The large allocator reserves sizeof(Array<PageDataSize>) plus `extra` bytes.
                using Large = Array<PageDataSize>;
                const size_t need = ArrayDataOffset + bytes;
                return _make<Array<>>(need > sizeof(Large) ? need - sizeof(Large) : 0, bytes / sizeof(Type));
            }
        }
    };
}
