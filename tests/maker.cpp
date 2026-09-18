//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

TEST(Maker_Tests, DefaultConstructor) {
    struct S {
        char value = 2;
    };
    auto ptr = make_tracked<S>();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 2);
    auto tr = make_tracked<tracked_ptr<int>>();
    EXPECT_NE(tr, nullptr);
    EXPECT_EQ(*tr, nullptr);
}

TEST(Maker_Tests, ParameterConstructor) {
    auto ptr = make_tracked<int>(3);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 3);
}

TEST(Maker_Tests, DefaultArrayConstructor) {
    struct S {
        char value = 9;
    };
    auto t = make_tracked<S[]>(3);
    EXPECT_NE(t, nullptr);
    EXPECT_EQ(t[0].value, 9);
    EXPECT_EQ(t[1].value, 9);
    EXPECT_EQ(t[2].value, 9);
    auto tr = make_tracked<tracked_ptr<int>[]>(3);
    EXPECT_NE(tr, nullptr);
    EXPECT_EQ(tr[0], nullptr);
    EXPECT_EQ(tr[1], nullptr);
    EXPECT_EQ(tr[2], nullptr);
}

TEST(Maker_Tests, ArrayConstructorNValues) {
    auto ptr = make_tracked<int[]>(3, 5);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr[2], 5);
    auto foo = make_tracked<Foo[]>(3, 7);
    EXPECT_NE(foo, nullptr);
    EXPECT_EQ(foo[2].get_value(), 7);
    ptr = make_tracked<int[]>(7000, 5);
    EXPECT_EQ(ptr.size(), 7000);
    for (size_t i = 0; i < 7000; ++i) {
        EXPECT_EQ(ptr.get()[i], 5);
        if (ptr.get()[i] != 5) {
            break;
        }
    }
}

TEST(Maker_Tests, InitializerListArrayConstructor) {
    auto ptr = make_tracked<int[]>({1, 2, 3});
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr[0], 1);
    EXPECT_EQ(ptr[1], 2);
    EXPECT_EQ(ptr[2], 3);
    auto foo = make_tracked<Foo[]>({4, 5, 6});
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(foo[0].get_value(), 4);
    EXPECT_EQ(foo[1].get_value(), 5);
    EXPECT_EQ(foo[2].get_value(), 6);
}

namespace {
    std::atomic<bool> self_alias_ctor_done = {false};
    std::atomic<bool> self_alias_destroyed_mid_construction = {false};

    struct SelfAliasGroup;

    struct SelfAliasNode {
        tracked_ptr<SelfAliasGroup> children;
        tracked_ptr<int> payload;

        SelfAliasNode();

        ~SelfAliasNode() {
            if (!self_alias_ctor_done.load()) {
                self_alias_destroyed_mid_construction = true;
            }
        }
    };

    struct SelfAliasGroup {
        tracked_ptr<SelfAliasNode> owner;

        explicit SelfAliasGroup(tracked_ptr<SelfAliasNode> o)
        : owner(std::move(o)) {
        }
    };

    // A constructor storing `this` into another tracked object (a child's back-reference to its
    // owner) hands the pointer over through the same unique_ptr -> tracked_ptr path make_tracked's
    // own result takes, while the object is still owned only by Maker's (non-root) unique_ptr.
    SelfAliasNode::SelfAliasNode()
    : children(make_tracked<SelfAliasGroup>(tracked_ptr<SelfAliasNode>(unique_ptr<SelfAliasNode>(detail::UniquePtr<SelfAliasNode>(this)))))
    , payload(make_tracked<int>(7)) {
        // Two full cycles: the first would demote a merely-Reachable slot to Used, the second would
        // then find it unreferenced from any root and destroy it while this constructor is running.
        collector::force_collect(true);
        collector::force_collect(true);
        self_alias_ctor_done = true;
    }
}

TEST(Maker_Tests, SelfAliasDuringConstructionSurvivesCollection) {
    self_alias_ctor_done = false;
    self_alias_destroyed_mid_construction = false;
    {
        tracked_ptr<SelfAliasNode> node = make_tracked<SelfAliasNode>();
        EXPECT_FALSE(self_alias_destroyed_mid_construction.load());
        EXPECT_NE(node->payload, nullptr);
        EXPECT_NE(node->children, nullptr);
        EXPECT_EQ(node->children->owner.get(), node.get());
        EXPECT_EQ(*node->payload, 7);
    }
    collector::force_collect(true);
    collector::force_collect(true);
    EXPECT_FALSE(self_alias_destroyed_mid_construction.load());
}
