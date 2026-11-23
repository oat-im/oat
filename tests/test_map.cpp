#include "test.h"

#include <oat/oat.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace oat;

// Keys whose hash is under the test's control.
struct ConstantHashKey {
  u32 v;
};
struct LowBitsKey {
  u32 v;
};
template <>
struct oat::KeyTraits<ConstantHashKey> {
  static u64 hash(const ConstantHashKey&) { return 7; } // every key collides
  static bool eq(const ConstantHashKey& a, const ConstantHashKey& b) { return a.v == b.v; }
};
template <>
struct oat::KeyTraits<LowBitsKey> {
  static u64 hash(const LowBitsKey& k) { return k.v & 3; } // four home slots
  static bool eq(const LowBitsKey& a, const LowBitsKey& b) { return a.v == b.v; }
};
// A float key is allowed once it has its own traits.
template <>
struct oat::KeyTraits<f32> {
  static u64 hash(const f32& k) {
    u32 bits;
    std::memcpy(&bits, &k, 4);
    return hash_u64(k == 0.0f ? 0 : bits); // +0 and -0 are equal
  }
  static bool eq(const f32& a, const f32& b) { return a == b; }
};

struct GridKey {
  i32 x, y;
};

template <class K, class GetKey>
static void map_model_test(u64 seed, u32 key_space, u32 steps, GetKey make_key) {
  Allocator h = heap();
  Rng rng = Rng::seeded(seed);
  Map<K, u64> map;
  std::unordered_map<u32, u64> model;
  bool ok = true;
  for (u32 step = 0; step < steps && ok; ++step) {
    u32 raw_key = rng.range(0, key_space);
    K key = make_key(raw_key);
    u64 value = rng.next_u64();
    switch (rng.range(0, 6)) {
      case 0:
      case 1: {
        bool added = map.insert(key, value, h);
        ok = ok && added == (model.find(raw_key) == model.end());
        model[raw_key] = value; // insert replaces
        break;
      }
      case 2: {
        bool removed = map.remove(key);
        ok = ok && removed == (model.erase(raw_key) == 1);
        break;
      }
      case 3: {
        u64* found = map.find(key);
        auto it = model.find(raw_key);
        ok = ok && (found != nullptr) == (it != model.end());
        if (found && it != model.end()) ok = ok && *found == it->second;
        ok = ok && map.contains(key) == (found != nullptr);
        break;
      }
      case 4: {
        bool existed = model.find(raw_key) != model.end();
        u64& slot = map.get_or_insert(key, h);
        ok = ok && (existed ? slot == model[raw_key] : slot == 0); // zero if new
        slot = value;
        model[raw_key] = value;
        break;
      }
      case 5:
        if (rng.range(0, 400) == 0) {
          map.clear();
          model.clear();
        }
        break;
    }
    ok = ok && map.len() == model.size();
    ok = ok && static_cast<u64>(map.len()) * 4 <= static_cast<u64>(map.raw.cap) * 3;
  }
  // Iteration visits every entry exactly once.
  u32 visited = 0;
  for (auto& e : map) {
    u32 raw_key;
    std::memcpy(&raw_key, &e.key, sizeof(raw_key));
    auto it = model.find(raw_key);
    ok = ok && it != model.end() && it->second == e.value;
    ++visited;
  }
  CHECK(ok && visited == model.size());
  map.free(h);
  CHECK(map.raw.entries == nullptr && map.len() == 0);
}

TEST(map_against_model) {
  HeapStats before = heap_stats();
  map_model_test<u32>(30, 64, 40000, [](u32 k) { return k; });
  map_model_test<u32>(31, 5000, 60000, [](u32 k) { return k; });
  CHECK(heap_stats().live_bytes == before.live_bytes);
}

TEST(map_forced_collisions) {
  // Every key hashes alike, so the whole table is one probe chain.
  map_model_test<ConstantHashKey>(32, 200, 30000, [](u32 k) { return ConstantHashKey{k}; });
  // Four interleaved chains that wrap around the end of the table.
  map_model_test<LowBitsKey>(33, 300, 30000, [](u32 k) { return LowBitsKey{k}; });
}

TEST(map_remove_in_long_chain) {
  Allocator h = heap();
  Map<ConstantHashKey, u32> map;
  for (u32 i = 0; i < 100; ++i) map.insert(ConstantHashKey{i}, i * 10, h);
  // Remove from the middle, the front, and the back of the chain; everything
  // behind the hole must still be reachable.
  for (u32 victim : {50u, 0u, 99u, 51u, 1u, 98u, 25u, 75u}) {
    CHECK(map.remove(ConstantHashKey{victim}));
    CHECK(!map.contains(ConstantHashKey{victim}));
    CHECK(!map.remove(ConstantHashKey{victim}));
  }
  bool ok = true;
  u32 live = 0;
  for (u32 i = 0; i < 100; ++i) {
    u32* v = map.find(ConstantHashKey{i});
    if (v) {
      ok = ok && *v == i * 10;
      ++live;
    }
  }
  CHECK(ok && live == 92 && map.len() == 92);
  // Empty it completely and reuse it.
  for (u32 i = 0; i < 100; ++i) map.remove(ConstantHashKey{i});
  CHECK(map.len() == 0 && !map.contains(ConstantHashKey{3}));
  map.insert(ConstantHashKey{3}, 33, h);
  CHECK(*map.find(ConstantHashKey{3}) == 33);
  map.free(h);
}

