//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <map>

// SPEC §5.2, §5.3, §6.

namespace {
    // Counted constructions (and, for ThrowingMoveElem, moves) throw Boom once `allow` reaches 0.
    struct Knob {
        inline static int allow = -1;
        inline static std::atomic<long> live = 0;
        // Destructions or uses of an object that is not alive: destroyed twice or never constructed.
        inline static std::atomic<long> errors = 0;

        static void tick() {
            if (allow >= 0 && allow-- == 0) {
                throw Boom{};
            }
        }

        static void arm(int n) {
            allow = n;
        }

        static void disarm() {
            allow = -1;
        }
    };

    template<bool NothrowMove>
    struct BasicElem {
        static constexpr uint64_t Alive = 0xA11CE5A11CE5A11Cull;
        static constexpr int MovedFrom = -1000;

        uint64_t magic;
        int value;

        BasicElem()
        : magic((Knob::tick(), Alive))
        , value(0) {
            ++Knob::live;
        }

        BasicElem(int v)
        : magic((Knob::tick(), Alive))
        , value(v) {
            ++Knob::live;
        }

        BasicElem(const BasicElem& o)
        : magic((Knob::tick(), Alive))
        , value(o.value) {
            o.check();
            ++Knob::live;
        }

        BasicElem(BasicElem&& o) noexcept(NothrowMove)
        : magic((tick_move(), Alive))
        , value(o.value) {
            o.check();
            o.value = MovedFrom;
            ++Knob::live;
        }

        BasicElem& operator=(const BasicElem& o) {
            Knob::tick();
            check();
            o.check();
            value = o.value;
            return *this;
        }

        BasicElem& operator=(BasicElem&& o) noexcept(NothrowMove) {
            tick_move();
            check();
            o.check();
            value = o.value;
            o.value = MovedFrom;
            return *this;
        }

        ~BasicElem() {
            if (magic != Alive) {
                ++Knob::errors;
            } else {
                --Knob::live;
            }
            magic = 0;
        }

        void check() const {
            if (magic != Alive) {
                ++Knob::errors;
            }
        }

        static void tick_move() noexcept(NothrowMove) {
            if constexpr(!NothrowMove) {
                Knob::tick();
            }
        }
    };

    using Elem = BasicElem<true>;
    using ThrowingMoveElem = BasicElem<false>;

    static_assert(std::is_nothrow_move_constructible_v<Elem> && std::is_nothrow_move_assignable_v<Elem>);
    static_assert(!std::is_nothrow_move_constructible_v<ThrowingMoveElem>);

    // An element built with the knob suspended.
    template<class E>
    E unarmed(int v) {
        struct Restore {
            int saved = Knob::allow;
            ~Restore() {
                Knob::allow = saved;
            }
        } restore;
        Knob::allow = -1;
        return E(v);
    }

    template<class E>
    std::vector<int> values(const sgcl::vector<E>& v) {
        std::vector<int> result;
        for (auto& e: v) {
            e.check();
            result.push_back(e.value);
        }
        return result;
    }

    template<class E>
    sgcl::vector<E> make_vec(int n, size_t reserve = 0) {
        sgcl::vector<E> v;
        if (reserve) {
            v.reserve(reserve);
        }
        for (int i = 0; i < n; ++i) {
            v.emplace_back(i + 1);
        }
        return v;
    }

    // At least `n` elements and no spare capacity.
    template<class E>
    sgcl::vector<E> make_full(size_t n) {
        sgcl::vector<E> v;
        while (v.size() < n || v.size() < v.capacity()) {
            v.emplace_back(int(v.size()) + 1);
        }
        return v;
    }

    std::vector<int> inserted(std::vector<int> v, size_t pos, std::vector<int> items) {
        v.insert(v.begin() + pos, items.begin(), items.end());
        return v;
    }

    // Runs `op` with the k-th counted construction throwing, for k = 0, 1, ... until `op` completes.
    template<class Setup, class Op, class OnThrow, class OnSuccess>
    void every_throw_point(Setup setup, Op op, OnThrow on_throw, OnSuccess on_success) {
        for (int k = 0; k < 10000; ++k) {
            auto v = setup();
            auto before = values(v);
            Knob::arm(k);
            bool threw = false;
            try {
                op(v);
            } catch (const Boom&) {
                threw = true;
            }
            Knob::disarm();
            if (!threw) {
                on_success(v, before);
                ASSERT_GT(k, 0) << "the operation never threw";
                return;
            }
            on_throw(v, before);
            // Still usable.
            v.push_back(unarmed<typename decltype(v)::value_type>(-7));
            ASSERT_EQ(v.back().value, -7);
        }
        FAIL() << "the operation kept throwing";
    }

