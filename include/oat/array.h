// oat/array.h - Array, FixedArray, RingBuffer, BitSet, FixedBitSet.
#pragma once

#include "base.h"
#include "memory.h"

namespace oat {

namespace detail {

// Non-template growth. Returns the new storage and updates *cap.
void* array_grow(void* data, u32* cap, u64 min_cap, usize elem_size, usize align, Allocator& a);
void* array_set_capacity(void* data, u32* cap, u32 new_cap, usize elem_size, usize align,
                         Allocator& a);

inline bool points_into(const void* p, const void* data, usize bytes) {
  usize a = reinterpret_cast<usize>(p);
  usize b = reinterpret_cast<usize>(data);
  return a >= b && a < b + bytes;
}

} // namespace detail

#define OAT_ELEMENT_CHECKS(T)                                                                      \
  static_assert(::oat::is_relocatable<T>,                                                          \
                "oat container elements must be movable with memcpy (see oat::is_relocatable)");   \
  static_assert(OAT_IS_TRIVIALLY_DESTRUCTIBLE(T),                                                  \
                "oat containers never run element destructors; give the type an explicit destroy()")

// ---------------------------------------------------------------------------
// Array: a growable array. 16 bytes; the allocator is passed to every call
// that can grow. Growth moves the elements, so never hold a pointer or
// reference across a call that takes an Allocator.
// ---------------------------------------------------------------------------

template <class T>
struct Array {
  T* data = nullptr;
  u32 len = 0;
  u32 cap = 0;

  T& push(const T& v, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    OAT_ASSERT_MSG(!aliases(&v), "pushing an element of an Array into itself");
    if (len == cap) grow(static_cast<u64>(len) + 1, a);
    T* p = ::new (static_cast<void*>(data + len)) T(v);
    ++len;
    return *p;
  }

  // Appends n elements without initializing them and returns the first.
  T* push_uninitialized(u32 n, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    u64 need = static_cast<u64>(len) + n;
    if (need > cap) grow(need, a);
    T* p = data + len;
    len = static_cast<u32>(need);
    return p;
  }

  void push_many(Span<const T> items, Allocator& a) {
    OAT_ASSERT_MSG(items.empty() || !aliases(items.data()), "pushing an Array into itself");
    T* dst = push_uninitialized(static_cast<u32>(items.size()), a);
    if (!items.empty()) std::memcpy(static_cast<void*>(dst), items.data(), items.size_bytes());
  }

  T pop() {
    OAT_ASSERT(len > 0);
    --len;
    return data[len];
  }

  void insert(u32 i, const T& v, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    OAT_ASSERT(i <= len);
    OAT_ASSERT_MSG(!aliases(&v), "inserting an element of an Array into itself");
    if (len == cap) grow(static_cast<u64>(len) + 1, a);
    std::memmove(static_cast<void*>(data + i + 1), static_cast<const void*>(data + i),
                 sizeof(T) * (len - i));
    ::new (static_cast<void*>(data + i)) T(v);
    ++len;
  }

  // Keeps order; moves every later element down by one.
  void remove_ordered(u32 i) {
    OAT_ELEMENT_CHECKS(T);
    OAT_ASSERT(i < len);
    std::memmove(static_cast<void*>(data + i), static_cast<const void*>(data + i + 1),
                 sizeof(T) * (len - i - 1));
    --len;
  }

  // Moves the last element into the hole.
  void remove_swap(u32 i) {
    OAT_ELEMENT_CHECKS(T);
    OAT_ASSERT(i < len);
    --len;
    if (i != len)
      std::memcpy(static_cast<void*>(data + i), static_cast<const void*>(data + len), sizeof(T));
  }

  void reserve(u32 new_cap, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    if (new_cap > cap)
      data = static_cast<T*>(
          detail::array_set_capacity(data, &cap, new_cap, sizeof(T), alignof(T), a));
  }

  // New elements are zero-filled.
  void resize(u32 new_len, Allocator& a) {
    u32 old_len = len;
    resize_uninitialized(new_len, a);
    if (new_len > old_len)
      std::memset(static_cast<void*>(data + old_len), 0, sizeof(T) * (new_len - old_len));
  }

  void resize_uninitialized(u32 new_len, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    if (new_len > cap) grow(new_len, a);
    len = new_len;
  }

  void clear() { len = 0; }

  void free(Allocator& a) {
    a.free(data, sizeof(T) * static_cast<usize>(cap));
    data = nullptr;
    len = 0;
    cap = 0;
  }

  T& operator[](u32 i) {
    OAT_ASSERT(i < len);
    return data[i];
  }
  const T& operator[](u32 i) const {
    OAT_ASSERT(i < len);
    return data[i];
  }

