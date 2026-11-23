// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: containers never run element destructors (Pool)
#include <oat/oat.h>

struct Elem {
  int v = 0;
#ifdef OAT_EXPECT_FAIL
  ~Elem() {}
#endif
};
template <>
constexpr bool oat::is_relocatable<Elem> = true;

void use(oat::Allocator& a) {
  oat::Pool<Elem> pool;
  pool.add(Elem{}, a);
}
