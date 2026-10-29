//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <memory>
#include <unordered_set>

using detail::TrackedPtrAccess;

TEST(TrackedPtr_Tests, DefaultConstructor) {
    { // stack
        tracked_ptr<int> ptr;
        EXPECT_EQ(ptr, nullptr);
        EXPECT_TRUE(TrackedPtrAccess::on_stack(ptr));
    }
    { // child
        auto ptr = make_tracked<tracked_ptr<int>>();
        EXPECT_EQ(*ptr, nullptr);
        EXPECT_TRUE(TrackedPtrAccess::child(*ptr));
    }
    { // external
        auto ptr = std::make_unique<tracked_ptr<int>>();
        EXPECT_EQ(*ptr, nullptr);
        EXPECT_TRUE(TrackedPtrAccess::external(*ptr));
    }
}

TEST(TrackedPtr_Tests, NullConstructor) {
    { // stack
        tracked_ptr<int> ptr(nullptr);
        EXPECT_EQ(ptr, nullptr);
    }
    { // child
        auto ptr = make_tracked<tracked_ptr<int>>(nullptr);
        EXPECT_EQ(*ptr, nullptr);
    }
    { // external
        auto ptr = std::make_unique<tracked_ptr<int>>(nullptr);
        EXPECT_EQ(*ptr, nullptr);
    }
}

TEST(TrackedPtr_Tests, UniqueConstructor) {
    { // stack
        tracked_ptr<Bar> ptr = make_tracked<Foo>(3);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(ptr->get_value(), 3);
    }
    { // child
        auto ptr = make_tracked<tracked_ptr<Foo>>(make_tracked<Foo>(3));
        ASSERT_NE(*ptr, nullptr);
        EXPECT_EQ((*ptr)->get_value(), 3);
    }
    { // external
        auto ptr = std::make_unique<tracked_ptr<Foo>>(make_tracked<Foo>(3));
        ASSERT_NE(*ptr, nullptr);
        EXPECT_EQ((*ptr)->get_value(), 3);
    }
}

TEST(TrackedPtr_Tests, RawConstructor) {
    { // stack
        tracked_ptr<Foo> foo = make_tracked<Foo>(10);
        tracked_ptr<int> alias(&foo->value);
        ASSERT_NE(alias, nullptr);
        EXPECT_EQ(*alias, 10);
    }
    { // child
        tracked_ptr<Foo> foo = make_tracked<Foo>(10);
        auto alias = make_tracked<tracked_ptr<int>>(&foo->value);
        ASSERT_NE(*alias, nullptr);
        EXPECT_EQ(**alias, 10);
    }
    { // external
        tracked_ptr<Foo> foo = make_tracked<Foo>(10);
        auto alias = std::make_unique<tracked_ptr<int>>(&foo->value);
        ASSERT_NE(*alias, nullptr);
        EXPECT_EQ(**alias, 10);
    }
    { // null
        tracked_ptr<int> ptr((int*)nullptr);
        EXPECT_EQ(ptr, nullptr);
    }
}

TEST(TrackedPtr_Tests, CopyConstructor) {
    tracked_ptr<int> ptr1 = make_tracked<int>(8);
    { // stack
        tracked_ptr<int> ptr2(ptr1);
        ASSERT_NE(ptr2, nullptr);
        EXPECT_EQ(*ptr2, 8);
    }
    { // child
        auto ptr2 = make_tracked<tracked_ptr<int>>(ptr1);
        ASSERT_NE(*ptr2, nullptr);
        EXPECT_EQ(**ptr2, 8);
    }
    { // external
        auto ptr2 = std::make_unique<tracked_ptr<int>>(ptr1);
        ASSERT_NE(*ptr2, nullptr);
        EXPECT_EQ(**ptr2, 8);
    }
}