  T& last() {
    OAT_ASSERT(len > 0);
    return data[len - 1];
  }
  const T& last() const {
    OAT_ASSERT(len > 0);
    return data[len - 1];
  }

  bool is_empty() const { return len == 0; }

  T* begin() { return data; }
  T* end() { return data + len; }
  const T* begin() const { return data; }
  const T* end() const { return data + len; }

  Span<T> span() { return Span<T>(data, len); }
  Span<const T> span() const { return Span<const T>(data, len); }

 private:
  bool aliases(const void* p) const {
    return detail::points_into(p, data, sizeof(T) * static_cast<usize>(cap));
  }
  void grow(u64 min_cap, Allocator& a) {
    data = static_cast<T*>(detail::array_grow(data, &cap, min_cap, sizeof(T), alignof(T), a));
  }
};

// ---------------------------------------------------------------------------
// FixedArray: inline storage for up to N elements, no allocator.
// ---------------------------------------------------------------------------

template <class T, u32 N>
struct FixedArray {
  alignas(T) unsigned char storage[sizeof(T) * N];
  u32 len = 0;

  T* data() { return reinterpret_cast<T*>(storage); }
  const T* data() const { return reinterpret_cast<const T*>(storage); }

  static constexpr u32 capacity() { return N; }
  bool is_empty() const { return len == 0; }
  bool is_full() const { return len == N; }

  T& push(const T& v) {
    OAT_ELEMENT_CHECKS(T);
    OAT_CHECK(len < N);
    T* p = ::new (static_cast<void*>(data() + len)) T(v);
    ++len;
    return *p;
  }

  T* push_uninitialized(u32 n) {
    OAT_ELEMENT_CHECKS(T);
    OAT_CHECK(static_cast<u64>(len) + n <= N);
    T* p = data() + len;
    len += n;
    return p;
  }

  T pop() {
    OAT_ASSERT(len > 0);
    --len;
    return data()[len];
  }

  void insert(u32 i, const T& v) {
    OAT_ELEMENT_CHECKS(T);
    OAT_ASSERT(i <= len);
    OAT_CHECK(len < N);
    T copy = v; // v may be one of the elements about to move
    std::memmove(static_cast<void*>(data() + i + 1), static_cast<const void*>(data() + i),
                 sizeof(T) * (len - i));
    ::new (static_cast<void*>(data() + i)) T(copy);
    ++len;
  }

  void remove_ordered(u32 i) {
    OAT_ASSERT(i < len);
    std::memmove(static_cast<void*>(data() + i), static_cast<const void*>(data() + i + 1),
                 sizeof(T) * (len - i - 1));
    --len;
  }

  void remove_swap(u32 i) {
    OAT_ASSERT(i < len);
    --len;
    if (i != len)
      std::memcpy(static_cast<void*>(data() + i), static_cast<const void*>(data() + len),
                  sizeof(T));
  }

  void resize(u32 new_len) {
    u32 old_len = len;
    resize_uninitialized(new_len);
    if (new_len > old_len)
      std::memset(static_cast<void*>(data() + old_len), 0, sizeof(T) * (new_len - old_len));
  }

  void resize_uninitialized(u32 new_len) {
    OAT_ELEMENT_CHECKS(T);
    OAT_CHECK(new_len <= N);
    len = new_len;
  }

  void clear() { len = 0; }

  T& operator[](u32 i) {
    OAT_ASSERT(i < len);
    return data()[i];
  }
  const T& operator[](u32 i) const {
    OAT_ASSERT(i < len);
    return data()[i];
  }

  T& last() {
    OAT_ASSERT(len > 0);
    return data()[len - 1];
  }
  const T& last() const {
    OAT_ASSERT(len > 0);
    return data()[len - 1];
  }

  T* begin() { return data(); }
  T* end() { return data() + len; }
  const T* begin() const { return data(); }
  const T* end() const { return data() + len; }

  Span<T> span() { return Span<T>(data(), len); }
  Span<const T> span() const { return Span<const T>(data(), len); }
};

// ---------------------------------------------------------------------------
// RingBuffer: a FIFO with a capacity fixed at creation.
// ---------------------------------------------------------------------------

template <class T>
struct RingBuffer {
  T* data = nullptr;
  u32 cap = 0;
  u32 head = 0; // index of the oldest element
  u32 len = 0;

  static RingBuffer create(u32 capacity, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    RingBuffer r;
    r.data = alloc_array<T>(a, capacity);
    r.cap = capacity;
    return r;
  }

