//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

TEST(UniquePtr_Tests, DefaultConstructor) {
    unique_ptr<int> ptr;
    EXPECT_EQ(ptr.get(), nullptr);
    EXPECT_FALSE(ptr);
}

TEST(UniquePtr_Tests, NullConstructor) {
    unique_ptr<int> ptr(nullptr);
    EXPECT_EQ(ptr.get(), nullptr);
    EXPECT_FALSE(ptr);
}

TEST(UniquePtr_Tests, MakeTracked) {
    auto ptr = make_tracked<int>(7);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 7);
}

TEST(UniquePtr_Tests, MoveConstructor) {
    auto foo = make_tracked<Foo>(3);
    unique_ptr<Foo> foo2(std::move(foo));
    EXPECT_FALSE(foo);
    ASSERT_TRUE(foo2);
    EXPECT_EQ(foo2->get_value(), 3);
}

TEST(UniquePtr_Tests, MoveCastConstructor) {
    auto foo = make_tracked<Foo>(4);
    unique_ptr<Bar> bar(std::move(foo));
    EXPECT_FALSE(foo);
    ASSERT_TRUE(bar);
    EXPECT_EQ(bar->get_value(), 4);
}

TEST(UniquePtr_Tests, MoveAssignmentOperator) {
    auto ptr = make_tracked<int>(5);
    unique_ptr<int> ptr2;
    ptr2 = std::move(ptr);
    EXPECT_FALSE(ptr);
    ASSERT_TRUE(ptr2);
    EXPECT_EQ(*ptr2, 5);
}

TEST(UniquePtr_Tests, MoveCastAssignmentOperator) {
    auto foo = make_tracked<Foo>(6);
    unique_ptr<Bar> bar;
    bar = std::move(foo);
    EXPECT_FALSE(foo);
    ASSERT_TRUE(bar);
    EXPECT_EQ(bar->get_value(), 6);
}

TEST(UniquePtr_Tests, NullAssignmentOperator) {
    auto ptr = make_tracked<int>(8);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 8);
    ptr = nullptr;
    EXPECT_FALSE(ptr);
}

TEST(UniquePtr_Tests, IndirectionOperator) {
    auto ptr = make_tracked<int>(15);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, *ptr.get());
}

TEST(UniquePtr_Tests, StructureDereferenceOperator) {
    auto foo = make_tracked<Foo>(3);
    ASSERT_TRUE(foo);
    EXPECT_EQ(foo->value, foo.get()->value);
}

namespace {
    struct OwnedTag;
    using Owned = Probe<OwnedTag>;

    struct OwnedWithHook {
        ~OwnedWithHook() {
            ++destroyed;
        }
        void sgcl_finalize() noexcept {
            ++finalized;
        }
        inline static std::atomic<int> destroyed = 0;
        inline static std::atomic<int> finalized = 0;
    };
}

// SPEC §3.2: destroying or resetting a non-empty unique_ptr destroys the object on the calling thread, without a hook.
TEST(UniquePtr_Tests, ResetDestroysSynchronously) {
    auto before = Owned::destroyed.load();
    {
        auto p = make_tracked<Owned>(1);
    }
    EXPECT_EQ(Owned::destroyed.load(), before + 1);
    auto p = make_tracked<Owned>(2);
    p.reset();
    EXPECT_FALSE(p);
    EXPECT_EQ(Owned::destroyed.load(), before + 2);
    p = make_tracked<Owned>(3);
    p = nullptr;
    EXPECT_EQ(Owned::destroyed.load(), before + 3);
    p = make_tracked<Owned>(4);
    p = make_tracked<Owned>(5);
    EXPECT_EQ(Owned::destroyed.load(), before + 4);
    EXPECT_EQ(p->value, 5);
    p.reset();

    {
        auto h = make_tracked<OwnedWithHook>();
    }
    EXPECT_EQ(OwnedWithHook::destroyed.load(), 1);
    settle();
    EXPECT_EQ(OwnedWithHook::destroyed.load(), 1);
    EXPECT_EQ(OwnedWithHook::finalized.load(), 0);
}

// The owned object is a root: it survives collections, and so does what it refers to.
TEST(UniquePtr_Tests, OwnedObjectIsARoot) {
    struct Holder {
        tracked_ptr<Owned> child;
    };
    auto holder = make_tracked<Holder>();
    holder->child = make_tracked<Owned>(9);
    auto before = Owned::destroyed.load();
    settle();
    EXPECT_EQ(Owned::destroyed.load(), before);
    EXPECT_EQ(holder->child->value, 9);
    tracked_ptr<Holder> tracked = std::move(holder);
    EXPECT_FALSE(holder);
    settle();
    EXPECT_EQ(Owned::destroyed.load(), before);
    tracked.reset();
    EXPECT_TRUE(collect_until([&] { return Owned::destroyed.load() == before + 1; }));
}

TEST(UniquePtr_Tests, Release) {
    auto p = make_tracked<Owned>(6);
    Owned* raw = p.release();
    EXPECT_FALSE(p);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw->value, 6);
}
