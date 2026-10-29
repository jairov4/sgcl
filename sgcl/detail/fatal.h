//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include <cstdio>
#include <cstdlib>
#include <new>

namespace sgcl::detail {
    // Out of memory and detected misuse end the process (SPEC.md §5.7, §5.8); nothing is thrown.
    [[noreturn]] inline void fatal(const char* reason) noexcept {
        std::fprintf(stderr, "sgcl: fatal: %s\n", reason);
        std::fflush(stderr);
        std::abort();
    }

    [[noreturn]] inline void fatal_out_of_memory() noexcept {
        fatal("out of memory");
    }

    inline void* checked_malloc(size_t size) noexcept {
        void* p = std::malloc(size);
        if (!p) {
            fatal_out_of_memory();
        }
        return p;
    }

    inline void* checked_operator_new(size_t size) noexcept {
        void* p = ::operator new(size, std::nothrow);
        if (!p) {
            fatal_out_of_memory();
        }
        return p;
    }

    inline void* checked_operator_new(size_t size, std::align_val_t align) noexcept {
        void* p = ::operator new(size, align, std::nothrow);
        if (!p) {
            fatal_out_of_memory();
        }
        return p;
    }

    // `new T(args...)` for SGCL's own bookkeeping, whose constructors only allocate.
    template<class T, class... A>
    T* checked_new(A&&... a) noexcept {
        try {
            return new T(static_cast<A&&>(a)...);
        } catch (...) {
            fatal_out_of_memory();
        }
    }

    // Runs bookkeeping that may only throw std::bad_alloc or std::system_error (containers, std::thread).
    template<class F>
    void or_fatal(F&& f) noexcept {
        try {
            f();
        } catch (...) {
            fatal("out of memory or system resources");
        }
    }
}
