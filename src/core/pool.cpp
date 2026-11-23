#include <oat/pool.h>

namespace oat {
namespace detail {

u32 pool_add(PoolRaw* p, usize chunk_bytes, usize chunk_align, usize items_offset, Allocator& a) {
  p->items_offset = static_cast<u32>(items_offset);
  u32 index;
  if (p->free_head) {
    index = p->free_head - 1;
    PoolChunk* chunk = p->chunks[index / POOL_CHUNK_SLOTS];
    p->free_head = chunk->next_free[index % POOL_CHUNK_SLOTS];
  } else {
    index = p->slot_count;
    if (index / POOL_CHUNK_SLOTS == p->chunk_count) {
      if (p->chunk_count == p->chunk_cap) {
        p->chunks = static_cast<PoolChunk**>(
            array_grow(p->chunks, &p->chunk_cap, static_cast<u64>(p->chunk_count) + 1,
                       sizeof(PoolChunk*), alignof(PoolChunk*), a));
      }
      if (static_cast<u64>(p->slot_count) + POOL_CHUNK_SLOTS > U32_MAX)
        assert_fail("oat Pool exceeds 2^32 - 1 slots", __FILE__, __LINE__);
      PoolChunk* chunk = static_cast<PoolChunk*>(a.alloc(chunk_bytes, chunk_align));
      chunk->live = 0;
      for (u32 i = 0; i < POOL_CHUNK_SLOTS; ++i) {
        chunk->generation[i] = 1;
        chunk->next_free[i] = 0;
      }
      p->chunks[p->chunk_count++] = chunk;
    }
    ++p->slot_count;
  }
  p->chunks[index / POOL_CHUNK_SLOTS]->live |= u64{1} << (index % POOL_CHUNK_SLOTS);
  ++p->len;
  return index;
}

void pool_remove(PoolRaw* p, u32 index) {
  PoolChunk* chunk = p->chunks[index / POOL_CHUNK_SLOTS];
  u32 slot = index % POOL_CHUNK_SLOTS;
  OAT_ASSERT(chunk->live & (u64{1} << slot));
  chunk->live &= ~(u64{1} << slot);
  // Bumping the generation is what makes outstanding handles stale. Skip 0,
  // which is reserved for the null handle.
  if (++chunk->generation[slot] == 0) chunk->generation[slot] = 1;
  chunk->next_free[slot] = p->free_head;
  p->free_head = index + 1;
  --p->len;
}

void pool_free(PoolRaw* p, usize chunk_bytes, Allocator& a) {
  for (u32 i = 0; i < p->chunk_count; ++i) a.free(p->chunks[i], chunk_bytes);
  a.free(p->chunks, sizeof(PoolChunk*) * static_cast<usize>(p->chunk_cap));
  *p = PoolRaw{};
}

u32 pool_next_live(const PoolRaw* p, u32 from) {
  while (from < p->slot_count) {
    u32 slot = from % POOL_CHUNK_SLOTS;
    u64 bits = p->chunks[from / POOL_CHUNK_SLOTS]->live >> slot;
    if (bits) {
      u32 index = from + static_cast<u32>(std::countr_zero(bits));
      return index < p->slot_count ? index : p->slot_count;
    }
    from += POOL_CHUNK_SLOTS - slot;
  }
  return p->slot_count;
}

} // namespace detail
} // namespace oat
