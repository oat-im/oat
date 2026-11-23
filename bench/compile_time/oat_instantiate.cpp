// The compile-time probe: includes oat.h and instantiates every container for
// 20 distinct element types. tools/compile_time.py times this file.
#include <oat/oat.h>

using namespace oat;

template <int N>
struct Elem {
  u32 id;
  u32 payload[N + 1];
};

template <int N>
u32 exercise(Allocator& a) {
  using T = Elem<N>;
  T value{};
  value.id = N;

  Array<T> array;
  array.push(value, a);
  array.insert(0, value, a);
  array.resize(8, a);
  array.remove_swap(0);
  array.remove_ordered(0);

  FixedArray<T, 8> fixed;
  fixed.push(value);

  RingBuffer<T> ring = RingBuffer<T>::create(8, a);
  ring.push_back(value);

  Map<u32, T> by_id;
  by_id.insert(value.id, value, a);
  by_id.get_or_insert(7, a);
  by_id.remove(7);

  Map<T, u32> by_value;
  by_value.insert(value, 1, a);

  Set<T> set;
  set.insert(value, a);

  Pool<T> pool;
  Handle<T> handle = pool.add(value, a);

  oat::sort(array.span(), [](const T& x, const T& y) { return x.id < y.id; });
  oat::sort_stable(array.span(), [](const T& x, const T& y) { return x.id < y.id; }, a);
  u32 at = lower_bound(array.span(), value, [](const T& x, const T& y) { return x.id < y.id; });

  u32 total = array.len + fixed.len + ring.len + by_id.len() + by_value.len() + set.len() +
              pool.len() + at + (pool.get(handle) ? 1 : 0) + (by_id.find(1) ? 1 : 0);
  for (T& t : array) total += t.id;
  for (auto& e : by_id) total += e.key;
  for (T& t : pool) total += t.id;

  array.free(a);
  ring.free(a);
  by_id.free(a);
  by_value.free(a);
  set.free(a);
  pool.free(a);
  return total;
}

u32 exercise_all(Allocator& a) {
  u32 total = 0;
#define X(n) total += exercise<n>(a);
  X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9)
  X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)
#undef X
  StrBuf s;
  s.appendf(a, "%u", total);
  s.free(a);
  return total;
}
