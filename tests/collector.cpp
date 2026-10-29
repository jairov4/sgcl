//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <algorithm>

// SPEC §3.7, §4.10, §5.5.

using namespace std::chrono_literals;

namespace {
    struct CollectTag;
    using Garbage = Probe<CollectTag>;
}

TEST(Collector_Tests, ForceCollectWaitReclaimsEverythingUnreachableAtTheCall) {
    ASSERT_TRUE(collect_until([] { return Garbage::alive() == 0; }));
    for (int round = 0; round < 3; ++round) {
        {
            sgcl::vector<tracked_ptr<Garbage>> items;
            for (int i = 0; i < 1000; ++i) {
                items.push_back(make_tracked<Garbage>(i));
            }
        }
        EXPECT_TRUE(collector::force_collect(true));
        EXPECT_EQ(Garbage::alive(), 0);
    }
    EXPECT_TRUE(collector::force_collect(false));
}

TEST(Collector_Tests, ConcurrentWaitersAllReturn) {
    run_with_timeout([] {
        std::vector<std::thread> threads;
        std::atomic<int> returned = 0;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&] {
                for (int i = 0; i < 5; ++i) {
                    tracked_ptr<Garbage> g = make_tracked<Garbage>(i);
                    if (collector::force_collect(true)) {
                        ++returned;
                    }
                }
            });
        }
        for (auto& t: threads) {
            t.join();
        }
        EXPECT_EQ(returned.load(), 40);
    }, 60s, "concurrent force_collect(true)");
}

TEST(Collector_Tests, ThreadsAllocateConcurrentlyWithCollections) {
    ASSERT_TRUE(collect_until([] { return Garbage::alive() == 0; }));
    run_with_timeout([] {
        std::atomic<bool> stop = false;
        std::thread collector_driver([&] {
            while (!stop.load()) {
                collector::force_collect(true);
            }
        });
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([t] {
                sgcl::vector<tracked_ptr<Garbage>> kept;
                for (int i = 0; i < 20000; ++i) {
                    tracked_ptr<Garbage> g = make_tracked<Garbage>(i);
                    if (i % 100 == 0) {
                        kept.push_back(g);
                    }
                }
                for (size_t i = 0; i < kept.size(); ++i) {
                    EXPECT_EQ(kept[i]->value, int(i * 100)) << t;
                }
            });
        }
        for (auto& t: threads) {
            t.join();
        }
        stop = true;
        collector_driver.join();
    }, 60s, "allocation during collections");
    EXPECT_TRUE(collect_until([] { return Garbage::alive() == 0; }));
}

TEST(Collector_Tests, LiveObjectCountReflectsTheLastCycle) {
    ASSERT_TRUE(collect_until([] { return Garbage::alive() == 0; }));
    settle();
    auto base = collector::get_live_object_count();
    {
        sgcl::vector<tracked_ptr<Garbage>> items;
        for (int i = 0; i < 500; ++i) {
            items.push_back(make_tracked<Garbage>(i));
        }
        collector::force_collect(true);
        EXPECT_GE(collector::get_live_object_count(), base + 500);
    }
    settle();
    EXPECT_LE(collector::get_live_object_count(), base + 10);
}

namespace {
    struct ListedTag;
    using Listed = Probe<ListedTag>;
}

TEST(Collector_Tests, GetLiveObjects) {
    tracked_ptr<Listed> a = make_tracked<Listed>(1);
    tracked_ptr<Listed> b = make_tracked<Listed>(2);
    void* dropped;
    {
        tracked_ptr<Listed> c = make_tracked<Listed>(3);
        dropped = c.get();
    }
    settle();
    {
        auto [guard, objects] = collector::get_live_objects();
        EXPECT_TRUE(guard);
        EXPECT_EQ(std::count(objects.begin(), objects.end(), (void*)a.get()), 1);
        EXPECT_EQ(std::count(objects.begin(), objects.end(), (void*)b.get()), 1);
        EXPECT_EQ(std::count(objects.begin(), objects.end(), dropped), 0);
        // Never blocks, even while the collector is paused.
        run_with_timeout([] { collector::get_live_object_count(); }, 10s, "get_live_object_count while paused");
        EXPECT_GE(collector::get_live_object_count(), objects.size());
        // Allocation and stores keep working while paused.
        tracked_ptr<Listed> d = make_tracked<Listed>(4);
        b = d;
    }
    settle();
    EXPECT_EQ(b->value, 4);
}

