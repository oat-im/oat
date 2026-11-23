// oat/os/file.h - file I/O.
#pragma once

#include "../base.h"
#include "../memory.h"

struct SDL_IOStream;

namespace oat::os {

// Reads a whole file. The buffer is allocated from `a` with one extra zero
// byte after the end, so text can be used as a C string. Free it with
// a.free(bytes.data(), bytes.size() + 1), or use an arena.
[[nodiscard]] Result<Span<u8>> read_entire_file(Str path, Allocator& a);

// Replaces the file safely: writes "<path>.tmp", forces its contents to disk,
// then renames it over the target. After a crash or a power cut the path holds
// either the complete old contents or the complete new ones, never a partial
// or empty file. (Whether the rename itself has reached the disk at the moment
// power is lost is up to the file system; either outcome is a whole file.)
// The sync makes this slow, tens of milliseconds on some disks: use it for
// saves and settings, not for bulk output.
[[nodiscard]] Result<void> write_entire_file(Str path, Span<const u8> bytes);
[[nodiscard]] Result<void> write_entire_file(Str path, Str text);

enum class OpenMode : u8 {
  Read,      // must exist
  Write,     // created or truncated
  Append,    // created if missing; writes go to the end
  ReadWrite, // must exist; contents kept
};

enum class SeekFrom : u8 { Start, Current, End };

struct File {
  SDL_IOStream* handle = nullptr;

  [[nodiscard]] static Result<File> open(Str path, OpenMode mode);

  // Returns how many bytes were read, which can be fewer than asked for.
  // EndOfStream once there is nothing left.
  [[nodiscard]] Result<usize> read(Span<u8> out);
  // Writes everything or fails.
  [[nodiscard]] Result<void> write(Span<const u8> bytes);
  // Returns the new position from the start of the file.
  [[nodiscard]] Result<u64> seek(i64 offset, SeekFrom from);
  [[nodiscard]] Result<u64> size();
  // Forces everything written so far onto the disk, past the operating
  // system's cache. Slow; only for data that must survive a power cut.
  [[nodiscard]] Result<void> flush();
  // Hands buffered writes to the operating system, so it can fail. It does
  // not force them to disk: call flush() first if that matters. The handle is
  // released either way.
  [[nodiscard]] Result<void> close();

  bool is_open() const { return handle != nullptr; }
};

} // namespace oat::os
