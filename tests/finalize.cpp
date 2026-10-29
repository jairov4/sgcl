//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <memory>

// SPEC §4.7, §5.4, §5.6.

namespace {
    // Two objects with hooks referring to each other; both become garbage in the same cycle.
    struct CycleNode {
        int id;
        tracked_ptr<CycleNode> other;
        tracked_ptr<int> payload;

        explicit CycleNode(int i)
        : id(i)
        , payload(make_tracked<int>(i * 10)) {
        }

        void sgcl_finalize() noexcept {
            ++hooks;
            if (destroyed.load() != 0) {
                ++hook_after_destructor;
            }
            if (!other || other->id != 1 - id || !other->payload || *other->payload != (1 - id) * 10) {
                ++hook_saw_broken_graph;
            }
            if (!payload || *payload != id * 10) {
                ++hook_saw_broken_graph;
            }
        }

        ~CycleNode() {
            ++destroyed;
            if (other) {
                ++destructor_saw_child;
            }
        }

        inline static std::atomic<int> hooks = 0;
        inline static std::atomic<int> destroyed = 0;
        inline static std::atomic<int> hook_after_destructor = 0;
        inline static std::atomic<int> hook_saw_broken_graph = 0;
        inline static std::atomic<int> destructor_saw_child = 0;
    };
}

TEST(Finalize_Tests, HooksRunOnceBeforeAnyDestructorOfTheCycle) {
    {
        tracked_ptr<CycleNode> a = make_tracked<CycleNode>(0);
        a->other = make_tracked<CycleNode>(1);
        a->other->other = a;
        settle();
        EXPECT_EQ(CycleNode::hooks.load(), 0);
    }
    EXPECT_TRUE(collect_until([] { return CycleNode::destroyed.load() == 2; }));
    settle();
    EXPECT_EQ(CycleNode::hooks.load(), 2);
    EXPECT_EQ(CycleNode::hook_after_destructor.load(), 0);
    EXPECT_EQ(CycleNode::hook_saw_broken_graph.load(), 0);
    // Children are nulled before destructors run (SPEC §4.6).
    EXPECT_EQ(CycleNode::destructor_saw_child.load(), 0);
}

namespace {
    struct Phoenix;
    std::unique_ptr<tracked_ptr<Phoenix>> phoenix_home;

    struct Phoenix {
        tracked_ptr<int> payload = make_tracked<int>(17);

        void sgcl_finalize() noexcept {
            ++hooks;
            *phoenix_home = tracked_ptr<Phoenix>(this);
            // A hook may allocate.
            extra = make_tracked<int>(18);
        }

        ~Phoenix() {
            ++destroyed;
        }

        tracked_ptr<int> extra;
        inline static std::atomic<int> hooks = 0;
        inline static std::atomic<int> destroyed = 0;
    };
}

// A hook that makes its object reachable keeps it, and everything it reaches; the hook never runs again.
TEST(Finalize_Tests, ResurrectionAndAtMostOnce) {
    phoenix_home = std::make_unique<tracked_ptr<Phoenix>>();
    {
        tracked_ptr<Phoenix> p = make_tracked<Phoenix>();
    }
    EXPECT_TRUE(collect_until([] { return Phoenix::hooks.load() == 1; }));
    settle();
    settle();
    EXPECT_EQ(Phoenix::hooks.load(), 1);
    EXPECT_EQ(Phoenix::destroyed.load(), 0);
    ASSERT_NE(*phoenix_home, nullptr);
    EXPECT_EQ(*(*phoenix_home)->payload, 17);
    ASSERT_NE((*phoenix_home)->extra, nullptr);
    EXPECT_EQ(*(*phoenix_home)->extra, 18);
    phoenix_home->reset();
    EXPECT_TRUE(collect_until([] { return Phoenix::destroyed.load() == 1; }));
    settle();
    EXPECT_EQ(Phoenix::hooks.load(), 1);
    EXPECT_EQ(Phoenix::destroyed.load(), 1);
    phoenix_home.reset();
}

namespace {
    struct ForceFromHook {
        void sgcl_finalize() noexcept {
            waited = collector::force_collect(true) ? 1 : 0;
            requested = collector::force_collect(false) ? 1 : 0;
            count = collector::get_live_object_count();
            ++hooks;
        }

        inline static std::atomic<int> waited = -1;
        inline static std::atomic<int> requested = -1;
        inline static std::atomic<size_t> count = 0;
        inline static std::atomic<int> hooks = 0;
    };
}

// SPEC §5.6: on the collector thread force_collect(true) returns false without blocking, force_collect(false) true.
TEST(Finalize_Tests, ForceCollectFromHook) {
    tracked_ptr<int> live = make_tracked<int>(1);
    run_with_timeout([] {
        {
            tracked_ptr<ForceFromHook> p = make_tracked<ForceFromHook>();
        }
        EXPECT_TRUE(collect_until([] { return ForceFromHook::hooks.load() == 1; }));
    }, std::chrono::seconds(30), "force_collect from a finalize hook");
    EXPECT_EQ(ForceFromHook::waited.load(), 0);
    EXPECT_EQ(ForceFromHook::requested.load(), 1);
    EXPECT_GT(ForceFromHook::count.load(), 0u);
}

namespace {
    struct NoHookExpected {
        void sgcl_finalize() noexcept {
            ++hooks;
        }

        ~NoHookExpected() {
            ++destroyed;
        }

        inline static std::atomic<int> hooks = 0;
        inline static std::atomic<int> destroyed = 0;
    };

    struct Base {
        virtual ~Base() = default;

        void sgcl_finalize() noexcept {
            ++hooks;
        }

        inline static std::atomic<int> hooks = 0;
    };

    struct Derived : Base {
        int value = 3;
    };
}

// No hook for unique_ptr destruction or vector elements; an inherited hook counts.
TEST(Finalize_Tests, WhenHooksDoNotRun) {
    {
        auto owned = make_tracked<NoHookExpected>();
    }
    EXPECT_EQ(NoHookExpected::destroyed.load(), 1);
    {
        sgcl::vector<NoHookExpected> elements;
        elements.resize(10);
        elements.resize(30);
        elements.pop_back();
    }
    EXPECT_TRUE(collect_until([] { return NoHookExpected::destroyed.load() == 41; }));
    settle();
    EXPECT_EQ(NoHookExpected::hooks.load(), 0);
    {
        tracked_ptr<Base> d = make_tracked<Derived>();
    }
    EXPECT_TRUE(collect_until([] { return Base::hooks.load() == 1; }));
}
