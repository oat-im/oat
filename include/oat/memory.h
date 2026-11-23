// oat/memory.h - the Allocator interface, the heap, and Arena.
#pragma once

#include "base.h"

namespace oat {

// A function pointer plus a context. One entry point covers all three
// operations:
//   allocate: old_ptr == nullptr, new_size > 0
//   resize:   old_ptr != nullptr, new_size > 0   (contents are preserved)
//   free:     old_ptr != nullptr, new_size == 0  (align is 0)
// Running out of memory is fatal, so these never return nullptr for a
// non-zero size.
struct Allocator {
  void* (*fn)(void* ctx, void* old_ptr, usize old_size, usize new_size, usize align) = nullptr;
  void* ctx = nullptr;

  void* alloc(usize size, usize align) { return size ? fn(ctx, nullptr, 0, size, align) : nullptr; }

  void* resize(void* p, usize old_size, usize new_size, usize align) {
    if (!p) return alloc(new_size, align);
    return fn(ctx, p, old_size, new_size, new_size ? align : 0);
  }

  void free(void* p, usize size) {
    if (p) fn(ctx, p, size, 0, 0);
  }
};

// Uninitialized storage for `count` objects.
template <class T>
T* alloc_array(Allocator& a, u32 count) {
  return static_cast<T*>(a.alloc(sizeof(T) * static_cast<usize>(count), alignof(T)));
}

// malloc-backed and thread-safe.
Allocator heap();

struct HeapStats {
  u64 live_bytes = 0;
  u64 live_count = 0;
  u64 peak_bytes = 0;
};

// Tracked when the oat library is built with OAT_DEBUG; all zeros otherwise.
HeapStats heap_stats();

struct ArenaBlock;

struct ArenaMark {
  ArenaBlock* block = nullptr;
  usize used = 0;
};

// A bump allocator. Blocks are chained from the backing allocator, so the
// arena itself makes no OS calls. Single-threaded: in debug builds every call
// asserts that it comes from the thread that created (or adopted) the arena.
//
// allocator() captures the arena's address, so do not move an Arena while an
// Allocator made from it is in use.
struct Arena {
  Allocator backing = {};
  ArenaBlock* current = nullptr; // newest block; older ones through ->prev
  ArenaBlock* spare = nullptr;   // standard-size blocks kept for reuse by reset
  usize used = 0;                // bytes used in `current`
  usize block_size = 0;
  usize owner = 0;               // token of the owning thread

  static Arena create(Allocator& backing, usize block_size = 64 * 1024);
  void destroy();

  void* alloc(usize size, usize align);
  ArenaMark mark();
  void reset_to(ArenaMark);
  void reset();
  Allocator allocator();

  // Makes the calling thread the owner.
  void adopt_thread();

  template <class T>
  T* alloc_array(u32 count) {
    return static_cast<T*>(alloc(sizeof(T) * static_cast<usize>(count), alignof(T)));
  }
};

// Marks on construction, resets to the mark on destruction.
struct ArenaScope {
  Arena& arena;
  ArenaMark saved;

  explicit ArenaScope(Arena& a) : arena(a), saved(a.mark()) {}
  ~ArenaScope() { arena.reset_to(saved); }
  ArenaScope(const ArenaScope&) = delete;
  ArenaScope& operator=(const ArenaScope&) = delete;
};

} // namespace oat
