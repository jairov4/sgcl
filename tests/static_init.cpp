//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "static_init.h"

// Built before main(), and before the collector has otherwise been used (SPEC §4.9).
tracked_ptr<StaticInitOuter> g_static_init_outer = make_static_init_outer();

TEST(StaticInit_Tests, GlobalConstructedDuringStaticInitialization) {
    ASSERT_NE(g_static_init_outer, nullptr);
    EXPECT_EQ(g_static_init_outer->value, 7);
    ASSERT_NE(g_static_init_outer->ptr, nullptr);
    EXPECT_EQ(g_static_init_outer->ptr->value, 42);
    collector::force_collect(true);
    collector::force_collect(true);
    EXPECT_EQ(g_static_init_outer->ptr->value, 42);
}