TEST(TrackedPtr_Tests, CopyCastConstructor) {
    tracked_ptr<Foo> foo = make_tracked<Foo>(4);
    { // stack
        tracked_ptr<Bar> bar = foo;
        ASSERT_NE(bar, nullptr);
        EXPECT_EQ(bar->get_value(), 4);
    }
    { // child
        auto bar = make_tracked<tracked_ptr<Bar>>(foo);
        ASSERT_NE(*bar, nullptr);
        EXPECT_EQ((*bar)->get_value(), 4);
    }
    { // external
        auto bar = std::make_unique<tracked_ptr<Bar>>(foo);
        ASSERT_NE(*bar, nullptr);
        EXPECT_EQ((*bar)->get_value(), 4);
    }
}

TEST(TrackedPtr_Tests, MoveConstructor) {
    { // into stack
        tracked_ptr<int> s = make_tracked<int>(8);
        auto h = make_tracked<tracked_ptr<int>>(make_tracked<int>(9));
        auto e = std::make_unique<tracked_ptr<int>>(make_tracked<int>(10));
        tracked_ptr<int> ss(std::move(s));
        ASSERT_NE(ss, nullptr);
        EXPECT_EQ(*ss, 8);
        EXPECT_EQ(s, nullptr);
        tracked_ptr<int> sh(std::move(*h));
        ASSERT_NE(sh, nullptr);
        EXPECT_EQ(*sh, 9);
        EXPECT_EQ(*h, nullptr);
        tracked_ptr<int> se(std::move(*e));
        ASSERT_NE(se, nullptr);
        EXPECT_EQ(*se, 10);
        EXPECT_EQ(*e, nullptr);
    }
    { // into child
        tracked_ptr<int> s = make_tracked<int>(8);
        auto h = make_tracked<tracked_ptr<int>>(make_tracked<int>(9));
        auto e = std::make_unique<tracked_ptr<int>>(make_tracked<int>(10));
        auto hs = make_tracked<tracked_ptr<int>>(std::move(s));
        ASSERT_NE(*hs, nullptr);
        EXPECT_EQ(**hs, 8);
        EXPECT_EQ(s, nullptr);
        auto hh = make_tracked<tracked_ptr<int>>(std::move(*h));
        ASSERT_NE(*hh, nullptr);
        EXPECT_EQ(**hh, 9);
        EXPECT_EQ(*h, nullptr);
        auto he = make_tracked<tracked_ptr<int>>(std::move(*e));
        ASSERT_NE(*he, nullptr);
        EXPECT_EQ(**he, 10);
        EXPECT_EQ(*e, nullptr);
    }
    { // into external
        tracked_ptr<int> s = make_tracked<int>(8);
        auto h = make_tracked<tracked_ptr<int>>(make_tracked<int>(9));
        auto e = std::make_unique<tracked_ptr<int>>(make_tracked<int>(10));
        auto es = std::make_unique<tracked_ptr<int>>(std::move(s));
        ASSERT_NE(*es, nullptr);
        EXPECT_EQ(**es, 8);
        EXPECT_EQ(s, nullptr);
        auto eh = std::make_unique<tracked_ptr<int>>(std::move(*h));
        ASSERT_NE(*eh, nullptr);
        EXPECT_EQ(**eh, 9);
        EXPECT_EQ(*h, nullptr);
        auto ee = std::make_unique<tracked_ptr<int>>(std::move(*e));
        ASSERT_NE(*ee, nullptr);
        EXPECT_EQ(**ee, 10);
        EXPECT_EQ(*e, nullptr);
    }
}

