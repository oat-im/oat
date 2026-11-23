// oat/map.h - Map and Set: flat open-addressing hash tables.
#pragma once

#include "algo.h"
#include "array.h"
#include "base.h"
#include "memory.h"

namespace oat {

// The one customization point for keys: hash() and eq().
//
// The default treats the key as raw bytes, so the key type must have no
// padding and no floating-point members (both make equal values differ, or
// different values compare equal, byte for byte). Specialize KeyTraits for a
// key that needs something else.
template <class K>
struct KeyTraits {
  static u64 hash(const K& key) {
    static_assert(__has_unique_object_representations(K),
                  "raw-byte hash keys must have no padding and no floats; specialize "
                  "oat::KeyTraits for this key type");
    if (sizeof(K) <= sizeof(u64)) {
      u64 bits = 0;
      std::memcpy(&bits, &key, sizeof(K));
      return hash_u64(bits);
    }
    return hash_bytes(&key, sizeof(K));
  }
  static bool eq(const K& a, const K& b) { return std::memcmp(&a, &b, sizeof(K)) == 0; }
};

// string_view keys hash and compare their characters. The map stores the view,
// not the characters: use interned or arena strings that outlive the map.
template <>
struct KeyTraits<Str> {
  static u64 hash(Str key) { return hash_bytes(key.data(), key.size()); }
  static bool eq(Str a, Str b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
  }
};

namespace detail {

// One allocation holds `cap` entries followed by `cap` 32-bit hashes.
// A stored hash of 0 marks an empty slot.
struct MapRaw {
  void* entries = nullptr;
  u32* hashes = nullptr;
  u32 len = 0;
  u32 cap = 0; // zero or a power of two
};

void map_grow(MapRaw* m, usize entry_size, usize entry_align, Allocator& a);
void map_reserve(MapRaw* m, u32 count, usize entry_size, usize entry_align, Allocator& a);
void map_remove_slot(MapRaw* m, u32 slot, usize entry_size);
void map_clear(MapRaw* m);
void map_free(MapRaw* m, usize entry_size, Allocator& a);

struct Unit {};

} // namespace detail

template <class K, class V>
struct MapEntry {
  K key;
  OAT_NO_UNIQUE_ADDRESS V value;
};

// Open addressing, linear probing, power-of-two capacity, at most 75% full,
// backward-shift deletion. No pointer stability: a pointer or reference into
// the map is invalid after any insert or remove. Do not insert or remove
// while iterating.
template <class K, class V>
struct Map {
  using Entry = MapEntry<K, V>;

  detail::MapRaw raw;

  V* find(const K& key) {
    u32 slot = find_slot(key);
    return slot == U32_MAX ? nullptr : &entries()[slot].value;
  }
  const V* find(const K& key) const {
    u32 slot = find_slot(key);
    return slot == U32_MAX ? nullptr : &entries()[slot].value;
  }

  bool contains(const K& key) const { return find_slot(key) != U32_MAX; }

  // The value is zero-filled if the key was not present.
  V& get_or_insert(const K& key, Allocator& a) {
    bool added;
    Entry& e = add(key, a, &added);
    if constexpr (!__is_empty(V)) {
      if (added) std::memset(static_cast<void*>(&e.value), 0, sizeof(V));
    }
    return e.value;
  }

  // Stores the value, replacing any existing one. True if the key was new.
  bool insert(const K& key, const V& value, Allocator& a) {
    OAT_ASSERT_MSG(!aliases(&value), "inserting a value of a Map into itself");
    bool added;
    Entry& e = add(key, a, &added);
    if constexpr (!__is_empty(V)) {
      ::new (static_cast<void*>(&e.value)) V(value);
    }
    return added;
  }

  bool remove(const K& key) {
    u32 slot = find_slot(key);
    if (slot == U32_MAX) return false;
    detail::map_remove_slot(&raw, slot, sizeof(Entry));
    return true;
  }

  // Makes room for `count` entries in total without further growth.
  void reserve(u32 count, Allocator& a) {
    element_checks();
    detail::map_reserve(&raw, count, sizeof(Entry), alignof(Entry), a);
  }

  void clear() { detail::map_clear(&raw); }
  void free(Allocator& a) { detail::map_free(&raw, sizeof(Entry), a); }

  u32 len() const { return raw.len; }
  bool is_empty() const { return raw.len == 0; }

