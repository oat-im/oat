#include "os_internal.h"

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

void* Library::symbol(const char* name) {
  OAT_ASSERT(handle != nullptr);
  SDL_FunctionPointer fn = SDL_LoadFunction(handle, name);
  if (!fn) detail::set_error_text(SDL_GetError());
  return reinterpret_cast<void*>(fn);
}

void Library::unload() {
  if (handle) SDL_UnloadObject(handle);
  handle = nullptr;
}

} // namespace oat::os
