#include <oat/array.h>

namespace oat {
namespace detail {

void* array_set_capacity(void* data, u32* cap, u32 new_cap, usize elem_size, usize align,
                         Allocator& a) {
  void* p = a.resize(data, elem_size * static_cast<usize>(*cap),
                     elem_size * static_cast<usize>(new_cap), align);
  *cap = new_cap;
  return p;
}

void* array_grow(void* data, u32* cap, u64 min_cap, usize elem_size, usize align, Allocator& a) {
  // Length and capacity are 32-bit.
  if (min_cap > U32_MAX) assert_fail("oat container exceeds 2^32 - 1 elements", __FILE__, __LINE__);
  u64 new_cap = static_cast<u64>(*cap) * 2;
  if (new_cap < 8) new_cap = 8;
  if (new_cap < min_cap) new_cap = min_cap;
  if (new_cap > U32_MAX) new_cap = U32_MAX;
  return array_set_capacity(data, cap, static_cast<u32>(new_cap), elem_size, align, a);
}

u32 bits_count(const u64* words, u32 bit_count) {
  u32 n = 0;
  u32 full = bit_count >> 6;
  for (u32 i = 0; i < full; ++i) n += static_cast<u32>(std::popcount(words[i]));
  u32 rest = bit_count & 63;
  if (rest) n += static_cast<u32>(std::popcount(words[full] & ((u64{1} << rest) - 1)));
  return n;
}

u32 bits_find_set(const u64* words, u32 bit_count, u32 from) {
  if (from >= bit_count) return U32_MAX;
  u32 w = from >> 6;
  u32 word_count = (bit_count + 63) >> 6;
  u64 bits = words[w] & (~u64{0} << (from & 63));
  for (;;) {
    if (bits) {
      u32 i = (w << 6) + static_cast<u32>(std::countr_zero(bits));
      return i < bit_count ? i : U32_MAX;
    }
    if (++w == word_count) return U32_MAX;
    bits = words[w];
  }
}

} // namespace detail

void BitSet::resize(u32 bits, Allocator& a) {
  u32 need = static_cast<u32>((static_cast<u64>(bits) + 63) >> 6);
  if (need > word_cap) {
    u32 old_cap = word_cap;
    words = static_cast<u64*>(
        detail::array_grow(words, &word_cap, need, sizeof(u64), alignof(u64), a));
    std::memset(words + old_cap, 0, sizeof(u64) * (word_cap - old_cap));
  }
  if (bits < bit_count) {
    // Clear everything past the new end so growing again exposes zeros.
    u32 old_words = (bit_count + 63) >> 6;
    if (bits & 63) words[bits >> 6] &= (u64{1} << (bits & 63)) - 1;
    for (u32 w = (bits + 63) >> 6; w < old_words; ++w) words[w] = 0;
  }
  bit_count = bits;
}

void BitSet::free(Allocator& a) {
  a.free(words, sizeof(u64) * static_cast<usize>(word_cap));
  *this = BitSet{};
}

void BitSet::set_all() {
  u32 word_count = (bit_count + 63) >> 6;
  for (u32 w = 0; w < word_count; ++w) words[w] = ~u64{0};
  // Bits past the end stay zero.
  if (bit_count & 63) words[word_count - 1] = (u64{1} << (bit_count & 63)) - 1;
}

void BitSet::clear_all() {
  u32 word_count = (bit_count + 63) >> 6;
  if (word_count) std::memset(words, 0, sizeof(u64) * word_count);
}

} // namespace oat
