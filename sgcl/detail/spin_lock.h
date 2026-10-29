//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <thread>

namespace sgcl::detail {
    // Constant-initialized and trivially destructible, so it can live in ProcessState and in per-type statics.
    // Guards only a few pointer operations.
    class SpinLock {
    public:
        constexpr SpinLock() noexcept = default;

        void lock() noexcept {
            while (_flag.test_and_set(std::memory_order_acquire)) {
                while (_flag.test(std::memory_order_relaxed)) {
                    std::this_thread::yield();
                }
            }
        }

        void unlock() noexcept {
            _flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag _flag;
    };

    class SpinLockGuard {
    public:
        explicit SpinLockGuard(SpinLock& l) noexcept
        : _lock(l) {
            _lock.lock();
        }

        SpinLockGuard(const SpinLockGuard&) = delete;
        SpinLockGuard& operator=(const SpinLockGuard&) = delete;

        ~SpinLockGuard() noexcept {
            _lock.unlock();
        }

    private:
        SpinLock& _lock;
    };
}
