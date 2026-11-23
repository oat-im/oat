// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: container elements are movable with memcpy (FixedArray)
#include <oat/oat.h>

struct Elem {
  int v = 0;
#ifdef OAT_EXPECT_FAIL
  Elem() = default;
  Elem(const Elem& o) : v(o.v) {} // user-provided copy: not trivially copyable, not opted in
#endif
};

void use() {
  oat::FixedArray<Elem, 4> arr;
  arr.push(Elem{});
}