TEST(TrackedPtr_Tests, MoveCastConstructor) {
    { // into stack
        tracked_ptr<Foo> s = make_tracked<Foo>(4);
        auto h = make_tracked<tracked_ptr<Foo>>(make_tracked<Foo>(5));
        auto e = std::make_unique<tracked_ptr<Foo>>(make_tracked<Foo>(6));
        tracked_ptr<Bar> ss(std::move(s));
        ASSERT_NE(ss, nullptr);
        EXPECT_EQ(ss->get_value(), 4);
        EXPECT_EQ(s, nullptr);
        tracked_ptr<Bar> sh(std::move(*h));
        ASSERT_NE(sh, nullptr);
        EXPECT_EQ(sh->get_value(), 5);
        EXPECT_EQ(*h, nullptr);
        tracked_ptr<Bar> se(std::move(*e));
        ASSERT_NE(se, nullptr);
        EXPECT_EQ(se->get_value(), 6);
        EXPECT_EQ(*e, nullptr);
    }
    { // into child
        tracked_ptr<Foo> s = make_tracked<Foo>(4);
        auto h = make_tracked<tracked_ptr<Foo>>(make_tracked<Foo>(5));
        auto e = std::make_unique<tracked_ptr<Foo>>(make_tracked<Foo>(6));
        auto hs = make_tracked<tracked_ptr<Bar>>(std::move(s));
        ASSERT_NE(*hs, nullptr);
        EXPECT_EQ((*hs)->get_value(), 4);
        EXPECT_EQ(s, nullptr);
        auto hh = make_tracked<tracked_ptr<Bar>>(std::move(*h));
        ASSERT_NE(*hh, nullptr);
        EXPECT_EQ((*hh)->get_value(), 5);
        EXPECT_EQ(*h, nullptr);
        auto he = make_tracked<tracked_ptr<Bar>>(std::move(*e));
        ASSERT_NE(*he, nullptr);
        EXPECT_EQ((*he)->get_value(), 6);
        EXPECT_EQ(*e, nullptr);
    }
    { // into external
        tracked_ptr<Foo> s = make_tracked<Foo>(4);
        auto h = make_tracked<tracked_ptr<Foo>>(make_tracked<Foo>(5));
        auto e = std::make_unique<tracked_ptr<Foo>>(make_tracked<Foo>(6));
        auto es = std::make_unique<tracked_ptr<Bar>>(std::move(s));
        ASSERT_NE(*es, nullptr);
        EXPECT_EQ((*es)->get_value(), 4);
        EXPECT_EQ(s, nullptr);
        auto eh = std::make_unique<tracked_ptr<Bar>>(std::move(*h));
        ASSERT_NE(*eh, nullptr);
        EXPECT_EQ((*eh)->get_value(), 5);
        EXPECT_EQ(*h, nullptr);
        auto ee = std::make_unique<tracked_ptr<Bar>>(std::move(*e));
        ASSERT_NE(*ee, nullptr);
        EXPECT_EQ((*ee)->get_value(), 6);
        EXPECT_EQ(*e, nullptr);
    }
}

TEST(TrackedPtr_Tests, CopyAssignmentOperator) {
    tracked_ptr<int> ptr1 = make_tracked<int>(3);
    tracked_ptr<int> ptr2;
    ptr2 = ptr1;
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(*ptr2, 3);
    ptr2 = ptr2;
    EXPECT_EQ(ptr2, ptr1);
}

TEST(TrackedPtr_Tests, CopyCastAssignmentOperator) {
    tracked_ptr<Foo> foo = make_tracked<Foo>(5);
    tracked_ptr<Bar> bar;
    bar = foo;
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->get_value(), 5);
}

TEST(TrackedPtr_Tests, MoveAssignmentOperator) {
    tracked_ptr<Foo> foo = make_tracked<Foo>(5);
    tracked_ptr<Bar> bar;
    bar = std::move(foo);
    EXPECT_EQ(foo, nullptr);
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->get_value(), 5);
    tracked_ptr<Bar> other = make_tracked<Foo>(6);
    bar = std::move(other);
    EXPECT_EQ(other, nullptr);
    EXPECT_EQ(bar->get_value(), 6);
    bar = std::move(bar);
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->get_value(), 6);
}

TEST(TrackedPtr_Tests, NullAssignmentOperator) {
    tracked_ptr<int> ptr = make_tracked<int>(8);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 8);
    ptr = nullptr;
    EXPECT_EQ(ptr, nullptr);
}

