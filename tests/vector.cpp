//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <vector>

namespace {
    // Every Int is gone before and after each test.
    class Vector_Test : public ::testing::Test {
    protected:
        void SetUp() override {
            ASSERT_TRUE(collect_until([] { return Int::live() == 0; }));
        }

        void TearDown() override {
            EXPECT_TRUE(collect_until([] { return Int::live() == 0; }));
        }

        // Replaced storage keeps its elements until collected.
        static void expect_live(size_t n) {
            EXPECT_TRUE(collect_until([n] { return Int::live() == n; })) << "live " << Int::live() << ", expected " << n;
        }
    };

    template<class V>
    std::vector<int> values(const V& v) {
        return std::vector<int>(v.begin(), v.end());
    }
}

TEST_F(Vector_Test, DefaultConstructorEmpty) {
    sgcl::vector<Int> vec;
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vec.capacity(), 0u);
    EXPECT_EQ(vec.data(), nullptr);
    EXPECT_EQ(vec.begin(), vec.end());
    EXPECT_EQ(vec.cbegin(), vec.cend());
    EXPECT_EQ(vec.rbegin(), vec.rend());
    EXPECT_EQ(vec.crbegin(), vec.crend());
    vec.clear();
    vec.resize(0);
    vec.reserve(0);
    EXPECT_EQ(vec.data(), nullptr);
}

TEST_F(Vector_Test, InitializerList) {
    sgcl::vector<Int> vec({4, 5, 6});
    expect_live(3);
    EXPECT_FALSE(vec.empty());
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_GE(vec.capacity(), 3u);
    EXPECT_EQ(values(vec), (std::vector<int>{4, 5, 6}));
}

TEST_F(Vector_Test, CopyConstructor) {
    sgcl::vector<Int> other({1, 2, 3});
    sgcl::vector<Int> vec(other);
    expect_live(6);
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_NE(vec.data(), other.data());
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 3}));
    sgcl::vector<Int> empty;
    sgcl::vector<Int> copy_of_empty(empty);
    EXPECT_TRUE(copy_of_empty.empty());
}

TEST_F(Vector_Test, MoveConstructor) {
    sgcl::vector<Int> other({4, 5, 6});
    auto data = other.data();
    sgcl::vector<Int> vec(std::move(other));
    expect_live(3);
    EXPECT_TRUE(other.empty());
    EXPECT_EQ(other.data(), nullptr);
    EXPECT_EQ(vec.data(), data);
    EXPECT_EQ(values(vec), (std::vector<int>{4, 5, 6}));
    other.push_back(1);
    EXPECT_EQ(values(other), (std::vector<int>{1}));
}

TEST_F(Vector_Test, CopyAssignment) {
    sgcl::vector<Int> other({1, 2, 3});
    sgcl::vector<Int> vec;
    vec = other;
    expect_live(6);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(std::vector<int>(vec.rbegin(), vec.rend()), (std::vector<int>{3, 2, 1}));

    other = sgcl::vector<Int>({2, 3});
    vec = other;
    expect_live(4);
    EXPECT_EQ(values(vec), (std::vector<int>{2, 3}));

    vec = vec;
    EXPECT_EQ(values(vec), (std::vector<int>{2, 3}));

    other = sgcl::vector<Int>();
    vec = other;
    expect_live(0);
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vec.begin(), vec.end());
}

TEST_F(Vector_Test, MoveAssignment) {
    sgcl::vector<Int> other({4, 5, 6});
    sgcl::vector<Int> vec({1});
    vec = std::move(other);
    expect_live(3);
    EXPECT_TRUE(other.empty());
    EXPECT_EQ(values(vec), (std::vector<int>{4, 5, 6}));
    vec = {7, 8, 9};
    expect_live(3);
    EXPECT_EQ(values(vec), (std::vector<int>{7, 8, 9}));
}

