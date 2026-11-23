// oat/os/fs.h - directories and file metadata.
//
// Error codes here are a best guess. SDL reports failures as text only, so
// oat picks the code by looking at the path afterwards: path_info says
// NotFound for any failure to read the metadata, and a failed open of a file
// that exists says PermissionDenied even when the real cause is a sharing
// violation or running out of file handles. Branch on the code for the common
// cases; os::last_error_text() is the truth.
//
// On Windows, paths longer than 260 characters fail unless long paths are
// enabled for the machine and in the application's manifest.
#pragma once

#include "../array.h"
#include "../base.h"
#include "../memory.h"

namespace oat::os {

enum class PathKind : u8 { File, Directory, Other };

struct PathInfo {
  PathKind kind;
  u64 size;        // bytes; meaningful for files
  i64 modified_ns; // wall-clock nanoseconds since the Unix epoch
};

// NotFound if nothing is there.
[[nodiscard]] Result<PathInfo> path_info(Str path);
bool exists(Str path);

// Notices when a file has been rewritten, by polling: no OS file watching.
// Call poll() as often as you like, typically once a frame. It returns true
// once per new version, and only after the file's size and modified time have
// stayed the same for `settle_ns` of real time, so the answer does not depend
// on the frame rate.
//
// Settling is a heuristic. Some linkers create the output at its full size
// and fill it in through a memory map, so the file can look finished while it
// is still being written. If what you do with the new version fails (a
// library that will not load yet), call retry(): poll() reports the same
// version again after another settle period.
struct FileChange {
  i64 seen_ns = 0;      // modified time at the last poll
  u64 seen_size = 0;
  u64 changed_at = 0;   // now_ns() when the file last looked different
  bool primed = false;  // the first sighting is the baseline, not a change
  bool pending = false; // a new version is waiting to settle

  bool poll(Str path, u64 settle_ns = 250ull * 1000 * 1000);
  void retry();
};

struct DirEntry {
  Str name; // one component, no directory; NUL-terminated
  PathKind kind;
};

// The entries of a directory, sorted by name. The array and every name are
// allocated from `a`, so this is meant for an arena.
[[nodiscard]] Result<Array<DirEntry>> list_dir(Str path, Allocator& a);

// Creates the directory and any missing parents. Succeeds if it already exists.
[[nodiscard]] Result<void> create_dirs(Str path);

// Removes a file or an empty directory. Succeeds if nothing is there.
[[nodiscard]] Result<void> remove(Str path);

// Replaces `to` if it exists. Both must be on the same volume.
[[nodiscard]] Result<void> rename(Str from, Str to);

// The directory the executable is in (the Resources directory inside a macOS
// .app bundle). Ends with '/'. Empty if the platform cannot tell.
Str exe_dir();

// A per-user, per-application directory for saves and settings, created if
// missing. Ends with '/'. The string is allocated from `a` and NUL-terminated.
[[nodiscard]] Result<Str> user_data_dir(Str org, Str app, Allocator& a);

} // namespace oat::os