    auto strong = [](auto& v, const std::vector<int>& before) {
        EXPECT_EQ(values(v), before);
    };

    auto basic = [](auto& v, const std::vector<int>&) {
        values(v);
    };

    class ExceptionSafety_Test : public ::testing::Test {
    protected:
        void SetUp() override {
            Knob::disarm();
            ASSERT_TRUE(collect_until([] { return Knob::live.load() == 0; })) << Knob::live.load();
            Knob::errors = 0;
        }

        void TearDown() override {
            Knob::disarm();
            EXPECT_TRUE(collect_until([] { return Knob::live.load() == 0; })) << "leaked " << Knob::live.load();
            EXPECT_EQ(Knob::errors.load(), 0) << "destroyed twice or never constructed";
        }
    };
}

template<class E>
void push_back_cases() {
    auto src = unarmed<E>(99);
    for (bool growth: {false, true}) {
        SCOPED_TRACE(growth ? "growth" : "no growth");
        auto setup = [growth] { return growth ? make_full<E>(5) : make_vec<E>(5, 16); };
        auto appended = [](auto& v, const std::vector<int>& before) {
            auto expected = before;
            expected.push_back(99);
            EXPECT_EQ(values(v), expected);
        };
        every_throw_point(setup, [&](auto& v) { v.push_back(src); }, strong, appended);
        every_throw_point(setup, [](auto& v) { v.emplace_back(99); }, strong, appended);
        every_throw_point(setup, [](auto& v) { v.push_back(E(99)); }, strong, appended);
    }
    EXPECT_EQ(src.value, 99);
}

TEST_F(ExceptionSafety_Test, VectorPushBack) {
    push_back_cases<Elem>();
    push_back_cases<ThrowingMoveElem>();
}

template<class E, class Check>
void insert_value_cases(Check check) {
    auto src = unarmed<E>(99);
    for (bool growth: {false, true}) {
        SCOPED_TRACE(growth ? "growth" : "no growth");
        auto setup = [growth] { return growth ? make_full<E>(6) : make_vec<E>(6, 16); };
        auto done = [](auto& v, const std::vector<int>& before) {
            EXPECT_EQ(values(v), inserted(before, 2, {99}));
        };
        every_throw_point(setup, [&](auto& v) {
            auto it = v.insert(v.begin() + 2, src);
            EXPECT_EQ(it, v.begin() + 2);
        }, check, done);
        every_throw_point(setup, [](auto& v) { v.insert(v.begin() + 2, E(99)); }, check, done);
    }
}

TEST_F(ExceptionSafety_Test, VectorInsertValue) {
    insert_value_cases<Elem>(strong);
    insert_value_cases<ThrowingMoveElem>(basic);
}

template<class E, class Check>
void insert_range_cases(Check check) {
    std::vector<E> source;
    for (int i = 0; i < 3; ++i) {
        source.push_back(unarmed<E>(90 + i));
    }
    for (bool growth: {false, true}) {
        SCOPED_TRACE(growth ? "growth" : "no growth");
        auto setup = [growth] { return growth ? make_full<E>(6) : make_vec<E>(6, 32); };
        every_throw_point(setup, [&](auto& v) {
            auto it = v.insert(v.begin() + 1, source.begin(), source.end());
            EXPECT_EQ(it, v.begin() + 1);
        }, check, [](auto& v, const std::vector<int>& before) {
            EXPECT_EQ(values(v), inserted(before, 1, {90, 91, 92}));
        });
        // A gap that reaches past the old end.
        every_throw_point(setup, [&](auto& v) {
            v.insert(v.end() - 1, source.begin(), source.end());
        }, check, [](auto& v, const std::vector<int>& before) {
            EXPECT_EQ(values(v), inserted(before, before.size() - 1, {90, 91, 92}));
        });
        // The range is the vector itself.
        every_throw_point(setup, [](auto& v) {
            v.insert(v.begin() + 1, v.begin(), v.end());
        }, check, [](auto& v, const std::vector<int>& before) {
            EXPECT_EQ(values(v), inserted(before, 1, before));
        });
    }
    for (auto& e: source) {
        EXPECT_EQ(e.magic, E::Alive);
    }
}

TEST_F(ExceptionSafety_Test, VectorInsertRange) {
    insert_range_cases<Elem>(strong);
    insert_range_cases<ThrowingMoveElem>(basic);
}

