// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: container elements are movable with memcpy (Map values)
#include <oat/oat.h>

struct Value {
  int v = 0;
#ifdef OAT_EXPECT_FAIL
  Value() = default;
  Value(const Value& o) : v(o.v) {}
#endif
};

void use(oat::Allocator& a) {
  oat::Map<oat::u32, Value> map;
  map.insert(1, Value{}, a);
}
