// oat/pool.h - Pool with generational handles, and IntrusiveList.
#pragma once

#include "array.h"
#include "base.h"
#include "memory.h"

namespace oat {

// A weak reference to a Pool slot. A default Handle refers to nothing.
template <class T>
struct Handle {
  u32 index = 0;
  u32 generation = 0; // 0 is never a live generation

  bool is_null() const { return generation == 0; }
  bool operator==(const Handle&) const = default;
};

namespace detail {

inline constexpr u32 POOL_CHUNK_SLOTS = 64;

// The header of a chunk; the 64 items follow at the item type's alignment.
struct PoolChunk {
  u64 live; // one bit per slot
  u32 generation[POOL_CHUNK_SLOTS];
  u32 next_free[POOL_CHUNK_SLOTS]; // free-list link: slot index + 1, or 0
};

struct PoolRaw {
  PoolChunk** chunks = nullptr;
  u32 chunk_count = 0;
  u32 chunk_cap = 0;
  u32 len = 0;        // live elements
  u32 slot_count = 0; // slots ever handed out
  u32 free_head = 0;  // slot index + 1, or 0
  u32 items_offset = 0; // where the items start in a chunk; here for debugger visualizers
};

// Claims a slot (adding a chunk if needed), marks it live, and returns its index.
u32 pool_add(PoolRaw* p, usize chunk_bytes, usize chunk_align, usize items_offset, Allocator& a);
void pool_remove(PoolRaw* p, u32 index);
void pool_free(PoolRaw* p, usize chunk_bytes, Allocator& a);
// The first live slot at or after `from`, or slot_count.
u32 pool_next_live(const PoolRaw* p, u32 from);

} // namespace detail

// Storage is chunked, so an element's address never changes while it is live.
// Slots are reused; a Handle to a removed element stops resolving.
template <class T>
struct Pool {
  detail::PoolRaw raw;

  Handle<T> add(const T& v, Allocator& a) {
    OAT_ELEMENT_CHECKS(T);
    u32 index = detail::pool_add(&raw, chunk_bytes(), chunk_align(), items_offset(), a);
    detail::PoolChunk* chunk = raw.chunks[index / detail::POOL_CHUNK_SLOTS];
    u32 slot = index % detail::POOL_CHUNK_SLOTS;
    ::new (static_cast<void*>(items(chunk) + slot)) T(v);
    return Handle<T>{index, chunk->generation[slot]};
  }

  // nullptr if the handle is null or its element was removed.
  T* get(Handle<T> h) {
    u32 c = h.index / detail::POOL_CHUNK_SLOTS;
    u32 slot = h.index % detail::POOL_CHUNK_SLOTS;
    if (c >= raw.chunk_count) return nullptr;
    detail::PoolChunk* chunk = raw.chunks[c];
    if (chunk->generation[slot] != h.generation) return nullptr;
    return items(chunk) + slot;
  }
  const T* get(Handle<T> h) const { return const_cast<Pool*>(this)->get(h); }

  // False if the handle was already stale.
  bool remove(Handle<T> h) {
    if (!get(h)) return false;
    detail::pool_remove(&raw, h.index);
    return true;
  }

  u32 len() const { return raw.len; }
  bool is_empty() const { return raw.len == 0; }

  void free(Allocator& a) { detail::pool_free(&raw, chunk_bytes(), a); }

  // Visits live elements in slot order. Removing the current element while
  // iterating is safe; adding is not.
  struct Iter {
    Pool* pool;
    u32 index;
    T& operator*() const {
      return items(pool->raw.chunks[index / detail::POOL_CHUNK_SLOTS])
          [index % detail::POOL_CHUNK_SLOTS];
    }
    Handle<T> handle() const {
      return Handle<T>{index, pool->raw.chunks[index / detail::POOL_CHUNK_SLOTS]
                                  ->generation[index % detail::POOL_CHUNK_SLOTS]};
    }
    Iter& operator++() {
      index = detail::pool_next_live(&pool->raw, index + 1);
      return *this;
    }
    bool operator!=(const Iter& o) const { return index != o.index; }
  };
  Iter begin() { return Iter{this, detail::pool_next_live(&raw, 0)}; }
  Iter end() { return Iter{this, raw.slot_count}; }

 private:
  static constexpr usize chunk_align() {
    return alignof(T) > alignof(detail::PoolChunk) ? alignof(T) : alignof(detail::PoolChunk);
  }
  static constexpr usize items_offset() {
    return (sizeof(detail::PoolChunk) + alignof(T) - 1) & ~(alignof(T) - 1);
  }
  static constexpr usize chunk_bytes() {
    return items_offset() + sizeof(T) * detail::POOL_CHUNK_SLOTS;
  }
  static T* items(detail::PoolChunk* chunk) {
    return reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(chunk) + items_offset());
  }
};

// ---------------------------------------------------------------------------
// IntrusiveList: a doubly linked list threaded through a ListNode member.
//
//   struct Enemy { ListNode link; ... };
//   IntrusiveList<Enemy, &Enemy::link> active;
//
// The list stores pointers to its elements, so they must not move while
// linked. Pool elements qualify; Array elements do not, because growth moves
// them.
// ---------------------------------------------------------------------------

struct ListNode {
  ListNode* prev = nullptr;
  ListNode* next = nullptr;
};

template <class T, ListNode T::* Member>
struct IntrusiveList {
  ListNode* head = nullptr;
  ListNode* tail = nullptr;

  bool is_empty() const { return head == nullptr; }

  void push_back(T& item) {
    ListNode* n = &(item.*Member);
    OAT_ASSERT_MSG(!n->prev && !n->next && head != n, "node is already in a list");
    n->prev = tail;
    n->next = nullptr;
    if (tail) tail->next = n;
    else head = n;
    tail = n;
  }

  void push_front(T& item) {
    ListNode* n = &(item.*Member);
    OAT_ASSERT_MSG(!n->prev && !n->next && head != n, "node is already in a list");
    n->prev = nullptr;
    n->next = head;
    if (head) head->prev = n;
    else tail = n;
    head = n;
  }

  // `item` must be in this list.
  void remove(T& item) {
    ListNode* n = &(item.*Member);
    if (n->prev) n->prev->next = n->next;
    else head = n->next;
    if (n->next) n->next->prev = n->prev;
    else tail = n->prev;
    n->prev = nullptr;
    n->next = nullptr;
  }

  T* front() { return head ? owner(head) : nullptr; }
  T* back() { return tail ? owner(tail) : nullptr; }

  // The successor is read before the body runs, so removing the current
  // element while iterating is safe.
  struct Iter {
    ListNode* node;
    ListNode* next;
    T& operator*() const { return *owner(node); }
    Iter& operator++() {
      node = next;
      next = node ? node->next : nullptr;
      return *this;
    }
    bool operator!=(const Iter& o) const { return node != o.node; }
  };
  Iter begin() { return Iter{head, head ? head->next : nullptr}; }
  Iter end() { return Iter{nullptr, nullptr}; }

 private:
  static T* owner(ListNode* n) {
    // The offset of the member, taken from a non-null, well-aligned address
    // that is never dereferenced.
    constexpr usize fake = 0x10000;
    usize offset = reinterpret_cast<usize>(&(reinterpret_cast<T*>(fake)->*Member)) - fake;
    return reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(n) - offset);
  }
};

} // namespace oat
