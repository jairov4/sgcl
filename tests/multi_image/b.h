//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "a.h"

#if defined(MI_BUILDING_b)
#define MI_API_b MI_EXPORT
#else
#define MI_API_b MI_IMPORT
#endif

namespace mi::b {
    // A BNode whose `next` is an ANode made by image a: an edge between two libraries.
    MI_API_b sgcl::unique_ptr<Node> make_pair(int payload);
    // A subclass of a::Base allocated by this image: a's code constructs its base part.
    MI_API_b sgcl::unique_ptr<a::Base> make_derived(int payload);
    // A BNode, a type only this image allocates.
    MI_API_b sgcl::unique_ptr<Node> make_node(int payload);
    // A plain Node allocated by this image's own allocator for it.
    MI_API_b sgcl::unique_ptr<Node> make_plain(int payload);
    // Stores `to` into `from.next` from this image's code.
    MI_API_b void link(Node& from, const sgcl::tracked_ptr<Node>& to);
    MI_API_b const void* state_address() noexcept;
    MI_API_b const void* collector_address() noexcept;
    MI_API_b const void* thread_address() noexcept;
    // Starts a std::thread inside this image that builds and drops a chain of `count` nodes, then joins it.
    MI_API_b void churn_in_own_thread(int count);
}
