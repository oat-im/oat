// oat/os/path.h - path string helpers. Pure string work: nothing here touches
// the file system. Paths are UTF-8 with '/' separators.
#pragma once

#include "../base.h"
#include "../memory.h"

namespace oat::os {

// The last component: "a/b/c.txt" -> "c.txt", "a/b/" -> "".
Str filename(Str path);

// The extension of the last component, with its dot: "a/b.tar.gz" -> ".gz".
// Empty if there is none; a leading dot (".gitignore") is not an extension.
Str extension(Str path);

// The last component without its extension: "a/b.tar.gz" -> "b.tar".
Str stem(Str path);

// Everything before the last component: "a/b/c" -> "a/b", "/a" -> "/", "a" -> "".
Str parent(Str path);

bool is_absolute(Str path);

// a + "/" + b, without doubling the separator. If b is absolute the result is
// b. The result is allocated from `a` and NUL-terminated.
Str join(Str left, Str right, Allocator& a);

// Turns '\' into '/', collapses repeated separators, and resolves "." and
// ".." components where it can ("a/./b/../c" -> "a/c"). A leading ".." that
// cannot be resolved is kept. The result is allocated from `a` and
// NUL-terminated; an empty result is ".".
Str normalize(Str path, Allocator& a);

} // namespace oat::os
