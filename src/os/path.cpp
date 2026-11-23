#include "os_internal.h"

namespace oat::os {

static bool path_has_drive(Str p) {
  if (p.size() < 2 || p[1] != ':') return false;
  char c = p[0];
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

Str filename(Str path) {
  usize slash = path.rfind('/');
  return slash == Str::npos ? path : path.substr(slash + 1);
}

Str extension(Str path) {
  Str name = filename(path);
  if (name == "." || name == "..") return Str();
  usize dot = name.rfind('.');
  if (dot == Str::npos || dot == 0) return Str();
  return name.substr(dot);
}

Str stem(Str path) {
  Str name = filename(path);
  return name.substr(0, name.size() - extension(path).size());
}

Str parent(Str path) {
  usize slash = path.rfind('/');
  if (slash == Str::npos) return Str();
  if (slash == 0) return path.substr(0, 1);
  return path.substr(0, slash);
}

bool is_absolute(Str path) {
  if (!path.empty() && path[0] == '/') return true;
  return path_has_drive(path) && path.size() >= 3 && path[2] == '/';
}

Str join(Str left, Str right, Allocator& a) {
  if (left.empty() || is_absolute(right)) return detail::copy_path(right, a);
  if (right.empty()) return detail::copy_path(left, a);
  bool need_slash = left.back() != '/';
  usize size = left.size() + (need_slash ? 1 : 0) + right.size();
  char* p = static_cast<char*>(a.alloc(size + 1, 1));
  std::memcpy(p, left.data(), left.size());
  usize at = left.size();
  if (need_slash) p[at++] = '/';
  std::memcpy(p + at, right.data(), right.size());
  p[size] = '\0';
  return Str(p, size);
}

Str normalize(Str path, Allocator& a) {
  // The result is never longer than the input, except "" -> ".".
  usize room = path.size() < 1 ? 1 : path.size();
  char* out = static_cast<char*>(a.alloc(room + 1, 1));
  usize len = 0;
  usize i = 0;

  // The root, if any, is copied through and never popped: "/", "C:" or "C:/".
  if (path_has_drive(path)) {
    out[len++] = path[0];
    out[len++] = ':';
    i = 2;
  }
  bool rooted = i < path.size() && (path[i] == '/' || path[i] == '\\');
  if (rooted) out[len++] = '/';
  usize root_len = len;

  while (i < path.size()) {
    while (i < path.size() && (path[i] == '/' || path[i] == '\\')) ++i;
    usize start = i;
    while (i < path.size() && path[i] != '/' && path[i] != '\\') ++i;
    Str part = path.substr(start, i - start);
    if (part.empty() || part == ".") continue;

    if (part == "..") {
      // Find the last component written so far.
      usize last = len;
      while (last > root_len && out[last - 1] != '/') --last;
      Str prev(out + last, len - last);
      if (!prev.empty() && prev != "..") {
        len = last > root_len ? last - 1 : root_len; // drop it and its separator
        continue;
      }
      if (rooted) continue; // ".." at the root stays at the root
    }
    if (len > root_len) out[len++] = '/';
    std::memcpy(out + len, part.data(), part.size());
    len += part.size();
  }

  if (len == 0) out[len++] = '.';
  out[len] = '\0';
  return Str(out, len);
}

} // namespace oat::os
