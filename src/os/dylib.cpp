#include "os_internal.h"

#include <atomic>

namespace oat::os {

Result<Library> Library::load(Str path) {
  detail::CStr cpath(path);
  SDL_SharedObject* object = SDL_LoadObject(cpath.get());
  if (!object) {
    detail::set_error_text(SDL_GetError());
    SDL_PathInfo info;
    // A bare name ("libfoo.so") is searched for by the loader, so a missing
    // file is only certain when the path names a location.
    bool has_dir = path.find('/') != Str::npos;
    if (has_dir && !SDL_GetPathInfo(cpath.get(), &info)) return err(Error::NotFound);
    return err(Error::InvalidArgument);
  }
  Library lib;
  lib.handle = object;
  return lib;
}

Result<Library> Library::load_copy(Str path) {
  static std::atomic<u32> counter{0};
  Allocator h = heap();

  // "dir/game.dll" -> "dir/game.live-<time>-<n>.dll". The extension is kept
  // because Windows uses it; the time keeps two running games apart.
  Str ext = extension(path);
  StrBuf copy;
  copy.append(path.substr(0, path.size() - ext.size()), h);
  copy.appendf(h, ".live-%08x-%u", static_cast<u32>(SDL_GetTicksNS() ^ static_cast<u64>(wall_clock())),
               counter.fetch_add(1));
  copy.append(ext, h);

  detail::CStr cpath(path);
  if (!SDL_CopyFile(cpath.get(), copy.c_str())) {
    detail::set_error_text(SDL_GetError());
    SDL_PathInfo info;
    bool missing = !SDL_GetPathInfo(cpath.get(), &info);
    SDL_RemovePath(copy.c_str()); // a failed copy can leave a partial file
    copy.free(h);
    return err(missing ? Error::NotFound : Error::Io);
  }
  SDL_SharedObject* object = SDL_LoadObject(copy.c_str());
  if (!object) {
    // Most often a library the linker has not finished writing.
    detail::set_error_text(SDL_GetError());
    SDL_RemovePath(copy.c_str());
    copy.free(h);
    return err(Error::InvalidArgument);
  }
  Library lib;
  lib.handle = object;
  lib.copy_path = copy.data; // the StrBuf's storage now belongs to the Library
  lib.copy_path_size = copy.cap;
  return lib;
}

void* Library::symbol(const char* name) {
  OAT_ASSERT(handle != nullptr);
  SDL_FunctionPointer fn = SDL_LoadFunction(handle, name);
  if (!fn) detail::set_error_text(SDL_GetError());
  return reinterpret_cast<void*>(fn);
}

void Library::unload() {
  if (handle) SDL_UnloadObject(handle);
  handle = nullptr;
  if (copy_path) {
    SDL_RemovePath(copy_path);
    Allocator h = heap();
    h.free(copy_path, copy_path_size);
    copy_path = nullptr;
    copy_path_size = 0;
  }
}

} // namespace oat::os
