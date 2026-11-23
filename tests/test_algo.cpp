#include "test.h"

#include <oat/oat.h>

#include <algorithm>
#include <vector>

using namespace oat;

struct Item {
  u32 key;
  u32 seq;
};

static std::vector<u32> make_input(Rng& rng, u32 n, u32 pattern) {
  std::vector<u32> v(n);
  for (u32 i = 0; i < n; ++i) {
    switch (pattern) {
      case 0: v[i] = static_cast<u32>(rng.next_u64()); break;       // random
      case 1: v[i] = i; break;                                      // sorted
      case 2: v[i] = n - i; break;                                  // reversed
      case 3: v[i] = 42; break;                                     // all equal
      case 4: v[i] = rng.range(0, 4); break;                        // few distinct
      case 5: v[i] = i < n / 2 ? i : n - i; break;                  // organ pipe
      default: v[i] = (i % 2) ? i : n - i; break;                   // interleaved
    }
  }
  return v;
}

TEST(sort_against_std) {
  Rng rng = Rng::seeded(10);
  bool ok = true;
  for (u32 n : {0u, 1u, 2u, 3u, 24u, 25u, 26u, 100u, 1000u, 20000u}) {
    for (u32 pattern = 0; pattern < 7; ++pattern) {
      std::vector<u32> v = make_input(rng, n, pattern);
      std::vector<u32> expected = v;
      std::sort(expected.begin(), expected.end());
      oat::sort(std::span<u32>(v), [](u32 a, u32 b) { return a < b; });
      ok = ok && v == expected;

      // Descending, through a different comparator.
      oat::sort(std::span<u32>(v), [](u32 a, u32 b) { return a > b; });
      std::reverse(expected.begin(), expected.end());
      ok = ok && v == expected;
    }
  }
  CHECK(ok);
}

TEST(sort_heap_fallback) {
  // Depth 0 forces the heapsort path that guards against quadratic inputs.
  Rng rng = Rng::seeded(11);
  bool ok = true;
  for (u32 n : {25u, 100u, 5000u}) {
    std::vector<u32> v = make_input(rng, n, 0);
    std::vector<u32> expected = v;
    std::sort(expected.begin(), expected.end());
    auto less = [](u32 a, u32 b) { return a < b; };
    oat::detail::intro_sort(v.data(), v.size(), 0, less);
    ok = ok && v == expected;
  }
  CHECK(ok);
}

TEST(sort_stable_against_std) {
  Allocator h = heap();
  HeapStats before = heap_stats();
  Rng rng = Rng::seeded(12);
  bool ok = true;
  for (u32 n : {0u, 1u, 16u, 17u, 33u, 1000u, 20000u}) {
    std::vector<Item> v(n);
    for (u32 i = 0; i < n; ++i) v[i] = Item{rng.range(0, 20), i};
    std::vector<Item> expected = v;
    auto less = [](const Item& a, const Item& b) { return a.key < b.key; };
    std::stable_sort(expected.begin(), expected.end(), less);
    oat::sort_stable(std::span<Item>(v), less, h);
    for (u32 i = 0; i < n; ++i)
      ok = ok && v[i].key == expected[i].key && v[i].seq == expected[i].seq;
  }
  CHECK(ok);
  CHECK(heap_stats().live_bytes == before.live_bytes); // scratch was returned
}

template <class K>
static bool radix_case(Rng& rng, u32 n, u32 bits, Allocator& a) {
  std::vector<K> keys(n);
  for (u32 i = 0; i < n; ++i) {
    u64 r = rng.next_u64();
    keys[i] = static_cast<K>(bits >= 64 ? r : r & ((u64{1} << bits) - 1));
  }
  std::vector<K> original = keys;
  std::vector<K> expected = keys;
  std::sort(expected.begin(), expected.end());
  std::vector<u32> order(n);
  radix_sort(std::span<K>(keys), std::span<u32>(order), a);
  bool ok = keys == expected;
  for (u32 i = 0; i < n && ok; ++i) {
    ok = ok && original[order[i]] == keys[i];
    // Stable: equal keys keep their original order.
    if (i > 0 && keys[i] == keys[i - 1]) ok = ok && order[i] > order[i - 1];
  }
  // Keys only.
  std::vector<K> again = original;
  radix_sort(std::span<K>(again), std::span<u32>(), a);
  return ok && again == expected;
}

TEST(radix_sort_against_std) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  Rng rng = Rng::seeded(13);
  bool ok = true;
  for (u32 n : {0u, 1u, 2u, 255u, 5000u}) {
    for (u32 bits : {4u, 8u, 20u, 32u}) ok = ok && radix_case<u32>(rng, n, bits, a);
    for (u32 bits : {4u, 20u, 40u, 64u}) ok = ok && radix_case<u64>(rng, n, bits, h);
  }
  CHECK(ok);
  CHECK(arena.used == 0 || arena.current != nullptr);
  arena.destroy();
}

