//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"

#include <atomic>
#include <cstddef>

namespace sgcl::detail {
    struct DataPage;
    struct ThreadData;

    // Every mutable value SGCL shares between threads. There is one per process under SGCL_EXTERNAL_STATE, else one
    // per image. Constant-initialized and trivially destructible, so it is usable before and after static
    // initialization and destruction of any image.
    struct ProcessState {
        std::atomic<ThreadData*> threads_data = {nullptr};
        std::atomic<unsigned> type_counter = {0};
        std::atomic<DataPage*> empty_pages = {nullptr};
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

#if defined(SGCL_EXTERNAL_STATE)
    SGCL_API ProcessState& SGCL_STATE_SYMBOL(process_state)() noexcept;

    inline ProcessState& process_state() noexcept {
        return SGCL_STATE_SYMBOL(process_state)();
    }
#else
    inline ProcessState& process_state() noexcept {
        static constinit ProcessState state;
        return state;
    }
#endif
}