template<class E>
void resize_cases() {
    auto src = unarmed<E>(99);
    for (bool growth: {false, true}) {
        SCOPED_TRACE(growth ? "growth" : "no growth");
        auto setup = [growth] { return growth ? make_full<E>(5) : make_vec<E>(5, 16); };
        every_throw_point(setup, [](auto& v) { v.resize(9); }, strong, [](auto& v, const std::vector<int>& before) {
            auto expected = before;
            expected.resize(9, 0);
            EXPECT_EQ(values(v), expected);
        });
        every_throw_point(setup, [&](auto& v) { v.resize(9, src); }, strong, [](auto& v, const std::vector<int>& before) {
            auto expected = before;
            expected.resize(9, 99);
            EXPECT_EQ(values(v), expected);
        });
    }
}

TEST_F(ExceptionSafety_Test, VectorResize) {
    resize_cases<Elem>();
    resize_cases<ThrowingMoveElem>();
}

// Strong includes the storage: a throw while growing leaves capacity and data() as they were.
TEST_F(ExceptionSafety_Test, VectorResizeThrowKeepsStorage) {
    auto v = make_full<Elem>(5);
    auto size = v.size();
    auto capacity = v.capacity();
    auto data = v.data();
    Knob::arm(2);
    EXPECT_ANY_THROW(v.resize(capacity + 4));
    Knob::disarm();
    EXPECT_EQ(v.size(), size);
    EXPECT_EQ(v.capacity(), capacity);
    EXPECT_EQ(v.data(), data);
}

template<class E>
void copy_cases() {
    // Copy construction: the source is untouched and nothing leaks.
    every_throw_point([] { return make_vec<E>(7); }, [](auto& v) {
        sgcl::vector<E> copy(v);
        EXPECT_EQ(values(copy), values(v));
    }, strong, strong);
    // Copy assignment: the target keeps its contents.
    auto source = make_vec<E>(7);
    for (auto& e: source) {
        e.value += 100;
    }
    every_throw_point([] { return make_vec<E>(3); }, [&](auto& v) { v = source; }, strong,
                      [&](auto& v, const std::vector<int>&) { EXPECT_EQ(values(v), values(source)); });
    // Initializer list.
    every_throw_point([] { return make_vec<E>(0); }, [](auto& v) {
        sgcl::vector<E> list{E(1), E(2), E(3)};
        v = std::move(list);
    }, strong, [](auto& v, const std::vector<int>&) { EXPECT_EQ(values(v), (std::vector<int>{1, 2, 3})); });
}

TEST_F(ExceptionSafety_Test, VectorCopy) {
    copy_cases<Elem>();
    copy_cases<ThrowingMoveElem>();
}

// A throwing move makes growth copy (std::move_if_noexcept), so reserve is strong.
TEST_F(ExceptionSafety_Test, VectorReserve) {
    every_throw_point([] { return make_full<ThrowingMoveElem>(9); }, [](auto& v) { v.reserve(v.capacity() * 3); },
                      strong, strong);
    auto v = make_full<Elem>(9);
    auto before = values(v);
    Knob::arm(0);
    v.reserve(v.capacity() * 3);
    Knob::disarm();
    EXPECT_EQ(values(v), before);
}

// Basic: every element in [0, size) stays valid.
TEST_F(ExceptionSafety_Test, VectorEraseThrowingMove) {
    every_throw_point([] { return make_vec<ThrowingMoveElem>(8); }, [](auto& v) { v.erase(v.begin() + 1, v.begin() + 3); },
                      basic, [](auto& v, const std::vector<int>&) {
                          EXPECT_EQ(values(v), (std::vector<int>{1, 4, 5, 6, 7, 8}));
                      });
    every_throw_point([] { return make_vec<ThrowingMoveElem>(8); }, [](auto& v) { v.erase(v.begin()); },
                      basic, [](auto& v, const std::vector<int>&) {
                          EXPECT_EQ(values(v), (std::vector<int>{2, 3, 4, 5, 6, 7, 8}));
                      });
}

