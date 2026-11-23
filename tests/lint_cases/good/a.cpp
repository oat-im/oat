#include "game.h"
#include <oat/oat.h>
using namespace oat;
int f(Option<int> o, Span<int> s) { return o ? *o : oat::move(s)[0]; }
Expected<int, Error> g(Str v);
Atomic<u32> counter;
int lines[] = {1, 2}; // brackets; and semicolons
