// oat/algo.h - hashing, sorting, binary search, random numbers.
#pragma once

#include "base.h"
#include "memory.h"

namespace oat {

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

u64 hash_bytes(const void* data, usize size);

// A full-avalanche mix of one 64-bit value (the splitmix64 finalizer).
constexpr u64 hash_u64(u64 x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ull;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebull;
  x ^= x >> 31;
  return x;
}

constexpr u64 hash_combine(u64 seed, u64 value) {
  return hash_u64(seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2)));
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

namespace detail {

template <class T, class Less>
void insertion_sort(T* a, usize n, Less& less) {
  for (usize i = 1; i < n; ++i) {
    T v = a[i];
    usize j = i;
    while (j > 0 && less(v, a[j - 1])) {
      a[j] = a[j - 1];
      --j;
    }
    a[j] = v;
  }
}

template <class T, class Less>
void sift_down(T* a, usize root, usize n, Less& less) {
  T v = a[root];
  for (;;) {
    usize child = 2 * root + 1;
    if (child >= n) break;
    if (child + 1 < n && less(a[child], a[child + 1])) ++child;
    if (!less(v, a[child])) break;
    a[root] = a[child];
    root = child;
  }
  a[root] = v;
}

template <class T, class Less>
void heap_sort(T* a, usize n, Less& less) {
  for (usize i = n / 2; i-- > 0;) sift_down(a, i, n, less);
  for (usize end = n; end-- > 1;) {
    oat::swap(a[0], a[end]);
    sift_down(a, 0, end, less);
  }
}

// Quicksort with a median-of-three pivot, insertion sort for small ranges,
// and a heapsort fallback when the recursion gets too deep.
template <class T, class Less>
void intro_sort(T* a, usize n, u32 depth, Less& less) {
  while (n > 24) {
    if (depth == 0) {
      heap_sort(a, n, less);
      return;
    }
    --depth;

    usize mid = n / 2;
    if (less(a[mid], a[0])) oat::swap(a[mid], a[0]);
    if (less(a[n - 1], a[0])) oat::swap(a[n - 1], a[0]);
    if (less(a[n - 1], a[mid])) oat::swap(a[n - 1], a[mid]);
    T pivot = a[mid];

    isize i = 0;
    isize j = static_cast<isize>(n) - 1;
    while (i <= j) {
      while (less(a[i], pivot)) ++i;
      while (less(pivot, a[j])) --j;
      if (i <= j) {
        oat::swap(a[i], a[j]);
        ++i;
        --j;
      }
    }

    // Recurse into the smaller side, loop on the larger.
    usize left_n = static_cast<usize>(j + 1);
    usize right_n = n - static_cast<usize>(i);
    if (left_n < right_n) {
      intro_sort(a, left_n, depth, less);
      a += i;
      n = right_n;
    } else {
      intro_sort(a + i, right_n, depth, less);
      n = left_n;
    }
  }
  insertion_sort(a, n, less);
}

} // namespace detail

// Introsort. Not stable. `less(a, b)` returns true when a orders before b.
template <class T, class Less>
void sort(Span<T> items, Less less) {
  usize n = items.size();
  if (n < 2) return;
  u32 depth = 2 * static_cast<u32>(std::bit_width(n));
  detail::intro_sort(items.data(), n, depth, less);
}

// Merge sort. Stable. Borrows room for items.size() elements from `scratch`
// and returns it before the call ends.
template <class T, class Less>
void sort_stable(Span<T> items, Less less, Allocator& scratch) {
  constexpr usize RUN = 16;
  usize n = items.size();
  T* a = items.data();
  for (usize lo = 0; lo < n; lo += RUN)
    detail::insertion_sort(a + lo, n - lo < RUN ? n - lo : RUN, less);
  if (n <= RUN) return;

  T* buf = static_cast<T*>(scratch.alloc(sizeof(T) * n, alignof(T)));
  T* src = a;
  T* dst = buf;
  for (usize width = RUN; width < n; width *= 2) {
    for (usize lo = 0; lo < n; lo += 2 * width) {
      usize mid = lo + width < n ? lo + width : n;
      usize hi = lo + 2 * width < n ? lo + 2 * width : n;
      usize i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        // Take from the right run only when it is strictly smaller.
        if (less(src[j], src[i])) ::new (static_cast<void*>(dst + k++)) T(src[j++]);
        else ::new (static_cast<void*>(dst + k++)) T(src[i++]);
      }
      while (i < mid) ::new (static_cast<void*>(dst + k++)) T(src[i++]);
      while (j < hi) ::new (static_cast<void*>(dst + k++)) T(src[j++]);
    }
    T* t = src;
    src = dst;
    dst = t;
  }
  if (src != a) std::memcpy(static_cast<void*>(a), static_cast<const void*>(src), sizeof(T) * n);
  scratch.free(buf, sizeof(T) * n);
}

// LSD radix sort. Sorts `keys` ascending in place. If `order` is not empty it
// must be the same length as `keys`; on return order[i] is the original index
// of the key now at position i, so it can be used to permute a parallel array.
// Stable. `scratch` lends a temporary copy of both spans.
void radix_sort(Span<u32> keys, Span<u32> order, Allocator& scratch);
void radix_sort(Span<u64> keys, Span<u32> order, Allocator& scratch);

// ---------------------------------------------------------------------------
// Binary search over a sorted span. T may be const.
// ---------------------------------------------------------------------------

// Index of the first element that does not order before `key`:
// `less(element, key)` is false.
template <class T, class K, class Less>
u32 lower_bound(Span<T> items, const K& key, Less less) {
  usize lo = 0;
  usize n = items.size();
  while (n > 0) {
    usize half = n / 2;
    if (less(items[lo + half], key)) {
      lo += half + 1;
      n -= half + 1;
    } else {
      n = half;
    }
  }
  return static_cast<u32>(lo);
}

// Index of the first element that orders after `key`: `less(key, element)` is true.
template <class T, class K, class Less>
u32 upper_bound(Span<T> items, const K& key, Less less) {
  usize lo = 0;
  usize n = items.size();
  while (n > 0) {
    usize half = n / 2;
    if (!less(key, items[lo + half])) {
      lo += half + 1;
      n -= half + 1;
    } else {
      n = half;
    }
  }
  return static_cast<u32>(lo);
}

// ---------------------------------------------------------------------------
// Rng: xoshiro256**. Fast and good enough for gameplay; not for cryptography.
// ---------------------------------------------------------------------------

struct Rng {
  u64 s[4];

  static Rng seeded(u64 seed);

  u64 next_u64() {
    u64 result = rotl(s[1] * 5, 7) * 9;
    u64 t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
  }

  // Uniform in [lo, hi). Requires lo < hi.
  u32 range(u32 lo, u32 hi);

  // Uniform in [0, 1).
  f32 next_f32() { return static_cast<f32>(next_u64() >> 40) * (1.0f / 16777216.0f); }

 private:
  static constexpr u64 rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }
};

} // namespace oat
