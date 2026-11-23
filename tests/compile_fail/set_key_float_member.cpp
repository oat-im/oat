// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: raw-byte hash keys have no floats (Set, struct member)
#include <oat/oat.h>

struct Key {
  oat::i32 x;
#ifdef OAT_EXPECT_FAIL
  oat::f32 y;
#else
  oat::i32 y;
#endif
};

void use(oat::Allocator& a) {
  oat::Set<Key> set;
  set.insert(Key{1, 2}, a);
}