// A second snapshot waits until the first guard is released (SPEC §3.7).
TEST(Collector_Tests, GetLiveObjectsIsExclusive) {
    tracked_ptr<Listed> held = make_tracked<Listed>(5);
    void* address = held.get();
    run_with_timeout([address] {
        auto first = std::make_unique<std::tuple<collector::pause_guard, std::vector<void*>>>(collector::get_live_objects());
        ASSERT_TRUE(std::get<0>(*first));
        auto& listed = std::get<1>(*first);
        EXPECT_EQ(std::count(listed.begin(), listed.end(), address), 1);
        std::atomic<bool> second_done = false;
        std::atomic<bool> released = false;
        std::atomic<bool> overlapped = false;
        std::thread other([&] {
            auto [guard, objects] = collector::get_live_objects();
            if (!released.load()) {
                overlapped = true;
            }
            EXPECT_TRUE(guard);
            EXPECT_EQ(std::count(objects.begin(), objects.end(), address), 1);
            second_done = true;
        });
        std::this_thread::sleep_for(300ms);
        EXPECT_FALSE(second_done.load());
        released = true;
        first.reset();
        other.join();
        EXPECT_TRUE(second_done.load());
        EXPECT_FALSE(overlapped.load());
    }, 30s, "concurrent get_live_objects");
}

// A waiting force_collect from another thread completes once the snapshot is released.
TEST(Collector_Tests, ForceCollectWaitsForPauseRelease) {
    run_with_timeout([] {
        auto snapshot = std::make_unique<std::tuple<collector::pause_guard, std::vector<void*>>>(collector::get_live_objects());
        std::atomic<bool> done = false;
        std::thread waiter([&] {
            EXPECT_TRUE(collector::force_collect(true));
            done = true;
        });
        std::this_thread::sleep_for(200ms);
        EXPECT_FALSE(done.load());
        snapshot.reset();
        waiter.join();
        EXPECT_TRUE(done.load());
    }, 30s, "force_collect during a snapshot");
}

namespace {
    struct ThreadTag;
    using FromThread = Probe<ThreadTag>;
}

// Objects outlive the thread that allocated them; that thread's pages keep being collected (SPEC §5.5).
TEST(Collector_Tests, ObjectsOutliveTheirThread) {
    ASSERT_TRUE(collect_until([] { return FromThread::alive() == 0; }));
    auto kept = std::make_unique<tracked_ptr<FromThread>>();
    for (int t = 0; t < 8; ++t) {
        std::thread([&kept, t] {
            for (int i = 0; i < 1000; ++i) {
                tracked_ptr<FromThread> g = make_tracked<FromThread>(t * 1000 + i);
                if (t == 3 && i == 500) {
                    *kept = g;
                }
            }
        }).join();
    }
    EXPECT_TRUE(collect_until([] { return FromThread::alive() == 1; }));
    EXPECT_EQ((*kept)->value, 3500);
    kept.reset();
    EXPECT_TRUE(collect_until([] { return FromThread::alive() == 0; }));
    // A detached thread is fine as well.
    std::atomic<bool> finished = false;
    std::thread([&finished] {
        {
            tracked_ptr<FromThread> g = make_tracked<FromThread>(1);
        }
        finished = true;
    }).detach();
    while (!finished.load()) {
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(collect_until([] { return FromThread::alive() == 0; }));
}
