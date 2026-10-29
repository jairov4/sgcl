//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

// Every image of the multi-image test includes this header first: it makes `core`, the shared library that defines
// the state, export it while every other image imports it.
#if defined(_WIN32)
#define MI_EXPORT __declspec(dllexport)
#define MI_IMPORT __declspec(dllimport)
#else
#define MI_EXPORT __attribute__((visibility("default")))
#define MI_IMPORT __attribute__((visibility("default")))
#endif

#if defined(MI_BUILDING_core)
#define MI_API_core MI_EXPORT
#else
#define MI_API_core MI_IMPORT
#endif

#define SGCL_API MI_API_core

#include "sgcl/sgcl.h"

#include <atomic>

namespace mi {
    MI_API_core std::atomic<int>& created() noexcept;
    MI_API_core std::atomic<int>& finalized() noexcept;
    MI_API_core std::atomic<int>& destroyed() noexcept;
    // Finalizer runs on a node already finalized; never expected.
    MI_API_core std::atomic<int>& refinalized() noexcept;

    // Header-only. On Windows every image that allocates one has its own allocator and type index for it; ELF and
    // Mach-O may merge those statics across images.
    struct Node {
        explicit Node(int p) noexcept : payload(p) {
            created().fetch_add(1, std::memory_order_relaxed);
        }
        virtual ~Node() {
            destroyed().fetch_add(1, std::memory_order_relaxed);
        }
        void sgcl_finalize() noexcept {
            if (finalized_once) {
                refinalized().fetch_add(1, std::memory_order_relaxed);
            }
            finalized_once = true;
            finalized().fetch_add(1, std::memory_order_relaxed);
        }

        sgcl::tracked_ptr<Node> next;
        int payload;
        bool finalized_once = false;
    };
}
