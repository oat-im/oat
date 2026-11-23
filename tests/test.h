// The oat test runner: TEST(name) { ... } registers a test, CHECK(cond)
// records a failure, and the process exits non-zero if any CHECK failed.
#pragma once

#include <oat/base.h>

#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace oat_test {

struct Test {
  const char* name;
  void (*fn)();
  Test* next;
};

inline Test* g_tests = nullptr;
inline int g_failures = 0;
inline thread_local bool g_expect_assert = false;
inline thread_local std::jmp_buf g_assert_jump;

struct Register {
  explicit Register(Test* t) {
    t->next = g_tests;
    g_tests = t;
  }
};

inline void fail(const char* file, int line, const char* what) {
  std::printf("%s:%d: %s\n", file, line, what);
  ++g_failures;
}

// While CHECK_ASSERTS is running, a failed oat assert jumps back to the test.
inline void on_assert(const char* expr, const char* file, int line) {
  if (g_expect_assert) std::longjmp(g_assert_jump, 1);
  std::printf("%s:%d: unexpected oat assert: %s\n", file, line, expr);
}

inline int run_all(int argc, char** argv) {
  oat::set_assert_handler(on_assert);
  int ran = 0;
  for (Test* t = g_tests; t; t = t->next) {
    if (argc > 1 && !std::strstr(t->name, argv[1])) continue;
    int before = g_failures;
    std::printf("[ run ] %s\n", t->name);
    std::fflush(stdout); // so a hang or crash shows which test it was in
    t->fn();
    ++ran;
    if (g_failures != before) std::printf("FAILED %s\n", t->name);
  }
  std::printf("%d tests, %d failed checks\n", ran, g_failures);
  return g_failures ? 1 : 0;
}

} // namespace oat_test

#define TEST(name)                                                                                 \
  static void test_##name();                                                                       \
  static oat_test::Test test_entry_##name{#name, test_##name, nullptr};                            \
  static oat_test::Register test_register_##name{&test_entry_##name};                              \
  static void test_##name()

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) oat_test::fail(__FILE__, __LINE__, "CHECK failed: " #cond);                       \
  } while (0)

// Passes only if running `stmt` trips an oat assert.
#define CHECK_ASSERTS(stmt)                                                                        \
  do {                                                                                             \
    oat_test::g_expect_assert = true;                                                              \
    if (setjmp(oat_test::g_assert_jump) == 0) {                                                    \
      stmt;                                                                                        \
      oat_test::g_expect_assert = false;                                                           \
      oat_test::fail(__FILE__, __LINE__, "expected an oat assert: " #stmt);                        \
    }                                                                                              \
    oat_test::g_expect_assert = false;                                                             \
  } while (0)
