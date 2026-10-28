//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "counter.h"
#include "state.h"

#include <atomic>

namespace sgcl::detail {
    class MemoryCounters {
    public:
        inline static void update_alloc(int64_t count, int64_t size) noexcept {
            auto& state = process_state();
            state.alloc_count.fetch_add(count, std::memory_order_relaxed);
            state.alloc_size.fetch_add(size, std::memory_order_relaxed);
            state.live_count.fetch_add(count, std::memory_order_relaxed);
            state.live_size.fetch_add(size, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
        }
        inline static void update_free(size_t count, int64_t size) noexcept {
            auto& state = process_state();
            state.free_count.fetch_add(count, std::memory_order_relaxed);
            state.free_size.fetch_add(size, std::memory_order_relaxed);
            state.live_count.fetch_sub(count, std::memory_order_relaxed);
            state.live_size.fetch_sub(size, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
        }
        inline static Counter alloc_counter() noexcept {
            auto& state = process_state();
            std::atomic_thread_fence(std::memory_order_acquire);
            return Counter(state.alloc_count.load(std::memory_order_relaxed), state.alloc_size.load(std::memory_order_relaxed));
        }
        inline static Counter last_alloc_counter() noexcept {
            auto& state = process_state();
            std::atomic_thread_fence(std::memory_order_acquire);
            return Counter(state.last_alloc_count.load(std::memory_order_relaxed), state.last_alloc_size.load(std::memory_order_relaxed));
        }
        inline static Counter free_counter() noexcept {
            auto& state = process_state();
            std::atomic_thread_fence(std::memory_order_acquire);
            return Counter(state.free_count.load(std::memory_order_relaxed), state.free_size.load(std::memory_order_relaxed));
        }
        inline static Counter live_counter() noexcept {
            auto& state = process_state();
            std::atomic_thread_fence(std::memory_order_acquire);
            return Counter(state.live_count.load(std::memory_order_relaxed), state.live_size.load(std::memory_order_relaxed));
        }
        inline static size_t live_count() noexcept {
            return process_state().live_count.load(std::memory_order_acquire);
        }
        inline static size_t alloc_count() noexcept {
            return process_state().alloc_count.load(std::memory_order_acquire);
        }
        inline static size_t last_alloc_count() noexcept {
            return process_state().last_alloc_count.load(std::memory_order_acquire);
        }
        inline static size_t free_count() noexcept {
            return process_state().free_count.load(std::memory_order_acquire);
        }
        inline static void reset_alloc() noexcept {
            auto& state = process_state();
            state.alloc_count.store(0, std::memory_order_relaxed);
            state.alloc_size.store(0, std::memory_order_relaxed);
        }
        inline static void reset_free() noexcept {
            auto& state = process_state();
            state.free_count.store(0, std::memory_order_relaxed);
            state.free_size.store(0, std::memory_order_relaxed);
        }
        inline static void reset_all() noexcept {
            auto& state = process_state();
            state.last_alloc_count.store(state.alloc_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            state.last_alloc_size.store(state.alloc_size.load(std::memory_order_relaxed), std::memory_order_relaxed);
            reset_alloc();
            reset_free();
        }
    };
}