TEST(map_growth_at_load_limit) {
  Allocator h = heap();
  Map<u32, u32> map;
  CHECK(map.raw.cap == 0 && map.find(1) == nullptr && !map.remove(1) && !map.contains(1));
  for (u32 i = 0; i < 6; ++i) map.insert(i, i, h);
  CHECK(map.raw.cap == 8); // 6 of 8 is exactly 75%
  map.insert(3, 99, h);    // replacing an existing key at the limit does not grow
  CHECK(map.raw.cap == 8 && *map.find(3) == 99);
  map.get_or_insert(4, h);
  CHECK(map.raw.cap == 8);
  map.insert(6, 6, h); // the seventh entry does
  CHECK(map.raw.cap == 16 && map.len() == 7);
  for (u32 i = 7; i < 12; ++i) map.insert(i, i, h);
  CHECK(map.raw.cap == 16);
  map.insert(12, 12, h);
  CHECK(map.raw.cap == 32);
  bool ok = true;
  for (u32 i = 0; i < 13; ++i) ok = ok && map.contains(i);
  CHECK(ok);
  map.free(h);

  // reserve leaves room for exactly that many entries.
  Map<u32, u32> reserved;
  reserved.reserve(1000, h);
  u32 cap = reserved.raw.cap;
  void* entries = reserved.raw.entries;
  CHECK(std::has_single_bit(cap) && cap * 3 >= 1000 * 4 && cap / 2 * 3 < 1000 * 4);
  for (u32 i = 0; i < 1000; ++i) reserved.insert(i, i, h);
  CHECK(reserved.raw.cap == cap && reserved.raw.entries == entries);
  reserved.reserve(10, h); // never shrinks
  CHECK(reserved.raw.cap == cap);
  reserved.free(h);
}

TEST(map_string_view_keys) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  Map<std::string_view, u32> map;
  std::unordered_map<std::string, u32> model;
  Rng rng = Rng::seeded(34);
  StrBuf scratch;
  for (u32 i = 0; i < 3000; ++i) {
    scratch.clear();
    scratch.appendf(h, "key%u", rng.range(0, 1500));
    std::string_view key = scratch.view();
    if (!map.contains(key)) {
      // The map does not own the characters: give it an arena copy.
      const char* stored = to_cstr(key, a);
      map.insert(std::string_view(stored, key.size()), i, h);
    } else {
      *map.find(key) = i;
    }
    model[std::string(key)] = i;
  }
  bool ok = map.len() == model.size();
  for (auto& [k, v] : model) {
    u32* found = map.find(std::string_view(k)); // a different pointer, same characters
    ok = ok && found && *found == v;
  }
  CHECK(ok);
  CHECK(!map.contains("absent"));
  map.insert("", 5, h);
  CHECK(*map.find(std::string_view()) == 5);
  scratch.free(h);
  map.free(h);
  arena.destroy();
}

TEST(map_key_kinds) {
  Allocator h = heap();

  Map<GridKey, u32> grid; // a padding-free struct hashes as raw bytes
  for (i32 y = -10; y < 10; ++y)
    for (i32 x = -10; x < 10; ++x) grid.insert(GridKey{x, y}, static_cast<u32>(x * 31 + y), h);
  CHECK(grid.len() == 400 && *grid.find(GridKey{-3, 7}) == static_cast<u32>(-3 * 31 + 7));
  grid.free(h);

  int objects[4];
  Map<int*, u32> by_pointer;
  for (u32 i = 0; i < 4; ++i) by_pointer.insert(&objects[i], i, h);
  CHECK(*by_pointer.find(&objects[2]) == 2);
  by_pointer.free(h);

  Map<u64, Array<u32>> lists; // a container as the value
  lists.get_or_insert(5, h).push(1, h);
  lists.get_or_insert(5, h).push(2, h);
  CHECK(lists.find(5)->len == 2);
  for (auto& e : lists) e.value.free(h);
  lists.free(h);

  Map<f32, u32> by_float; // floats need custom traits
  by_float.insert(1.5f, 1, h);
  by_float.insert(0.0f, 2, h);
  CHECK(*by_float.find(-0.0f) == 2 && by_float.len() == 2);
  by_float.free(h);

  const Map<u32, u32> empty;
  CHECK(empty.find(1) == nullptr && empty.begin().i == empty.end().i);
}

TEST(set_against_model) {
  Allocator h = heap();
  static_assert(sizeof(MapEntry<u32, detail::Unit>) == sizeof(u32)); // no per-entry overhead
  Set<u32> set;
  std::unordered_set<u32> model;
  Rng rng = Rng::seeded(35);
  bool ok = true;
  for (u32 step = 0; step < 30000; ++step) {
    u32 key = rng.range(0, 500);
    switch (rng.range(0, 3)) {
      case 0: ok = ok && set.insert(key, h) == model.insert(key).second; break;
      case 1: ok = ok && set.remove(key) == (model.erase(key) == 1); break;
      case 2: ok = ok && set.contains(key) == (model.count(key) == 1); break;
    }
    ok = ok && set.len() == model.size();
  }
  u32 visited = 0;
  for (const u32& key : set) {
    ok = ok && model.count(key) == 1;
    ++visited;
  }
  CHECK(ok && visited == model.size());
  set.clear();
  CHECK(set.is_empty() && !set.contains(1));
  set.reserve(100, h);
  set.free(h);
}

#if OAT_DEBUG
TEST(map_self_insert_asserts) {
  Allocator h = heap();
  Map<u32, u32> map;
  map.insert(1, 10, h);
  auto it = map.begin();
  CHECK_ASSERTS(map.insert((*it).key, 5u, h));  // key that lives in the map
  CHECK_ASSERTS(map.insert(2u, (*it).value, h)); // value that lives in the map
  CHECK_ASSERTS(map.get_or_insert((*it).key, h));
  u32 value = *map.find(1); // copy out first
  map.insert(2, value, h);
  CHECK(*map.find(2) == 10);
  map.free(h);
}
#endif
