// Must not compile with oat's flags when OAT_EXPECT_FAIL is defined.
// Rule: no exceptions
#include <oat/oat.h>

int parse(int x) {
#ifdef OAT_EXPECT_FAIL
  try {
    if (x < 0) throw 1;
  } catch (int) {
    return 0;
  }
#endif
  return x;
}
