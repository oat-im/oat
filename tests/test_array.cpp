#include "test.h"

#include <oat/oat.h>

#include <deque>
#include <vector>

using namespace oat;

struct Vec3 {
  f32 x, y, z;
  bool operator==(const Vec3&) const = default;
};

// A self-referential type compiles because the asserts live in member functions.
struct Node {
  Array<Node> children;
  int value;
};

template <class A, class M>
static bool same(const A& a, const M& model) {
  if (a.len != model.size()) return false;
  for (u32 i = 0; i < a.len; ++i)
    if (!(a[i] == model[i])) return false;
  return true;
}

static void array_model_test(Allocator& a, u64 seed) {
  Rng rng = Rng::seeded(seed);
  Array<u64> arr;
  std::vector<u64> model;
  bool ok = true;
  for (u32 step = 0; step < 20000 && ok; ++step) {
    u64 v = rng.next_u64();
    switch (rng.range(0, 11)) {
      case 0:
      case 1:
      case 2:
      case 3: {
        u64& ref = arr.push(v, a);
        model.push_back(v);
        ok = ok && ref == v && &ref == &arr.last();
        break;
      }
      case 4:
        if (!model.empty()) {
          ok = ok && arr.pop() == model.back();
          model.pop_back();
        }
        break;
      case 5: {
        u32 i = rng.range(0, arr.len + 1);
        arr.insert(i, v, a);
        model.insert(model.begin() + i, v);
        break;
      }
      case 6:
        if (!model.empty()) {
          u32 i = rng.range(0, arr.len);
          arr.remove_ordered(i);
          model.erase(model.begin() + i);
        }
        break;
      case 7:
        if (!model.empty()) {
          u32 i = rng.range(0, arr.len);
          arr.remove_swap(i);
          model[i] = model.back();
          model.pop_back();
        }
        break;
      case 8: {
        u32 n = rng.range(0, 200);
        arr.resize(n, a);
        model.resize(n, 0);
        break;
      }
      case 9: {
        u32 n = rng.range(0, 10);
        u64* p = arr.push_uninitialized(n, a);
        for (u32 i = 0; i < n; ++i) {
          p[i] = v + i;
          model.push_back(v + i);
        }
        break;
      }
      case 10:
        if (rng.range(0, 50) == 0) {
          arr.clear();
          model.clear();
        } else {
          arr.reserve(rng.range(0, 300), a);
        }
        break;
    }
    ok = ok && arr.len <= arr.cap && same(arr, model);
  }
  CHECK(ok);
  arr.free(a);
  CHECK(arr.data == nullptr && arr.len == 0 && arr.cap == 0);
}

TEST(array_against_model_heap) {
  Allocator h = heap();
  HeapStats before = heap_stats();
  array_model_test(h, 1);
  array_model_test(h, 2);
  CHECK(heap_stats().live_bytes == before.live_bytes);
}

TEST(array_against_model_arena) {
  Allocator h = heap();
  Arena arena = Arena::create(h, 4096);
  Allocator a = arena.allocator();
  array_model_test(a, 3);
  arena.destroy();
}

TEST(array_basics) {
  Allocator h = heap();
  Array<Vec3> arr;
  CHECK(arr.is_empty());
  CHECK(arr.begin() == arr.end());
  arr.push(Vec3{1, 2, 3}, h);
  arr.push(Vec3{4, 5, 6}, h);
  CHECK(arr.len == 2 && arr[1].y == 5 && arr.last().z == 6);
  f32 sum = 0;
  for (const Vec3& v : arr) sum += v.x;
  CHECK(sum == 5);
  CHECK(arr.span().size() == 2);
  const Array<Vec3>& c = arr;
  CHECK(c.span()[0].x == 1 && c[0].x == 1 && c.last().x == 4);

  Vec3 more[3] = {{7, 7, 7}, {8, 8, 8}, {9, 9, 9}};
  arr.push_many(more, h);
  CHECK(arr.len == 5 && arr[4].x == 9);

  arr.resize(8, h);
  CHECK(arr[7] == (Vec3{0, 0, 0})); // zero-filled
  arr.resize_uninitialized(2, h);
  CHECK(arr.len == 2);
  arr.free(h);

  // Over-aligned elements keep their alignment through growth.
  struct alignas(64) Wide {
    u8 bytes[64];
  };
  Array<Wide> wide;
  bool aligned = true;
  for (u32 i = 0; i < 100; ++i) {
    wide.push(Wide{}, h);
    aligned = aligned && (reinterpret_cast<usize>(wide.data) % 64) == 0;
  }
  CHECK(aligned);
  wide.free(h);
}

