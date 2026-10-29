//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "make_tracked.h"
#include "tracked_ptr.h"
#include "vector.h"

#include <functional>
#include <tuple>
#include <utility>

namespace sgcl {
    // SPEC.md §3.6, §4.8, §5.3. Each node keeps the hash of its key, so Hash runs once per operation on the
    // argument key and never on a stored key; KeyEqual runs only on equal hashes.
    template<class Key, class T, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
    class unordered_map {
        static_assert(std::is_nothrow_copy_constructible_v<Hash> && std::is_nothrow_move_constructible_v<Hash>
                      && std::is_nothrow_copy_constructible_v<KeyEqual> && std::is_nothrow_move_constructible_v<KeyEqual>,
                      "Hash and KeyEqual must be nothrow-copyable and nothrow-movable (SPEC.md §3.6)");

    public:
        using key_type = Key;
        using mapped_type = T;
        using value_type = std::pair<const Key, T>;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using hasher = Hash;
        using key_equal = KeyEqual;

    private:
        struct Node {
            template<class K, class M>
            Node(size_t h, K&& k, M&& m)
            : value(std::piecewise_construct, std::forward_as_tuple(std::forward<K>(k)), std::forward_as_tuple(std::forward<M>(m)))
            , hash(h) {
            }

            value_type value;
            const size_t hash;
            tracked_ptr<Node> next;
        };

        using NodePtr = tracked_ptr<Node>;

        template <bool IsConst>
        class Iterator {
            using MapPtr = std::conditional_t<IsConst, const unordered_map*, unordered_map*>;

        public:
            using value_type = unordered_map::value_type;
            using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
            using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            Iterator() noexcept = default;

            template <bool C = IsConst, std::enable_if_t<C, int> = 0>
            Iterator(const Iterator<false>& other) noexcept
            : _map(other._map)
            , _bucket_index(other._bucket_index)
            , _node(other._node) {
            }

            reference operator*() const noexcept {
                return _node->value;
            }

            pointer operator->() const noexcept {
                return std::addressof(_node->value);
            }

            Iterator& operator++() noexcept {
                if (_node) {
                    _node = _node->next;
                }
                _to_next_valid();
                return *this;
            }

            Iterator operator++(int) noexcept {
                Iterator tmp(*this);
                ++*this;
                return tmp;
            }

            template<bool B>
            bool operator==(const Iterator<B>& other) const noexcept {
                return _node == other._node;
            }

        private:
            MapPtr _map = nullptr;
            size_t _bucket_index = 0;
            NodePtr _node;

            Iterator(MapPtr map, size_t index, const NodePtr& node) noexcept
            : _map(map)
            , _bucket_index(index)
            , _node(node) {
                _to_next_valid();
            }

            // Bounds-checked against the current buckets, so a stale iterator stays memory-safe.
            void _to_next_valid() noexcept {
                while (!_node && _bucket_index + 1 < _map->_buckets.size()) {
                    ++_bucket_index;
                    _node = _map->_buckets[_bucket_index];
                }
            }

            template<bool> friend class Iterator;
            friend class unordered_map;
        };

    public:
        using iterator = Iterator<false>;
        using const_iterator = Iterator<true>;

        unordered_map() noexcept = default;
        unordered_map(const unordered_map&) = delete;
        unordered_map& operator=(const unordered_map&) = delete;
        ~unordered_map() noexcept = default;

        iterator begin() noexcept {
            return _buckets.empty() ? end() : iterator(this, 0, _buckets[0]);
        }

        const_iterator begin() const noexcept {
            return _buckets.empty() ? end() : const_iterator(this, 0, _buckets[0]);
        }

        const_iterator cbegin() const noexcept {
            return begin();
        }

        iterator end() noexcept {
            return iterator(this, _buckets.size(), nullptr);
        }

        const_iterator end() const noexcept {
            return const_iterator(this, _buckets.size(), nullptr);
        }

