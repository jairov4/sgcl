//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <algorithm>
#include <memory>
#include <optional>

using detail::TrackedPtrAccess;

TEST(Maker_Tests, DefaultConstructor) {
    struct S {
        char value = 2;
    };
    auto ptr = make_tracked<S>();
    ASSERT_TRUE(ptr);
    EXPECT_EQ(ptr->value, 2);
    auto tr = make_tracked<tracked_ptr<int>>();
    ASSERT_TRUE(tr);
    EXPECT_EQ(*tr, nullptr);
}

TEST(Maker_Tests, ParameterConstructor) {
    auto ptr = make_tracked<int>(3);
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 3);
    auto foo = make_tracked<Foo>(4);
    EXPECT_EQ(foo->get_value(), 4);
    EXPECT_EQ(foo->ptr->value, 4);
}

TEST(Maker_Tests, LargestObject) {
    struct Big {
        char bytes[60000];
        tracked_ptr<int> tail;
    };
    auto big = make_tracked<Big>();
    big->tail = make_tracked<int>(5);
    big->bytes[59999] = 1;
    tracked_ptr<Big> held = std::move(big);
    settle();
    EXPECT_EQ(*held->tail, 5);
}

namespace {
    struct DiscoveryTag;
    using DiscoveryChild = Probe<DiscoveryTag>;

    struct DiscoveryThrower {
        tracked_ptr<DiscoveryChild> child;
        tracked_ptr<DiscoveryThrower> other;
        int value;

        DiscoveryThrower(int v, bool fail)
        : child(make_tracked<DiscoveryChild>(v))
        , value(v) {
            last_this = this;
            if (fail) {
                throw Boom{};
            }
        }

        ~DiscoveryThrower() {
            ++destroyed;
        }

        inline static std::atomic<int> destroyed = 0;
        inline static void* last_this = nullptr;
    };
}

// SPEC §3.1, §4.4, §5.1: the first constructions of a type throw while its layout is being discovered.
TEST(Maker_Tests, ConstructorThrowsDuringLayoutDiscovery) {
    EXPECT_THROW(make_tracked<DiscoveryThrower>(1, true), Boom);
    void* failed = DiscoveryThrower::last_this;
    EXPECT_TRUE(collect_until([] { return DiscoveryChild::alive() == 0; }));
    EXPECT_EQ(DiscoveryThrower::destroyed.load(), 0);
    {
        auto [guard, objects] = collector::get_live_objects();
        EXPECT_EQ(std::count(objects.begin(), objects.end(), failed), 0);
    }
    EXPECT_THROW(make_tracked<DiscoveryThrower>(2, true), Boom);
    {
        tracked_ptr<DiscoveryThrower> a = make_tracked<DiscoveryThrower>(3, false);
        EXPECT_TRUE(TrackedPtrAccess::child(a->child));
        EXPECT_TRUE(TrackedPtrAccess::child(a->other));
        tracked_ptr<DiscoveryThrower> b = make_tracked<DiscoveryThrower>(4, false);
        EXPECT_TRUE(TrackedPtrAccess::child(b->child));
        EXPECT_TRUE(TrackedPtrAccess::child(b->other));
        a->other = b;
        b->other = a;
        // Thrown after the layout is final.
        EXPECT_THROW(make_tracked<DiscoveryThrower>(5, true), Boom);
        EXPECT_TRUE(collect_until([] { return DiscoveryChild::alive() == 2; }));
        b = nullptr;
        settle();
        EXPECT_EQ(DiscoveryThrower::destroyed.load(), 0);
        EXPECT_EQ(DiscoveryChild::alive(), 2);
        EXPECT_EQ(a->child->value, 3);
        EXPECT_EQ(a->other->child->value, 4);
        EXPECT_EQ(a->other->other, a);
    }
    // The cycle a <-> b is collected; the failed constructions never ran a destructor.
    EXPECT_TRUE(collect_until([] { return DiscoveryThrower::destroyed.load() == 2; }));
    EXPECT_TRUE(collect_until([] { return DiscoveryChild::alive() == 0; }));
    settle();
    EXPECT_EQ(DiscoveryThrower::destroyed.load(), 2);
}

namespace {
    struct LeakerTag;
    using LeakerChild = Probe<LeakerTag>;

    std::unique_ptr<tracked_ptr<struct Leaker>> leaked;

    struct Leaker {
        tracked_ptr<LeakerChild> child;

        Leaker(bool fail)
        : child(make_tracked<LeakerChild>()) {
            if (fail) {
                *leaked = tracked_ptr<Leaker>(this);
                throw Boom{};
            }
        }

        ~Leaker() {
            ++destroyed;
        }

        inline static std::atomic<int> destroyed = 0;
    };
}

// SPEC §5.1 row 4: `this` stored into an external tracked_ptr, then the constructor throws.
TEST(Maker_Tests, ConstructorLeaksThisThenThrows) {
    leaked = std::make_unique<tracked_ptr<Leaker>>();
    EXPECT_THROW(make_tracked<Leaker>(true), Boom);
    EXPECT_NE(leaked->get(), nullptr);
    settle();
    {
        tracked_ptr<Leaker> ok = make_tracked<Leaker>(false);
        EXPECT_THROW(make_tracked<Leaker>(true), Boom);
        settle();
        EXPECT_NE(ok->child, nullptr);
        EXPECT_EQ(Leaker::destroyed.load(), 0);
    }
    leaked->reset();
    EXPECT_TRUE(collect_until([] { return Leaker::destroyed.load() == 1 && LeakerChild::alive() == 0; }));
    for (int i = 0; i < 100; ++i) {
        tracked_ptr<Leaker> p = make_tracked<Leaker>(false);
    }
    EXPECT_TRUE(collect_until([] { return Leaker::destroyed.load() == 101 && LeakerChild::alive() == 0; }));
    leaked.reset();
}

