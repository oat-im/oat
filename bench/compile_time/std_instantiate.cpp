// The same work as oat_instantiate.cpp written against the standard library,
// for comparison. Not part of the CI gate.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using u32 = std::uint32_t;

template <int N>
struct Elem {
  u32 id;
  u32 payload[N + 1];
  bool operator==(const Elem& o) const { return id == o.id; }
};

template <int N>
struct std::hash<Elem<N>> {
  size_t operator()(const Elem<N>& e) const { return std::hash<u32>()(e.id); }
};

template <int N>
u32 exercise() {
  using T = Elem<N>;
  T value{};
  value.id = N;

  std::vector<T> array;
  array.push_back(value);
  array.insert(array.begin(), value);
  array.resize(8);
  array[0] = array.back();
  array.pop_back();
  array.erase(array.begin());

  std::array<T, 8> fixed{};
  fixed[0] = value;

  std::deque<T> ring;
  ring.push_back(value);

  std::unordered_map<u32, T> by_id;
  by_id.insert_or_assign(value.id, value);
  by_id[7];
  by_id.erase(7);

  std::unordered_map<T, u32> by_value;
  by_value.insert_or_assign(value, 1);

  std::unordered_set<T> set;
  set.insert(value);

  std::vector<std::unique_ptr<T>> pool;
  pool.push_back(std::make_unique<T>(value));

  std::sort(array.begin(), array.end(), [](const T& x, const T& y) { return x.id < y.id; });
  std::stable_sort(array.begin(), array.end(), [](const T& x, const T& y) { return x.id < y.id; });
  u32 at = static_cast<u32>(
      std::lower_bound(array.begin(), array.end(), value,
                       [](const T& x, const T& y) { return x.id < y.id; }) -
      array.begin());

  u32 total = static_cast<u32>(array.size() + ring.size() + by_id.size() + by_value.size() +
                               set.size() + pool.size()) +
              at + fixed[0].id + (by_id.count(1) ? 1 : 0);
  for (T& t : array) total += t.id;
  for (auto& e : by_id) total += e.first;
  for (auto& t : pool) total += t->id;
  return total;
}

u32 exercise_all() {
  u32 total = 0;
#define X(n) total += exercise<n>();
  X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9)
  X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19)
#undef X
  std::string s = std::to_string(total);
  return total + static_cast<u32>(s.size());
}
