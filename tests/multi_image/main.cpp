//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
// One collector for a process made of several images: `core` owns SGCL's state (SGCL_EXTERNAL_STATE), `a` and `b`
// are shared libraries that allocate their own types and a shared header-only one, and this executable links all
// three; it loads a fourth library, `d`, from a worker thread. Prints one PASS/FAIL line per check and returns the
// number of failures.
#include "a.h"
#include "b.h"

#include <cstdio>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using mi::Node;
using sgcl::tracked_ptr;

namespace {
    int failures = 0;

    void check(bool ok, const char* what) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) {
            ++failures;
        }
    }

    int alive() {
        return mi::created().load() - mi::destroyed().load();
    }

    // Collects until `expected` nodes are alive and every destroyed node was finalized exactly once, or gives up.
    bool collect_until_alive(int expected) {
        for (int i = 0; i < 100; ++i) {
            sgcl::collector::force_collect(true);
            if (alive() == expected && mi::finalized().load() == mi::destroyed().load()) {
                return mi::refinalized().load() == 0;
            }
        }
        std::printf("  created %d, finalized %d, refinalized %d, destroyed %d\n", mi::created().load(),
                    mi::finalized().load(), mi::refinalized().load(), mi::destroyed().load());
        return false;
    }

    // Loads `d` on a worker thread, runs it there and lets the thread exit; returns what d's churn returned, or -1.
    int churn_in_library_loaded_by_worker(int count) {
        int result = -1;
        std::thread([&result, count] {
#if defined(_WIN32)
            HMODULE library = LoadLibraryA("d.dll");
            auto churn = library ? reinterpret_cast<int (*)(int)>(GetProcAddress(library, "mi_d_churn")) : nullptr;
#else
#if defined(__APPLE__)
            void* library = dlopen("@rpath/libd.dylib", RTLD_NOW | RTLD_GLOBAL);
#else
            void* library = dlopen("libd.so", RTLD_NOW | RTLD_GLOBAL);
#endif
            auto churn = library ? reinterpret_cast<int (*)(int)>(dlsym(library, "mi_d_churn")) : nullptr;
#endif
            if (churn) {
                result = churn(count);
            }
        }).join();
        return result;
    }

    // A ring through every image: exe -> a -> b -> a's plain Node -> b's pair (b -> a) -> exe.
    tracked_ptr<Node> make_ring(int base) {
        tracked_ptr<Node> head = sgcl::make_tracked<Node>(base);
        tracked_ptr<Node> n1 = mi::a::make_node(base + 1);
        tracked_ptr<Node> n2 = mi::b::make_node(base + 2);
        tracked_ptr<Node> n3 = mi::a::make_plain(base + 3);
        tracked_ptr<Node> n4 = mi::b::make_pair(base + 4);
        head->next = n1;
        mi::a::link(*n1, n2);
        mi::b::link(*n2, n3);
        n3->next = n4;
        mi::b::link(*n4->next, head);
        return head;
    }

    constexpr int RingSize = 6;

    int ring_sum(const tracked_ptr<Node>& head) {
        int sum = 0;
        auto node = head;
        for (int i = 0; i < RingSize; ++i) {
            sum += node->payload;
            node = node->next;
        }
        return node == head ? sum : -1;
    }

    void churn(int seed, int rounds, bool& kept_intact) {
        tracked_ptr<Node> keep = make_ring(seed);
        const int expected = ring_sum(keep);
        for (int round = 0; round < rounds; ++round) {
            tracked_ptr<Node> chain;
            for (int i = 0; i < 16; ++i) {
                tracked_ptr<Node> node;
                switch (i % 3) {
                case 0: node = mi::a::make_node(i); break;
                case 1: node = mi::b::make_plain(i); break;
                default: node = sgcl::make_tracked<Node>(i); break;
                }
                node->next = chain;
                chain = node;
            }
            tracked_ptr<Node> ring = make_ring(round);
            mi::a::link(*ring, chain);
            if (round % 64 == 0) {
                sgcl::collector::force_collect();
            }
        }
        kept_intact = ring_sum(keep) == expected;
    }
}

int main() {
    const void* state = &sgcl::detail::process_state();
    check(mi::a::state_address() == state && mi::b::state_address() == state, "every image reaches one process state");
    const void* collector = &sgcl::detail::collector_instance();
    check(mi::a::collector_address() == collector && mi::b::collector_address() == collector,
          "every image reaches one collector");
    const void* thread = &sgcl::detail::current_thread();
    check(mi::a::thread_address() == thread && mi::b::thread_address() == thread,
          "every image reaches the main thread's one Thread");
    bool worker_agrees = false;
    std::thread([&worker_agrees] {
        const void* own = &sgcl::detail::current_thread();
        worker_agrees = mi::a::thread_address() == own && mi::b::thread_address() == own;
    }).join();
    check(worker_agrees, "every image reaches a worker thread's one Thread");

    {
        tracked_ptr<Node> kept = make_ring(100);
        {
            tracked_ptr<Node> dropped = make_ring(200);
        }
        check(collect_until_alive(RingSize), "a dropped ring through every image is finalized and destroyed");
        check(ring_sum(kept) == 6 * 100 + 0 + 1 + 2 + 3 + 4 + 5, "a rooted ring through every image survives");
    }
    check(collect_until_alive(0), "the rooted ring is collected once dropped");

    std::vector<std::thread> threads;
    bool intact[4] = {};
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(churn, 1000 * (t + 1), 2000, std::ref(intact[t]));
    }
    for (auto& t : threads) {
        t.join();
    }
    check(intact[0] && intact[1] && intact[2] && intact[3], "rooted rings survive concurrent churn on four threads");
    mi::a::churn_in_own_thread(1000);
    mi::b::churn_in_own_thread(1000);
    check(collect_until_alive(0), "every node the threads made is finalized once and destroyed once");

    check(churn_in_library_loaded_by_worker(100) == 99 * 100 / 2, "a library loaded by a worker thread allocates");
    check(!sgcl::detail::Collector::terminated() && sgcl::collector::force_collect(true),
          "the loading thread's exit leaves the collector running");
    check(collect_until_alive(0), "that library's nodes are finalized once and destroyed once");
    sgcl::collector::terminate();
    check(sgcl::detail::Collector::terminated(), "an explicit terminate stops the collector");

    std::printf("%s\n", failures ? "FAIL: multi-image collector" : "PASS: one collector serves every image");
    return failures;
}
