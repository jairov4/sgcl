//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#include "types.h"

#include <map>
#include <set>
#include <string>

TEST(UnorderedMap_Test, InsertOrAssignAndFind) {
    sgcl::unordered_map<std::string, int> map;
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.find("a"), map.end());
    auto [it, inserted] = map.insert_or_assign("a", 1);
    EXPECT_TRUE(inserted);
    EXPECT_EQ(it->first, "a");
    EXPECT_EQ(it->second, 1);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_FALSE(map.empty());
    auto found = map.find("a");
    ASSERT_NE(found, map.end());
    EXPECT_EQ(found, it);
    EXPECT_EQ(map.find("b"), map.end());
    auto [it2, inserted2] = map.insert_or_assign("a", 2);
    EXPECT_FALSE(inserted2);
    EXPECT_EQ(it2, it);
    EXPECT_EQ(it->second, 2);
    EXPECT_EQ(map.size(), 1u);
    const auto& cmap = map;
    auto cfound = cmap.find("a");
    ASSERT_NE(cfound, cmap.end());
    EXPECT_EQ(cfound->second, 2);
    EXPECT_EQ(cmap.find("z"), cmap.cend());
    static_assert(std::is_same_v<decltype(*it), std::pair<const std::string, int>&>);
    static_assert(std::is_same_v<decltype(map)::value_type, std::pair<const std::string, int>>);
}

TEST(UnorderedMap_Test, InsertOrAssignOverloads) {
    sgcl::unordered_map<std::string, std::string> map;
    const std::string k1 = "k1", v1 = "v1";
    std::string k2 = "k2", v2 = "v2", k3 = "k3", v3 = "v3", k4 = "k4", v4 = "v4";
    EXPECT_TRUE(map.insert_or_assign(k1, v1).second);
    EXPECT_TRUE(map.insert_or_assign(k2, std::move(v2)).second);
    EXPECT_TRUE(map.insert_or_assign(std::move(k3), v3).second);
    EXPECT_TRUE(map.insert_or_assign(std::move(k4), std::move(v4)).second);
    EXPECT_EQ(map.size(), 4u);
    EXPECT_EQ(map.find("k1")->second, "v1");
    EXPECT_EQ(map.find("k2")->second, "v2");
    EXPECT_EQ(map.find("k3")->second, "v3");
    EXPECT_EQ(map.find("k4")->second, "v4");
    std::string again = "k4", value = "w4";
    EXPECT_FALSE(map.insert_or_assign(std::move(again), std::move(value)).second);
    EXPECT_EQ(map.find("k4")->second, "w4");
    EXPECT_EQ(map.size(), 4u);
}

TEST(UnorderedMap_Test, EraseByKey) {
    sgcl::unordered_map<std::string, int> map;
    EXPECT_EQ(map.erase("none"), 0u);
    map.insert_or_assign("one", 1);
    map.insert_or_assign("two", 2);
    EXPECT_EQ(map.erase("one"), 1u);
    EXPECT_EQ(map.find("one"), map.end());
    EXPECT_EQ(map.erase("one"), 0u);
    EXPECT_EQ(map.erase("not-there"), 0u);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.find("two")->second, 2);
}

TEST(UnorderedMap_Test, Clear) {
    sgcl::unordered_map<std::string, int> map;
    map.insert_or_assign("a", 1);
    map.insert_or_assign("b", 2);
    map.clear();
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0u);
    EXPECT_EQ(map.begin(), map.end());
    EXPECT_EQ(map.find("a"), map.end());
    map.insert_or_assign("c", 3);
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.find("c")->second, 3);
}

TEST(UnorderedMap_Test, CopyAndMoveAreDeleted) {
    using M = sgcl::unordered_map<int, int>;
    static_assert(!std::is_copy_constructible_v<M>);
    static_assert(!std::is_copy_assignable_v<M>);
    static_assert(!std::is_move_constructible_v<M>);
    static_assert(!std::is_move_assignable_v<M>);
    M m;
    static_assert(noexcept(m.clear()) && noexcept(m.size()) && noexcept(m.begin()) && noexcept(m.end()));
}

namespace {
    struct Empty {};
}

// SPEC §2 U9: a set is a map whose value is an empty marker.
TEST(UnorderedMap_Test, UsedAsSet) {
    sgcl::unordered_map<std::string, Empty> set;
    for (int i = 0; i < 100; ++i) {
        set.insert_or_assign(std::to_string(i % 40), Empty{});
    }
    EXPECT_EQ(set.size(), 40u);
    for (int i = 0; i < 40; ++i) {
        EXPECT_NE(set.find(std::to_string(i)), set.end());
    }
    EXPECT_EQ(set.find("40"), set.end());
    std::set<std::string> seen;
    for (auto& [k, v]: set) {
        EXPECT_TRUE(seen.insert(k).second);
    }
    EXPECT_EQ(seen.size(), 40u);
}

