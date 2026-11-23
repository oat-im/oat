#include "test.h"

#include <oat/oat.h>

#include <thread>

#if OAT_ASAN
extern "C" int __asan_address_is_poisoned(void const volatile* addr);
#endif

using namespace oat;

static bool is_aligned(const void* p, usize align) {
  return (reinterpret_cast<usize>(p) & (align - 1)) == 0;
}

TEST(heap_alloc_resize_free) {
  Allocator h = heap();
  HeapStats before = heap_stats();

  u8* p = static_cast<u8*>(h.alloc(100, 8));
  CHECK(p != nullptr);
  for (u32 i = 0; i < 100; ++i) p[i] = static_cast<u8>(i);
  p = static_cast<u8*>(h.resize(p, 100, 10000, 8));
  bool same = true;
  for (u32 i = 0; i < 100; ++i) same = same && p[i] == i;
  CHECK(same);
#if OAT_DEBUG
  CHECK(heap_stats().live_bytes == before.live_bytes + 10000);
  CHECK(heap_stats().live_count == before.live_count + 1);
  CHECK(heap_stats().peak_bytes >= before.live_bytes + 10000);
#endif
  h.free(p, 10000);
  CHECK(heap_stats().live_bytes == before.live_bytes);
  CHECK(heap_stats().live_count == before.live_count);

  CHECK(h.alloc(0, 8) == nullptr);
  h.free(nullptr, 0);
}

TEST(heap_over_aligned) {
  Allocator h = heap();
  for (usize align : {usize{32}, usize{64}, usize{4096}}) {
    u8* p = static_cast<u8*>(h.alloc(24, align));
    CHECK(is_aligned(p, align));
    std::memset(p, 0xab, 24);
    p = static_cast<u8*>(h.resize(p, 24, 5000, align));
    CHECK(is_aligned(p, align));
    CHECK(p[0] == 0xab && p[23] == 0xab);
    h.free(p, 5000);
  }
}

TEST(alloc_array_typed) {
  Allocator h = heap();
  u64* p = alloc_array<u64>(h, 10);
  for (u32 i = 0; i < 10; ++i) p[i] = i;
  CHECK(p[9] == 9);
  h.free(p, sizeof(u64) * 10);
}

TEST(arena_basic) {
  Allocator h = heap();
  HeapStats before = heap_stats();
  Arena arena = Arena::create(h, 1024);

  void* a = arena.alloc(10, 1);
  void* b = arena.alloc(8, 8);
  void* c = arena.alloc(1, 1);
  void* d = arena.alloc(64, 64);
  CHECK(a && b && c && d);
  CHECK(is_aligned(b, 8));
  CHECK(is_aligned(d, 64));
  CHECK(a != b && b != c && c != d);
  std::memset(a, 1, 10);
  std::memset(b, 2, 8);
  std::memset(d, 4, 64);
  CHECK(static_cast<u8*>(a)[9] == 1);

  // Crosses several blocks, and one allocation larger than a block.
  for (u32 i = 0; i < 100; ++i) std::memset(arena.alloc(100, 4), 7, 100);
  u8* big = static_cast<u8*>(arena.alloc(10000, 16));
  std::memset(big, 9, 10000);
  u8* big_aligned = static_cast<u8*>(arena.alloc(5000, 256));
  CHECK(is_aligned(big_aligned, 256));
  std::memset(big_aligned, 9, 5000);
  CHECK(arena.alloc(0, 1) == nullptr);

  arena.destroy();
  CHECK(heap_stats().live_bytes == before.live_bytes);
  CHECK(heap_stats().live_count == before.live_count);
}

