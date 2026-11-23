#include <oat/algo.h>

#if defined(_MSC_VER) && !defined(__SIZEOF_INT128__)
#  include <intrin.h>
#endif

namespace oat {

// ---------------------------------------------------------------------------
// hash_bytes
//
// This is wyhash (final version 4.3) by Wang Yi, with its default secret and a
// seed of 0: https://github.com/wangyi-fudan/wyhash. The author released it
// into the public domain under The Unlicense. It is transcribed without
// changes to the algorithm, so its published quality results (SMHasher) apply,
// and tests/test_algo.cpp checks it against values from the reference code.
// Little-endian only, like the rest of oat.
// ---------------------------------------------------------------------------

static inline void wy_mum(u64* a, u64* b) {
#if defined(__SIZEOF_INT128__)
  __uint128_t r = *a;
  r *= *b;
  *a = static_cast<u64>(r);
  *b = static_cast<u64>(r >> 64);
#elif defined(_M_X64)
  *a = _umul128(*a, *b, b);
#elif defined(_M_ARM64)
  u64 hi = __umulh(*a, *b);
  *a = *a * *b;
  *b = hi;
#else
#  error "oat::hash_bytes needs a 64x64->128 multiply on this target"
#endif
}

static inline u64 wy_mix(u64 a, u64 b) {
  wy_mum(&a, &b);
  return a ^ b;
}

static inline u64 wy_r8(const u8* p) {
  u64 v;
  std::memcpy(&v, p, 8);
  return v;
}

static inline u64 wy_r4(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

static inline u64 wy_r3(const u8* p, usize k) {
  return (static_cast<u64>(p[0]) << 16) | (static_cast<u64>(p[k >> 1]) << 8) | p[k - 1];
}

u64 hash_bytes(const void* data, usize len) {
  static constexpr u64 secret[4] = {0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull,
                                    0x4b33a62ed433d4a3ull, 0x4d5a2da51de1aa47ull};
  const u8* p = static_cast<const u8*>(data);
  u64 seed = 0;
  seed ^= wy_mix(seed ^ secret[0], secret[1]);
  u64 a, b;
  if (len <= 16) {
    if (len >= 4) {
      a = (wy_r4(p) << 32) | wy_r4(p + ((len >> 3) << 2));
      b = (wy_r4(p + len - 4) << 32) | wy_r4(p + len - 4 - ((len >> 3) << 2));
    } else if (len > 0) {
      a = wy_r3(p, len);
      b = 0;
    } else {
      a = b = 0;
    }
  } else {
    usize i = len;
    if (i >= 48) {
      u64 see1 = seed, see2 = seed;
      do {
        seed = wy_mix(wy_r8(p) ^ secret[1], wy_r8(p + 8) ^ seed);
        see1 = wy_mix(wy_r8(p + 16) ^ secret[2], wy_r8(p + 24) ^ see1);
        see2 = wy_mix(wy_r8(p + 32) ^ secret[3], wy_r8(p + 40) ^ see2);
        p += 48;
        i -= 48;
      } while (i >= 48);
      seed ^= see1 ^ see2;
    }
    while (i > 16) {
      seed = wy_mix(wy_r8(p) ^ secret[1], wy_r8(p + 8) ^ seed);
      i -= 16;
      p += 16;
    }
    a = wy_r8(p + i - 16);
    b = wy_r8(p + i - 8);
  }
  a ^= secret[1];
  b ^= seed;
  wy_mum(&a, &b);
  return wy_mix(a ^ secret[0] ^ len, b ^ secret[1]);
}

// ---------------------------------------------------------------------------
// radix_sort
// ---------------------------------------------------------------------------

template <class K>
static void radix_sort_impl(Span<K> keys, Span<u32> order, Allocator& scratch) {
  usize n = keys.size();
  OAT_CHECK(n <= U32_MAX);
  bool with_order = !order.empty();
  OAT_ASSERT_MSG(!with_order || order.size() == n, "radix_sort: order must match keys in length");
  if (with_order)
    for (usize i = 0; i < n; ++i) order[i] = static_cast<u32>(i);
  if (n < 2) return;

  K* key_buf = static_cast<K*>(scratch.alloc(sizeof(K) * n, alignof(K)));
  u32* order_buf =
      with_order ? static_cast<u32*>(scratch.alloc(sizeof(u32) * n, alignof(u32))) : nullptr;

  K* ksrc = keys.data();
  K* kdst = key_buf;
  u32* osrc = order.data();
  u32* odst = order_buf;

  for (u32 pass = 0; pass < sizeof(K); ++pass) {
    u32 shift = pass * 8;
    usize counts[256] = {};
    for (usize i = 0; i < n; ++i) ++counts[(ksrc[i] >> shift) & 0xff];
    if (counts[(ksrc[0] >> shift) & 0xff] == n) continue; // every key has the same byte here

    usize offset = 0;
    for (usize& c : counts) {
      usize count = c;
      c = offset;
      offset += count;
    }
    for (usize i = 0; i < n; ++i) {
      usize d = counts[(ksrc[i] >> shift) & 0xff]++;
      kdst[d] = ksrc[i];
      if (with_order) odst[d] = osrc[i];
    }
    K* kt = ksrc;
    ksrc = kdst;
    kdst = kt;
    u32* ot = osrc;
    osrc = odst;
    odst = ot;
  }

  if (ksrc != keys.data()) {
    std::memcpy(keys.data(), ksrc, sizeof(K) * n);
    if (with_order) std::memcpy(order.data(), osrc, sizeof(u32) * n);
  }
  // Freed in reverse order so an arena gets the space back.
  if (order_buf) scratch.free(order_buf, sizeof(u32) * n);
  scratch.free(key_buf, sizeof(K) * n);
}

void radix_sort(Span<u32> keys, Span<u32> order, Allocator& scratch) {
  radix_sort_impl(keys, order, scratch);
}

void radix_sort(Span<u64> keys, Span<u32> order, Allocator& scratch) {
  radix_sort_impl(keys, order, scratch);
}

// ---------------------------------------------------------------------------
// Rng
// ---------------------------------------------------------------------------

Rng Rng::seeded(u64 seed) {
  // splitmix64 expands the seed, as the xoshiro authors recommend.
  Rng r;
  for (u64& word : r.s) {
    seed += 0x9e3779b97f4a7c15ull;
    word = hash_u64(seed);
  }
  return r;
}

u32 Rng::range(u32 lo, u32 hi) {
  OAT_ASSERT(lo < hi);
  u32 span = hi - lo;
  // Lemire's multiply-shift with rejection, so the result has no modulo bias.
  u64 m = static_cast<u64>(static_cast<u32>(next_u64() >> 32)) * span;
  u32 low = static_cast<u32>(m);
  if (low < span) {
    u32 threshold = (0u - span) % span;
    while (low < threshold) {
      m = static_cast<u64>(static_cast<u32>(next_u64() >> 32)) * span;
      low = static_cast<u32>(m);
    }
  }
  return lo + static_cast<u32>(m >> 32);
}

} // namespace oat