// The argument may be an element of the same vector (SPEC §3.5).
TEST_F(ExceptionSafety_Test, VectorAliasing) {
    {
        auto v = make_full<Elem>(4);
        auto expected = values(v);
        expected.push_back(expected[0]);
        v.push_back(v[0]);
        EXPECT_EQ(values(v), expected);
    }
    {
        auto v = make_full<Elem>(4);
        auto expected = values(v);
        expected.push_back(expected[0]);
        v.push_back(std::move(v[0]));
        EXPECT_EQ(v.back().value, expected.back());
        EXPECT_EQ(v.size(), expected.size());
    }
    {
        auto v = make_full<Elem>(4);
        auto expected = values(v);
        expected.push_back(expected[1]);
        v.emplace_back(v[1]);
        EXPECT_EQ(values(v), expected);
    }
    {
        auto v = make_vec<Elem>(4, 16);
        auto expected = values(v);
        expected.push_back(expected[2]);
        v.push_back(v[2]);
        EXPECT_EQ(values(v), expected);
    }
    for (bool growth: {false, true}) {
        SCOPED_TRACE(growth ? "growth" : "no growth");
        auto setup = [growth] { return growth ? make_full<Elem>(5) : make_vec<Elem>(5, 16); };
        {
            auto v = setup();
            auto before = values(v);
            v.insert(v.begin() + 1, v[0]);
            EXPECT_EQ(values(v), inserted(before, 1, {before[0]}));
        }
        {
            auto v = setup();
            auto before = values(v);
            v.insert(v.begin(), v.back());
            EXPECT_EQ(values(v), inserted(before, 0, {before.back()}));
        }
        {
            auto v = setup();
            auto before = values(v);
            v.insert(v.begin() + 2, v[3]);
            EXPECT_EQ(values(v), inserted(before, 2, {before[3]}));
        }
        {
            auto v = setup();
            auto before = values(v);
            v.resize(v.capacity() + 3, v[0]);
            auto expected = before;
            expected.resize(v.size(), before[0]);
            EXPECT_EQ(values(v), expected);
        }
        {
            auto v = setup();
            auto before = values(v);
            v.resize(v.size() + 1, v.back());
            auto expected = before;
            expected.push_back(before.back());
            EXPECT_EQ(values(v), expected);
        }
    }
}

namespace {
    struct MapKnob {
        inline static int hash_allow = -1;
        inline static int equal_allow = -1;
        inline static size_t modulus = 0;

        static void disarm() {
            hash_allow = -1;
            equal_allow = -1;
        }
    };

    struct ThrowingHash {
        size_t operator()(int k) const {
            if (MapKnob::hash_allow >= 0 && MapKnob::hash_allow-- == 0) {
                throw Boom{};
            }
            return MapKnob::modulus ? size_t(k) % MapKnob::modulus : size_t(k);
        }

        size_t operator()(const Elem& k) const {
            return (*this)(k.value);
        }
    };

    struct ThrowingEqual {
        bool operator()(int a, int b) const {
            if (MapKnob::equal_allow >= 0 && MapKnob::equal_allow-- == 0) {
                throw Boom{};
            }
            return a == b;
        }

        bool operator()(const Elem& a, const Elem& b) const {
            return (*this)(a.value, b.value);
        }
    };

    using ValueMap = sgcl::unordered_map<int, Elem, ThrowingHash, ThrowingEqual>;
    using KeyMap = sgcl::unordered_map<Elem, int, ThrowingHash, ThrowingEqual>;

    class MapExceptionSafety_Test : public ExceptionSafety_Test {
    protected:
        void SetUp() override {
            MapKnob::disarm();
            MapKnob::modulus = 4;
            ExceptionSafety_Test::SetUp();
        }

        void TearDown() override {
            MapKnob::disarm();
            MapKnob::modulus = 0;
            ExceptionSafety_Test::TearDown();
        }
    };

    std::map<int, int> contents(const ValueMap& map) {
        std::map<int, int> result;
        size_t count = 0;
        for (auto& [k, v]: map) {
            v.check();
            result[k] = v.value;
            ++count;
        }
        EXPECT_EQ(count, map.size());
        EXPECT_EQ(result.size(), map.size());
        for (auto& [k, v]: result) {
            auto it = map.find(k);
            EXPECT_NE(it, map.end()) << k;
            if (it != map.end()) {
                EXPECT_EQ(it->second.value, v);
            }
        }
        return result;
    }

    void fill(ValueMap& map, int n) {
        for (int i = 0; i < n; ++i) {
            map.insert_or_assign(i, unarmed<Elem>(i * 10));
        }
    }

    // Runs `op` with the k-th Hash call, KeyEqual call or counted construction throwing, until it completes.
    template<class Op>
    void every_map_throw_point(int n, Op op) {
        for (int which = 0; which < 3; ++which) {
            for (int k = 0; k < 1000; ++k) {
                ValueMap map;
                fill(map, n);
                auto before = contents(map);
                (which == 0 ? MapKnob::hash_allow : which == 1 ? MapKnob::equal_allow : Knob::allow) = k;
                bool threw = false;
                try {
                    op(map);
                } catch (const Boom&) {
                    threw = true;
                }
                MapKnob::disarm();
                Knob::disarm();
                if (!threw) {
                    break;
                }
                EXPECT_EQ(contents(map), before) << "throw point " << which << "/" << k;
                map.insert_or_assign(1000, unarmed<Elem>(1));
                EXPECT_EQ(map.find(1000)->second.value, 1);
            }
        }
    }
}

