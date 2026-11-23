// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: container elements are movable with memcpy (RingBuffer)
#include <oat/oat.h>

struct Elem {
  int v = 0;
#ifdef OAT_EXPECT_FAIL
  Elem() = default;
  Elem(const Elem& o) : v(o.v) {}
#endif
};

void use(oat::Allocator& a) {
  oat::RingBuffer<Elem> ring = oat::RingBuffer<Elem>::create(4, a);
  ring.free(a);
}
