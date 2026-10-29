//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "detail/pointer.h"
#include "make_tracked.h"
#include "unique_ptr.h"
#include "types.h"

#include <atomic>
#include <compare>
#include <cstdlib>
#include <functional>

namespace sgcl {
    namespace detail {
        struct TrackedPtrAccess;
    }

    // SPEC.md §3.3. The cell lives on the shadow stack, inside the managed object, or in an external-root cell,
    // decided at construction (§4.3).
    template<class T>
    class tracked_ptr : detail::Tracked {
#ifdef SGCL_ARCH_X86_64
        static constexpr uintptr_t StackFlag = uintptr_t(1) << 63;
        static constexpr uintptr_t ExternalHeapFlag = uintptr_t(1) << 62;
#else
        static constexpr uintptr_t StackFlag = 1;
        static constexpr uintptr_t ExternalHeapFlag = 2;
#endif
        static constexpr uintptr_t ClearMask = ~(StackFlag | ExternalHeapFlag);

        struct AdoptTag {};

    public:
        using element_type = T;

        tracked_ptr() noexcept {
            _init();
        }

        tracked_ptr(std::nullptr_t) noexcept
        : tracked_ptr() {
        }

        // Preconditions in SPEC.md §5.8.
        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        explicit tracked_ptr(U* p) noexcept
        : tracked_ptr() {
            if (p) {
                _check_adoptable(p);
                _cell()->store(static_cast<T*>(p));
            }
        }