TEST_F(Vector_Test, Access) {
    sgcl::vector<Int> vec({1, 2, 3, 4, 5, 6, 7});
    for (size_t i = 0; i < vec.size(); ++i) {
        EXPECT_EQ(vec[i], int(i + 1));
        EXPECT_EQ(&vec[i], vec.data() + i);
    }
    EXPECT_EQ(vec.front(), 1);
    EXPECT_EQ(vec.back(), 7);
    const auto& cvec = vec;
    EXPECT_EQ(cvec[2], 3);
    EXPECT_EQ(cvec.front(), 1);
    EXPECT_EQ(cvec.back(), 7);
    EXPECT_EQ(cvec.data(), vec.data());
    EXPECT_EQ(std::vector<int>(cvec.crbegin(), cvec.crend()), (std::vector<int>{7, 6, 5, 4, 3, 2, 1}));
}

TEST_F(Vector_Test, PushAndEmplaceBack) {
    sgcl::vector<Int> vec({1, 2, 3});
    vec.push_back(4);
    Int five(5);
    vec.push_back(five);
    auto& v = vec.emplace_back(6);
    EXPECT_EQ(&v, &vec.back());
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 3, 4, 5, 6}));
    for (int i = 7; i <= 1000; ++i) {
        vec.push_back(i);
    }
    EXPECT_EQ(vec.size(), 1000u);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(vec[i], i + 1);
    }
    expect_live(1001);
}

TEST_F(Vector_Test, PopBack) {
    sgcl::vector<Int> vec({2, 3, 4});
    vec.pop_back();
    expect_live(2);
    EXPECT_EQ(values(vec), (std::vector<int>{2, 3}));
}

TEST_F(Vector_Test, InsertValue) {
    sgcl::vector<Int> vec({2, 3, 5});
    auto it = vec.insert(vec.begin(), 1);
    EXPECT_EQ(*it, 1);
    EXPECT_EQ(it, vec.begin());
    it = vec.insert(vec.begin() + 3, 4);
    EXPECT_EQ(*it, 4);
    EXPECT_EQ(it, vec.begin() + 3);
    Int six(6);
    it = vec.insert(vec.end(), six);
    EXPECT_EQ(*it, 6);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 3, 4, 5, 6}));
    vec.reserve(20);
    it = vec.insert(vec.begin() + 1, 9);
    EXPECT_EQ(*it, 9);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 9, 2, 3, 4, 5, 6}));
    expect_live(8);
}

TEST_F(Vector_Test, InsertRange) {
    sgcl::vector<Int> vec({2, 3, 5});
    std::vector<int> other = {4, 5};
    auto it = vec.insert(vec.end(), other.begin(), other.begin());
    EXPECT_EQ(vec.size(), 3u);
    EXPECT_EQ(it, vec.end());
    it = vec.insert(vec.begin(), other.begin(), other.end());
    EXPECT_EQ(*it, 4);
    EXPECT_EQ(*++it, 5);
    other = std::vector<int>{7, 8};
    it = vec.insert(vec.begin() + 4, other.begin(), other.end());
    EXPECT_EQ(*it, 7);
    EXPECT_EQ(*++it, 8);
    other = std::vector<int>{2, 3};
    it = vec.insert(vec.end(), other.begin(), other.end());
    EXPECT_EQ(*it, 2);
    EXPECT_EQ(*++it, 3);
    EXPECT_EQ(values(vec), (std::vector<int>{4, 5, 2, 3, 7, 8, 5, 2, 3}));
    vec.reserve(100);
    // Within capacity, with a gap reaching past the old end.
    std::vector<int> many = {10, 11, 12, 13, 14};
    vec.insert(vec.end() - 2, many.begin(), many.end());
    EXPECT_EQ(values(vec), (std::vector<int>{4, 5, 2, 3, 7, 8, 5, 10, 11, 12, 13, 14, 2, 3}));
    expect_live(14);
}

