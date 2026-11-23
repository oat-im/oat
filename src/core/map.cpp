#include <oat/map.h>

namespace oat {
namespace detail {

static constexpr u32 MAP_MIN_CAP = 8;

static usize map_hashes_offset(u32 cap, usize entry_size) {
  return (entry_size * static_cast<usize>(cap) + (alignof(u32) - 1)) & ~(alignof(u32) - 1);
}

static usize map_alloc_size(u32 cap, usize entry_size) {
  return map_hashes_offset(cap, entry_size) + sizeof(u32) * static_cast<usize>(cap);
}

static void map_rehash(MapRaw* m, u32 new_cap, usize entry_size, usize entry_align, Allocator& a) {
  OAT_ASSERT(std::has_single_bit(new_cap));
  usize align = entry_align > alignof(u32) ? entry_align : alignof(u32);
  u8* block = static_cast<u8*>(a.alloc(map_alloc_size(new_cap, entry_size), align));
  u8* new_entries = block;
  u32* new_hashes = reinterpret_cast<u32*>(block + map_hashes_offset(new_cap, entry_size));
  std::memset(new_hashes, 0, sizeof(u32) * static_cast<usize>(new_cap));

  const u8* old_entries = static_cast<const u8*>(m->entries);
  u32 mask = new_cap - 1;
  for (u32 i = 0; i < m->cap; ++i) {
    u32 h = m->hashes[i];
    if (h == 0) continue;
    u32 slot = h & mask;
    while (new_hashes[slot] != 0) slot = (slot + 1) & mask;
    new_hashes[slot] = h;
    std::memcpy(new_entries + entry_size * slot, old_entries + entry_size * i, entry_size);
  }

  a.free(m->entries, map_alloc_size(m->cap, entry_size));
  m->entries = new_entries;
  m->hashes = new_hashes;
  m->cap = new_cap;
}

void map_grow(MapRaw* m, usize entry_size, usize entry_align, Allocator& a) {
  if (m->cap >= 0x80000000u) assert_fail("oat Map exceeds 2^31 slots", __FILE__, __LINE__);
  map_rehash(m, m->cap ? m->cap * 2 : MAP_MIN_CAP, entry_size, entry_align, a);
}

void map_reserve(MapRaw* m, u32 count, usize entry_size, usize entry_align, Allocator& a) {
  // Smallest power of two that keeps `count` entries at or under 75% load.
  u64 need = (static_cast<u64>(count) * 4 + 2) / 3;
  if (need < MAP_MIN_CAP) need = MAP_MIN_CAP;
  need = std::bit_ceil(need);
  if (need > 0x80000000u) assert_fail("oat Map exceeds 2^31 slots", __FILE__, __LINE__);
  if (need > m->cap) map_rehash(m, static_cast<u32>(need), entry_size, entry_align, a);
}

// Backward-shift deletion: close the hole by pulling later entries of the
// probe run back, so no tombstone is needed.
void map_remove_slot(MapRaw* m, u32 slot, usize entry_size) {
  u8* entries = static_cast<u8*>(m->entries);
  u32 mask = m->cap - 1;
  u32 hole = slot;
  for (u32 j = (hole + 1) & mask;; j = (j + 1) & mask) {
    u32 h = m->hashes[j];
    if (h == 0) break;
    // An entry may move into the hole only if the hole is not before its home
    // slot, measured along the probe direction.
    u32 dist_from_home = (j - (h & mask)) & mask;
    u32 dist_from_hole = (j - hole) & mask;
    if (dist_from_home >= dist_from_hole) {
      std::memcpy(entries + entry_size * hole, entries + entry_size * j, entry_size);
      m->hashes[hole] = h;
      hole = j;
    }
  }
  m->hashes[hole] = 0;
  --m->len;
}

void map_clear(MapRaw* m) {
  if (m->cap) std::memset(m->hashes, 0, sizeof(u32) * static_cast<usize>(m->cap));
  m->len = 0;
}

void map_free(MapRaw* m, usize entry_size, Allocator& a) {
  a.free(m->entries, map_alloc_size(m->cap, entry_size));
  *m = MapRaw{};
}

} // namespace detail
} // namespace oat