namespace {
    std::atomic<bool> self_alias_ctor_done = {false};
    std::atomic<bool> self_alias_destroyed_mid_construction = {false};
    std::unique_ptr<tracked_ptr<struct SelfAliasNode>> self_alias_external;

    struct SelfAliasGroup;

    struct SelfAliasNode {
        tracked_ptr<SelfAliasGroup> children;
        tracked_ptr<SelfAliasNode> self;
        tracked_ptr<int> payload;

        SelfAliasNode();

        ~SelfAliasNode() {
            if (!self_alias_ctor_done.load()) {
                self_alias_destroyed_mid_construction = true;
            }
            ++destroyed;
        }

        inline static std::atomic<int> destroyed = 0;
    };

    struct SelfAliasGroup {
        tracked_ptr<SelfAliasNode> owner;

        explicit SelfAliasGroup(tracked_ptr<SelfAliasNode> o)
        : owner(std::move(o)) {
        }
    };

    // `this` goes into a temporary, a child of another object, a member of this object and an external root.
    SelfAliasNode::SelfAliasNode()
    : children(make_tracked<SelfAliasGroup>(tracked_ptr<SelfAliasNode>(this)))
    , self(this)
    , payload(make_tracked<int>(7)) {
        *self_alias_external = tracked_ptr<SelfAliasNode>(this);
        settle();
        self_alias_ctor_done = true;
    }
}

// SPEC §5.1 row 3: the object stays protected until its constructor returns, then is owned as usual.
TEST(Maker_Tests, TrackedPtrToThisInConstructor) {
    self_alias_external = std::make_unique<tracked_ptr<SelfAliasNode>>();
    {
        tracked_ptr<SelfAliasNode> node = make_tracked<SelfAliasNode>();
        EXPECT_FALSE(self_alias_destroyed_mid_construction.load());
        ASSERT_NE(node->payload, nullptr);
        ASSERT_NE(node->children, nullptr);
        EXPECT_EQ(node->children->owner.get(), node.get());
        EXPECT_EQ(node->self.get(), node.get());
        EXPECT_EQ(self_alias_external->get(), node.get());
        EXPECT_EQ(*node->payload, 7);
    }
    settle();
    EXPECT_EQ(SelfAliasNode::destroyed.load(), 0);
    EXPECT_EQ(*(*self_alias_external)->payload, 7);
    tracked_ptr<SelfAliasNode> first = *self_alias_external;
    {
        // Owned by the unique_ptr after the constructor: a root even with no tracked_ptr.
        self_alias_ctor_done = false;
        auto owned = make_tracked<SelfAliasNode>();
        self_alias_external->reset();
        settle();
        EXPECT_EQ(SelfAliasNode::destroyed.load(), 0);
        EXPECT_EQ(*owned->payload, 7);
        tracked_ptr<SelfAliasNode> handed(std::move(owned));
        settle();
        EXPECT_EQ(SelfAliasNode::destroyed.load(), 0);
    }
    EXPECT_TRUE(collect_until([] { return SelfAliasNode::destroyed.load() == 1; }));
    first.reset();
    self_alias_external.reset();
    EXPECT_TRUE(collect_until([] { return SelfAliasNode::destroyed.load() == 2; }));
    EXPECT_FALSE(self_alias_destroyed_mid_construction.load());
}

namespace {
    struct ConditionalTag;
    using ConditionalReferent = Probe<ConditionalTag>;

    // A tracked_ptr member constructed by some constructions only.
    struct ConditionalHolder {
        int id;
        std::optional<tracked_ptr<ConditionalReferent>> member;

        ConditionalHolder(int i, bool engage)
        : id(i) {
            if (engage) {
                member.emplace(make_tracked<ConditionalReferent>(i));
            }
        }
    };
}

// SPEC §4.4: a member whose offset the layout does not record becomes an external root.
TEST(Maker_Tests, ConditionalMemberIsAnExternalRoot) {
    {
        tracked_ptr<ConditionalHolder> first = make_tracked<ConditionalHolder>(0, false);
        tracked_ptr<ConditionalHolder> later = make_tracked<ConditionalHolder>(1, true);
        ASSERT_TRUE(later->member.has_value());
        EXPECT_TRUE(TrackedPtrAccess::external(*later->member));
        first->member.emplace(make_tracked<ConditionalReferent>(2));
        EXPECT_TRUE(TrackedPtrAccess::external(*first->member));
        sgcl::vector<tracked_ptr<ConditionalHolder>> many;
        for (int i = 0; i < 50; ++i) {
            many.push_back(make_tracked<ConditionalHolder>(10 + i, i % 2 == 0));
        }
        settle();
        settle();
        EXPECT_EQ(ConditionalReferent::alive(), 27);
        EXPECT_EQ((*later->member)->value, 1);
        EXPECT_EQ((*first->member)->value, 2);
        for (int i = 0; i < 50; i += 2) {
            EXPECT_EQ((*many[i]->member)->value, 10 + i);
        }
        later->member.reset();
        many.resize(10);
        EXPECT_TRUE(collect_until([] { return ConditionalReferent::alive() == 6; }));
    }
    EXPECT_TRUE(collect_until([] { return ConditionalReferent::alive() == 0; }));
}
