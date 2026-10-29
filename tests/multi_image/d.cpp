//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
// A library nothing links: the executable loads it from a worker thread, as a host loads a plugin.
#include "core.h"

namespace {
    struct DNode : mi::Node {
        using Node::Node;
    };
}

// Builds a chain of `count` nodes, drops it and returns the sum of their payloads.
extern "C" MI_EXPORT int mi_d_churn(int count) {
    sgcl::tracked_ptr<mi::Node> head;
    for (int i = 0; i < count; ++i) {
        sgcl::tracked_ptr<mi::Node> node = sgcl::make_tracked<DNode>(i);
        node->next = head;
        head = node;
    }
    int sum = 0;
    for (auto node = head; node; node = node->next) {
        sum += node->payload;
    }
    return sum;
}