TEST(TrackedPtr_Tests, UniqueCastAssignmentOperator) {
    auto foo = make_tracked<Foo>(11);
    tracked_ptr<Bar> bar;
    bar = std::move(foo);
    EXPECT_FALSE(foo);
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->get_value(), 11);
}

TEST(TrackedPtr_Tests, BoolOperator) {
    tracked_ptr<int> ptr = make_tracked<int>(5);
    EXPECT_TRUE(ptr);
    ptr = nullptr;
    EXPECT_FALSE(ptr);
}

TEST(TrackedPtr_Tests, IndirectionOperator) {
    tracked_ptr<int> ptr = make_tracked<int>(15);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, *ptr.get());
}

TEST(TrackedPtr_Tests, StructureDereferenceOperator) {
    tracked_ptr<Foo> ptr = make_tracked<Foo>(3);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, ptr.get()->value);
}

TEST(TrackedPtr_Tests, Reset) {
    tracked_ptr<int> ptr = make_tracked<int>(9);
    ptr.reset();
    EXPECT_EQ(ptr, nullptr);
    ptr.reset();
    EXPECT_EQ(ptr, nullptr);
}

TEST(TrackedPtr_Tests, Swap) {
    { // stack with stack
        tracked_ptr<int> ptr1 = make_tracked<int>(2);
        tracked_ptr<int> ptr2 = make_tracked<int>(5);
        ptr1.swap(ptr2);
        ASSERT_NE(ptr1, nullptr);
        ASSERT_NE(ptr2, nullptr);
        EXPECT_EQ(*ptr1, 5);
        EXPECT_EQ(*ptr2, 2);
    }
    { // every pair of storage classes, including null
        tracked_ptr<int> s = make_tracked<int>(1);
        auto c = make_tracked<tracked_ptr<int>>(make_tracked<int>(2));
        auto e = std::make_unique<tracked_ptr<int>>(make_tracked<int>(3));
        auto n = std::make_unique<tracked_ptr<int>>();
        s.swap(*c);
        EXPECT_EQ(*s, 2);
        EXPECT_EQ(**c, 1);
        c->swap(*e);
        EXPECT_EQ(**c, 3);
        EXPECT_EQ(**e, 1);
        e->swap(s);
        EXPECT_EQ(**e, 2);
        EXPECT_EQ(*s, 1);
        n->swap(*e);
        EXPECT_EQ(*e, nullptr);
        EXPECT_EQ(**n, 2);
        EXPECT_TRUE(TrackedPtrAccess::on_stack(s));
        EXPECT_TRUE(TrackedPtrAccess::child(*c));
        EXPECT_TRUE(TrackedPtrAccess::external(*e));
    }
}

TEST(TrackedPtr_Tests, Comparisons) {
    tracked_ptr<Foo> a = make_tracked<Foo>(14);
    tracked_ptr<Foo> b = a;

    EXPECT_FALSE(a == nullptr);
    EXPECT_TRUE(a == b);

    EXPECT_FALSE(nullptr == a);
    EXPECT_TRUE(b == a);

    EXPECT_TRUE(a != nullptr);
    EXPECT_FALSE(a != b);

    EXPECT_TRUE(nullptr != a);
    EXPECT_FALSE(b != a);

    EXPECT_FALSE(a < nullptr);
    EXPECT_FALSE(a < b);

    EXPECT_TRUE(nullptr < a);
    EXPECT_FALSE(b < a);

    EXPECT_FALSE(a <= nullptr);
    EXPECT_TRUE(a <= b);

    EXPECT_TRUE(nullptr <= a);
    EXPECT_TRUE(b <= a);

    EXPECT_TRUE(a > nullptr);
    EXPECT_FALSE(a > b);

    EXPECT_FALSE(nullptr > a);
    EXPECT_FALSE(b > a);

    EXPECT_TRUE(a >= nullptr);
    EXPECT_TRUE(a >= b);

    EXPECT_FALSE(nullptr >= a);
    EXPECT_TRUE(b >= a);

    tracked_ptr<Foo> c = make_tracked<Foo>(15);
    EXPECT_EQ(a < c, a.get() < c.get());
    EXPECT_EQ(a <=> c, a.get() <=> c.get());
}

