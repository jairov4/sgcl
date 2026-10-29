//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"
#include "page_registry.h"
#include "spin_lock.h"

#include <atomic>
#include <cstddef>

namespace sgcl::detail {
    struct DataPage;
    struct ThreadData;

    // Every mutable value SGCL shares between threads, one per process (SPEC.md §4.9). Constant-initialized and
    // trivially destructible, so it is usable before and after static initialization and destruction of any image.
    struct ProcessState {
        std::atomic<ThreadData*> threads_data = {nullptr};
        std::atomic<unsigned> type_counter = {0};
        // Free data pages; pops and pushes hold empty_pages_lock (a CAS pop is ABA-prone).
        DataPage* empty_pages = {nullptr};
        SpinLock empty_pages_lock;
        // The cell of every moved-from external-root tracked_ptr: always null, never written non-null.
        std::atomic<void*> null_cell = {nullptr};
        PageRegistry pages;
        std::atomic<bool> collector_created = {false};
        std::atomic<bool> collector_terminating = {false};
        std::atomic<size_t> live_count = {0};
        std::atomic<size_t> live_size = {0};
        std::atomic<size_t> alloc_count = {0};
        std::atomic<size_t> alloc_size = {0};
        std::atomic<size_t> last_alloc_count = {0};
        std::atomic<size_t> last_alloc_size = {0};
        std::atomic<size_t> free_count = {0};
        std::atomic<size_t> free_size = {0};
    };

    SGCL_API ProcessState& SGCL_STATE_SYMBOL(process_state)() noexcept;

    inline ProcessState& process_state() noexcept {
        return SGCL_STATE_SYMBOL(process_state)();
    }
}