TEST(bounds_against_std) {
  Rng rng = Rng::seeded(14);
  bool ok = true;
  for (u32 n : {0u, 1u, 2u, 7u, 100u}) {
    std::vector<u32> v(n);
    for (u32& x : v) x = rng.range(0, 30);
    std::sort(v.begin(), v.end());
    auto less = [](u32 a, u32 b) { return a < b; };
    for (u32 key = 0; key < 32; ++key) {
      u32 lo = lower_bound(std::span<const u32>(v), key, less);
      u32 hi = upper_bound(std::span<const u32>(v), key, less);
      ok = ok && lo == static_cast<u32>(std::lower_bound(v.begin(), v.end(), key) - v.begin());
      ok = ok && hi == static_cast<u32>(std::upper_bound(v.begin(), v.end(), key) - v.begin());
    }
  }
  CHECK(ok);

  // A key of a different type than the element, on a mutable span.
  Item items[] = {{1, 0}, {3, 0}, {3, 1}, {8, 0}};
  u32 at = lower_bound(std::span<Item>(items), 3u, [](const Item& it, u32 k) { return it.key < k; });
  u32 past = upper_bound(std::span<Item>(items), 3u, [](u32 k, const Item& it) { return k < it.key; });
  CHECK(at == 1 && past == 3);
}

TEST(hash_bytes_matches_wyhash) {
  // Values produced by the reference wyhash.h (wyhash_final_version_4_3), default
  // secret, seed 0, for the messages its own test program uses. They cover
  // every length class: 0, 1-3, 4-16, 17-47 and 48 or more.
  struct Vector {
    const char* text;
    u64 hash;
  };
  const Vector vectors[] = {
      {"", 0x93228a4de0eec5a2ull},
      {"a", 0xaced12527fe5bff8ull},
      {"abc", 0x989b4a209c1011c9ull},
      {"message digest", 0x309ab4c045215e8full},
      {"abcdefghijklmnopqrstuvwxyz", 0xccaeadc12a061176ull},
      {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 0x1fdd130ecb5b4709ull},
      {"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
       0x7e22da19f1a6055aull},
  };
  for (const Vector& v : vectors) CHECK(hash_bytes(v.text, std::strlen(v.text)) == v.hash);
}

TEST(hashing) {
  // Deterministic, and independent of the buffer's alignment.
  alignas(8) u8 buf[200];
  for (u32 i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<u8>(i * 7 + 1);
  bool ok = true;
  for (usize len = 0; len <= 130; ++len) {
    u64 h0 = hash_bytes(buf, len);
    u8 shifted[201];
    std::memcpy(shifted + 1, buf, len);
    ok = ok && hash_bytes(shifted + 1, len) == h0;
    // Every byte matters.
    for (usize i = 0; i < len; ++i) {
      buf[i] ^= 0x10;
      ok = ok && hash_bytes(buf, len) != h0;
      buf[i] ^= 0x10;
    }
    // So does the length.
    if (len > 0) ok = ok && hash_bytes(buf, len - 1) != h0;
  }
  CHECK(ok);

  // Sequential integers spread over the low bits, which is what Map uses.
  u32 buckets[256] = {};
  for (u64 i = 0; i < 25600; ++i) ++buckets[hash_u64(i) & 255];
  u32 lo = U32_MAX, hi = 0;
  for (u32 b : buckets) {
    lo = oat::min(lo, b);
    hi = oat::max(hi, b);
  }
  CHECK(lo > 50 && hi < 150);

  u32 byte_buckets[256] = {};
  for (u64 i = 0; i < 25600; ++i) {
    u64 key[3] = {i, 0, 0};
    ++byte_buckets[hash_bytes(key, sizeof(key)) & 255];
  }
  lo = U32_MAX, hi = 0;
  for (u32 b : byte_buckets) {
    lo = oat::min(lo, b);
    hi = oat::max(hi, b);
  }
  CHECK(lo > 50 && hi < 150);

  CHECK(hash_combine(1, 2) != hash_combine(2, 1));
  CHECK(hash_combine(1, 2) == hash_combine(1, 2));
  static_assert(hash_u64(1) != hash_u64(2));
}

TEST(rng) {
  Rng a = Rng::seeded(99);
  Rng b = Rng::seeded(99);
  Rng c = Rng::seeded(100);
  bool same = true, differs = false;
  for (u32 i = 0; i < 100; ++i) {
    u64 x = a.next_u64();
    same = same && x == b.next_u64();
    differs = differs || x != c.next_u64();
  }
  CHECK(same && differs);

  bool in_range = true;
  u32 hits[10] = {};
  f64 sum = 0;
  for (u32 i = 0; i < 100000; ++i) {
    u32 r = a.range(5, 15);
    in_range = in_range && r >= 5 && r < 15;
    ++hits[r - 5];
    f32 f = a.next_f32();
    in_range = in_range && f >= 0.0f && f < 1.0f;
    sum += f;
  }
  CHECK(in_range);
  for (u32 h : hits) CHECK(h > 9000 && h < 11000);
  CHECK(sum / 100000 > 0.49 && sum / 100000 < 0.51);
  CHECK(a.range(7, 8) == 7);
  CHECK(a.range(0, U32_MAX) < U32_MAX);
}
