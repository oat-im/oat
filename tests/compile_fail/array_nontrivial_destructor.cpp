// Must not compile when OAT_EXPECT_FAIL is defined, and must compile without it.
// Rule: containers never run element destructors
#include <oat/oat.h>

struct Owner {
  int* p = nullptr;
#ifdef OAT_EXPECT_FAIL
  ~Owner() { delete p; } // would never run
#endif
};
// Opted in, so only the destructor rule can reject it.
template <>
constexpr bool oat::is_relocatable<Owner> = true;

void use(oat::Allocator& a) {
  oat::Array<Owner> arr;
  arr.push(Owner{}, a);
}