TEST(UnorderedMap_Test, IterationVisitsEveryElementOnce) {
    sgcl::unordered_map<int, int> map;
    for (int i = 0; i < 1000; ++i) {
        map.insert_or_assign(i, i * 2);
    }
    for (int i = 0; i < 1000; i += 3) {
        map.erase(i);
    }
    std::map<int, int> seen;
    for (auto& [k, v]: map) {
        ++seen[k];
        EXPECT_EQ(v, k * 2);
    }
    EXPECT_EQ(seen.size(), map.size());
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(seen.count(i), i % 3 ? 1u : 0u) << i;
        if (i % 3) {
            EXPECT_EQ(seen[i], 1) << i;
        }
    }
    const auto& cmap = map;
    size_t count = 0;
    for (auto it = cmap.cbegin(); it != cmap.cend(); ++it) {
        ++count;
    }
    EXPECT_EQ(count, map.size());
    for (auto it = map.begin(); it != map.end(); ++it) {
        it->second = -1;
    }
    for (auto it = map.begin(); it != map.end(); it++) {
        EXPECT_EQ(it->second, -1);
    }
    sgcl::unordered_map<int, int>::const_iterator converted = map.begin();
    EXPECT_EQ(converted, map.cbegin());
}

TEST(UnorderedMap_Test, IteratorStability) {
    sgcl::unordered_map<int, std::string> map;
    for (int i = 0; i < 50; ++i) {
        map.insert_or_assign(i, std::to_string(i));
    }
    auto it = map.find(10);
    ASSERT_NE(it, map.end());
    auto [same, inserted] = map.insert_or_assign(10, "ten");
    EXPECT_FALSE(inserted);
    EXPECT_EQ(same, it);
    EXPECT_EQ(it->first, 10);
    EXPECT_EQ(it->second, "ten");
    for (int i = 0; i < 50; ++i) {
        if (i != 10) {
            EXPECT_EQ(map.erase(i), 1u);
            EXPECT_EQ(it->first, 10);
            EXPECT_EQ(it->second, "ten");
        }
    }
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.begin(), it);
    ++it;
    EXPECT_EQ(it, map.end());
}

TEST(UnorderedMap_Test, IteratorKeptInsideManagedObject) {
    using Map = sgcl::unordered_map<int, int>;
    struct Cursor {
        Map::iterator it;
    };
    auto map = std::make_unique<Map>();
    for (int i = 0; i < 20; ++i) {
        map->insert_or_assign(i, i);
    }
    tracked_ptr<Cursor> cursor = make_tracked<Cursor>();
    cursor->it = map->find(7);
    map->erase(8);
    settle();
    EXPECT_EQ(cursor->it->first, 7);
    EXPECT_EQ(cursor->it->second, 7);
}

namespace {
    struct ValueTag;
    using MapValue = Probe<ValueTag>;
    struct KeyTag;
    using MapKey = Probe<KeyTag>;

    struct ProbeHash {
        size_t operator()(const tracked_ptr<MapKey>& k) const noexcept {
            return std::hash<int>{}(k->value);
        }
    };

    struct ProbeEqual {
        bool operator()(const tracked_ptr<MapKey>& a, const tracked_ptr<MapKey>& b) const noexcept {
            return a->value == b->value;
        }
    };
}

// Keys and values that are references stay alive while in the map and are reclaimed after removal.
TEST(UnorderedMap_Test, KeysAndValuesAreTraced) {
    ASSERT_TRUE(collect_until([] { return MapValue::alive() == 0 && MapKey::alive() == 0; }));
    {
        sgcl::unordered_map<tracked_ptr<MapKey>, tracked_ptr<MapValue>, ProbeHash, ProbeEqual> map;
        for (int i = 0; i < 200; ++i) {
            map.insert_or_assign(tracked_ptr<MapKey>(make_tracked<MapKey>(i)), tracked_ptr<MapValue>(make_tracked<MapValue>(i)));
        }
        settle();
        EXPECT_EQ(MapKey::alive(), 200);
        EXPECT_EQ(MapValue::alive(), 200);
        for (int i = 0; i < 200; ++i) {
            tracked_ptr<MapKey> probe = make_tracked<MapKey>(i);
            auto it = map.find(probe);
            ASSERT_NE(it, map.end());
            ASSERT_EQ(it->second->value, i);
        }
        for (int i = 0; i < 100; ++i) {
            tracked_ptr<MapKey> probe = make_tracked<MapKey>(i);
            EXPECT_EQ(map.erase(probe), 1u);
        }
        tracked_ptr<MapKey> probe = make_tracked<MapKey>(150);
        map.insert_or_assign(probe, tracked_ptr<MapValue>(make_tracked<MapValue>(-150)));
        EXPECT_TRUE(collect_until([] { return MapKey::alive() == 101 && MapValue::alive() == 100; }));
        EXPECT_EQ(map.find(probe)->second->value, -150);
    }
    EXPECT_TRUE(collect_until([] { return MapValue::alive() == 0 && MapKey::alive() == 0; }));
}

