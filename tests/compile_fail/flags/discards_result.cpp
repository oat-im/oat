// Must not compile with oat's flags (warnings as errors) when OAT_EXPECT_FAIL
// is defined.
// Rule: fallible functions return Result, and the result cannot be ignored
#include <oat/oat.h>

oat::i64 read(oat::Str text) {
#ifdef OAT_EXPECT_FAIL
  oat::parse_int(text); // the Result is dropped
  return 0;
#else
  oat::Result<oat::i64> r = oat::parse_int(text);
  return r ? *r : 0;
#endif
}
