#include "test.h"

#include <oat/oat.h>
#include <oat/os.h>

using namespace oat;

static std::span<const u8> as_bytes(std::string_view s) {
  return std::span<const u8>(reinterpret_cast<const u8*>(s.data()), s.size());
}
static std::string_view as_text(std::span<u8> s) {
  return std::string_view(reinterpret_cast<const char*>(s.data()), s.size());
}

// Every test works inside a fresh directory next to the test executable.
struct TempDir {
  Arena arena;
  Allocator a;
  std::string_view root;

  explicit TempDir(const char* name) {
    Allocator h = heap();
    arena = Arena::create(h);
    a = arena.allocator();
    root = os::join(os::exe_dir(), name, a);
    wipe(root);
    CHECK(os::create_dirs(root).has_value());
  }
  ~TempDir() {
    wipe(root);
    arena.destroy();
  }
  std::string_view path(std::string_view rel) { return os::join(root, rel, a); }

  void wipe(std::string_view dir) {
    Result<Array<os::DirEntry>> entries = os::list_dir(dir, a);
    if (!entries) return;
    for (os::DirEntry& e : *entries) {
      std::string_view p = os::join(dir, e.name, a);
      if (e.kind == os::PathKind::Directory) wipe(p);
      else CHECK(os::remove(p).has_value());
    }
    CHECK(os::remove(dir).has_value());
  }
};

TEST(file_works_without_sdl_init) {
  // Nothing in these tests calls SDL_Init; the file functions must not need it.
  CHECK(!os::exe_dir().empty());
  CHECK(os::exe_dir().back() == '/');
  CHECK(os::exists(os::exe_dir()));
}

TEST(file_read_write_entire) {
  TempDir tmp("oat_tmp_entire");
  Allocator h = heap();
  std::string_view path = tmp.path("hello.txt");

  CHECK(!os::exists(path));
  Result<std::span<u8>> missing = os::read_entire_file(path, h);
  CHECK(!missing && missing.error() == Error::NotFound);
  CHECK(!os::last_error_text().empty());

  CHECK(os::write_entire_file(path, "hello, oat").has_value());
  CHECK(os::exists(path));
  CHECK(!os::exists(tmp.path("hello.txt.tmp"))); // the temporary was renamed away

  Result<std::span<u8>> data = os::read_entire_file(path, h);
  CHECK(data.has_value());
  if (data) {
    CHECK(as_text(*data) == "hello, oat");
    CHECK(data->data()[data->size()] == 0); // terminated for C-string use
    h.free(data->data(), data->size() + 1);
  }

  // Overwriting replaces the old contents completely.
  CHECK(os::write_entire_file(path, "x").has_value());
  data = os::read_entire_file(path, tmp.a);
  CHECK(data && as_text(*data) == "x");

  // Empty file.
  CHECK(os::write_entire_file(path, "").has_value());
  data = os::read_entire_file(path, tmp.a);
  CHECK(data && data->empty());

  // Binary data larger than any internal buffer.
  Array<u8> big;
  Rng rng = Rng::seeded(50);
  big.resize_uninitialized(300000, h);
  for (u8& b : big) b = static_cast<u8>(rng.next_u64());
  CHECK(os::write_entire_file(path, big.span()).has_value());
  data = os::read_entire_file(path, tmp.a);
  CHECK(data && data->size() == big.len && std::memcmp(data->data(), big.data, big.len) == 0);
  big.free(h);

  // Writing into a directory that does not exist fails cleanly.
  Result<void> no_dir = os::write_entire_file(tmp.path("missing_dir/file.txt"), "x");
  CHECK(!no_dir && no_dir.error() == Error::NotFound);
  // Reading a directory is an error, not a crash.
  CHECK(!os::read_entire_file(tmp.root, tmp.a).has_value());
}

