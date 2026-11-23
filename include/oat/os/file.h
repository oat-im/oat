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

// Writes to "<path>.tmp" and renames it over the target, so a crash cannot
// leave a half-written file behind.
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
  // Closing flushes, so it can fail. The handle is released either way.
  [[nodiscard]] Result<void> close();

  bool is_open() const { return handle != nullptr; }
};

} // namespace oat::os