        tracked_ptr(const tracked_ptr& p) noexcept
        : tracked_ptr() {
            _cell()->store(p.get());
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr(const tracked_ptr<U>& p) noexcept
        : tracked_ptr() {
            _cell()->store(static_cast<T*>(p.get()));
        }

        tracked_ptr(tracked_ptr&& p) noexcept {
            _init_from_move(p);
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr(tracked_ptr<U>&& p) noexcept {
            _init_from_move(p);
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr(unique_ptr<U>&& u) noexcept
        : tracked_ptr() {
            _cell()->store_handover(static_cast<T*>(u.release()));
        }

        ~tracked_ptr() noexcept {
            if (_is_external()) {
                _drop_cell();
            } else {
                _cell()->store(nullptr);
            }
        }

        tracked_ptr& operator=(std::nullptr_t) noexcept {
            reset();
            return *this;
        }

        tracked_ptr& operator=(const tracked_ptr& p) noexcept {
            _writable_cell()->store(p.get());
            return *this;
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr& operator=(const tracked_ptr<U>& p) noexcept {
            _writable_cell()->store(static_cast<T*>(p.get()));
            return *this;
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr& operator=(unique_ptr<U>&& u) noexcept {
            _writable_cell()->store_handover(static_cast<T*>(u.release()));
            return *this;
        }

        tracked_ptr& operator=(tracked_ptr&& p) noexcept {
            _assign_from_move(p);
            return *this;
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        tracked_ptr& operator=(tracked_ptr<U>&& p) noexcept {
            _assign_from_move(p);
            return *this;
        }

        explicit operator bool() const noexcept {
            return get() != nullptr;
        }

        template <class U = element_type, std::enable_if_t<!std::is_void_v<U>, int> = 0>
        U& operator*() const noexcept {
            return *get();
        }

        template <class U = element_type, std::enable_if_t<!std::is_void_v<U>, int> = 0>
        U* operator->() const noexcept {
            return get();
        }

        element_type* get() const noexcept {
            return (element_type*)_cell()->load();
        }

        void reset() noexcept {
            if (!_has_null_cell()) {
                _cell()->store(nullptr);
            }
        }

        void swap(tracked_ptr& p) noexcept {
            if (_is_external() && p._is_external()) {
                std::swap(_ref, p._ref);
            } else {
                tracked_ptr t(*this);
                *this = p;
                p = t;
            }
        }

    private:
        explicit tracked_ptr(T* p, AdoptTag) noexcept
        : tracked_ptr() {
            _cell()->store(p);
        }

        // The ref word names the cell and its storage class. On x86-64 a child keeps its cell in the word itself
        // and the others tag the cell's address with the two highest bits; elsewhere a child's word is 0 and the
        // others hold the complement of the tagged address. Either way a ref word is never an address inside the
        // 48-bit range the page registry covers, so the collector never mistakes one for a referent (SPEC.md §4.4).
        static uintptr_t _encode(detail::Pointer* cell, uintptr_t flag) noexcept {
#ifdef SGCL_ARCH_X86_64
            if ((uintptr_t)cell & ~ClearMask) {
                detail::fatal("an address uses the two highest bits (SPEC.md §8)");
            }
            return (uintptr_t)cell | flag;
#else
            return ~((uintptr_t)cell | flag);
#endif
        }

        uintptr_t _flags() const noexcept {
#ifdef SGCL_ARCH_X86_64
            return _ref & ~ClearMask;
#else
            return _ref ? ~_ref & ~ClearMask : 0;
#endif
        }

        detail::Pointer* _decoded() const noexcept {
#ifdef SGCL_ARCH_X86_64
            return (detail::Pointer*)(_ref & ClearMask);
#else
            return (detail::Pointer*)(~_ref & ClearMask);
#endif
        }

        bool _this_on_stack() const noexcept {
            uintptr_t this_addr = (uintptr_t)this;
            uintptr_t stack_addr = (uintptr_t)&this_addr;
            ptrdiff_t offset = this_addr - stack_addr;
            return std::abs(offset) <= (ptrdiff_t)config::MaxOffsetForStackDetection;
        }

        bool _is_external() const noexcept {
            return _flags() & ExternalHeapFlag;
        }

        bool _is_stack() const noexcept {
            return _flags() & StackFlag;
        }

        bool _is_child() const noexcept {
            return !_flags();
        }

        static detail::Pointer* _null_cell() noexcept {
            return reinterpret_cast<detail::Pointer*>(&detail::process_state().null_cell);
        }

        bool _has_null_cell() const noexcept {
            return _is_external() && _decoded() == _null_cell();
        }

        detail::Pointer* _cell() const noexcept {
            if (_is_child()) [[likely]] {
                return const_cast<detail::Pointer*>(&_raw_ptr);
            }
            return _decoded();
        }

#ifdef SGCL_ARCH_X86_64
        union {
            uintptr_t _ref;
            detail::Pointer _raw_ptr;
        };
#else
        uintptr_t _ref;
        union {
            detail::Pointer _raw_ptr;
        };
#endif

        // A writable cell: a moved-from external root gets a fresh one.
        detail::Pointer* _writable_cell() noexcept {
            if (_has_null_cell()) {
                _ref = _encode(_new_external_cell(), ExternalHeapFlag);
            }
            return _cell();
        }

        static detail::Pointer* _new_external_cell() noexcept {
            return detail::Maker<detail::Pointer>::make_tracked().release();
        }

        void _drop_cell() noexcept {
            if (!_has_null_cell()) {
                detail::Page::set_state<detail::State::Destroyed>(_cell());
            }
        }

        // Chooses the storage class (SPEC.md §4.3) and leaves the cell null.
        void _init() noexcept {
            if (!_init_stack_or_child()) {
                _ref = _encode(_new_external_cell(), ExternalHeapFlag);
            }
        }

        bool _init_stack_or_child() noexcept {
            auto& thread = detail::current_thread();
            if (_this_on_stack()) {
                _ref = _encode(thread.stack_allocator->alloc(this), StackFlag);
                _cell()->store(nullptr);
                return true;
            }
            auto& c = thread.construction;
            auto cell = (uintptr_t)&_raw_ptr;
            if (cell - c.base < c.size && c.layout->claim(&_raw_ptr, cell - c.base)) {
#ifndef SGCL_ARCH_X86_64
                _ref = 0;
#endif
                // An atomic store, not a constructor: the collector may already trace this storage.
                std::atomic_ref<void*>(*reinterpret_cast<void**>(&_raw_ptr)).store(nullptr, std::memory_order_relaxed);
                return true;
            }
            return false;
        }

        template<class U>
        void _init_from_move(tracked_ptr<U>& p) noexcept {
            auto value = static_cast<T*>(p.get());
            if (_init_stack_or_child()) {
                _cell()->store(value);
            } else if (p._is_external()) {
                // Take the source's cell; the source gets the shared null cell.
                _ref = p._ref;
                if (!_has_null_cell()) {
                    _cell()->store_no_update(value);
                }
                p._ref = _encode(_null_cell(), ExternalHeapFlag);
                return;
            } else {
                _ref = _encode(_new_external_cell(), ExternalHeapFlag);
                _cell()->store(value);
            }
            p.reset();
        }

        template<class U>
        void _assign_from_move(tracked_ptr<U>& p) noexcept {
            if ((void*)this == (void*)&p) {
                return;
            }
            auto value = static_cast<T*>(p.get());
            if (_is_external() && p._is_external() && !p._has_null_cell()) {
                _drop_cell();
                _ref = p._ref;
                _cell()->store_no_update(value);
                p._ref = _encode(_null_cell(), ExternalHeapFlag);
                return;
            }
            _writable_cell()->store(value);
            p.reset();
        }

        // SPEC.md §5.8: a live object in the first PageSize bytes of a managed allocation.
        static void _check_adoptable(const void* p) noexcept {
            if (!detail::process_state().pages.contains(p)) {
                detail::fatal("tracked_ptr(T*) on memory SGCL does not manage");
            }
            auto page = detail::Page::page_of(p);
            if (page->index_of(p) >= page->metadata->object_count) {
                detail::fatal("tracked_ptr(T*) on memory SGCL does not manage");
            }
            auto state = detail::Page::state_of(p).load(std::memory_order_acquire);
            if (!(state & detail::State::ReachableMask) && state != detail::State::Used) {
                detail::fatal("tracked_ptr(T*) on a destroyed or free object");
            }
        }

        template<class> friend class tracked_ptr;
        template<class> friend class vector;
        template<class, class, class, class> friend class unordered_map;
        friend struct detail::TrackedPtrAccess;
        template<class U, class V> friend tracked_ptr<U> dynamic_pointer_cast(const tracked_ptr<V>&) noexcept;
    };

    template<class T, class U>
    inline std::strong_ordering operator<=>(const tracked_ptr<T>& l, const tracked_ptr<U>& r) noexcept {
        using Y = typename std::common_type<decltype(l.get()), decltype(r.get())>::type;
        return std::compare_three_way()(static_cast<Y>(l.get()), static_cast<Y>(r.get()));
    }

    template<class T, class U>
    inline bool operator==(const tracked_ptr<T>& l, const tracked_ptr<U>& r) noexcept {
        using Y = typename std::common_type<decltype(l.get()), decltype(r.get())>::type;
        return static_cast<Y>(l.get()) == static_cast<Y>(r.get());
    }

    template<class T>
    inline std::strong_ordering operator<=>(const tracked_ptr<T>& l, std::nullptr_t) noexcept {
        return std::compare_three_way()(l.get(), static_cast<T*>(nullptr));
    }

    template<class T>
    inline bool operator==(const tracked_ptr<T>& l, std::nullptr_t) noexcept {
        return l.get() == nullptr;
    }

    // SPEC.md §3.3.
    template<class T, class U>
    inline tracked_ptr<T> dynamic_pointer_cast(const tracked_ptr<U>& p) noexcept {
        return tracked_ptr<T>(dynamic_cast<T*>(p.get()), typename tracked_ptr<T>::AdoptTag{});
    }

    namespace detail {
        // Storage-class queries for tests.
        struct TrackedPtrAccess {
            template<class T>
            static bool on_stack(const tracked_ptr<T>& p) noexcept { return p._is_stack(); }
            template<class T>
            static bool child(const tracked_ptr<T>& p) noexcept { return p._is_child(); }
            template<class T>
            static bool external(const tracked_ptr<T>& p) noexcept { return p._is_external(); }
        };
    }
}

namespace std {
    template<class T>
    struct hash<sgcl::tracked_ptr<T>> {
        std::size_t operator()(const sgcl::tracked_ptr<T>& p) const noexcept {
            return std::hash<T*>{}(p.get());
        }
    };
}
