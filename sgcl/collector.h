//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "detail/collector.h"

namespace sgcl {
    // SPEC.md §3.7.
    class collector {
    public:
        using pause_guard = detail::Collector::PauseGuard;

        static bool force_collect(bool wait = false) noexcept {
            return detail::collector_instance().force_collect(wait);
        }

        static size_t get_live_object_count() noexcept {
            return detail::Collector::created() ? detail::collector_instance().live_object_count() : 0;
        }

        static std::tuple<pause_guard, std::vector<void*>> get_live_objects() noexcept {
            return detail::collector_instance().get_live_objects();
        }

        static void terminate() noexcept {
            detail::Collector::terminate();
        }
    };
}
