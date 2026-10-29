//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "static_init.h"

tracked_ptr<StaticInitOuter> make_static_init_outer() {
    return make_tracked<StaticInitOuter>();
}
