//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <cstring>
#if !defined(_WIN32)
#include <pthread.h>
#endif

// SPEC §3.2, §4.2, §4.10, §5.1: ownership handover, adoption, termination.

using namespace std::chrono_literals;

namespace {
    struct OwnedTag;
    using OwnedProbe = Probe<OwnedTag>;

    struct SelfAliasing {
        tracked_ptr<SelfAliasing> self;
        OwnedProbe probe{1};
        SelfAliasing() : self(this) {}
    };

    std::atomic<long> self_adopting_alive = 0;

    // A vector element that adopts itself while being built into storage that is still owned.
    struct SelfAdoptingElement {
        long value;
        explicit SelfAdoptingElement(long v) : value(v) {
            adopt();
        }
        SelfAdoptingElement(SelfAdoptingElement&& o) noexcept : value(o.value) {
            adopt();
        }
        ~SelfAdoptingElement() {
            --self_adopting_alive;
        }
        void adopt() noexcept {
            ++self_adopting_alive;
            tracked_ptr<SelfAdoptingElement> me(this);
            EXPECT_EQ(me->value, value);
        }
    };
}

// An alias of an object a unique_ptr owns does not hand it over: it is destroyed once, by the reset.
TEST(Lifecycle_Tests, AliasDoesNotTakeOverUniquePtrOwnership) {
    ASSERT_TRUE(collect_until([] { return OwnedProbe::alive() == 0; }));
    auto owned = make_tracked<SelfAliasing>();
    {
        tracked_ptr<SelfAliasing> alias = owned->self;
        tracked_ptr<SelfAliasing> adopted(owned.get());
        EXPECT_EQ(alias.get(), owned.get());
        EXPECT_EQ(adopted.get(), owned.get());
    }
    settle();
    settle();
    EXPECT_EQ(OwnedProbe::alive(), 1);
    owned.reset();
    EXPECT_EQ(OwnedProbe::alive(), 0);
    settle();
    EXPECT_EQ(OwnedProbe::alive(), 0);
}

TEST(Lifecycle_Tests, VectorElementAdoptsItselfWhileGrowing) {
    {
        vector<SelfAdoptingElement> v;
        for (long i = 0; i < 100; ++i) {
            v.emplace_back(i);
        }
        settle();
        ASSERT_EQ(v.size(), 100u);
        EXPECT_EQ(v[57].value, 57);
    }
    EXPECT_TRUE(collect_until([] { return self_adopting_alive.load() == 0; }));
}

namespace {
    struct alignas(16) Aligned16 {
        double value = 1;
        tracked_ptr<Aligned16> next;
    };
}

// SPEC §8: up to 16-byte alignment, for objects and vector elements alike.
TEST(Lifecycle_Tests, SixteenByteAlignedTypes) {
    vector<tracked_ptr<Aligned16>> objects;
    for (int i = 0; i < 5000; ++i) {
        auto p = make_tracked<Aligned16>();
        EXPECT_EQ((uintptr_t)p.get() % 16, 0u);
        objects.push_back(std::move(p));
    }
    vector<Aligned16> elements;
    for (int i = 0; i < 5000; ++i) {
        elements.emplace_back();
        EXPECT_EQ((uintptr_t)&elements.back() % 16, 0u);
    }
    settle();
    EXPECT_EQ(elements[4999].value, 1);
}

#if !defined(_WIN32)
namespace {
    struct DeepTag;
    using DeepProbe = Probe<DeepTag>;

    // Holds one tracked_ptr per frame; past MaxStackSize they are external roots, still keeping their referents.
    long hold_deep(long depth, std::atomic<bool>& collected) {
        tracked_ptr<DeepProbe> here = make_tracked<DeepProbe>(1);
        char pad[1024];
        std::memset(pad, (int)depth, sizeof(pad));
        if (depth == 0) {
            collector::force_collect(true);
            collector::force_collect(true);
            collected = true;
            return DeepProbe::alive();
        }
        long alive = hold_deep(depth - 1, collected);
        return alive + (here->value == 1 ? 0 : 1000000) + pad[depth % sizeof(pad)] * 0;
    }

    void* run_deep(void* arg) {
        auto* result = static_cast<long*>(arg);
        std::atomic<bool> collected = false;
        *result = hold_deep(12000, collected);
        return nullptr;
    }
}

// SPEC §4.3: stack roots beyond the shadow stack's depth are external roots, so nothing they reference is collected.
TEST(Lifecycle_Tests, TrackedPtrsDeeperThanTheShadowStack) {
    ASSERT_TRUE(collect_until([] { return DeepProbe::alive() == 0; }));
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024 * 1024);
    pthread_t thread;
    long alive_at_bottom = 0;
    ASSERT_EQ(pthread_create(&thread, &attr, run_deep, &alive_at_bottom), 0);
    pthread_join(thread, nullptr);
    pthread_attr_destroy(&attr);
    EXPECT_EQ(alive_at_bottom, 12001);
    EXPECT_TRUE(collect_until([] { return DeepProbe::alive() == 0; }));
}
#endif

TEST(Lifecycle_Tests, ResizeValueInitializesScalars) {
    vector<long> v{1, 2, 3, 4, 5, 6, 7, 8};
    v.clear();
    v.resize(8);
    for (auto x : v) {
        EXPECT_EQ(x, 0);
    }
}

namespace {
    class LifecycleDeathTest : public ::testing::Test {
    protected:
        void SetUp() override {
            GTEST_FLAG_SET(death_test_style, "threadsafe");
        }
    };
}

// terminate() returns although detached threads keep producing garbage.
TEST_F(LifecycleDeathTest, TerminateIsBoundedUnderSteadyAllocation) {
    EXPECT_EXIT({
        for (int t = 0; t < 4; ++t) {
            std::thread([] {
                for (;;) {
                    tracked_ptr<OwnedProbe> p = make_tracked<OwnedProbe>(1);
                }
            }).detach();
        }
        std::this_thread::sleep_for(100ms);
        auto start = std::chrono::steady_clock::now();
        collector::terminate();
        auto elapsed = std::chrono::steady_clock::now() - start;
        std::_Exit(elapsed < 20s ? 0 : 1);
    }, ::testing::ExitedWithCode(0), "");
}

// terminate() before any collector exists: none ever runs.
TEST_F(LifecycleDeathTest, TerminateBeforeFirstUseNeverCollects) {
    EXPECT_EXIT({
        // The re-executed child has not used SGCL yet when this statement starts only if nothing ran before it;
        // the flag is set either way, and a running collector stops here too.
        collector::terminate();
        { tracked_ptr<OwnedProbe> p = make_tracked<OwnedProbe>(1); }
        bool collected = collector::force_collect(true);
        std::_Exit(!collected && OwnedProbe::alive() == 1 ? 0 : 1);
    }, ::testing::ExitedWithCode(0), "");
}
