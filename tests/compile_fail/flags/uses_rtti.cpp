// Must not compile with oat's flags when OAT_EXPECT_FAIL is defined.
// Rule: no RTTI
#include <oat/oat.h>

struct Base {
  virtual int id() { return 0; }
};
struct Derived : Base {
  int id() override { return 1; }
};

Derived* as_derived(Base* b) {
#ifdef OAT_EXPECT_FAIL
  return dynamic_cast<Derived*>(b);
#else
  return static_cast<Derived*>(b);
#endif
}