TEST(file_handle) {
  TempDir tmp("oat_tmp_handle");
  std::string_view path = tmp.path("data.bin");

  Result<os::File> missing = os::File::open(path, os::OpenMode::Read);
  CHECK(!missing && missing.error() == Error::NotFound);
  CHECK(os::File::open(path, os::OpenMode::ReadWrite).error() == Error::NotFound);
  CHECK(os::File::open(tmp.root, os::OpenMode::Write).error() == Error::InvalidArgument);

  Result<os::File> w = os::File::open(path, os::OpenMode::Write);
  CHECK(w.has_value());
  if (!w) return;
  CHECK(w->is_open());
  CHECK(w->write(as_bytes("0123456789")).has_value());
  CHECK(w->write(std::span<const u8>()).has_value());
  CHECK(w->flush().has_value()); // forced to disk; the file is still usable
  CHECK(*w->size() == 10);
  CHECK(w->close().has_value());
  CHECK(!w->is_open());
  CHECK(w->close().has_value()); // closing twice is harmless

  Result<os::File> a = os::File::open(path, os::OpenMode::Append);
  CHECK(a && a->write(as_bytes("AB")).has_value() && a->close().has_value());

  Result<os::File> r = os::File::open(path, os::OpenMode::Read);
  CHECK(r.has_value());
  if (!r) return;
  CHECK(*r->size() == 12);
  u8 buf[8];
  Result<usize> n = r->read(buf);
  CHECK(n && *n == 8 && std::memcmp(buf, "01234567", 8) == 0);
  n = r->read(buf);
  CHECK(n && *n == 4 && std::memcmp(buf, "89AB", 4) == 0); // short read at the end
  n = r->read(buf);
  CHECK(!n && n.error() == Error::EndOfStream);

  CHECK(*r->seek(2, os::SeekFrom::Start) == 2);
  CHECK(*r->read(std::span<u8>(buf, 3)) == 3 && std::memcmp(buf, "234", 3) == 0);
  CHECK(*r->seek(-2, os::SeekFrom::Current) == 3);
  CHECK(*r->seek(-1, os::SeekFrom::End) == 11);
  CHECK(*r->read(std::span<u8>(buf, 1)) == 1 && buf[0] == 'B');
  CHECK(!r->seek(-100, os::SeekFrom::Start).has_value());
  CHECK(*r->read(std::span<u8>()) == 0);
  CHECK(r->close().has_value());

  // ReadWrite keeps the contents and can patch them in place.
  Result<os::File> rw = os::File::open(path, os::OpenMode::ReadWrite);
  CHECK(rw.has_value());
  if (!rw) return;
  CHECK(rw->seek(0, os::SeekFrom::Start).has_value());
  CHECK(rw->write(as_bytes("xy")).has_value());
  CHECK(rw->close().has_value());
  Result<std::span<u8>> all = os::read_entire_file(path, tmp.a);
  CHECK(all && as_text(*all) == "xy23456789AB");
}