TEST_F(Vector_Test, InsertRangeFromSelf) {
    sgcl::vector<Int> vec({1, 2, 3});
    vec.insert(vec.begin() + 1, vec.begin(), vec.end());
    EXPECT_EQ(values(vec), (std::vector<int>{1, 1, 2, 3, 2, 3}));
    vec.reserve(50);
    vec.insert(vec.begin(), vec.begin() + 2, vec.begin() + 4);
    EXPECT_EQ(values(vec), (std::vector<int>{2, 3, 1, 1, 2, 3, 2, 3}));
    vec.insert(vec.end(), vec.begin(), vec.end());
    EXPECT_EQ(values(vec), (std::vector<int>{2, 3, 1, 1, 2, 3, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3}));
    expect_live(16);
}

TEST_F(Vector_Test, Erase) {
    sgcl::vector<Int> vec({1, 2, 3, 4, 5});
    auto it = vec.erase(vec.begin());
    expect_live(4);
    EXPECT_EQ(*it, 2);
    it = vec.erase(vec.begin() + 1);
    expect_live(3);
    EXPECT_EQ(*it, 4);
    it = vec.erase(vec.end() - 1);
    expect_live(2);
    EXPECT_EQ(it, vec.end());
    EXPECT_EQ(values(vec), (std::vector<int>{2, 4}));
}

TEST_F(Vector_Test, EraseRange) {
    sgcl::vector<Int> vec({1, 2, 3, 4, 5, 6, 7, 8});
    auto it = vec.erase(vec.begin(), vec.begin() + 2);
    expect_live(6);
    EXPECT_EQ(*it, 3);
    it = vec.erase(vec.begin() + 1, vec.begin() + 3);
    expect_live(4);
    EXPECT_EQ(*it, 6);
    it = vec.erase(vec.end() - 2, vec.end());
    expect_live(2);
    EXPECT_EQ(it, vec.end());
    it = vec.erase(vec.end(), vec.end());
    EXPECT_EQ(it, vec.end());
    EXPECT_EQ(values(vec), (std::vector<int>{3, 6}));
}

TEST_F(Vector_Test, Resize) {
    sgcl::vector<Int> vec({1, 2, 3});
    vec.resize(5);
    expect_live(5);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 3, 0, 0}));
    vec.resize(2);
    expect_live(2);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2}));
    vec.resize(4, 8);
    expect_live(4);
    EXPECT_EQ(values(vec), (std::vector<int>{1, 2, 8, 8}));
    auto capacity = vec.capacity();
    vec.resize(0);
    expect_live(0);
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vec.capacity(), capacity);
}

TEST_F(Vector_Test, Reserve) {
    sgcl::vector<Int> vec({1, 2, 3});
    vec.reserve(100);
    EXPECT_GE(vec.capacity(), 100u);
    auto data = vec.data();
    while (vec.size() < 100) {
        vec.push_back(int(vec.size()) + 1);
    }
    EXPECT_EQ(vec.data(), data);
    vec.reserve(10);
    EXPECT_EQ(vec.data(), data);
    expect_live(100);
}

TEST_F(Vector_Test, Swap) {
    sgcl::vector<Int> vec1({1, 2});
    sgcl::vector<Int> vec2({4, 5, 6, 7});
    vec1.swap(vec2);
    expect_live(6);
    EXPECT_EQ(values(vec1), (std::vector<int>{4, 5, 6, 7}));
    EXPECT_EQ(values(vec2), (std::vector<int>{1, 2}));
}

TEST_F(Vector_Test, Clear) {
    sgcl::vector<Int> vec({5, 6, 7});
    auto capacity = vec.capacity();
    vec.clear();
    expect_live(0);
    EXPECT_TRUE(vec.empty());
    EXPECT_EQ(vec.capacity(), capacity);
    EXPECT_EQ(vec.begin(), vec.end());
    EXPECT_EQ(vec.rbegin(), vec.rend());
}

