//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "core.h"

#if defined(MI_BUILDING_a)
#define MI_API_a MI_EXPORT
#else
#define MI_API_a MI_IMPORT
#endif

namespace mi::a {
    // A tracked base class whose constructor runs in this image, so a subclass another image allocates builds this
    // part's tracked_ptr members through this image's code.
    struct Base : Node {
        MI_API_a explicit Base(int payload);

        sgcl::tracked_ptr<Node> extra;
    };

    // An ANode, a type only this image allocates.
    MI_API_a sgcl::unique_ptr<Node> make_node(int payload);
    // A plain Node allocated by this image's own allocator for it.
    MI_API_a sgcl::unique_ptr<Node> make_plain(int payload);
    // Stores `to` into `from.next` from this image's code.
    MI_API_a void link(Node& from, const sgcl::tracked_ptr<Node>& to);
    MI_API_a const void* state_address() noexcept;
    MI_API_a const void* collector_address() noexcept;
    MI_API_a const void* thread_address() noexcept;
    // Starts a std::thread inside this image that builds and drops a chain of `count` nodes, then joins it.
    MI_API_a void churn_in_own_thread(int count);
}
