// A second process for the hot-reload test: it loads and unloads copies of the
// same library the test is using, sweeping the test's copies as it goes.
//
//   oat_reload_peer <library> <iterations>
//
// Exits 0 if every iteration ended with a loaded, working library.
#include <oat/oat.h>
#include <oat/os.h>

using namespace oat;

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  Result<i64> iterations = parse_int(argv[2]);
  if (!iterations) return 2;
  for (i64 i = 0; i < *iterations; ++i) {
    // A load can lose a race with the other process's sweep; like the
    // hot-reload recipe, try again.
    Result<os::Library> lib = os::Library::load_copy(argv[1]);
    for (u32 attempt = 0; !lib && attempt < 200; ++attempt) {
      os::sleep_ns(2 * 1000 * 1000);
      lib = os::Library::load_copy(argv[1]);
    }
    if (!lib) return 1;
    auto add = lib->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
    bool ok = add && add(20, 22) == 42;
    lib->unload();
    if (!ok) return 1;
  }
  return 0;
}
