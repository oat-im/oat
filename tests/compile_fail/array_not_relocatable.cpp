// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: container elements are movable with memcpy
#include <oat/oat.h>

struct Bad {
  Bad* self;
  Bad() : self(this) {}
  Bad(const Bad&) : self(this) {} // keeps a pointer to itself: cannot be memcpy'd
};
struct Good {
  int v;
};
#ifdef OAT_EXPECT_FAIL
using Elem = Bad;
#else
using Elem = Good;
#endif

void use(oat::Allocator& a) {
  oat::Array<Elem> arr;
  arr.push(Elem{}, a);
}
