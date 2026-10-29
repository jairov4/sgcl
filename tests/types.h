//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "sgcl/sgcl.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <thread>
#include <vector>

using namespace sgcl;

struct Bar {
    virtual ~Bar() = default;
    virtual int get_value() const { return 0; };
    virtual void set_value(int) {};
};

struct Baz : Bar {
    int value;

    Baz() {}

    Baz(int val)
    : value(val) {
    }

    ~Baz() {
        value = 0;
    }

    int get_value() const override {
        return value;
    }

    void set_value(int val) override {
        value = val;
    }
};

struct Faz {
    int value;

    virtual ~Faz() {
        value = 0;
    }

    void set_value(int val) {
        value = val;
    }
};

struct Far {
    int value;

    virtual ~Far() {
        value = 0;
    }

    void set_value(int val) {
        value = val;
    }
};

struct Foo : Far, Bar, Faz {
    int value;
    tracked_ptr<Baz> ptr;

    Foo() {
    }

    Foo(int val) {
        Foo::set_value(val);
    }

    ~Foo() {
        value = 0;
        ptr = nullptr;
    }

    int get_value() const override {
        return value;
    }

    void set_value(int val) override {
        value = val;
        ptr = make_tracked<Baz>(val);
        Far::set_value(val + 1);
        Faz::set_value(val + 2);
    }
};

// Counts live instances; destructors of discarded vector storage run on the collector thread.
struct Int {
    Int() noexcept
    : _value(0) {
        ++counter;
    }

    Int(int value) noexcept
    : _value(value) {
        ++counter;
    }

    Int(const Int& other) noexcept
    : _value(other._value) {
        ++counter;
    }

    ~Int() {
        --counter;
    }

    Int& operator=(const Int& other) noexcept {
        _value = other._value;
        return *this;
    }

    operator int() const noexcept {
        return _value;
    }

    bool operator==(int value) const noexcept {
        return _value == value;
    }

    static size_t live() noexcept {
        return counter.load();
    }

    inline static std::atomic<size_t> counter = 0;

private:
    int _value;
};

// A referent with its own live counter per Tag, so tests do not observe each other's garbage.
template<class Tag>
struct Probe {
    explicit Probe(int v = 0) noexcept
    : value(v) {
        ++constructed;
    }

    ~Probe() {
        ++destroyed;
    }

    static long alive() noexcept {
        return constructed.load() - destroyed.load();
    }

    int value;
    inline static std::atomic<long> constructed = 0;
    inline static std::atomic<long> destroyed = 0;
};

struct Boom {};

// Collects until `done()` holds, at most `cycles` forced collections; returns `done()`.
inline bool collect_until(const std::function<bool()>& done, int cycles = 20) {
    for (int i = 0; i < cycles && !done(); ++i) {
        collector::force_collect(true);
    }
    return done();
}

// Two full cycles: everything unreachable before the call and not kept by a finalizer is gone.
inline void settle() {
    collector::force_collect(true);
    collector::force_collect(true);
}

// Runs `f` on another thread; a timeout fails the test and ends the process instead of hanging it.
inline void run_with_timeout(std::function<void()> f, std::chrono::milliseconds timeout, const char* what) {
    std::promise<void> done;
    auto finished = done.get_future();
    std::thread worker([&] {
        f();
        done.set_value();
    });
    if (finished.wait_for(timeout) != std::future_status::ready) {
        ADD_FAILURE() << what << " did not finish within " << timeout.count() << " ms";
        std::fflush(stdout);
        std::_Exit(1);
    }
    worker.join();
}
