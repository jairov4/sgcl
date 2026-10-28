//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
// The state definitions the test executables own when built with SGCL_TEST_EXTERNAL_STATE.
#if defined(SGCL_EXTERNAL_STATE)
#include "sgcl/detail/state_impl.h"

#include <gtest/gtest.h>

namespace {
    // Nothing stops the collector implicitly in this mode: stop it once every test has run, before static destruction.
    class TerminateCollector : public ::testing::Environment {
    public:
        void TearDown() override {
            sgcl::collector::terminate();
        }
    };

    [[maybe_unused]] auto* const terminate_collector = ::testing::AddGlobalTestEnvironment(new TerminateCollector);
}
#endif
