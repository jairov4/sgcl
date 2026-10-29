//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "sgcl/state_impl.h"

#include <gtest/gtest.h>

namespace {
    // Nothing stops the collector implicitly: stop it once every test has run, before static destruction.
    class TerminateCollector : public ::testing::Environment {
    public:
        void TearDown() override {
            sgcl::collector::terminate();
        }
    };

    [[maybe_unused]] auto* const terminate_collector = ::testing::AddGlobalTestEnvironment(new TerminateCollector);
}