TEST(array_self_referential_type) {
  Allocator h = heap();
  Node root{};
  root.value = 1;
  for (int i = 0; i < 3; ++i) {
    Node child{};
    child.value = 10 + i;
    child.children.push(Node{{}, 100 + i}, h);
    root.children.push(child, h);
  }
  CHECK(root.children.len == 3);
  CHECK(root.children[2].children[0].value == 102);
  for (Node& c : root.children) c.children.free(h);
  root.children.free(h);
}

TEST(array_asserts) {
  Allocator h = heap();
  Array<u32> arr;
  arr.push(1, h);
  arr.push(2, h);

  // 32-bit length overflow is caught before any memory is touched.
  Array<u8> huge;
  huge.len = 0xfffffff0u;
  huge.cap = 0xfffffff0u;
  CHECK_ASSERTS(huge.push_uninitialized(100, h));

#if OAT_DEBUG
  CHECK_ASSERTS(arr.push(arr[0], h));      // element of itself
  CHECK_ASSERTS(arr.insert(0, arr[1], h)); // element of itself
  CHECK_ASSERTS(arr.push_many(arr.span(), h));
  CHECK_ASSERTS((void)arr[2]);
  CHECK_ASSERTS(arr.remove_ordered(2));
  CHECK_ASSERTS(arr.remove_swap(5));
  CHECK_ASSERTS(arr.insert(3, 9u, h));
  Array<u32> empty;
  CHECK_ASSERTS(empty.pop());
  CHECK_ASSERTS(empty.last());
#endif
  u32 copy = arr[0]; // the supported way to duplicate an element
  arr.push(copy, h);
  CHECK(arr.len == 3 && arr[2] == 1);
  arr.free(h);
}

TEST(fixed_array) {
  FixedArray<u32, 8> fa;
  std::vector<u32> model;
  Rng rng = Rng::seeded(4);
  bool ok = true;
  for (u32 step = 0; step < 5000; ++step) {
    u32 v = static_cast<u32>(rng.next_u64());
    switch (rng.range(0, 6)) {
      case 0:
      case 1:
        if (!fa.is_full()) {
          fa.push(v);
          model.push_back(v);
        }
        break;
      case 2:
        if (!fa.is_empty()) {
          ok = ok && fa.pop() == model.back();
          model.pop_back();
        }
        break;
      case 3:
        if (!fa.is_full()) {
          u32 i = rng.range(0, fa.len + 1);
          fa.insert(i, v);
          model.insert(model.begin() + i, v);
        }
        break;
      case 4:
        if (!fa.is_empty()) {
          u32 i = rng.range(0, fa.len);
          fa.remove_ordered(i);
          model.erase(model.begin() + i);
        }
        break;
      case 5:
        if (!fa.is_empty()) {
          u32 i = rng.range(0, fa.len);
          fa.remove_swap(i);
          model[i] = model.back();
          model.pop_back();
        }
        break;
    }
    ok = ok && same(fa, model);
  }
  CHECK(ok);
  CHECK(fa.capacity() == 8);

  fa.clear();
  fa.resize(8);
  CHECK(fa.is_full() && fa[7] == 0 && fa.span().size() == 8);
  CHECK_ASSERTS(fa.push(1));
  CHECK_ASSERTS(fa.resize(9));
  // Inserting one of its own elements is fine: nothing reallocates.
  fa.clear();
  fa.push(1);
  fa.push(2);
  fa.insert(0, fa[1]);
  CHECK(fa[0] == 2 && fa[1] == 1 && fa[2] == 2);
}

