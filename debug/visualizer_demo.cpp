// A program with one of each container, for checking the debugger
// visualizers. Break on the line marked below and inspect the locals.
#include <oat/oat.h>

using namespace oat;

struct Monster {
  u32 id;
  f32 hp;
};

static void stop_here() {}

int main() {
  Allocator h = heap();

  Array<Monster> array;
  for (u32 i = 0; i < 3; ++i) array.push(Monster{i, 10.0f * static_cast<f32>(i + 1)}, h);

  FixedArray<u32, 8> fixed;
  fixed.push(7);
  fixed.push(8);

  RingBuffer<u32> ring = RingBuffer<u32>::create(4, h);
  for (u32 i = 1; i <= 6; ++i) ring.push_back_overwrite(i * 100); // holds 300 400 500 600

  Map<u32, Monster> map;
  map.insert(42, Monster{42, 1.5f}, h);
  map.insert(7, Monster{7, 2.5f}, h);

  Set<u64> set;
  set.insert(1000, h);
  set.insert(2000, h);

  StrBuf str;
  str.append("hello oat", h);

  Pool<Monster> pool;
  Handle<Monster> first = pool.add(Monster{1, 11.0f}, h);
  Handle<Monster> second = pool.add(Monster{2, 22.0f}, h);
  Handle<Monster> third = pool.add(Monster{3, 33.0f}, h);
  pool.remove(second); // leaves a hole: live slots are 0 and 2
  Handle<Monster> null_handle;

  stop_here(); // <- break here

  u32 total = array.len + fixed.len + ring.len + map.len() + set.len() + str.len + pool.len() +
              first.index + third.index + null_handle.index;
  pool.free(h);
  str.free(h);
  set.free(h);
  map.free(h);
  ring.free(h);
  array.free(h);
  return total > 0 ? 0 : 1;
}