TEST(fs_directories) {
  TempDir tmp("oat_tmp_dirs");

  Result<os::PathInfo> root = os::path_info(tmp.root);
  CHECK(root && root->kind == os::PathKind::Directory);
  CHECK(os::path_info(tmp.path("nope")).error() == Error::NotFound);

  CHECK(os::create_dirs(tmp.path("a/b/c")).has_value()); // parents are created
  CHECK(os::create_dirs(tmp.path("a/b/c")).has_value()); // already there is fine
  CHECK(os::path_info(tmp.path("a/b"))->kind == os::PathKind::Directory);

  CHECK(os::write_entire_file(tmp.path("a/zeta.txt"), "12345").has_value());
  CHECK(os::write_entire_file(tmp.path("a/alpha.txt"), "1").has_value());
  CHECK(os::create_dirs(tmp.path("a/file.txt/sub")).has_value() == false ||
        os::path_info(tmp.path("a/file.txt"))->kind == os::PathKind::Directory);

  Result<os::PathInfo> info = os::path_info(tmp.path("a/zeta.txt"));
  CHECK(info && info->kind == os::PathKind::File && info->size == 5);
  i64 now = os::wall_clock();
  const i64 minute = 60ll * 1000000000ll;
  CHECK(info && info->modified_ns > now - minute && info->modified_ns < now + minute);

  // A file in the way of a directory.
  Result<void> blocked = os::create_dirs(tmp.path("a/alpha.txt"));
  CHECK(!blocked && blocked.error() == Error::AlreadyExists);

  // Listing is sorted by name and reports kinds.
  CHECK(os::remove(tmp.path("a/file.txt/sub")).has_value());
  CHECK(os::remove(tmp.path("a/file.txt")).has_value());
  Result<Array<os::DirEntry>> list = os::list_dir(tmp.path("a"), tmp.a);
  CHECK(list.has_value());
  if (list) {
    CHECK(list->len == 3);
    if (list->len == 3) {
      CHECK((*list)[0].name == "alpha.txt" && (*list)[0].kind == os::PathKind::File);
      CHECK((*list)[1].name == "b" && (*list)[1].kind == os::PathKind::Directory);
      CHECK((*list)[2].name == "zeta.txt" && (*list)[2].kind == os::PathKind::File);
      CHECK((*list)[0].name.data()[9] == '\0');
    }
  }
  Result<Array<os::DirEntry>> empty = os::list_dir(tmp.path("a/b/c"), tmp.a);
  CHECK(empty && empty->len == 0);
  CHECK(os::list_dir(tmp.path("nope"), tmp.a).error() == Error::NotFound);
  CHECK(os::list_dir(tmp.path("a/zeta.txt"), tmp.a).error() == Error::InvalidArgument);

  // rename moves, and replaces an existing target.
  CHECK(os::rename(tmp.path("a/zeta.txt"), tmp.path("a/alpha.txt")).has_value());
  CHECK(!os::exists(tmp.path("a/zeta.txt")));
  Result<std::span<u8>> moved = os::read_entire_file(tmp.path("a/alpha.txt"), tmp.a);
  CHECK(moved && as_text(*moved) == "12345");
  CHECK(os::rename(tmp.path("nope"), tmp.path("x")).error() == Error::NotFound);

  // remove: files, empty directories, and things that are already gone.
  Result<void> not_empty = os::remove(tmp.path("a"));
  CHECK(!not_empty && not_empty.error() == Error::Io);
  CHECK(!os::last_error_text().empty());
  CHECK(os::remove(tmp.path("a/alpha.txt")).has_value());
  CHECK(os::remove(tmp.path("a/alpha.txt")).has_value());
  CHECK(os::remove(tmp.path("a/b/c")).has_value());
  CHECK(!os::exists(tmp.path("a/b/c")));
}

