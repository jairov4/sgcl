//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "sgcl/sgcl.h"

#include <gtest/gtest.h>

using namespace sgcl;

// The first constructions of both types happen during static initialization, the inner one nested in the outer.
struct StaticInitInner {
    int value = 42;
};

struct StaticInitOuter {
    int value;
    tracked_ptr<StaticInitInner> ptr;

    StaticInitOuter() {
        value = 7;
        ptr = make_tracked<StaticInitInner>();
    }
};

// Defined in another translation unit, so the per-type statics are first used there.
tracked_ptr<StaticInitOuter> make_static_init_outer();
