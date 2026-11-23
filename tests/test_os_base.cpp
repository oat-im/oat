#include "test.h"

#include <oat/oat.h>
#include <oat/os.h>

using namespace oat;

// os::init has to run before SDL allocates anything, so it runs before main.
static const bool g_init_ok = os::init().has_value();

TEST(os_init) {
  CHECK(g_init_ok);
  CHECK(os::init().has_value()); // calling it again is fine
}

#if OAT_DEBUG
TEST(os_sdl_memory_is_tracked) {
  // An SDL object allocated now shows up in oat's heap statistics.
  HeapStats before = heap_stats();
  os::Mutex m = os::Mutex::create();
  HeapStats during = heap_stats();
  CHECK(during.live_count > before.live_count);
  CHECK(during.live_bytes > before.live_bytes);
  m.destroy();
  CHECK(heap_stats().live_count == before.live_count);
  CHECK(heap_stats().live_bytes == before.live_bytes);
}
#endif

TEST(os_time) {
  u64 a = os::now_ns();
  os::sleep_ns(5 * 1000 * 1000);
  u64 b = os::now_ns();
  CHECK(b > a);
  CHECK(b - a >= 4 * 1000 * 1000);          // slept about 5 ms
  CHECK(b - a < 2000ull * 1000 * 1000);     // and not seconds

  i64 wall = os::wall_clock();
  CHECK(wall > 1700000000ll * 1000000000ll); // after November 2023

  Result<os::Calendar> utc = os::to_calendar(wall, false);
  CHECK(utc.has_value());
  if (utc) {
    CHECK(utc->year >= 2024 && utc->month >= 1 && utc->month <= 12);
    CHECK(utc->day >= 1 && utc->day <= 31 && utc->hour >= 0 && utc->hour < 24);
    CHECK(utc->utc_offset == 0);
  }
  // A known instant: 2000-03-01 12:34:56.5 UTC, a Wednesday.
  Result<os::Calendar> known = os::to_calendar(951914096500000000ll, false);
  CHECK(known.has_value());
  if (known) {
    CHECK(known->year == 2000 && known->month == 3 && known->day == 1);
    CHECK(known->hour == 12 && known->minute == 34 && known->second == 56);
    CHECK(known->nanosecond == 500000000 && known->day_of_week == 3);
  }
  CHECK(os::to_calendar(wall, true).has_value());
}

TEST(os_dylib) {
  Result<os::Library> lib = os::Library::load(OAT_TEST_PLUGIN_PATH);
  CHECK(lib.has_value());
  if (lib) {
    auto add = lib->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
    CHECK(add != nullptr);
    if (add) CHECK(add(40, 2) == 42);
    CHECK(lib->symbol("no_such_function") == nullptr);
    CHECK(!os::last_error_text().empty());
    lib->unload();
    CHECK(lib->handle == nullptr);
    lib->unload(); // harmless
  }

  Result<os::Library> missing = os::Library::load("/no/such/dir/libmissing.so");
  CHECK(!missing && missing.error() == Error::NotFound);
  CHECK(!os::last_error_text().empty());

  // A file that exists but is not a library.
  Allocator h = heap();
  StrBuf path;
  path.append(os::exe_dir(), h);
  path.append("not_a_library.txt", h);
  CHECK(os::write_entire_file(path.view(), "plain text").has_value());
  Result<os::Library> bad = os::Library::load(path.view());
  CHECK(!bad && bad.error() == Error::InvalidArgument);
  CHECK(os::remove(path.view()).has_value());
  path.free(h);
}

TEST(os_dylib_load_copy) {
  // The copy is what gets loaded, so the original stays free to be rebuilt.
  Result<os::Library> lib = os::Library::load_copy(OAT_TEST_PLUGIN_PATH);
  CHECK(lib.has_value());
  if (lib) {
    CHECK(lib->copy_path != nullptr);
    Str copy(lib->copy_path);
    CHECK(copy != Str(OAT_TEST_PLUGIN_PATH));
    CHECK(os::extension(copy) == os::extension(OAT_TEST_PLUGIN_PATH));
    CHECK(os::parent(copy) == os::parent(OAT_TEST_PLUGIN_PATH));
    CHECK(os::exists(copy));
    auto add = lib->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
    CHECK(add != nullptr);
    if (add) CHECK(add(1, 2) == 3);

    // A second copy can be loaded while the first is in use: this is a reload.
    Result<os::Library> next = os::Library::load_copy(OAT_TEST_PLUGIN_PATH);
    CHECK(next.has_value());
    if (next) {
      CHECK(Str(next->copy_path) != copy);
      next->unload();
    }

    Allocator h = heap();
    const char* copy_name = to_cstr(copy, h);
    lib->unload();
    CHECK(lib->copy_path == nullptr);
    CHECK(!os::exists(copy_name)); // unload cleans the copy up
    h.free(const_cast<char*>(copy_name), copy.size() + 1);
  }
  CHECK(os::Library::load_copy("/no/such/dir/libmissing.so").error() == Error::NotFound);
}

TEST(os_hot_reload_signal) {
  // dylib plus a modification time is all hot reload needs.
  Result<os::PathInfo> info = os::path_info(OAT_TEST_PLUGIN_PATH);
  CHECK(info && info->kind == os::PathKind::File && info->size > 0 && info->modified_ns > 0);
}