TEST(fs_file_change) {
  // Nothing here depends on a sleep being short, only on it being at least
  // as long as asked: "too soon" is checked with a settle time of an hour,
  // "settled" with one shorter than the sleep before it.
  TempDir tmp("oat_tmp_change");
  Str path = tmp.path("game.lib");
  const u64 ms = 1000 * 1000;
  const u64 never = 3600 * 1000 * ms; // longer than the test runs
  const u64 quick = 20 * ms;
  const u64 nap = 30 * ms; // longer than `quick`
  os::FileChange change;
  CHECK(!change.poll(path, quick)); // nothing there yet

  CHECK(os::write_entire_file(path, "version one").has_value());
  CHECK(!change.poll(path, quick)); // the first sighting is the baseline, not a change
  os::sleep_ns(nap);
  CHECK(!change.poll(path, quick));

  // A rewrite is reported once, and only after it has been still for the
  // settle time, however many polls that takes.
  CHECK(os::write_entire_file(path, "version two, longer").has_value());
  u32 polls = 0;
  u64 start = os::now_ns();
  while (!change.poll(path, 50 * ms) && os::now_ns() - start < 5000 * ms) ++polls;
  u64 waited = os::now_ns() - start;
  CHECK(waited >= 50 * ms && waited < 5000 * ms);
  CHECK(polls > 3); // polling fast does not make it report sooner
  os::sleep_ns(nap);
  CHECK(!change.poll(path, quick)); // reported once

  // A file that keeps changing is not reported until it stops.
  CHECK(os::write_entire_file(path, "3").has_value());
  CHECK(!change.poll(path, never)); // seen; the clock starts
  os::sleep_ns(nap);
  CHECK(os::write_entire_file(path, "3 and more").has_value()); // the linker is still going
  CHECK(!change.poll(path, quick)); // changed again, so the clock starts over
  CHECK(!change.poll(path, never)); // still inside the settle time
  os::sleep_ns(nap);
  CHECK(change.poll(path, quick));

  // retry(): the same version is reported again after another settle period,
  // for when loading it failed.
  change.retry();
  CHECK(!change.poll(path, never));
  os::sleep_ns(nap);
  CHECK(change.poll(path, quick));
  CHECK(!change.poll(path, quick));

  // Vanishing for a moment (replace by delete and create) is not a change by itself.
  CHECK(os::remove(path).has_value());
  os::sleep_ns(nap);
  CHECK(!change.poll(path, quick));
  CHECK(os::write_entire_file(path, "4th version").has_value());
  CHECK(!change.poll(path, never));
  os::sleep_ns(nap);
  CHECK(change.poll(path, quick));
}

TEST(fs_long_and_unicode_paths) {
  TempDir tmp("oat_tmp_paths");
  // UTF-8 names round-trip through the file system.
  std::string_view unicode = tmp.path("caf\xc3\xa9-\xe6\x97\xa5\xe6\x9c\xac.txt");
  CHECK(os::write_entire_file(unicode, "ok").has_value());
  Result<Array<os::DirEntry>> list = os::list_dir(tmp.root, tmp.a);
  CHECK(list && list->len == 1);
  if (list && list->len == 1) CHECK((*list)[0].name == os::filename(unicode));
  CHECK(os::remove(unicode).has_value());

  // A path longer than the small on-stack buffer oat uses for SDL calls.
  Allocator h = heap();
  StrBuf deep;
  deep.append(tmp.root, h);
  for (u32 i = 0; i < 12; ++i) deep.append("/a-directory-name-that-is-fifty-characters-long-00", h);
  CHECK(deep.len > 600);
  CHECK(!os::exists(deep.view()));
  CHECK(os::path_info(deep.view()).error() == Error::NotFound);
#if !defined(_WIN32)
  // Windows limits paths to 260 characters unless long paths are switched on
  // for the machine and the application, so the rest runs elsewhere.
  CHECK(os::create_dirs(deep.view()).has_value());
  CHECK(os::exists(deep.view()));
  deep.append("/f.txt", h);
  CHECK(os::write_entire_file(deep.view(), "deep").has_value());
  Result<std::span<u8>> data = os::read_entire_file(deep.view(), tmp.a);
  CHECK(data && as_text(*data) == "deep");
#endif
  deep.free(h);
}

TEST(fs_user_data_dir) {
  Allocator h = heap();
  Arena arena = Arena::create(h);
  Allocator a = arena.allocator();
  Result<std::string_view> dir = os::user_data_dir("oat-tests", "oat-os-test", a);
  CHECK(dir.has_value());
  if (dir) {
    CHECK(!dir->empty() && dir->back() == '/');
    CHECK(dir->find('\\') == std::string_view::npos);
    CHECK(os::exists(*dir)); // created on demand
    // Leave nothing behind: the app directory, then the org directory.
    std::string_view app_dir = dir->substr(0, dir->size() - 1);
    CHECK(os::remove(app_dir).has_value());
    (void)os::remove(os::parent(app_dir)); // fails harmlessly if the user keeps other things there
  }
  arena.destroy();
}
