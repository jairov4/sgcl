//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

// SPEC §4.10, §5.7, §5.8. Each statement runs in a re-executed child process; the collector thread rules out fork.

using namespace std::chrono_literals;

namespace {
    class FatalDeathTest : public ::testing::Test {
    protected:
        void SetUp() override {
            GTEST_FLAG_SET(death_test_style, "threadsafe");
        }
    };

    struct Victim {
        int value = 1;
    };

    struct LiveObjectsFromHook {
        void sgcl_finalize() noexcept {
            collector::get_live_objects();
        }
    };

    struct TerminateFromHook {
        void sgcl_finalize() noexcept {
            collector::terminate();
        }
    };

    // Waits long enough for the collector thread to abort the process.
    void collect_and_wait() {
        for (int i = 0; i < 4; ++i) {
            collector::force_collect(false);
            std::this_thread::sleep_for(50ms);
        }
        collector::force_collect(true);
        std::this_thread::sleep_for(10s);
    }
}

TEST_F(FatalDeathTest, AdoptingUnmanagedMemory) {
    EXPECT_DEATH({
        static Victim outside;
        tracked_ptr<Victim> adopted(&outside);
    }, "sgcl: fatal: .*does not manage");
}

TEST_F(FatalDeathTest, TerminateWhileHoldingPauseGuard) {
    EXPECT_DEATH({
        auto snapshot = collector::get_live_objects();
        collector::terminate();
    }, "sgcl: fatal: .*pause_guard");
}

namespace {
    // Constructed before the thread's first SGCL use, so destroyed after the thread's record retires.
    struct LateUser {
        ~LateUser() {
            tracked_ptr<Victim> late = make_tracked<Victim>();
        }
    };
}

TEST_F(FatalDeathTest, UseAfterThreadRecordRetired) {
    EXPECT_DEATH({
        std::thread([] {
            thread_local LateUser late_user;
            (void)&late_user;
            tracked_ptr<Victim> first = make_tracked<Victim>();
        }).join();
    }, "sgcl: fatal: .*retired");
}

TEST_F(FatalDeathTest, AdoptingADestroyedObject) {
    EXPECT_DEATH({
        auto owned = make_tracked<Victim>();
        Victim* raw = owned.get();
        owned.reset();
        tracked_ptr<Victim> adopted(raw);
    }, "sgcl: fatal: ");
}

TEST_F(FatalDeathTest, GetLiveObjectsFromAFinalizeHook) {
    EXPECT_DEATH({
        {
            tracked_ptr<LiveObjectsFromHook> p = make_tracked<LiveObjectsFromHook>();
        }
        collect_and_wait();
    }, "sgcl: fatal: .*collector thread");
}

TEST_F(FatalDeathTest, TerminateFromAFinalizeHook) {
    EXPECT_DEATH({
        {
            tracked_ptr<TerminateFromHook> p = make_tracked<TerminateFromHook>();
        }
        collect_and_wait();
    }, "sgcl: fatal: .*collector thread");
}

TEST_F(FatalDeathTest, GetLiveObjectsWhileHoldingAPauseGuard) {
    EXPECT_DEATH({
        auto first = collector::get_live_objects();
        auto second = collector::get_live_objects();
    }, "sgcl: fatal: .*pause_guard");
}

namespace {
    // SPEC §4.10: terminate is idempotent; afterwards force_collect returns false at once and allocation still works.
    [[noreturn]] void use_after_terminate() {
        tracked_ptr<Victim> kept = make_tracked<Victim>();
        collector::terminate();
        collector::terminate();
        bool ok = !collector::force_collect(true) && !collector::force_collect(false);
        auto snapshot = collector::get_live_objects();
        ok = ok && !std::get<0>(snapshot);
        for (int i = 0; i < 1000; ++i) {
            tracked_ptr<Victim> v = make_tracked<Victim>();
            ok = ok && v->value == 1;
        }
        ok = ok && kept->value == 1;
        std::fflush(stdout);
        std::_Exit(ok ? 0 : 3);
    }
}

TEST_F(FatalDeathTest, AfterTerminate) {
    EXPECT_EXIT(use_after_terminate(), ::testing::ExitedWithCode(0), "");
}