namespace {
    // Hash and KeyEqual with observable call counts (SPEC §3.6).
    struct Calls {
        inline static long hash = 0;
        inline static long equal = 0;
        inline static size_t modulus = 0;
        inline static const int* argument = nullptr;
        inline static bool argument_second = true;

        static void reset() {
            hash = 0;
            equal = 0;
        }
    };

    struct CountingHash {
        size_t operator()(const int& k) const {
            ++Calls::hash;
            return Calls::modulus ? size_t(k) % Calls::modulus : size_t(k);
        }
    };

    struct CountingEqual {
        bool operator()(const int& stored, const int& argument) const {
            ++Calls::equal;
            if (Calls::argument && &argument != Calls::argument) {
                Calls::argument_second = false;
            }
            return stored == argument;
        }
    };

    using CountingMap = sgcl::unordered_map<int, int, CountingHash, CountingEqual>;
}

TEST(UnorderedMap_Test, HashCalledOncePerOperationNeverOnStoredKeys) {
    Calls::modulus = 0;
    CountingMap map;
    Calls::reset();
    for (int i = 0; i < 5000; ++i) {
        map.insert_or_assign(i, i);
    }
    // Many rehashes happened; none hashed a stored key, and distinct hashes never reached KeyEqual.
    EXPECT_EQ(Calls::hash, 5000);
    EXPECT_EQ(Calls::equal, 0);
    Calls::reset();
    map.insert_or_assign(42, 0);
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 1);
    Calls::reset();
    EXPECT_NE(map.find(77), map.end());
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 1);
    Calls::reset();
    EXPECT_EQ(map.find(-1), map.end());
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 0);
    Calls::reset();
    EXPECT_NE(std::as_const(map).find(5), map.cend());
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 1);
    Calls::reset();
    EXPECT_EQ(map.erase(77), 1u);
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 1);
    Calls::reset();
    EXPECT_EQ(map.erase(77), 0u);
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 0);
    Calls::reset();
    for (auto& [k, v]: map) {
        (void)k;
        (void)v;
    }
    map.clear();
    EXPECT_EQ(Calls::hash, 0);
    EXPECT_EQ(Calls::equal, 0);
}

TEST(UnorderedMap_Test, HashCalledOnceOnEmptyMap) {
    Calls::modulus = 0;
    CountingMap map;
    Calls::reset();
    EXPECT_EQ(map.find(1), map.end());
    EXPECT_EQ(Calls::hash, 1);
    Calls::reset();
    EXPECT_EQ(map.erase(1), 0u);
    EXPECT_EQ(Calls::hash, 1);
    map.insert_or_assign(1, 1);
    map.clear();
    Calls::reset();
    EXPECT_EQ(map.erase(1), 0u);
    EXPECT_EQ(Calls::hash, 1);
    Calls::reset();
    EXPECT_EQ(map.find(1), map.end());
    EXPECT_EQ(Calls::hash, 1);
}

TEST(UnorderedMap_Test, KeyEqualOnlyOnEqualHashes) {
    Calls::modulus = 4;
    CountingMap map;
    for (int i = 0; i < 100; ++i) {
        map.insert_or_assign(i, i);
    }
    // 25 stored keys share each hash; the others in the same bucket are never compared.
    Calls::reset();
    EXPECT_EQ(map.find(1000), map.end());
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 25);
    Calls::reset();
    EXPECT_EQ(map.erase(1004), 0u);
    EXPECT_EQ(Calls::equal, 25);
    for (int k = 0; k < 100; ++k) {
        Calls::reset();
        ASSERT_NE(map.find(k), map.end());
        EXPECT_GE(Calls::equal, 1);
        EXPECT_LE(Calls::equal, 25);
    }
    Calls::reset();
    EXPECT_TRUE(map.insert_or_assign(100, 0).second);
    EXPECT_EQ(Calls::hash, 1);
    EXPECT_EQ(Calls::equal, 25);
    // Stored key first, argument second.
    int probe = 33;
    Calls::argument = &probe;
    Calls::argument_second = true;
    map.find(probe);
    map.insert_or_assign(probe, 1);
    map.erase(probe);
    Calls::argument = nullptr;
    EXPECT_TRUE(Calls::argument_second);
    Calls::modulus = 0;
}
