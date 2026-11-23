// Push, iterate, map insert, map find and sort: oat against the standard
// library and (optionally) EASTL. Prints a Markdown table of nanoseconds per
// element, best of several runs.
//
// The containers' template code is compiled at this file's optimization level
// (-O0 or -O2). oat's non-template core lives in the oat library, which is
// always optimized; that is the design, and part of what is being measured.
#include <oat/oat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

#if OAT_BENCH_EASTL
#  include <EASTL/hash_map.h>
#  include <EASTL/sort.h>
#  include <EASTL/vector.h>

// EASTL leaves these two allocation hooks to the application.
void* operator new[](size_t size, const char*, int, unsigned, const char*, int) {
  return ::operator new[](size);
}
void* operator new[](size_t size, size_t, size_t, const char*, int, unsigned, const char*, int) {
  return ::operator new[](size);
}
#endif

using namespace oat;

static constexpr u32 N = 1000000;
static constexpr int RUNS = 7;

static u64 g_sink = 0; // every benchmark feeds its result here so nothing is optimized away

template <class Fn>
static double best_ns_per_item(Fn fn) {
  double best = 1e30;
  g_sink += fn(); // untimed warm-up, so no library pays for first-touch page faults
  for (int run = 0; run < RUNS; ++run) {
    auto start = std::chrono::steady_clock::now();
    g_sink += fn();
    auto stop = std::chrono::steady_clock::now();
    double ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
    best = std::min(best, ns / N);
  }
  return best;
}

struct Row {
  const char* name;
  double oat, std_, eastl;
};

static void print(const Row& r) {
#if OAT_BENCH_EASTL
  std::printf("| %-28s | %7.2f | %7.2f | %7.2f |\n", r.name, r.oat, r.std_, r.eastl);
#else
  std::printf("| %-28s | %7.2f | %7.2f |\n", r.name, r.oat, r.std_);
#endif
}

int main() {
  Allocator h = heap();

  // Shared inputs.
  std::vector<u64> keys(N);
  Rng rng = Rng::seeded(2026);
  for (u64& k : keys) k = rng.next_u64();

  Row push{"push 1M u64", 0, 0, 0};
  Row iterate{"iterate 1M u64", 0, 0, 0};
  Row insert{"map insert 1M u64 -> u64", 0, 0, 0};
  Row find{"map find 1M (all hits)", 0, 0, 0};
  Row sorting{"sort 1M u64", 0, 0, 0};

  // ---- oat ----
  push.oat = best_ns_per_item([&] {
    Array<u64> a;
    for (u32 i = 0; i < N; ++i) a.push(keys[i], h);
    u64 r = a[N / 2];
    a.free(h);
    return r;
  });
  {
    Array<u64> a;
    for (u32 i = 0; i < N; ++i) a.push(keys[i], h);
    iterate.oat = best_ns_per_item([&] {
      u64 sum = 0;
      for (u64 v : a) sum += v;
      return sum;
    });
    a.free(h);
  }
  insert.oat = best_ns_per_item([&] {
    Map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert(keys[i], i, h);
    u64 r = m.len();
    m.free(h);
    return r;
  });
  {
    Map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert(keys[i], i, h);
    find.oat = best_ns_per_item([&] {
      u64 sum = 0;
      for (u32 i = 0; i < N; ++i) sum += *m.find(keys[i]);
      return sum;
    });
    m.free(h);
  }
  sorting.oat = best_ns_per_item([&] {
    Array<u64> a;
    a.push_many(Span<const u64>(keys.data(), N), h);
    oat::sort(a.span(), [](u64 x, u64 y) { return x < y; });
    u64 r = a[N / 2];
    a.free(h);
    return r;
  });

  // ---- std ----
  push.std_ = best_ns_per_item([&] {
    std::vector<u64> a;
    for (u32 i = 0; i < N; ++i) a.push_back(keys[i]);
    return a[N / 2];
  });
  {
    std::vector<u64> a(keys);
    iterate.std_ = best_ns_per_item([&] {
      u64 sum = 0;
      for (u64 v : a) sum += v;
      return sum;
    });
  }
  insert.std_ = best_ns_per_item([&] {
    std::unordered_map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert_or_assign(keys[i], i);
    return static_cast<u64>(m.size());
  });
  {
    std::unordered_map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert_or_assign(keys[i], i);
    find.std_ = best_ns_per_item([&] {
      u64 sum = 0;
      for (u32 i = 0; i < N; ++i) sum += m.find(keys[i])->second;
      return sum;
    });
  }
  sorting.std_ = best_ns_per_item([&] {
    std::vector<u64> a(keys);
    std::sort(a.begin(), a.end(), [](u64 x, u64 y) { return x < y; });
    return a[N / 2];
  });

#if OAT_BENCH_EASTL
  // ---- EASTL ----
  push.eastl = best_ns_per_item([&] {
    eastl::vector<u64> a;
    for (u32 i = 0; i < N; ++i) a.push_back(keys[i]);
    return a[N / 2];
  });
  {
    eastl::vector<u64> a(keys.data(), keys.data() + N);
    iterate.eastl = best_ns_per_item([&] {
      u64 sum = 0;
      for (u64 v : a) sum += v;
      return sum;
    });
  }
  insert.eastl = best_ns_per_item([&] {
    eastl::hash_map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert_or_assign(keys[i], i);
    return static_cast<u64>(m.size());
  });
  {
    eastl::hash_map<u64, u64> m;
    for (u32 i = 0; i < N; ++i) m.insert_or_assign(keys[i], i);
    find.eastl = best_ns_per_item([&] {
      u64 sum = 0;
      for (u32 i = 0; i < N; ++i) sum += m.find(keys[i])->second;
      return sum;
    });
  }
  sorting.eastl = best_ns_per_item([&] {
    eastl::vector<u64> a(keys.data(), keys.data() + N);
    eastl::sort(a.begin(), a.end(), [](u64 x, u64 y) { return x < y; });
    return a[N / 2];
  });
#endif

  std::printf("ns per element, best of %d runs, built with %s\n\n", RUNS, OAT_BENCH_OPT);
#if OAT_BENCH_EASTL
  std::printf("| %-28s | %7s | %7s | %7s |\n", "benchmark", "oat", "std", "EASTL");
  std::printf("| %-28s | %7s | %7s | %7s |\n", "---", "---:", "---:", "---:");
#else
  std::printf("| %-28s | %7s | %7s |\n", "benchmark", "oat", "std");
  std::printf("| %-28s | %7s | %7s |\n", "---", "---:", "---:");
#endif
  for (const Row& r : {push, iterate, insert, find, sorting}) print(r);
  std::printf("\n(checksum %llu)\n", static_cast<unsigned long long>(g_sink));
  return 0;
}
