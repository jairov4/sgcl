//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// Defines the process state (SPEC.md §3.8, §4.9). Include from exactly one translation unit of the owning image,
// with SGCL_API exporting.

#include "sgcl.h"

#include <utility>

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
#if defined(__APPLE__)
#include <mach/mach.h>
extern "C" {
#include <mach/thread_state.h>
}
#elif defined(__linux__)
#include <linux/membarrier.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace sgcl::detail {
    // The calling thread's real stack as [low, high), or {0, 0} when the platform does not report it.
    inline std::pair<uintptr_t, uintptr_t> current_stack_bounds() noexcept {
#if defined(_WIN32)
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery(&region, &region, sizeof(region))) {
            return {0, 0};
        }
        return {(uintptr_t)region.AllocationBase, (uintptr_t)((NT_TIB*)NtCurrentTeb())->StackBase};
#elif defined(__APPLE__)
        auto high = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        return {high - pthread_get_stacksize_np(pthread_self()), high};
#else
        pthread_attr_t attr;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) {
            return {0, 0};
        }
        void* addr = nullptr;
        size_t size = 0;
        int rc = pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        return rc == 0 ? std::pair<uintptr_t, uintptr_t>{(uintptr_t)addr, (uintptr_t)addr + size} : std::pair<uintptr_t, uintptr_t>{0, 0};
#endif
    }

#if defined(__linux__)
    // Changing the protection of a page this process has touched makes the kernel interrupt every CPU running one
    // of its threads (TLB shootdown), which serializes them.
    inline void mprotect_fence() noexcept {
        static std::mutex lock;
        static char* page = [] {
            void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) {
                fatal("mmap failed");
            }
            return (char*)p;
        }();
        std::lock_guard<std::mutex> guard(lock);
        mprotect(page, 4096, PROT_READ | PROT_WRITE);
        *(volatile char*)page = 0;
        mprotect(page, 4096, PROT_READ);
    }
#endif

    void SGCL_STATE_SYMBOL(process_fence)() noexcept {
#if defined(_WIN32)
        FlushProcessWriteBuffers();
#elif defined(__APPLE__)
        // Reading another thread's registers interrupts it, which serializes it.
        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
            fatal("task_threads failed");
        }
        for (mach_msg_type_number_t i = 0; i < count; ++i) {
            uintptr_t sp = 0;
            uintptr_t registers[128];
            size_t size = 128;
            thread_get_register_pointer_values(threads[i], &sp, &size, registers);
            mach_port_deallocate(mach_task_self(), threads[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(thread_act_t));
#elif defined(__linux__)
        static const bool expedited =
            syscall(__NR_membarrier, MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED, 0, 0) == 0;
        if (!expedited || syscall(__NR_membarrier, MEMBARRIER_CMD_PRIVATE_EXPEDITED, 0, 0) != 0) {
            mprotect_fence();
        }
#else
#error "no process-wide memory barrier for this platform"
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
        static thread_local Thread instance = [] {
            auto [low, high] = current_stack_bounds();
            return Thread(low, high);
        }();
        // Constructed after the record, so destroyed before it.
        static thread_local ThreadRetirer retirer{instance};
        return instance;
    }
}
