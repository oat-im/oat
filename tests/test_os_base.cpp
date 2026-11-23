#include "test.h"

#include <oat/oat.h>
#include <oat/os.h>

#include <cstdlib>

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

TEST(os_dylib_load_copy_sweeps_stale_copies) {
  // What a crashed run leaves behind: copies nobody will ever unload.
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  Str plugin = OAT_TEST_PLUGIN_PATH;
  Str ext = os::extension(plugin);
  Str base = plugin.substr(0, plugin.size() - ext.size());

  StrBuf stale1, stale2, unrelated;
  stale1.append(base, a);
  stale1.append(".live-0badc0de-0", a);
  stale1.append(ext, a);
  stale2.append(base, a);
  stale2.append(".live-0badc0de-7", a);
  stale2.append(ext, a);
  unrelated.append(base, a); // same directory, different pattern: must survive
  unrelated.append(".notes.txt", a);
  CHECK(os::write_entire_file(stale1.view(), "old").has_value());
  CHECK(os::write_entire_file(stale2.view(), "old").has_value());
  CHECK(os::write_entire_file(unrelated.view(), "keep").has_value());

  // Fresh files are not stale yet: with the default age limit they survive,
  // which is what protects a copy another process is about to load.
  Result<os::Library> young = os::Library::load_copy(plugin);
  CHECK(young.has_value());
  CHECK(os::exists(stale1.view()) && os::exists(stale2.view()));
  if (young) {
    // A copy has a fresh modified time of its own, not the original's, so
    // the age limit covers it from the moment it is made.
    Result<os::PathInfo> copy_info = os::path_info(young->copy_path);
    const i64 second = 1000000000ll;
    CHECK(copy_info && copy_info->modified_ns > os::wall_clock() - 30 * second);
    young->unload();
  }

  // With no age limit, everything this process did not make is swept.
  Result<os::Library> first = os::Library::load_copy(plugin, 0);
  CHECK(first.has_value());
  CHECK(!os::exists(stale1.view()) && !os::exists(stale2.view()));
  CHECK(os::exists(unrelated.view()));
  CHECK(os::exists(plugin));

  if (first) {
    // The sweep leaves this process's own live copies alone.
    Result<os::Library> second = os::Library::load_copy(plugin, 0);
    CHECK(second.has_value());
    CHECK(os::exists(first->copy_path));
    auto add = first->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
    CHECK(add && add(2, 2) == 4);
    if (second) second->unload();
    first->unload();
  }
  CHECK(os::remove(unrelated.view()).has_value());
  arena.destroy();
}

TEST(os_dylib_two_processes_reload_one_library) {
  // A client and a server run from one build directory and both hot-reload
  // the same game library. Each process sweeps the other's copies.
  Result<os::Library> held = os::Library::load_copy(OAT_TEST_PLUGIN_PATH);
  CHECK(held.has_value());
  if (!held) return;
  auto held_add = held->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
  CHECK(held_add != nullptr);

  // The other process, running at the same time as the loop below.
  struct Peer {
    int exit_code = -1;
  } peer;
  os::Thread runner = *os::Thread::start(
      "oat-peer",
      [](void* p) -> i32 {
#if defined(_WIN32)
        // cmd.exe strips one outer pair of quotes from the whole line.
        const char* command = "\"\"" OAT_TEST_PEER_PATH "\" \"" OAT_TEST_PLUGIN_PATH "\" 40\"";
#else
        const char* command = "\"" OAT_TEST_PEER_PATH "\" \"" OAT_TEST_PLUGIN_PATH "\" 40";
#endif
        static_cast<Peer*>(p)->exit_code = std::system(command);
        return 0;
      },
      &peer);

  // Meanwhile this process reloads too. Fresh copies are never swept, so
  // neither process disturbs the other; the retry is there, as in the
  // recipe, in case a load fails for some other reason.
  u32 loaded = 0;
  u32 retries = 0;
  for (u32 i = 0; i < 40; ++i) {
    Result<os::Library> lib = os::Library::load_copy(OAT_TEST_PLUGIN_PATH);
    for (u32 attempt = 0; !lib && attempt < 200; ++attempt) {
      ++retries;
      os::sleep_ns(2 * 1000 * 1000);
      lib = os::Library::load_copy(OAT_TEST_PLUGIN_PATH);
    }
    if (!lib) break;
    auto add = lib->symbol_as<int (*)(int, int)>("oat_test_plugin_add");
    if (add && add(i, 1) == static_cast<int>(i) + 1) ++loaded;
    lib->unload();
  }
  runner.join();
  CHECK(loaded == 40);          // every reload here got through
  CHECK(peer.exit_code == 0);   // and every reload in the other process
  (void)retries;                // how often the race was lost varies run to run

  // The copy loaded before all of that still works.
  if (held_add) CHECK(held_add(40, 2) == 42);
  held->unload();
}

TEST(os_hot_reload_signal) {
  // dylib plus a modification time is all hot reload needs.
  Result<os::PathInfo> info = os::path_info(OAT_TEST_PLUGIN_PATH);
  CHECK(info && info->kind == os::PathKind::File && info->size > 0 && info->modified_ns > 0);
}