        const_iterator cend() const noexcept {
            return end();
        }

        bool empty() const noexcept {
            return _size == 0;
        }

        size_t size() const noexcept {
            return _size;
        }

        void clear() noexcept {
            _buckets = vector<NodePtr>();
            _size = 0;
        }

        template<class M>
        std::pair<iterator, bool> insert_or_assign(const Key& k, M&& obj) {
            return _insert_or_assign(k, std::forward<M>(obj));
        }

        template<class M>
        std::pair<iterator, bool> insert_or_assign(Key&& k, M&& obj) {
            return _insert_or_assign(std::move(k), std::forward<M>(obj));
        }

        iterator find(const Key& key) {
            auto [index, node] = _find(key, _hash(key));
            return node ? iterator(this, index, node) : end();
        }

        const_iterator find(const Key& key) const {
            auto [index, node] = _find(key, _hash(key));
            return node ? const_iterator(this, index, node) : end();
        }

        size_t erase(const Key& key) {
            auto h = _hash(key);
            if (_buckets.empty()) {
                return 0;
            }
            auto index = h % _buckets.size();
            Node* prev = nullptr;
            for (Node* node = _buckets[index].get(); node; prev = node, node = node->next.get()) {
                if (node->hash == h && _equal(node->value.first, key)) {
                    if (prev) {
                        prev->next = node->next;
                    } else {
                        _buckets[index] = node->next;
                    }
                    --_size;
                    return 1;
                }
            }
            return 0;
        }

    private:
        static constexpr size_t MinBucketCount = 8;

        vector<NodePtr> _buckets;
        size_t _size = 0;
        [[no_unique_address]] Hash _hash;
        [[no_unique_address]] KeyEqual _equal;

        // The bucket and node holding `key`, or a null node.
        std::pair<size_t, NodePtr> _find(const Key& key, size_t h) const {
            if (_buckets.empty()) {
                return {0, nullptr};
            }
            auto index = h % _buckets.size();
            for (Node* node = _buckets[index].get(); node; node = node->next.get()) {
                if (node->hash == h && _equal(node->value.first, key)) {
                    return {index, NodePtr(node, typename NodePtr::AdoptTag{})};
                }
            }
            return {0, nullptr};
        }

        // The node is built before the map changes, so a throw from Hash, KeyEqual or the node's constructors
        // leaves the map as it was.
        template<class K, class M>
        std::pair<iterator, bool> _insert_or_assign(K&& k, M&& obj) {
            auto h = _hash(k);
            auto [index, found] = _find(k, h);
            if (found) {
                found->value.second = std::forward<M>(obj);
                return {iterator(this, index, found), false};
            }
            auto node = make_tracked<Node>(h, std::forward<K>(k), std::forward<M>(obj));
            _reserve_for(_size + 1);
            index = h % _buckets.size();
            node->next = _buckets[index];
            NodePtr inserted(std::move(node));
            _buckets[index] = inserted;
            ++_size;
            return {iterator(this, index, inserted), true};
        }

        // Load factor at most 3/4. Never throws: relinking uses stored hashes and allocation failure is fatal.
        void _reserve_for(size_t n) noexcept {
            auto count = _buckets.size();
            if (!count) {
                _rehash(MinBucketCount);
            } else if (n > count / 4 * 3) {
                _rehash(count * 2);
            }
        }

        void _rehash(size_t count) noexcept {
            vector<NodePtr> buckets;
            _resize_buckets(buckets, count);
            for (auto& head : _buckets) {
                NodePtr current = head;
                while (current) {
                    NodePtr next = current->next;
                    auto& bucket = buckets[current->hash % count];
                    current->next = bucket;
                    bucket = current;
                    current = next;
                }
            }
            _buckets = std::move(buckets);
        }

        static void _resize_buckets(vector<NodePtr>& buckets, size_t count) noexcept {
            buckets.resize(count);
        }
    };
}
