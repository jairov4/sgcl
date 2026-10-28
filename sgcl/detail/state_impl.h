//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// Defines the process-wide state declared in state.h. Include it from exactly one translation unit of the image that
// owns SGCL's state, with SGCL_EXTERNAL_STATE defined and SGCL_API exporting.
#if !defined(SGCL_EXTERNAL_STATE)
#error "sgcl/detail/state_impl.h requires SGCL_EXTERNAL_STATE"
#endif

#include "../sgcl.h"

namespace sgcl::detail {
    constinit ProcessState process_state_instance;

    ProcessState& process_state() noexcept {
        return process_state_instance;
    }

    // Immortal: only an explicit sgcl::collector::terminate() stops it, never static destruction, which would run
    // under the Windows loader lock after other threads are gone.
    Collector& collector_instance() {
        static Collector* const collector = new Collector;
        return *collector;
    }

    // Unlike the header-only mode, no thread's exit terminates the collector.
    Thread& current_thread_slow() noexcept {
        static thread_local Thread instance;
        return instance;
    }
}
