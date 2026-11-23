#include <oat/algo.h>

#if defined(_MSC_VER) && !defined(__SIZEOF_INT128__)
#  include <intrin.h>
#endif

namespace oat {

// ---------------------------------------------------------------------------
// hash_bytes
//
// A multiply-and-fold hash written for oat. It uses the same building block as
// wyhash (a 64x64 -> 128 bit multiply whose halves are xored together), which
// its authors placed in the public domain. Output is not stable across
// versions or byte orders: do not write it to disk.
// ---------------------------------------------------------------------------

static inline u64 hash_mum(u64 a, u64 b) {
#if defined(__SIZEOF_INT128__)
  __uint128_t r = static_cast<__uint128_t>(a) * b;
  return static_cast<u64>(r) ^ static_cast<u64>(r >> 64);
#elif defined(_M_X64)
  u64 hi;
  u64 lo = _umul128(a, b, &hi);
  return lo ^ hi;
#elif defined(_M_ARM64)
  return (a * b) ^ __umulh(a, b);
#else
#  error "oat::hash_bytes needs a 64x64->128 multiply on this target"
#endif
}

static inline u64 hash_read64(const u8* p) {
  u64 v;
  std::memcpy(&v, p, 8);
  return v;
}

static inline u64 hash_read32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}

u64 hash_bytes(const void* data, usize size) {
  constexpr u64 K0 = 0xa0761d6478bd642full;
  constexpr u64 K1 = 0xe7037ed1a0b428dbull;
  constexpr u64 K2 = 0x8ebc6af09c88c6e3ull;

  const u8* p = static_cast<const u8*>(data);
  u64 h = K0;
  u64 a = 0;
  u64 b = 0;

  if (size <= 16) {
    if (size >= 8) {
      a = hash_read64(p);
      b = hash_read64(p + size - 8);
    } else if (size >= 4) {
      a = hash_read32(p);
      b = hash_read32(p + size - 4);
    } else if (size > 0) {
      a = (static_cast<u64>(p[0]) << 16) | (static_cast<u64>(p[size >> 1]) << 8) | p[size - 1];
    }
  } else {
    const u8* end = p + size;
    usize left = size;
    while (left > 16) {
      h = hash_mum(hash_read64(p) ^ K1, hash_read64(p + 8) ^ h);
      p += 16;
      left -= 16;
    }
    // The last 16 bytes, overlapping what the loop already consumed.
    a = hash_read64(end - 16);
    b = hash_read64(end - 8);
  }
  return hash_mum(K2 ^ size, hash_mum(a ^ K1, b ^ h));
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