// clear, pop_back, erase and resize destroy elements on the calling thread (SPEC §4.8).
TEST_F(Vector_Test, ShrinkingDestroysImmediately) {
    sgcl::vector<Int> vec;
    vec.reserve(10);
    for (int i = 0; i < 10; ++i) {
        vec.push_back(i);
    }
    EXPECT_EQ(Int::live(), 10u);
    vec.pop_back();
    EXPECT_EQ(Int::live(), 9u);
    vec.erase(vec.begin());
    EXPECT_EQ(Int::live(), 8u);
    vec.erase(vec.begin(), vec.begin() + 2);
    EXPECT_EQ(Int::live(), 6u);
    vec.resize(3);
    EXPECT_EQ(Int::live(), 3u);
    vec.clear();
    EXPECT_EQ(Int::live(), 0u);
}

TEST_F(Vector_Test, NoexceptOperations) {
    using V = sgcl::vector<Int>;
    static_assert(std::is_nothrow_default_constructible_v<V>);
    static_assert(std::is_nothrow_move_constructible_v<V>);
    static_assert(std::is_nothrow_move_assignable_v<V>);
    V v;
    static_assert(noexcept(v.clear()) && noexcept(v.pop_back()) && noexcept(v.swap(v)));
    static_assert(noexcept(v.size()) && noexcept(v.data()) && noexcept(v.begin()));
}

namespace {
    struct ElementTag;
    using ElementReferent = Probe<ElementTag>;

    struct WithChild {
        tracked_ptr<ElementReferent> child;
        int id = 0;
    };
}

// Elements' tracked_ptrs are children of the storage: traced, and dropped with it.
TEST_F(Vector_Test, ElementsAreTraced) {
    ASSERT_TRUE(collect_until([] { return ElementReferent::alive() == 0; }));
    {
        sgcl::vector<WithChild> vec;
        for (int i = 0; i < 300; ++i) {
            WithChild w;
            w.child = make_tracked<ElementReferent>(i);
            w.id = i;
            vec.push_back(w);
        }
        EXPECT_TRUE(detail::TrackedPtrAccess::child(vec[0].child));
        sgcl::vector<tracked_ptr<ElementReferent>> ptrs;
        for (int i = 0; i < 300; ++i) {
            ptrs.push_back(make_tracked<ElementReferent>(1000 + i));
        }
        settle();
        EXPECT_EQ(ElementReferent::alive(), 600);
        for (int i = 0; i < 300; ++i) {
            ASSERT_EQ(vec[i].child->value, i);
            ASSERT_EQ(ptrs[i]->value, 1000 + i);
        }
        vec.erase(vec.begin(), vec.begin() + 100);
        ptrs.resize(100);
        EXPECT_TRUE(collect_until([] { return ElementReferent::alive() == 300; }));
        EXPECT_EQ(vec[0].child->value, 100);
        auto moved = std::move(vec);
        settle();
        EXPECT_EQ(ElementReferent::alive(), 300);
        EXPECT_EQ(moved[199].child->value, 299);
    }
    EXPECT_TRUE(collect_until([] { return ElementReferent::alive() == 0; }));
}

// A vector held by a managed object keeps its elements alive through the object.
TEST_F(Vector_Test, VectorInsideManagedObject) {
    struct Owner {
        sgcl::vector<tracked_ptr<ElementReferent>> items;
    };
    ASSERT_TRUE(collect_until([] { return ElementReferent::alive() == 0; }));
    {
        tracked_ptr<Owner> owner = make_tracked<Owner>();
        for (int i = 0; i < 100; ++i) {
            owner->items.push_back(make_tracked<ElementReferent>(i));
        }
        settle();
        EXPECT_EQ(ElementReferent::alive(), 100);
        EXPECT_EQ(owner->items[99]->value, 99);
    }
    EXPECT_TRUE(collect_until([] { return ElementReferent::alive() == 0; }));
}