TEST(arena_mark_reset) {
  Allocator h = heap();
  Arena arena = Arena::create(h, 1024);
  u8* keep = static_cast<u8*>(arena.alloc(16, 1));
  std::memset(keep, 5, 16);

  ArenaMark m = arena.mark();
  u8* first = static_cast<u8*>(arena.alloc(32, 1));
  for (u32 i = 0; i < 50; ++i) arena.alloc(200, 8); // spills into new blocks
  arena.reset_to(m);
  CHECK(arena.mark().block == m.block && arena.mark().used == m.used);
  u8* again = static_cast<u8*>(arena.alloc(32, 1));
  CHECK(again == first); // the space is handed out again
  CHECK(keep[15] == 5);

  {
    ArenaScope scope(arena);
    arena.alloc(5000, 8);
  }
  CHECK(arena.alloc(32, 1) == again + 32);

#if OAT_ASAN
  ArenaMark m2 = arena.mark();
  u8* temp = static_cast<u8*>(arena.alloc(64, 1));
  CHECK(!__asan_address_is_poisoned(temp));
  arena.reset_to(m2);
  CHECK(__asan_address_is_poisoned(temp)); // reset memory is poisoned
#endif

  // reset keeps standard blocks for reuse: a second pass allocates nothing new.
  arena.reset();
  for (u32 i = 0; i < 20; ++i) arena.alloc(500, 8);
  arena.reset();
  HeapStats mid = heap_stats();
  for (u32 i = 0; i < 20; ++i) arena.alloc(500, 8);
  CHECK(heap_stats().live_count == mid.live_count);
  arena.destroy();
}

TEST(arena_allocator_resize) {
  Allocator h = heap();
  Arena arena = Arena::create(h, 4096);
  Allocator a = arena.allocator();

  // The most recent allocation grows in place.
  u8* p = static_cast<u8*>(a.alloc(16, 8));
  std::memset(p, 3, 16);
  u8* q = static_cast<u8*>(a.resize(p, 16, 64, 8));
  CHECK(q == p);
  CHECK(q[15] == 3);

  // After another allocation it has to move, and the contents follow.
  u8* other = static_cast<u8*>(a.alloc(8, 8));
  u8* r = static_cast<u8*>(a.resize(q, 64, 128, 8));
  CHECK(r != q);
  CHECK(r[0] == 3 && r[15] == 3);
#if OAT_ASAN
  CHECK(__asan_address_is_poisoned(q)); // the abandoned block is poisoned
  CHECK(!__asan_address_is_poisoned(other));
#endif
  (void)other;

  // Shrinking stays put; freeing the last allocation gives the space back.
  u8* s = static_cast<u8*>(a.resize(r, 128, 32, 8));
  CHECK(s == r);
  ArenaMark before_free = arena.mark();
  u8* last = static_cast<u8*>(a.alloc(40, 8));
  a.free(last, 40);
  CHECK(arena.mark().used == before_free.used);

  // Growing past the end of the block moves to a fresh one.
  u8* t = static_cast<u8*>(a.alloc(100, 8));
  std::memset(t, 8, 100);
  u8* u = static_cast<u8*>(a.resize(t, 100, 8000, 8));
  CHECK(u[99] == 8);
  arena.destroy();
}

TEST(arena_backs_containers) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  Array<u32> arr;
  for (u32 i = 0; i < 10000; ++i) arr.push(i, a);
  bool ok = true;
  for (u32 i = 0; i < 10000; ++i) ok = ok && arr[i] == i;
  CHECK(ok);
  arena.destroy();
}

#if OAT_DEBUG
TEST(arena_wrong_thread_asserts) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  arena.alloc(8, 8);
  bool asserted = false;
  std::thread worker([&] {
    oat_test::g_expect_assert = true;
    if (setjmp(oat_test::g_assert_jump) == 0) arena.alloc(8, 8);
    else asserted = true;
    oat_test::g_expect_assert = false;

    arena.adopt_thread(); // handing the arena over is allowed
    arena.alloc(8, 8);
  });
  worker.join();
  CHECK(asserted);
  arena.adopt_thread();
  arena.destroy();
}
#endif
