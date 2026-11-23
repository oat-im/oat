#include "os_internal.h"

namespace oat::os {

static PathKind fs_kind(SDL_PathType type) {
  switch (type) {
    case SDL_PATHTYPE_FILE: return PathKind::File;
    case SDL_PATHTYPE_DIRECTORY: return PathKind::Directory;
    default: return PathKind::Other;
  }
}

Result<PathInfo> path_info(Str path) {
  detail::CStr cpath(path);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(cpath.get(), &info) || info.type == SDL_PATHTYPE_NONE)
    return detail::fail(Error::NotFound);
  PathInfo out;
  out.kind = fs_kind(info.type);
  out.size = info.size;
  out.modified_ns = info.modify_time;
  return out;
}

bool exists(Str path) {
  detail::CStr cpath(path);
  SDL_PathInfo info;
  return SDL_GetPathInfo(cpath.get(), &info) && info.type != SDL_PATHTYPE_NONE;
}

bool FileChange::poll(Str path, u32 settle_polls) {
  detail::CStr cpath(path);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(cpath.get(), &info) || info.type != SDL_PATHTYPE_FILE) {
    settled = 0; // missing for a moment while it is being replaced
    return false;
  }
  i64 now_time = info.modify_time;
  u64 now_size = info.size;
  if (!primed) {
    primed = true;
    known_ns = seen_ns = now_time;
    known_size = seen_size = now_size;
    return false;
  }
  if (now_time != seen_ns || now_size != seen_size) {
    seen_ns = now_time; // still changing: start counting again
    seen_size = now_size;
    settled = 0;
    return false;
  }
  if (seen_ns == known_ns && seen_size == known_size) return false;
  if (++settled < settle_polls) return false;
  known_ns = seen_ns;
  known_size = seen_size;
  settled = 0;
  return true;
}

struct ListDirState {
  Array<DirEntry>* entries;
  Allocator* allocator;
  StrBuf full_path; // scratch, on the heap
};

static SDL_EnumerationResult SDLCALL list_dir_callback(void* userdata, const char* dirname,
                                                       const char* fname) {
  ListDirState* state = static_cast<ListDirState*>(userdata);
  Str name(fname);
  if (name == "." || name == "..") return SDL_ENUM_CONTINUE;

  // SDL promises that dirname ends with a separator.
  Allocator h = heap();
  state->full_path.clear();
  state->full_path.append(dirname, h);
  state->full_path.append(name, h);
  SDL_PathInfo info;
  DirEntry entry;
  entry.kind = SDL_GetPathInfo(state->full_path.c_str(), &info) ? fs_kind(info.type)
                                                               : PathKind::Other;
  entry.name = detail::copy_path(name, *state->allocator);
  state->entries->push(entry, *state->allocator);
  return SDL_ENUM_CONTINUE;
}

Result<Array<DirEntry>> list_dir(Str path, Allocator& a) {
  detail::CStr cpath(path);
  Array<DirEntry> entries;
  ListDirState state;
  state.entries = &entries;
  state.allocator = &a;
  bool ok = SDL_EnumerateDirectory(cpath.get(), list_dir_callback, &state);
  Allocator h = heap();
  state.full_path.free(h);
  if (!ok) {
    detail::set_error_text(SDL_GetError());
    for (DirEntry& e : entries) a.free(const_cast<char*>(e.name.data()), e.name.size() + 1);
    entries.free(a);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(cpath.get(), &info)) return err(Error::NotFound);
    if (info.type != SDL_PATHTYPE_DIRECTORY) return err(Error::InvalidArgument);
    return err(Error::PermissionDenied);
  }
  oat::sort(entries.span(), [](const DirEntry& x, const DirEntry& y) { return x.name < y.name; });
  return entries;
}

Result<void> create_dirs(Str path) {
  detail::CStr cpath(path);
  if (SDL_CreateDirectory(cpath.get())) return {};
  detail::set_error_text(SDL_GetError());
  SDL_PathInfo info;
  if (SDL_GetPathInfo(cpath.get(), &info) && info.type != SDL_PATHTYPE_DIRECTORY)
    return err(Error::AlreadyExists); // something that is not a directory is in the way
  return err(Error::Io);
}

Result<void> remove(Str path) {
  detail::CStr cpath(path);
  if (SDL_RemovePath(cpath.get())) return {};
  detail::set_error_text(SDL_GetError());
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(cpath.get(), &info)) return {}; // nothing there: already removed
  return err(Error::Io);                   // e.g. a directory that is not empty
}

Result<void> rename(Str from, Str to) {
  detail::CStr cfrom(from);
  detail::CStr cto(to);
  if (SDL_RenamePath(cfrom.get(), cto.get())) return {};
  detail::set_error_text(SDL_GetError());
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(cfrom.get(), &info)) return err(Error::NotFound);
  return err(Error::Io);
}

Str exe_dir() {
  struct Cached {
    char text[2048];
    usize len = 0;
    Cached() {
      const char* base = SDL_GetBasePath();
      if (!base) return;
      usize n = std::strlen(base);
      if (n >= sizeof(text)) return;
      for (usize i = 0; i < n; ++i) text[i] = base[i] == '\\' ? '/' : base[i];
      text[n] = '\0';
      len = n;
    }
  };
  static const Cached cached; // initialized once, thread-safely
  return Str(cached.text, cached.len);
}

Result<Str> user_data_dir(Str org, Str app, Allocator& a) {
  detail::CStr corg(org);
  detail::CStr capp(app);
  char* pref = SDL_GetPrefPath(corg.get(), capp.get());
  if (!pref) return detail::fail(Error::Io);
  Str out = detail::copy_path(pref, a);
  SDL_free(pref);
  return out;
}

} // namespace oat::os