TEST(ring_buffer) {
  Allocator h = heap();
  RingBuffer<u32> ring = RingBuffer<u32>::create(7, h);
  std::deque<u32> model;
  Rng rng = Rng::seeded(5);
  bool ok = true;
  for (u32 step = 0; step < 20000; ++step) {
    u32 v = static_cast<u32>(rng.next_u64());
    switch (rng.range(0, 5)) {
      case 0:
      case 1: {
        bool pushed = ring.push_back(v);
        ok = ok && pushed == (model.size() < 7);
        if (pushed) model.push_back(v);
        break;
      }
      case 2:
        if (!model.empty()) {
          ok = ok && ring.pop_front() == model.front();
          model.pop_front();
        }
        break;
      case 3:
        if (!model.empty()) {
          ok = ok && ring.pop_back() == model.back();
          model.pop_back();
        }
        break;
      case 4:
        ring.push_back_overwrite(v);
        if (model.size() == 7) model.pop_front();
        model.push_back(v);
        break;
    }
    ok = ok && ring.len == model.size() && ring.is_full() == (model.size() == 7);
    for (u32 i = 0; i < ring.len; ++i) ok = ok && ring[i] == model[i];
    if (ring.len) ok = ok && ring.front() == model.front() && ring.back() == model.back();
  }
  CHECK(ok);
  u32 i = 0;
  bool iter_ok = true;
  for (u32& v : ring) iter_ok = iter_ok && v == model[i++];
  CHECK(iter_ok && i == ring.len);
  ring.clear();
  CHECK(ring.is_empty());
  ring.free(h);
}

TEST(bit_set) {
  Allocator h = heap();
  BitSet bits;
  std::vector<bool> model;
  Rng rng = Rng::seeded(6);
  bool ok = true;
  for (u32 step = 0; step < 4000; ++step) {
    switch (rng.range(0, 8)) {
      case 0: {
        u32 n = rng.range(0, 400);
        bits.resize(n, h);
        model.resize(n, false);
        break;
      }
      case 1:
      case 2:
        if (bits.len()) {
          u32 i = rng.range(0, bits.len());
          bits.set(i);
          model[i] = true;
        }
        break;
      case 3:
        if (bits.len()) {
          u32 i = rng.range(0, bits.len());
          bits.unset(i);
          model[i] = false;
        }
        break;
      case 4:
        if (bits.len()) {
          u32 i = rng.range(0, bits.len());
          bits.toggle(i);
          model[i] = !model[i];
        }
        break;
      case 5:
        if (rng.range(0, 10) == 0) {
          bits.set_all();
          model.assign(model.size(), true);
        }
        break;
      case 6:
        if (rng.range(0, 10) == 0) {
          bits.clear_all();
          model.assign(model.size(), false);
        }
        break;
      case 7:
        if (bits.len()) {
          u32 i = rng.range(0, bits.len());
          bool on = rng.range(0, 2) == 1;
          bits.assign(i, on);
          model[i] = on;
        }
        break;
    }
    u32 count = 0;
    for (u32 i = 0; i < bits.len(); ++i) {
      ok = ok && bits.test(i) == model[i];
      count += model[i];
    }
    ok = ok && bits.count() == count;
    // find_set walks exactly the set bits.
    u32 walked = 0;
    for (u32 i = bits.find_set(); i != U32_MAX; i = bits.find_set(i + 1)) {
      ok = ok && model[i];
      ++walked;
    }
    ok = ok && walked == count;
  }
  CHECK(ok);
  bits.free(h);

  FixedBitSet<100> fixed;
  CHECK(fixed.count() == 0 && fixed.find_set() == U32_MAX);
  fixed.set(0);
  fixed.set(64);
  fixed.set(99);
  CHECK(fixed.count() == 3 && fixed.test(64) && !fixed.test(63));
  CHECK(fixed.find_set(1) == 64 && fixed.find_set(65) == 99 && fixed.find_set(100) == U32_MAX);
  fixed.toggle(64);
  fixed.unset(0);
  CHECK(fixed.count() == 1);
  fixed.set_all();
  CHECK(fixed.count() == 100);
  fixed.clear_all();
  CHECK(fixed.count() == 0);
#if OAT_DEBUG
  CHECK_ASSERTS(fixed.set(100));
#endif
}
