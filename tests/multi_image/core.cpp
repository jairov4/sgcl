//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#define MI_BUILDING_core
#include "core.h"

#include "sgcl/state_impl.h"

namespace mi {
    std::atomic<int>& created() noexcept {
        static std::atomic<int> count = {0};
        return count;
    }

    std::atomic<int>& finalized() noexcept {
        static std::atomic<int> count = {0};
        return count;
    }

    std::atomic<int>& destroyed() noexcept {
        static std::atomic<int> count = {0};
        return count;
    }

    std::atomic<int>& refinalized() noexcept {
        static std::atomic<int> count = {0};
        return count;
    }
}