  void free(Allocator& a) {
    a.free(data, sizeof(T) * static_cast<usize>(cap));
    *this = RingBuffer{};
  }

  bool is_empty() const { return len == 0; }
  bool is_full() const { return len == cap; }

  // False, and nothing stored, when the buffer is full.
  bool push_back(const T& v) {
    if (len == cap) return false;
    ::new (static_cast<void*>(data + wrap(head + len))) T(v);
    ++len;
    return true;
  }

  // Drops the oldest element to make room when the buffer is full.
  void push_back_overwrite(const T& v) {
    OAT_ASSERT(cap > 0);
    if (len == cap) pop_front();
    push_back(v);
  }

  T pop_front() {
    OAT_ASSERT(len > 0);
    T* p = data + head;
    head = wrap(head + 1);
    --len;
    return *p;
  }

  T pop_back() {
    OAT_ASSERT(len > 0);
    --len;
    return data[wrap(head + len)];
  }

  T& front() {
    OAT_ASSERT(len > 0);
    return data[head];
  }
  T& back() {
    OAT_ASSERT(len > 0);
    return data[wrap(head + len - 1)];
  }

  // Index 0 is the oldest element.
  T& operator[](u32 i) {
    OAT_ASSERT(i < len);
    return data[wrap(head + i)];
  }
  const T& operator[](u32 i) const {
    OAT_ASSERT(i < len);
    return data[wrap(head + i)];
  }

  void clear() {
    head = 0;
    len = 0;
  }

  struct Iter {
    RingBuffer* ring;
    u32 i;
    T& operator*() const { return (*ring)[i]; }
    Iter& operator++() {
      ++i;
      return *this;
    }
    bool operator!=(const Iter& o) const { return i != o.i; }
  };
  Iter begin() { return Iter{this, 0}; }
  Iter end() { return Iter{this, len}; }

 private:
  // head < cap and the offset is at most cap, so one subtraction is enough;
  // the sum is done in 64 bits so it cannot wrap.
  u32 wrap(u64 i) const { return static_cast<u32>(i >= cap ? i - cap : i); }
};

// ---------------------------------------------------------------------------
// Bit sets
// ---------------------------------------------------------------------------

namespace detail {
u32 bits_count(const u64* words, u32 bit_count);
// Index of the first set bit at or after `from`, or U32_MAX.
u32 bits_find_set(const u64* words, u32 bit_count, u32 from);
} // namespace detail

struct BitSet {
  u64* words = nullptr;
  u32 bit_count = 0;
  u32 word_cap = 0;

  // New bits are zero.
  void resize(u32 bits, Allocator& a);
  void free(Allocator& a);

  u32 len() const { return bit_count; }

  bool test(u32 i) const {
    OAT_ASSERT(i < bit_count);
    return (words[i >> 6] >> (i & 63)) & 1;
  }
  void set(u32 i) {
    OAT_ASSERT(i < bit_count);
    words[i >> 6] |= u64{1} << (i & 63);
  }
  void unset(u32 i) {
    OAT_ASSERT(i < bit_count);
    words[i >> 6] &= ~(u64{1} << (i & 63));
  }
  void assign(u32 i, bool on) { on ? set(i) : unset(i); }
  void toggle(u32 i) {
    OAT_ASSERT(i < bit_count);
    words[i >> 6] ^= u64{1} << (i & 63);
  }

  void set_all();
  void clear_all();
  u32 count() const { return detail::bits_count(words, bit_count); }
  u32 find_set(u32 from = 0) const { return detail::bits_find_set(words, bit_count, from); }
};

template <u32 N>
struct FixedBitSet {
  u64 words[(N + 63) / 64] = {};

  static constexpr u32 len() { return N; }

  bool test(u32 i) const {
    OAT_ASSERT(i < N);
    return (words[i >> 6] >> (i & 63)) & 1;
  }
  void set(u32 i) {
    OAT_ASSERT(i < N);
    words[i >> 6] |= u64{1} << (i & 63);
  }
  void unset(u32 i) {
    OAT_ASSERT(i < N);
    words[i >> 6] &= ~(u64{1} << (i & 63));
  }
  void assign(u32 i, bool on) { on ? set(i) : unset(i); }
  void toggle(u32 i) {
    OAT_ASSERT(i < N);
    words[i >> 6] ^= u64{1} << (i & 63);
  }

  void set_all() {
    for (u32 i = 0; i < N; ++i) set(i);
  }
  void clear_all() { std::memset(words, 0, sizeof(words)); }
  u32 count() const { return detail::bits_count(words, N); }
  u32 find_set(u32 from = 0) const { return detail::bits_find_set(words, N, from); }
};

} // namespace oat