  template <class E>
  struct IterT {
    E* entries;
    const u32* hashes;
    u32 i;
    u32 cap;
    E& operator*() const { return entries[i]; }
    IterT& operator++() {
      ++i;
      skip();
      return *this;
    }
    bool operator!=(const IterT& o) const { return i != o.i; }
    void skip() {
      while (i < cap && hashes[i] == 0) ++i;
    }
  };
  using Iter = IterT<Entry>;
  using ConstIter = IterT<const Entry>;

  Iter begin() {
    Iter it{entries(), raw.hashes, 0, raw.cap};
    it.skip();
    return it;
  }
  Iter end() { return Iter{entries(), raw.hashes, raw.cap, raw.cap}; }
  ConstIter begin() const {
    ConstIter it{entries(), raw.hashes, 0, raw.cap};
    it.skip();
    return it;
  }
  ConstIter end() const { return ConstIter{entries(), raw.hashes, raw.cap, raw.cap}; }

 private:
  static void element_checks() {
    OAT_ELEMENT_CHECKS(K);
    OAT_ELEMENT_CHECKS(V);
  }

  Entry* entries() { return static_cast<Entry*>(raw.entries); }
  const Entry* entries() const { return static_cast<const Entry*>(raw.entries); }

  bool aliases(const void* p) const {
    return detail::points_into(p, raw.entries, sizeof(Entry) * static_cast<usize>(raw.cap));
  }

  static u32 hash_of(const K& key) {
    u64 h = KeyTraits<K>::hash(key);
    u32 folded = static_cast<u32>(h ^ (h >> 32));
    return folded ? folded : 1;
  }

  u32 find_slot(const K& key) const {
    element_checks();
    if (raw.len == 0) return U32_MAX;
    u32 h = hash_of(key);
    u32 mask = raw.cap - 1;
    const Entry* e = entries();
    for (u32 i = h & mask;; i = (i + 1) & mask) {
      u32 stored = raw.hashes[i];
      if (stored == 0) return U32_MAX;
      if (stored == h && KeyTraits<K>::eq(e[i].key, key)) return i;
    }
  }

  Entry& add(const K& key, Allocator& a, bool* added) {
    element_checks();
    OAT_ASSERT_MSG(!aliases(&key), "inserting a key of a Map into itself");
    u32 h = hash_of(key);
    u32 slot = 0;
    if (raw.cap) {
      u32 mask = raw.cap - 1;
      Entry* e = entries();
      for (slot = h & mask;; slot = (slot + 1) & mask) {
        u32 stored = raw.hashes[slot];
        if (stored == 0) break;
        if (stored == h && KeyTraits<K>::eq(e[slot].key, key)) {
          *added = false;
          return e[slot];
        }
      }
    }
    if (static_cast<u64>(raw.len + 1) * 4 > static_cast<u64>(raw.cap) * 3) {
      detail::map_grow(&raw, sizeof(Entry), alignof(Entry), a);
      u32 mask = raw.cap - 1;
      for (slot = h & mask; raw.hashes[slot] != 0; slot = (slot + 1) & mask) {
      }
    }
    Entry* e = entries() + slot;
    raw.hashes[slot] = h;
    ::new (static_cast<void*>(&e->key)) K(key);
    ++raw.len;
    *added = true;
    return *e;
  }
};

template <class K>
struct Set {
  Map<K, detail::Unit> map;

  // True if the key was new.
  bool insert(const K& key, Allocator& a) { return map.insert(key, detail::Unit{}, a); }
  bool remove(const K& key) { return map.remove(key); }
  bool contains(const K& key) const { return map.contains(key); }
  void reserve(u32 count, Allocator& a) { map.reserve(count, a); }
  void clear() { map.clear(); }
  void free(Allocator& a) { map.free(a); }
  u32 len() const { return map.len(); }
  bool is_empty() const { return map.is_empty(); }

  struct Iter {
    typename Map<K, detail::Unit>::ConstIter it;
    const K& operator*() const { return (*it).key; }
    Iter& operator++() {
      ++it;
      return *this;
    }
    bool operator!=(const Iter& o) const { return it != o.it; }
  };
  Iter begin() const { return Iter{map.begin()}; }
  Iter end() const { return Iter{map.end()}; }
};

} // namespace oat
