//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#define MI_BUILDING_a
#include "a.h"

#include <thread>

namespace mi::a {
    namespace {
        struct ANode : Node {
            using Node::Node;
            int a_tag = 0xaa;
        };
    }

    Base::Base(int payload)
    : Node(payload) {
    }

    sgcl::unique_ptr<Node> make_node(int payload) {
        return sgcl::make_tracked<ANode>(payload);
    }

    sgcl::unique_ptr<Node> make_plain(int payload) {
        return sgcl::make_tracked<Node>(payload);
    }

    void link(Node& from, const sgcl::tracked_ptr<Node>& to) {
        from.next = to;
    }

    const void* state_address() noexcept {
        return &sgcl::detail::process_state();
    }

    const void* collector_address() noexcept {
        return &sgcl::detail::collector_instance();
    }

    const void* thread_address() noexcept {
        return &sgcl::detail::current_thread();
    }

    void churn_in_own_thread(int count) {
        std::thread([count] {
            sgcl::tracked_ptr<Node> head;
            for (int i = 0; i < count; ++i) {
                sgcl::tracked_ptr<Node> node = make_node(i);
                node->next = head;
                head = node;
            }
        }).join();
    }
}
