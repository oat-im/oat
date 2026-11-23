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

// Identifies this process's copies. Not unique in any strong sense; it only
// has to differ from the run that crashed and left files behind.
static u32 dylib_process_id() {
  static const u32 id = static_cast<u32>(SDL_GetTicksNS() ^ static_cast<u64>(wall_clock())) | 1;
  return id;
}

// Removes "<stem>.live-*<ext>" files in the library's directory that this
// process did not create and that are older than `stale_after_ns`: what a
// crashed run left behind. The age limit is what keeps this from touching a
// copy another process made a moment ago and is about to load.
static void dylib_sweep_stale_copies(Str path, Str own_marker, u64 stale_after_ns) {
  i64 newest_stale = wall_clock() - static_cast<i64>(stale_after_ns);
  Allocator h = heap();
  Arena arena = Arena::create(h, 16 * 1024);
  Allocator a = arena.allocator();
  Str ext = extension(path);
  Str dir = parent(path);
  if (dir.empty()) dir = ".";
  StrBuf prefix; // "game.live-"
  prefix.append(stem(path), a);
  prefix.append(".live-", a);

  Result<Array<DirEntry>> entries = list_dir(dir, a);
  if (entries) {
    for (const DirEntry& e : *entries) {
      Str name = e.name;
      if (e.kind != PathKind::File || !name.starts_with(prefix.view()) || !name.ends_with(ext))
        continue;
      if (name.substr(prefix.len).starts_with(own_marker)) continue; // one of ours, maybe loaded
      Str full = join(dir, name, a);
      Result<PathInfo> info = path_info(full);
      if (!info || info->modified_ns > newest_stale) continue; // too fresh to be abandoned
      (void)remove(full); // fails harmlessly if another process holds it open
    }
  }
  arena.destroy();
}

Result<Library> Library::load_copy(Str path, u64 stale_after_ns) {
  static std::atomic<u32> counter{0};
  Allocator h = heap();

  char marker[16]; // "<id>-", shared by every copy this process makes
  SDL_snprintf(marker, sizeof(marker), "%08x-", dylib_process_id());
  dylib_sweep_stale_copies(path, marker, stale_after_ns);

  // "dir/game.dll" -> "dir/game.live-<id>-<n>.dll". The extension is kept
  // because Windows uses it.
  Str ext = extension(path);
  StrBuf copy;
  copy.append(path.substr(0, path.size() - ext.size()), h);
  copy.appendf(h, ".live-%s%u", marker, counter.fetch_add(1));
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