TEST(TrackedPtr_Tests, CompareAfterConversion) {
    tracked_ptr<Foo> foo = make_tracked<Foo>(1);
    tracked_ptr<Bar> bar = foo;
    tracked_ptr<Faz> faz = foo;
    // Bar and Faz are at different offsets inside Foo; comparison converts to the common pointer type.
    EXPECT_NE((void*)bar.get(), (void*)faz.get());
    EXPECT_TRUE(foo == bar);
    EXPECT_TRUE(bar == foo);
    EXPECT_TRUE(foo == faz);
    EXPECT_EQ(foo <=> bar, std::strong_ordering::equal);
}

TEST(TrackedPtr_Tests, Hash) {
    tracked_ptr<Foo> a = make_tracked<Foo>(1);
    tracked_ptr<Foo> b = a;
    tracked_ptr<Foo> n;
    EXPECT_EQ(std::hash<tracked_ptr<Foo>>{}(a), std::hash<Foo*>{}(a.get()));
    EXPECT_EQ(std::hash<tracked_ptr<Foo>>{}(a), std::hash<tracked_ptr<Foo>>{}(b));
    EXPECT_EQ(std::hash<tracked_ptr<Foo>>{}(n), std::hash<Foo*>{}(nullptr));
    std::unordered_set<tracked_ptr<Foo>> set;
    set.insert(a);
    set.insert(b);
    EXPECT_EQ(set.size(), 1u);
}

TEST(TrackedPtr_Tests, DynamicPointerCast) {
    tracked_ptr<Bar> bar = make_tracked<Foo>(1);
    auto far = dynamic_pointer_cast<Far>(bar);
    ASSERT_NE(far, nullptr);
    EXPECT_EQ(far->value, 2);
    auto faz = dynamic_pointer_cast<Faz>(bar);
    ASSERT_NE(faz, nullptr);
    EXPECT_EQ(faz->value, 3);
    auto foo = dynamic_pointer_cast<Foo>(faz);
    ASSERT_NE(foo, nullptr);
    EXPECT_EQ(foo->value, 1);
    EXPECT_EQ(dynamic_pointer_cast<Baz>(bar), nullptr);
    EXPECT_EQ(dynamic_pointer_cast<Foo>(tracked_ptr<Bar>()), nullptr);
    static_assert(noexcept(dynamic_pointer_cast<Foo>(bar)));
}

TEST(TrackedPtr_Tests, MembersAreNoexcept) {
    tracked_ptr<Foo> p;
    tracked_ptr<Bar> q;
    unique_ptr<Foo> u;
    static_assert(std::is_nothrow_default_constructible_v<tracked_ptr<Foo>>);
    static_assert(std::is_nothrow_copy_constructible_v<tracked_ptr<Foo>>);
    static_assert(std::is_nothrow_move_constructible_v<tracked_ptr<Foo>>);
    static_assert(std::is_nothrow_copy_assignable_v<tracked_ptr<Foo>>);
    static_assert(std::is_nothrow_move_assignable_v<tracked_ptr<Foo>>);
    static_assert(std::is_nothrow_constructible_v<tracked_ptr<Bar>, const tracked_ptr<Foo>&>);
    static_assert(std::is_nothrow_constructible_v<tracked_ptr<Bar>, tracked_ptr<Foo>&&>);
    static_assert(std::is_nothrow_constructible_v<tracked_ptr<Bar>, unique_ptr<Foo>&&>);
    static_assert(std::is_nothrow_constructible_v<tracked_ptr<Bar>, Foo*>);
    static_assert(noexcept(p.get()) && noexcept(*p) && noexcept(p.reset()) && noexcept(p.swap(p)));
    static_assert(noexcept(p == q) && noexcept(p <=> q) && noexcept(p == nullptr) && noexcept(q = std::move(u)));
}

