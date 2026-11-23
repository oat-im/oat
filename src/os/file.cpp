#include "os_internal.h"

namespace oat::os {

// SDL only says that opening failed. Look at the path to say why.
static std::unexpected<Error> file_open_error(Str path, const char* cpath,
                                              bool creates) {
  detail::set_error_text(SDL_GetError());
  SDL_PathInfo info;
  if (SDL_GetPathInfo(cpath, &info) && info.type != SDL_PATHTYPE_NONE) {
    if (info.type == SDL_PATHTYPE_DIRECTORY) return err(Error::InvalidArgument);
    return err(Error::PermissionDenied);
  }
  if (creates) {
    // The file is absent, which is fine for a mode that creates it; if its
    // directory is there, we were not allowed to create it.
    Str dir = parent(path);
    detail::CStr cdir(dir.empty() ? Str(".") : dir);
    if (SDL_GetPathInfo(cdir.get(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
      return err(Error::PermissionDenied);
  }
  return err(Error::NotFound);
}

Result<File> File::open(Str path, OpenMode mode) {
  const char* sdl_mode = "rb";
  bool creates = false;
  switch (mode) {
    case OpenMode::Read: sdl_mode = "rb"; break;
    case OpenMode::Write:
      sdl_mode = "wb";
      creates = true;
      break;
    case OpenMode::Append:
      sdl_mode = "ab";
      creates = true;
      break;
    case OpenMode::ReadWrite: sdl_mode = "r+b"; break;
  }
  detail::CStr cpath(path);
  SDL_IOStream* io = SDL_IOFromFile(cpath.get(), sdl_mode);
  if (!io) return file_open_error(path, cpath.get(), creates);
  File f;
  f.handle = io;
  return f;
}

Result<usize> File::read(Span<u8> out) {
  OAT_ASSERT(handle != nullptr);
  if (out.empty()) return usize{0};
  usize n = SDL_ReadIO(handle, out.data(), out.size());
  if (n == 0) {
    if (SDL_GetIOStatus(handle) == SDL_IO_STATUS_EOF) return err(Error::EndOfStream);
    return detail::fail(Error::Io);
  }
  return n;
}

Result<void> File::write(Span<const u8> bytes) {
  OAT_ASSERT(handle != nullptr);
  if (bytes.empty()) return {};
  if (SDL_WriteIO(handle, bytes.data(), bytes.size()) != bytes.size())
    return detail::fail(Error::Io);
  return {};
}

Result<u64> File::seek(i64 offset, SeekFrom from) {
  OAT_ASSERT(handle != nullptr);
  // Platforms disagree about seeking before the start; refuse it everywhere.
  if (from == SeekFrom::Start && offset < 0)
    return detail::fail(Error::InvalidArgument, "seek before the start of the file");
  SDL_IOWhence whence = SDL_IO_SEEK_SET;
  if (from == SeekFrom::Current) whence = SDL_IO_SEEK_CUR;
  if (from == SeekFrom::End) whence = SDL_IO_SEEK_END;
  Sint64 pos = SDL_SeekIO(handle, offset, whence);
  if (pos < 0) return detail::fail(Error::InvalidArgument);
  return static_cast<u64>(pos);
}

Result<u64> File::size() {
  OAT_ASSERT(handle != nullptr);
  Sint64 n = SDL_GetIOSize(handle);
  if (n < 0) return detail::fail(Error::Io);
  return static_cast<u64>(n);
}

Result<void> File::close() {
  if (!handle) return {};
  SDL_IOStream* io = handle;
  handle = nullptr;
  if (!SDL_CloseIO(io)) return detail::fail(Error::Io);
  return {};
}

Result<Span<u8>> read_entire_file(Str path, Allocator& a) {
  OAT_TRY(file, File::open(path, OpenMode::Read));
  Result<u64> size = file.size();
  if (!size) {
    (void)file.close();
    return std::unexpected(size.error());
  }
  usize total = static_cast<usize>(*size);
  u8* data = static_cast<u8*>(a.alloc(total + 1, 1));
  usize got = 0;
  while (got < total) {
    Result<usize> n = file.read(Span<u8>(data + got, total - got));
    if (!n) break;
    got += *n;
  }
  (void)file.close();
  if (got != total) {
    // The file changed size while it was being read, or the device failed.
    a.free(data, total + 1);
    return detail::fail(Error::Io, "read_entire_file: short read");
  }
  data[total] = 0;
  return Span<u8>(data, total);
}

Result<void> write_entire_file(Str path, Span<const u8> bytes) {
  Allocator h = heap();
  StrBuf tmp;
  tmp.append(path, h);
  tmp.append(".tmp", h);

  Result<void> result;
  Result<File> file = File::open(tmp.view(), OpenMode::Write);
  if (!file) {
    result = std::unexpected(file.error());
  } else {
    result = file->write(bytes);
    Result<void> closed = file->close();
    if (result && !closed) result = closed;
    if (result) result = rename(tmp.view(), path);
    if (!result) {
      // Keep the message of the failure, not of the clean-up.
      char saved[256];
      Str text = last_error_text();
      std::memcpy(saved, text.data(), text.size());
      saved[text.size()] = '\0';
      (void)remove(tmp.view());
      detail::set_error_text(saved);
    }
  }
  tmp.free(h);
  return result;
}

Result<void> write_entire_file(Str path, Str text) {
  return write_entire_file(
      path, Span<const u8>(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace oat::os
