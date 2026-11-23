#include "test.h"

#include <oat/oat.h>

using namespace oat;

static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8);
static_assert(sizeof(i8) == 1 && sizeof(i16) == 2 && sizeof(i32) == 4 && sizeof(i64) == 8);
static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
static_assert(sizeof(usize) == sizeof(void*) && sizeof(isize) == sizeof(void*));
static_assert(sizeof(Error) == 4);
static_assert(sizeof(Array<int>) == 16);

struct OwnsPointer {
  int* p;
  OwnsPointer(const OwnsPointer& o) : p(o.p) {} // user-provided copy: not trivially copyable
};
template <>
constexpr bool oat::is_relocatable<OwnsPointer> = true;

struct NotRelocatable {
  NotRelocatable* self;
  NotRelocatable(const NotRelocatable&) : self(this) {}
};

static_assert(is_relocatable<int>);
static_assert(is_relocatable<Array<int>>);
static_assert(is_relocatable<Map<int, int>>);
static_assert(is_relocatable<StrBuf>);
static_assert(is_relocatable<Pool<int>>);
static_assert(is_relocatable<OwnsPointer>);
static_assert(!is_relocatable<NotRelocatable>);

TEST(base_min_max_clamp_swap) {
  CHECK(oat::min(3, 5) == 3);
  CHECK(oat::max(3, 5) == 5);
  CHECK(oat::clamp(7, 0, 5) == 5);
  CHECK(oat::clamp(-1, 0, 5) == 0);
  CHECK(oat::clamp(3, 0, 5) == 3);
  int a = 1, b = 2;
  oat::swap(a, b);
  CHECK(a == 2 && b == 1);
  int arr[7];
  CHECK(array_count(arr) == 7);
}

static Result<int> half(int v) {
  if (v % 2) return err(Error::InvalidArgument);
  return v / 2;
}

static Result<int> quarter(int v) {
  OAT_TRY(h, half(v));
  OAT_TRY(q, half(h));
  return q;
}

static Result<void> must_be_even(int v) {
  OAT_TRY_VOID(half(v));
  return {};
}

TEST(base_result_and_try) {
  Result<int> ok = quarter(12);
  CHECK(ok && *ok == 3);
  Result<int> bad = quarter(6); // 6 -> 3 -> odd
  CHECK(!bad && bad.error() == Error::InvalidArgument);
  CHECK(must_be_even(4).has_value());
  CHECK(!must_be_even(5).has_value());
  CHECK(std::strcmp(error_name(Error::NotFound), "NotFound") == 0);
  CHECK(std::strcmp(error_name(Error::Unknown), "Unknown") == 0);
}

TEST(base_assert_handler) {
  CHECK_ASSERTS(OAT_CHECK(1 == 2));
  CHECK_ASSERTS(assert_fail("x", __FILE__, __LINE__));
#if OAT_DEBUG
  CHECK_ASSERTS(OAT_ASSERT(false));
  CHECK_ASSERTS(OAT_ASSERT_MSG(false, "message"));
  CHECK_ASSERTS(OAT_UNREACHABLE());
#endif
  OAT_ASSERT(true);
  OAT_CHECK(true);
}
