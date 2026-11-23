#include <oat/memory.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>

#if defined(_WIN32)
#  include <malloc.h>
#endif

#if OAT_ASAN
extern "C" void __asan_poison_memory_region(void const volatile* addr, size_t size);
extern "C" void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
#  define OAT_POISON(p, n) __asan_poison_memory_region((p), (n))
#  define OAT_UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#  define OAT_POISON(p, n) ((void)(p), (void)(n))
#  define OAT_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

namespace oat {

// ---------------------------------------------------------------------------
// Heap
// ---------------------------------------------------------------------------

#if OAT_DEBUG
static std::atomic<u64> g_heap_live_bytes{0};
static std::atomic<u64> g_heap_live_count{0};
static std::atomic<u64> g_heap_peak_bytes{0};

static void heap_track(usize old_size, usize new_size, bool had_old) {
  if (had_old) {
    g_heap_live_bytes.fetch_sub(old_size, std::memory_order_relaxed);
    g_heap_live_count.fetch_sub(1, std::memory_order_relaxed);
  }
  if (new_size) {
    u64 live = g_heap_live_bytes.fetch_add(new_size, std::memory_order_relaxed) + new_size;
    g_heap_live_count.fetch_add(1, std::memory_order_relaxed);
    u64 peak = g_heap_peak_bytes.load(std::memory_order_relaxed);
    while (live > peak &&
           !g_heap_peak_bytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
  }
}
#else
static void heap_track(usize, usize, bool) {}
#endif

HeapStats heap_stats() {
  HeapStats s;
#if OAT_DEBUG
  s.live_bytes = g_heap_live_bytes.load(std::memory_order_relaxed);
  s.live_count = g_heap_live_count.load(std::memory_order_relaxed);
  s.peak_bytes = g_heap_peak_bytes.load(std::memory_order_relaxed);
#endif
  return s;
}

#if defined(_WIN32)

// free() is not told the alignment, so every block uses the aligned family.
static constexpr usize HEAP_MIN_ALIGN = 16;
static void* sys_alloc(usize size, usize align) {
  return _aligned_malloc(size, align < HEAP_MIN_ALIGN ? HEAP_MIN_ALIGN : align);
}
static void* sys_realloc(void* p, usize, usize new_size, usize align) {
  return _aligned_realloc(p, new_size, align < HEAP_MIN_ALIGN ? HEAP_MIN_ALIGN : align);
}
static void sys_free(void* p) { _aligned_free(p); }

#else

// posix_memalign blocks are released with free(), so free needs no alignment.
static void* sys_alloc(usize size, usize align) {
  if (align <= alignof(std::max_align_t)) return std::malloc(size);
  void* p = nullptr;
  if (posix_memalign(&p, align, size) != 0) return nullptr;
  return p;
}
static void* sys_realloc(void* p, usize old_size, usize new_size, usize align) {
#if defined(__APPLE__)
  // Measured: macOS realloc on a big block is about twice as slow as
  // allocating a new one and copying, so only small blocks go through it.
  constexpr usize REALLOC_LIMIT = 64 * 1024;
#else
  constexpr usize REALLOC_LIMIT = ~usize{0};
#endif
  if (align <= alignof(std::max_align_t) && old_size < REALLOC_LIMIT)
    return std::realloc(p, new_size);
  void* q = sys_alloc(new_size, align);
  if (q) {
    std::memcpy(q, p, old_size < new_size ? old_size : new_size);
    std::free(p);
  }
  return q;
}
static void sys_free(void* p) { std::free(p); }

#endif

static void* heap_fn(void*, void* old_ptr, usize old_size, usize new_size, usize align) {
  if (new_size == 0) {
    if (old_ptr) {
      sys_free(old_ptr);
      heap_track(old_size, 0, true);
    }
    return nullptr;
  }
  OAT_ASSERT(align != 0 && (align & (align - 1)) == 0);
  void* p = old_ptr ? sys_realloc(old_ptr, old_size, new_size, align) : sys_alloc(new_size, align);
  if (!p) assert_fail("out of memory", __FILE__, __LINE__);
  heap_track(old_size, new_size, old_ptr != nullptr);
  return p;
}

Allocator heap() {
  Allocator a;
  a.fn = heap_fn;
  a.ctx = nullptr;
  return a;
}

// ---------------------------------------------------------------------------
// Arena
// ---------------------------------------------------------------------------

struct ArenaBlock {
  ArenaBlock* prev;
  usize capacity; // usable bytes after this header
};

static constexpr usize ARENA_BLOCK_ALIGN = 16;
static_assert(sizeof(ArenaBlock) % ARENA_BLOCK_ALIGN == 0);

// The address of a thread_local is unique per live thread and needs no OS call.
static usize arena_thread_token() {
  static thread_local char token;
  return reinterpret_cast<usize>(&token);
}

static usize arena_base(ArenaBlock* b) { return reinterpret_cast<usize>(b + 1); }

static void arena_release_block(Arena* a, ArenaBlock* b) {
  if (b->capacity == a->block_size) {
    OAT_POISON(b + 1, b->capacity);
    b->prev = a->spare;
    a->spare = b;
  } else {
    OAT_UNPOISON(b + 1, b->capacity);
    a->backing.free(b, sizeof(ArenaBlock) + b->capacity);
  }
}

#define OAT_ARENA_THREAD_CHECK(a)                                                                  \
  OAT_ASSERT_MSG((a)->owner == arena_thread_token(), "Arena used from a thread that does not own it")

Arena Arena::create(Allocator& backing, usize block_size) {
  Arena a;
  a.backing = backing;
  a.block_size = block_size < 256 ? 256 : block_size;
  a.owner = arena_thread_token();
  return a;
}

void Arena::adopt_thread() { owner = arena_thread_token(); }

void Arena::destroy() {
  while (current) {
    ArenaBlock* prev = current->prev;
    OAT_UNPOISON(current + 1, current->capacity);
    backing.free(current, sizeof(ArenaBlock) + current->capacity);
    current = prev;
  }
  while (spare) {
    ArenaBlock* prev = spare->prev;
    OAT_UNPOISON(spare + 1, spare->capacity);
    backing.free(spare, sizeof(ArenaBlock) + spare->capacity);
    spare = prev;
  }
  used = 0;
}

void* Arena::alloc(usize size, usize align) {
  OAT_ARENA_THREAD_CHECK(this);
  OAT_ASSERT(align != 0 && (align & (align - 1)) == 0);
  if (size == 0) return nullptr;

  if (current) {
    usize base = arena_base(current);
    usize p = (base + used + (align - 1)) & ~(align - 1);
    if (p + size <= base + current->capacity) {
      used = p + size - base;
      OAT_UNPOISON(reinterpret_cast<void*>(p), size);
      return reinterpret_cast<void*>(p);
    }
  }

  usize need = size + (align > ARENA_BLOCK_ALIGN ? align : 0);
  ArenaBlock* b;
  if (need <= block_size && spare) {
    b = spare;
    spare = b->prev;
  } else {
    usize cap = need > block_size ? need : block_size;
    b = static_cast<ArenaBlock*>(backing.alloc(sizeof(ArenaBlock) + cap, ARENA_BLOCK_ALIGN));
    b->capacity = cap;
    OAT_POISON(b + 1, cap);
  }
  b->prev = current;
  current = b;

  usize base = arena_base(b);
  usize p = (base + (align - 1)) & ~(align - 1);
  used = p + size - base;
  OAT_UNPOISON(reinterpret_cast<void*>(p), size);
  return reinterpret_cast<void*>(p);
}

ArenaMark Arena::mark() {
  OAT_ARENA_THREAD_CHECK(this);
  ArenaMark m;
  m.block = current;
  m.used = used;
  return m;
}

void Arena::reset_to(ArenaMark m) {
  OAT_ARENA_THREAD_CHECK(this);
  while (current != m.block) {
    OAT_ASSERT_MSG(current != nullptr, "ArenaMark does not belong to this arena");
    if (!current) break;
    ArenaBlock* prev = current->prev;
    arena_release_block(this, current);
    current = prev;
  }
  if (current) {
    OAT_ASSERT(m.used <= current->capacity);
    used = m.used;
    OAT_POISON(reinterpret_cast<void*>(arena_base(current) + used), current->capacity - used);
  } else {
    used = 0;
  }
}

void Arena::reset() { reset_to(ArenaMark{}); }

static void* arena_fn(void* ctx, void* old_ptr, usize old_size, usize new_size, usize align) {
  Arena* a = static_cast<Arena*>(ctx);
  if (!old_ptr) return a->alloc(new_size, align);

  OAT_ARENA_THREAD_CHECK(a);
  usize old = reinterpret_cast<usize>(old_ptr);
  usize base = a->current ? arena_base(a->current) : 0;
  bool is_last = a->current && old + old_size == base + a->used;

  if (new_size == 0) {
    if (is_last) a->used = old - base;
    OAT_POISON(old_ptr, old_size);
    return nullptr;
  }

  // The most recent allocation grows or shrinks in place.
  if (is_last && old + new_size <= base + a->current->capacity) {
    a->used = old + new_size - base;
    if (new_size > old_size) {
      OAT_UNPOISON(reinterpret_cast<void*>(old + old_size), new_size - old_size);
    } else {
      OAT_POISON(reinterpret_cast<void*>(old + new_size), old_size - new_size);
    }
    return old_ptr;
  }
  if (new_size <= old_size) {
    OAT_POISON(reinterpret_cast<void*>(old + new_size), old_size - new_size);
    return old_ptr;
  }

  // Anything else copies and abandons the old block.
  void* p = a->alloc(new_size, align);
  std::memcpy(p, old_ptr, old_size);
  OAT_POISON(old_ptr, old_size);
  return p;
}

Allocator Arena::allocator() {
  Allocator a;
  a.fn = arena_fn;
  a.ctx = this;
  return a;
}

} // namespace oat