namespace {
    struct KeepAliveTag;
    using KeepAlive = Probe<KeepAliveTag>;

    struct KeepAliveHolder {
        tracked_ptr<KeepAlive> child;
    };
}

// A referent survives collections while any storage class refers to it, and is reclaimed after.
TEST(TrackedPtr_Tests, EveryStorageClassIsTraced) {
    ASSERT_TRUE(collect_until([] { return KeepAlive::alive() == 0; }));
    {
        tracked_ptr<KeepAlive> stack = make_tracked<KeepAlive>(1);
        tracked_ptr<KeepAliveHolder> holder = make_tracked<KeepAliveHolder>();
        holder->child = make_tracked<KeepAlive>(2);
        EXPECT_TRUE(TrackedPtrAccess::child(holder->child));
        auto external = std::make_unique<tracked_ptr<KeepAlive>>(make_tracked<KeepAlive>(3));
        std::vector<tracked_ptr<KeepAlive>> std_vector;
        for (int i = 0; i < 100; ++i) {
            std_vector.emplace_back(make_tracked<KeepAlive>(4 + i));
        }
        EXPECT_TRUE(TrackedPtrAccess::external(std_vector.front()));
        settle();
        EXPECT_EQ(KeepAlive::alive(), 103);
        EXPECT_EQ(stack->value, 1);
        EXPECT_EQ(holder->child->value, 2);
        EXPECT_EQ((*external)->value, 3);
        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(std_vector[i]->value, 4 + i);
        }
        std_vector.erase(std_vector.begin(), std_vector.begin() + 50);
        external.reset();
        stack = nullptr;
        EXPECT_TRUE(collect_until([] { return KeepAlive::alive() == 51; }));
        settle();
        EXPECT_EQ(KeepAlive::alive(), 51);
        EXPECT_EQ(holder->child->value, 2);
    }
    EXPECT_TRUE(collect_until([] { return KeepAlive::alive() == 0; }));
}

namespace {
    struct MovedFromTag;
    using MovedFrom = Probe<MovedFromTag>;

    struct MovedFromHolder {
        tracked_ptr<MovedFrom> child;
    };

    // Every use SPEC §3.3 allows on a moved-from tracked_ptr.
    void check_moved_from(tracked_ptr<MovedFrom>& p, const tracked_ptr<MovedFrom>& other) {
        EXPECT_EQ(p.get(), nullptr);
        EXPECT_FALSE(p);
        EXPECT_TRUE(p == nullptr);
        EXPECT_TRUE(p == tracked_ptr<MovedFrom>());
        EXPECT_FALSE(p == other);
        EXPECT_TRUE(p < other);
        p.reset();
        EXPECT_EQ(p, nullptr);
        p = other;
        EXPECT_EQ(p, other);
        tracked_ptr<MovedFrom> taken(std::move(p));
        EXPECT_EQ(p, nullptr);
        p = std::move(taken);
        EXPECT_EQ(p, other);
        EXPECT_EQ(taken, nullptr);
        tracked_ptr<MovedFrom> sink(std::move(p));
        EXPECT_EQ(p, nullptr);
        p = make_tracked<MovedFrom>(77);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->value, 77);
    }
}

