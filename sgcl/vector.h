//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "collector.h"
#include "make_tracked.h"
#include "tracked_ptr.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace sgcl {
    // SPEC.md §3.5, §4.8, §5.2. Elements [0, size) are constructed and [size, capacity) are zero; storage that
    // is replaced stays alive while anything refers to it and keeps its moved-from elements until collected.
    template<class T>
    class vector {
        static_assert(std::is_object_v<T> && !std::is_const_v<T>, "vector elements are non-const object types");
        static_assert(alignof(T) <= alignof(detail::ArrayBase), "vector elements are at most pointer-aligned (SPEC.md §8)");

        using Info = detail::TypeInfo<T>;
        using Maker = detail::Maker<T>;
        using Storage = detail::ArrayMaker<T>;

    public:
        using value_type = T;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using iterator = T*;
        using const_iterator = const T*;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        vector() noexcept = default;

        vector(std::initializer_list<T> init) {
            _append_copies(init.begin(), init.end(), init.size());
        }

        vector(const vector& other) {
            _append_copies(other.begin(), other.end(), other.size());
        }

        vector(vector&& other) noexcept
        : _data(std::move(other._data)) {
        }

        ~vector() noexcept = default;

        vector& operator=(const vector& other) {
            if (this != &other) {
                vector copy(other);
                swap(copy);
            }
            return *this;
        }

        vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                _data = std::move(other._data);
            }
            return *this;
        }

        reference operator[](size_t pos) noexcept {
            return data()[pos];
        }

        const_reference operator[](size_t pos) const noexcept {
            return data()[pos];
        }

        reference front() noexcept {
            return data()[0];
        }

        const_reference front() const noexcept {
            return data()[0];
        }

        reference back() noexcept {
            return data()[size() - 1];
        }

        const_reference back() const noexcept {
            return data()[size() - 1];
        }

        T* data() noexcept {
            return _data.get();
        }

        const T* data() const noexcept {
            return _data.get();
        }

        iterator begin() noexcept { return data(); }
        const_iterator begin() const noexcept { return data(); }
        const_iterator cbegin() const noexcept { return data(); }
        iterator end() noexcept { return data() + size(); }
        const_iterator end() const noexcept { return data() + size(); }
        const_iterator cend() const noexcept { return data() + size(); }
        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

        bool empty() const noexcept {
            return size() == 0;
        }

        size_t size() const noexcept {
            return _data ? _header()->size : 0;
        }

        size_t capacity() const noexcept {
            return _data ? _header()->capacity : 0;
        }

        static constexpr size_t max_size() noexcept {
            return Storage::max_capacity();
        }

        // Strong guarantee.
        void reserve(size_t n) {
            if (n > capacity()) {
                _reallocate(n);
            }
        }

        void push_back(const T& value) {
            emplace_back(value);
        }

        void push_back(T&& value) {
            emplace_back(std::move(value));
        }

        // Strong guarantee; the arguments may refer to elements of this vector.
        template<class... A>
        reference emplace_back(A&&... a) {
            auto s = size();
            if (s < capacity()) {
                Maker::construct(data() + s, std::forward<A>(a)...);
                _header()->size = s + 1;
            } else {
                _grow_and_emplace(s, std::forward<A>(a)...);
            }
            return data()[s];
        }

        void pop_back() noexcept {
            _destroy_tail(size() - 1);
        }

        // Strong when T's moves do not throw, else basic. `value` may be an element of this vector.
        iterator insert(const_iterator pos, const T& value) {
            return _emplace(pos - cbegin(), value);
        }

        iterator insert(const_iterator pos, T&& value) {
            return _emplace(pos - cbegin(), std::move(value));
        }

        template<class... A>
        iterator emplace(const_iterator pos, A&&... a) {
            return _emplace(pos - cbegin(), std::forward<A>(a)...);
        }

        // Strong when T's moves do not throw, else basic. The range may be part of this vector.
        template<std::forward_iterator It>
        iterator insert(const_iterator pos, It first, It last) {
            size_t index = pos - cbegin();
            vector copies;
            copies._append_copies(first, last, std::distance(first, last));
            auto n = copies.size();
            if (n) {
                _insert_moved(index, n, [&](size_t k) -> T& { return copies.data()[k]; });
            }
            return begin() + index;
        }

        // Basic; noexcept when T is nothrow-move-assignable.
        iterator erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
            return erase(pos, pos + 1);
        }

        iterator erase(const_iterator first, const_iterator last) noexcept(std::is_nothrow_move_assignable_v<T>) {
            size_t index = first - cbegin();
            size_t n = last - first;
            if (n) {
                auto s = size();
                auto p = data();
                for (size_t i = index; i + n < s; ++i) {
                    p[i] = std::move(p[i + n]);
                }
                _destroy_tail(s - n);
            }
            return begin() + index;
        }

        // Strong guarantee.
        void resize(size_t n) requires std::default_initializable<T> {
            _resize(n);
        }

        void resize(size_t n, const T& value) {
            if (n > size()) {
                T copy(value);
                _resize(n, copy);
            } else {
                _resize(n);
            }
        }

        void clear() noexcept {
            if (_data) {
                _destroy_tail(0);
            }
        }

        void swap(vector& other) noexcept {
            _data.swap(other._data);
        }

    private:
        // Points at element 0 of the storage.
        tracked_ptr<T> _data;

        detail::ArrayBase* _header() const noexcept {
            return Storage::header_of(_data.get());
        }

        // Destroys elements [from, size) and zeroes their storage.
        void _destroy_tail(size_t from) noexcept {
            auto h = _header();
            auto p = data();
            for (size_t i = h->size; i > from; --i) {
                _destroy_at(p + i - 1);
            }
            h->size = from;
        }

        static void _destroy_at(T* p) noexcept {
            Maker::destroy(p);
            if constexpr(Info::MayContainTracked) {
                std::memset((void*)p, 0, sizeof(T));
            }
        }

        static void _destroy_range(T* first, T* last) noexcept {
            while (last != first) {
                _destroy_at(--last);
            }
        }

        size_t _grown_capacity(size_t needed) const noexcept {
            auto c = capacity();
            auto grown = c <= max_size() / 2 ? std::max(c + c / 2, c + 1) : max_size();
            return std::max(grown, needed);
        }

        // Fresh storage of `n` elements, as a root until it is installed.
        static unique_ptr<T> _make_storage(size_t n) noexcept {
            return unique_ptr<T>(Storage::make(n));
        }

        void _install(unique_ptr<T>& storage, size_t size) noexcept {
            Storage::header_of(storage.get())->size = size;
            _data = std::move(storage);
        }

        // Moves (or copies, std::move_if_noexcept) [0, s) into `dst` leaving a gap of `gap` elements at `pos`.
        // On a throw the elements built in `dst` are destroyed and the old storage is untouched or, for a
        // move-only T with a throwing move, partly moved-from.
        void _relocate_into(T* dst, size_t pos, size_t gap) {
            auto src = data();
            auto s = size();
            size_t i = 0;
            try {
                for (; i < s; ++i) {
                    Maker::construct(dst + (i < pos ? i : i + gap), std::move_if_noexcept(src[i]));
                }
            } catch (...) {
                _destroy_range(dst, dst + std::min(i, pos));
                if (i > pos) {
                    _destroy_range(dst + pos + gap, dst + i + gap);
                }
                throw;
            }
        }

        void _reallocate(size_t n) {
            auto storage = _make_storage(n);
            auto s = size();
            _relocate_into(storage.get(), s, 0);
            _install(storage, s);
        }

        // The new element is built first, while the arguments still refer to valid objects.
        template<class... A>
        void _grow_and_emplace(size_t pos, A&&... a) {
            auto s = size();
            auto storage = _make_storage(_grown_capacity(s + 1));
            auto dst = storage.get();
            Maker::construct(dst + pos, std::forward<A>(a)...);
            try {
                _relocate_into(dst, pos, 1);
            } catch (...) {
                _destroy_at(dst + pos);
                throw;
            }
            _install(storage, s + 1);
        }

        template<class... A>
        iterator _emplace(size_t index, A&&... a) {
            auto s = size();
            if (index == s) {
                emplace_back(std::forward<A>(a)...);
            } else if (s == capacity()) {
                _grow_and_emplace(index, std::forward<A>(a)...);
            } else {
                T value(std::forward<A>(a)...);
                _insert_moved(index, 1, [&](size_t) -> T& { return value; });
            }
            return begin() + index;
        }

        // Inserts n elements, moved from source(0..n-1), at `index`.
        template<class Source>
        void _insert_moved(size_t index, size_t n, Source&& source) {
            auto s = size();
            if (s + n > capacity()) {
                auto storage = _make_storage(_grown_capacity(s + n));
                auto dst = storage.get();
                size_t k = 0;
                try {
                    for (; k < n; ++k) {
                        Maker::construct(dst + index + k, std::move_if_noexcept(source(k)));
                    }
                    _relocate_into(dst, index, n);
                } catch (...) {
                    _destroy_range(dst + index, dst + index + k);
                    throw;
                }
                _install(storage, s + n);
                return;
            }
            auto p = data();
            auto h = _header();
            // New slots [s, s + n), in order, so size always covers exactly the constructed prefix.
            for (size_t j = s; j < s + n; ++j) {
                if (j - n >= index && j >= n) {
                    Maker::construct(p + j, std::move(p[j - n]));
                } else {
                    Maker::construct(p + j, std::move(source(j - index)));
                }
                h->size = j + 1;
            }
            // Shift the old elements that stay inside the old size, then fill the gap.
            for (size_t i = s; i-- > index + n;) {
                p[i] = std::move(p[i - n]);
            }
            for (size_t k = 0; k < n && index + k < s; ++k) {
                p[index + k] = std::move(source(k));
            }
        }

        template<class It>
        void _append_copies(It first, It last, size_t n) {
            if (!n) {
                return;
            }
            reserve(size() + n);
            auto p = data();
            auto h = _header();
            for (; first != last; ++first) {
                Maker::construct(p + h->size, *first);
                ++h->size;
            }
        }

        template<class... A>
        void _resize(size_t n, A&... value) {
            auto s = size();
            if (n <= s) {
                if (_data) {
                    _destroy_tail(n);
                }
                return;
            }
            if (n > capacity()) {
                // New elements first, into fresh storage, so a throw leaves this vector as it was.
                auto storage = _make_storage(n);
                auto dst = storage.get();
                size_t i = s;
                try {
                    for (; i < n; ++i) {
                        Maker::construct(dst + i, value...);
                    }
                    _relocate_into(dst, s, 0);
                } catch (...) {
                    _destroy_range(dst + s, dst + i);
                    throw;
                }
                _install(storage, n);
                return;
            }
            auto p = data();
            auto h = _header();
            try {
                for (size_t i = s; i < n; ++i) {
                    Maker::construct(p + i, value...);
                    h->size = i + 1;
                }
            } catch (...) {
                _destroy_tail(s);
                throw;
            }
        }
    };
}
