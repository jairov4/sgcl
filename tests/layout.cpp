//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <functional>
#include <utility>
#include <variant>

// SPEC §3.4, §4.4: a recorded child offset may hold a cell in one object and foreign data in another.

namespace {
    struct FunctionTag;
    using Captured = Probe<FunctionTag>;
}

TEST(Layout_Tests, FunctionSmallBufferHoldsCellOrForeignData) {
    ASSERT_TRUE(collect_until([] { return Captured::alive() == 0; }));
    {
        sgcl::vector<std::function<long()>> fs;
        {
            tracked_ptr<Captured> x = make_tracked<Captured>(42);
            fs.push_back([x] { return long(x->value); });
        }
        for (long i = 0; i < 2000; ++i) {
            if (i % 100 == 0) {
                tracked_ptr<Captured> y = make_tracked<Captured>(int(i));
                fs.push_back([y] { return long(y->value); });
            } else {
                long a = 0x123456789 + i, b = 0x55555;
                fs.push_back([a, b] { return a + b; });
            }
        }
        for (int i = 0; i < 5; ++i) {
            collector::force_collect(true);
        }
        EXPECT_EQ(Captured::alive(), 21);
        EXPECT_EQ(fs[0](), 42);
        for (long i = 0; i < 2000; ++i) {
            ASSERT_EQ(fs[i + 1](), i % 100 == 0 ? i : 0x123456789 + i + 0x55555) << i;
        }
        // Dropping half the elements runs their destructors on this thread; the replaced storages die on the
        // collector thread.
        fs.resize(1000);
        EXPECT_TRUE(collect_until([] { return Captured::alive() == 11; }));
        EXPECT_EQ(fs[0](), 42);
        EXPECT_EQ(fs[999](), 0x123456789 + 998 + 0x55555);
    }
    EXPECT_TRUE(collect_until([] { return Captured::alive() == 0; }));
}

namespace {
    struct VariantTag;
    using Alternative = Probe<VariantTag>;

    struct VariantHolder {
        std::variant<tracked_ptr<Alternative>, std::pair<long, long>> v;

        explicit VariantHolder(tracked_ptr<Alternative> p)
        : v(std::move(p)) {
        }

        VariantHolder(long a, long b)
        : v(std::pair<long, long>(a, b)) {
        }
    };
}

TEST(Layout_Tests, VariantHoldsCellOrForeignData) {
    ASSERT_TRUE(collect_until([] { return Alternative::alive() == 0; }));
    {
        sgcl::vector<tracked_ptr<VariantHolder>> holders;
        holders.push_back(make_tracked<VariantHolder>(tracked_ptr<Alternative>(make_tracked<Alternative>(-1))));
        for (long i = 0; i < 2000; ++i) {
            if (i % 50 == 0) {
                holders.push_back(make_tracked<VariantHolder>(tracked_ptr<Alternative>(make_tracked<Alternative>(int(i)))));
            } else {
                holders.push_back(make_tracked<VariantHolder>(0x123456789 + i, 0x55555));
            }
        }
        // Switching alternatives after construction.
        holders[2]->v = tracked_ptr<Alternative>(make_tracked<Alternative>(-2));
        holders[1]->v = std::pair<long, long>(7, 8);
        for (int i = 0; i < 5; ++i) {
            collector::force_collect(true);
        }
        EXPECT_EQ(Alternative::alive(), 41);
        EXPECT_EQ(std::get<0>(holders[0]->v)->value, -1);
        EXPECT_EQ(std::get<1>(holders[1]->v), (std::pair<long, long>(7, 8)));
        EXPECT_EQ(std::get<0>(holders[2]->v)->value, -2);
        for (long i = 2; i < 2000; ++i) {
            auto& v = holders[i + 1]->v;
            if (i % 50 == 0) {
                ASSERT_EQ(std::get<0>(v)->value, i) << i;
            } else {
                ASSERT_EQ(std::get<1>(v), (std::pair<long, long>(0x123456789 + i, 0x55555))) << i;
            }
        }
        holders.resize(1000);
        EXPECT_TRUE(collect_until([] { return Alternative::alive() == 21; }));
        EXPECT_EQ(std::get<0>(holders[0]->v)->value, -1);
    }
    EXPECT_TRUE(collect_until([] { return Alternative::alive() == 0; }));
}

// A foreign word that happens to point into a live page is tolerated (conservative retention, never a crash).
TEST(Layout_Tests, ForeignWordPointingIntoAPage) {
    ASSERT_TRUE(collect_until([] { return Alternative::alive() == 0; }));
    {
        tracked_ptr<Alternative> target = make_tracked<Alternative>(5);
        sgcl::vector<tracked_ptr<VariantHolder>> holders;
        for (long i = 0; i < 100; ++i) {
            // Interior and exact addresses of a live object, and the address just past it.
            long address = (long)target.get() + (i % 3) * long(sizeof(int));
            holders.push_back(make_tracked<VariantHolder>(address, i));
        }
        for (int i = 0; i < 3; ++i) {
            collector::force_collect(true);
        }
        EXPECT_EQ(target->value, 5);
        for (long i = 0; i < 100; ++i) {
            ASSERT_EQ(std::get<1>(holders[i]->v).second, i);
        }
        target.reset();
        for (int i = 0; i < 3; ++i) {
            collector::force_collect(true);
        }
    }
    EXPECT_TRUE(collect_until([] { return Alternative::alive() == 0; }));
}