TEST(TrackedPtr_Tests, MovedFromIsNullAndUsable) {
    ASSERT_TRUE(collect_until([] { return MovedFrom::alive() == 0; }));
    tracked_ptr<MovedFrom> other = make_tracked<MovedFrom>(1);
    { // stack
        tracked_ptr<MovedFrom> p = make_tracked<MovedFrom>(2);
        tracked_ptr<MovedFrom> q(std::move(p));
        EXPECT_TRUE(TrackedPtrAccess::on_stack(p));
        check_moved_from(p, other);
        tracked_ptr<MovedFrom> r = make_tracked<MovedFrom>(3);
        q = std::move(r);
        check_moved_from(r, other);
    }
    { // child
        auto holder = make_tracked<MovedFromHolder>();
        holder->child = make_tracked<MovedFrom>(4);
        tracked_ptr<MovedFrom> q(std::move(holder->child));
        EXPECT_TRUE(TrackedPtrAccess::child(holder->child));
        check_moved_from(holder->child, other);
        auto holder2 = make_tracked<MovedFromHolder>();
        holder2->child = make_tracked<MovedFrom>(5);
        holder->child = std::move(holder2->child);
        check_moved_from(holder2->child, other);
    }
    { // external, through std::unique_ptr: moved into another external, a stack and a child tracked_ptr
        auto a = std::make_unique<tracked_ptr<MovedFrom>>(make_tracked<MovedFrom>(6));
        auto b = std::make_unique<tracked_ptr<MovedFrom>>(std::move(*a));
        EXPECT_TRUE(TrackedPtrAccess::external(*a));
        check_moved_from(*a, other);
        tracked_ptr<MovedFrom> s(std::move(*b));
        check_moved_from(*b, other);
        auto c = std::make_unique<tracked_ptr<MovedFrom>>(make_tracked<MovedFrom>(7));
        auto holder = make_tracked<MovedFromHolder>();
        holder->child = std::move(*c);
        check_moved_from(*c, other);
        // Moved-from twice, then assigned from another external one.
        auto d = std::make_unique<tracked_ptr<MovedFrom>>(make_tracked<MovedFrom>(8));
        auto e = std::make_unique<tracked_ptr<MovedFrom>>(std::move(*d));
        auto f = std::make_unique<tracked_ptr<MovedFrom>>(std::move(*d));
        EXPECT_EQ(*f, nullptr);
        *d = std::move(*e);
        EXPECT_EQ((*d)->value, 8);
        EXPECT_EQ(*e, nullptr);
        *f = std::move(*e);
        EXPECT_EQ(*f, nullptr);
        check_moved_from(*e, other);
    }
    { // external, as elements of a std::vector that reallocates
        std::vector<tracked_ptr<MovedFrom>> v;
        for (int i = 0; i < 64; ++i) {
            v.emplace_back(make_tracked<MovedFrom>(100 + i));
        }
        std::vector<tracked_ptr<MovedFrom>> w(std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
        for (auto& p: v) {
            check_moved_from(p, other);
        }
        v.clear();
        for (int i = 0; i < 64; ++i) {
            EXPECT_EQ(w[i]->value, 100 + i);
        }
    }
    EXPECT_TRUE(collect_until([] { return MovedFrom::alive() == 1; }));
    EXPECT_EQ(other->value, 1);
}

namespace {
    struct ReassignTag;
    using Reassign = Probe<ReassignTag>;
}

// A referent stored into a moved-from external tracked_ptr is kept alive by it.
TEST(TrackedPtr_Tests, MovedFromExternalKeepsNewReferentAlive) {
    ASSERT_TRUE(collect_until([] { return Reassign::alive() == 0; }));
    auto a = std::make_unique<tracked_ptr<Reassign>>(make_tracked<Reassign>(1));
    auto b = std::make_unique<tracked_ptr<Reassign>>(std::move(*a));
    b.reset();
    *a = make_tracked<Reassign>(2);
    auto c = std::make_unique<tracked_ptr<Reassign>>(make_tracked<Reassign>(3));
    auto d = std::make_unique<tracked_ptr<Reassign>>(std::move(*c));
    {
        tracked_ptr<Reassign> tmp = make_tracked<Reassign>(4);
        *c = tmp;
    }
    EXPECT_TRUE(collect_until([] { return Reassign::alive() == 3; }));
    settle();
    EXPECT_EQ(Reassign::alive(), 3);
    EXPECT_EQ((*a)->value, 2);
    EXPECT_EQ((*c)->value, 4);
    EXPECT_EQ((*d)->value, 3);
    a.reset();
    c.reset();
    d.reset();
    EXPECT_TRUE(collect_until([] { return Reassign::alive() == 0; }));
}