TEST_F(MapExceptionSafety_Test, InsertOrAssign) {
    auto src = unarmed<Elem>(5);
    for (int n: {0, 6, 20}) {
        SCOPED_TRACE(n);
        // A new key with colliding hashes, a new key that triggers a rehash at n == 6, and an existing key.
        every_map_throw_point(n, [&](ValueMap& m) { m.insert_or_assign(100, src); });
        every_map_throw_point(n, [&](ValueMap& m) { m.insert_or_assign(101, Elem(7)); });
        every_map_throw_point(n, [&](ValueMap& m) {
            int key = 102;
            m.insert_or_assign(std::move(key), src);
        });
    }
    // Existing key: Hash and KeyEqual throws leave it unchanged; V assignment is basic.
    for (int k = 0; k < 10; ++k) {
        ValueMap map;
        fill(map, 20);
        auto before = contents(map);
        MapKnob::equal_allow = k;
        try {
            map.insert_or_assign(16, src);
            MapKnob::disarm();
            auto after = before;
            after[16] = 5;
            EXPECT_EQ(contents(map), after);
            break;
        } catch (const Boom&) {
            MapKnob::disarm();
            EXPECT_EQ(contents(map), before);
        }
    }
}

TEST_F(MapExceptionSafety_Test, FindAndErase) {
    for (int key: {3, 50}) {
        SCOPED_TRACE(key);
        every_map_throw_point(20, [key](ValueMap& m) { m.find(key); });
        every_map_throw_point(20, [key](ValueMap& m) { std::as_const(m).find(key); });
        every_map_throw_point(20, [key](ValueMap& m) { m.erase(key); });
    }
}

TEST_F(MapExceptionSafety_Test, KeyConstructionThrows) {
    KeyMap map;
    auto key = unarmed<Elem>(3);
    Knob::arm(0);
    EXPECT_THROW(map.insert_or_assign(key, 1), Boom);
    Knob::disarm();
    EXPECT_TRUE(map.empty());
    for (int i = 0; i < 30; ++i) {
        map.insert_or_assign(unarmed<Elem>(i), i);
    }
    for (int i = 30; i < 40; ++i) {
        auto k = unarmed<Elem>(i);
        Knob::arm(0);
        EXPECT_THROW(map.insert_or_assign(k, i), Boom);
        Knob::disarm();
    }
    EXPECT_EQ(map.size(), 30u);
    for (int i = 0; i < 40; ++i) {
        auto it = map.find(unarmed<Elem>(i));
        if (i < 30) {
            ASSERT_NE(it, map.end());
            EXPECT_EQ(it->second, i);
        } else {
            EXPECT_EQ(it, map.end());
        }
    }
}

namespace {
    struct NodeTag;
    using NodeReferent = Probe<NodeTag>;

    struct ReferentHolder {
        explicit ReferentHolder(int v)
        : ref(make_tracked<NodeReferent>(v)) {
        }

        ReferentHolder(const ReferentHolder& o)
        : ref((Knob::tick(), o.ref)) {
        }

        tracked_ptr<NodeReferent> ref;
    };
}

// The first construction of a node type throws during layout discovery; later nodes are still traced.
TEST_F(MapExceptionSafety_Test, FirstNodeConstructionThrows) {
    ASSERT_TRUE(collect_until([] { return NodeReferent::alive() == 0; }));
    {
        sgcl::unordered_map<int, ReferentHolder, ThrowingHash, ThrowingEqual> map;
        {
            ReferentHolder value(-1);
            Knob::arm(0);
            EXPECT_THROW(map.insert_or_assign(1, value), Boom);
            Knob::disarm();
        }
        EXPECT_TRUE(map.empty());
        for (int i = 0; i < 100; ++i) {
            ReferentHolder value(i);
            map.insert_or_assign(i, value);
        }
        EXPECT_TRUE(collect_until([] { return NodeReferent::alive() == 100; }));
        settle();
        EXPECT_EQ(NodeReferent::alive(), 100);
        for (int i = 0; i < 100; ++i) {
            ASSERT_EQ(map.find(i)->second.ref->value, i);
        }
        for (int i = 0; i < 50; ++i) {
            map.erase(i);
        }
        EXPECT_TRUE(collect_until([] { return NodeReferent::alive() == 50; }));
    }
    EXPECT_TRUE(collect_until([] { return NodeReferent::alive() == 0; }));
}
