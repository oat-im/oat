// oat/os/fs.h - directories and file metadata.
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
