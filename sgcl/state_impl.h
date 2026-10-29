//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// Defines the process state (SPEC.md §3.8, §4.9). Include from exactly one translation unit of the owning image,
// with SGCL_API exporting.

#include "sgcl.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace sgcl::detail {
    // The highest address of the calling thread's stack, or 0 when the platform does not report it.
    inline uintptr_t current_stack_base() noexcept {
#if defined(_WIN32)
        return (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase;
#elif defined(__APPLE__)
        return (uintptr_t)pthread_get_stackaddr_np(pthread_self());
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) {
            return 0;
        }
        void* addr = nullptr;
        size_t size = 0;
        int rc = pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        return rc == 0 ? (uintptr_t)addr + size : 0;
#endif
    }

    constinit ProcessState process_state_instance;

    ProcessState& SGCL_STATE_SYMBOL(process_state)() noexcept {
        return process_state_instance;
    }

    // Immortal: only collector::terminate() stops it, never static destruction, which would run under the Windows
    // loader lock after other threads are gone.
    Collector& SGCL_STATE_SYMBOL(collector_instance)() noexcept {
        static Collector* const collector = checked_new<Collector>();
        return *collector;
    }

    // No thread's exit terminates the collector.
    Thread& SGCL_STATE_SYMBOL(current_thread)() noexcept {
        static thread_local Thread instance(current_stack_base());
        // Constructed after the record, so destroyed before it.
        static thread_local ThreadRetirer retirer{instance};
        return instance;
    }
}
