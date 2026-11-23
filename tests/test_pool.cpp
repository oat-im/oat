#include "test.h"

#include <oat/oat.h>

#include <vector>

using namespace oat;

struct Enemy {
  ListNode link;
  u32 id;
  u32 hp;
};

TEST(pool_basics) {
  Allocator h = heap();
  Pool<u64> pool;
  Handle<u64> null_handle;
  CHECK(null_handle.is_null() && pool.get(null_handle) == nullptr && pool.len() == 0);
  CHECK(pool.begin().index == pool.end().index);

  Handle<u64> a = pool.add(11, h);
  Handle<u64> b = pool.add(22, h);
  CHECK(!a.is_null() && !(a == b));
  CHECK(*pool.get(a) == 11 && *pool.get(b) == 22 && pool.len() == 2);

  CHECK(pool.remove(a));
  CHECK(pool.get(a) == nullptr); // stale
  CHECK(!pool.remove(a));        // removing twice is harmless
  CHECK(pool.len() == 1);

  // The slot is reused under a new generation; the old handle stays stale.
  Handle<u64> c = pool.add(33, h);
  CHECK(c.index == a.index && c.generation != a.generation);
  CHECK(pool.get(a) == nullptr && *pool.get(c) == 33);

  // A handle that points past the end is just stale.
  CHECK(pool.get(Handle<u64>{100000, 1}) == nullptr);
  const Pool<u64>& cpool = pool;
  CHECK(*cpool.get(b) == 22);
  pool.free(h);
  CHECK(pool.len() == 0 && pool.raw.chunks == nullptr);
}

TEST(pool_against_model) {
  Allocator h = heap();
  HeapStats before = heap_stats();
  Pool<u64> pool;
  struct Live {
    Handle<u64> handle;
    u64 value;
    u64* address;
  };
  std::vector<Live> live;
  std::vector<Handle<u64>> dead;
  Rng rng = Rng::seeded(40);
  bool ok = true;
  for (u32 step = 0; step < 30000 && ok; ++step) {
    u32 op = rng.range(0, 10);
    if (op < 6 || live.empty()) {
      u64 v = rng.next_u64();
      Handle<u64> hd = pool.add(v, h);
      live.push_back(Live{hd, v, pool.get(hd)});
    } else {
      u32 i = rng.range(0, static_cast<u32>(live.size()));
      ok = ok && pool.remove(live[i].handle);
      dead.push_back(live[i].handle);
      live[i] = live.back();
      live.pop_back();
    }
    ok = ok && pool.len() == live.size();
    if (step % 64 == 0) {
      for (const Live& l : live) {
        u64* p = pool.get(l.handle);
        // Same value, and the same address as the day it was added.
        ok = ok && p && *p == l.value && p == l.address;
      }
      for (Handle<u64> d : dead) ok = ok && pool.get(d) == nullptr;
    }
  }
  CHECK(ok);
  CHECK(pool.raw.chunk_count > 4); // growth really happened

  // Iteration visits exactly the live elements.
  u64 sum_model = 0, sum_pool = 0;
  u32 visited = 0;
  for (const Live& l : live) sum_model += l.value;
  for (u64& v : pool) {
    sum_pool += v;
    ++visited;
  }
  CHECK(visited == live.size() && sum_pool == sum_model);

  // Remove every element with an odd value while iterating.
  u32 kept = 0;
  for (auto it = pool.begin(); it != pool.end(); ++it) {
    if (*it & 1) pool.remove(it.handle());
    else ++kept;
  }
  CHECK(pool.len() == kept);
  bool all_even = true;
  for (u64& v : pool) all_even = all_even && (v & 1) == 0;
  CHECK(all_even);

  pool.free(h);
  CHECK(heap_stats().live_bytes == before.live_bytes);
}

TEST(pool_over_aligned) {
  Allocator h = heap();
  struct alignas(32) Wide {
    f32 v[8];
  };
  Pool<Wide> pool;
  bool ok = true;
  for (u32 i = 0; i < 200; ++i) {
    Handle<Wide> hd = pool.add(Wide{{static_cast<f32>(i)}}, h);
    Wide* w = pool.get(hd);
    ok = ok && (reinterpret_cast<usize>(w) % 32) == 0 && w->v[0] == static_cast<f32>(i);
  }
  CHECK(ok);
  pool.free(h);
}

TEST(intrusive_list) {
  Allocator h = heap();
  Pool<Enemy> pool;
  IntrusiveList<Enemy, &Enemy::link> list;
  CHECK(list.is_empty() && list.front() == nullptr && list.back() == nullptr);
  for (Enemy& e : list) {
    (void)e;
    CHECK(false);
  }

  // Pool elements never move, so they can be linked while the pool grows.
  std::vector<Handle<Enemy>> handles;
  for (u32 i = 0; i < 300; ++i) {
    handles.push_back(pool.add(Enemy{{}, i, 100}, h));
    Enemy* e = pool.get(handles.back());
    if (i % 2) list.push_back(*e);
    else list.push_front(*e);
  }
  // Expected order: evens descending (pushed to the front), then odds ascending.
  std::vector<u32> expected;
  for (i32 i = 298; i >= 0; i -= 2) expected.push_back(static_cast<u32>(i));
  for (u32 i = 1; i < 300; i += 2) expected.push_back(i);
  std::vector<u32> seen;
  for (Enemy& e : list) seen.push_back(e.id);
  CHECK(seen == expected);
  CHECK(list.front()->id == 298 && list.back()->id == 299);

  // Remove while iterating, including the head and the tail.
  for (Enemy& e : list) {
    if (e.id % 3 == 0 || e.id == 298 || e.id == 299) list.remove(e);
  }
  seen.clear();
  for (Enemy& e : list) seen.push_back(e.id);
  std::vector<u32> filtered;
  for (u32 id : expected)
    if (!(id % 3 == 0 || id == 298 || id == 299)) filtered.push_back(id);
  CHECK(seen == filtered);

  // A removed node can be linked again.
  Enemy* first = pool.get(handles[0]);
  CHECK(first->link.prev == nullptr && first->link.next == nullptr);
  list.push_back(*first);
  CHECK(list.back() == first);
#if OAT_DEBUG
  CHECK_ASSERTS(list.push_back(*first)); // already linked
#endif

  while (!list.is_empty()) list.remove(*list.front());
  CHECK(list.head == nullptr && list.tail == nullptr);
  pool.free(h);
}
