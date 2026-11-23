// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: raw-byte hash keys have no floats
#include <oat/oat.h>

#ifdef OAT_EXPECT_FAIL
using Key = oat::f32; // -0.0f == 0.0f but their bytes differ
#else
using Key = oat::u32;
#endif

void use(oat::Allocator& a) {
  oat::Map<Key, int> map;
  map.insert(Key{}, 1, a);
}
