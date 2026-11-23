// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: raw-byte hash keys have no padding
#include <oat/oat.h>

struct Key {
  oat::u8 kind;
#ifdef OAT_EXPECT_FAIL
  oat::u32 id; // three padding bytes sit before this member
#else
  oat::u8 id;
#endif
};

bool use(const oat::Map<Key, int>& map) { return map.contains(Key{1, 2}); }
