// Shared by the oat_os sources. Not installed.
#pragma once

#include <oat/oat.h>
#include <oat/os.h>

#include <SDL3/SDL.h>

namespace oat::os::detail {

// Remembers `text` for last_error_text().
void set_error_text(const char* text);

// Remembers SDL's current error message and returns `e` as an error result.
std::unexpected<Error> fail(Error e);

// Same, with a message of oat's own.
std::unexpected<Error> fail(Error e, const char* text);

// A NUL-terminated copy of a string_view for SDL. Short strings stay on the stack.
struct CStr {
  char small[512];
  char* ptr;
  usize heap_size = 0;

  explicit CStr(Str s) {
    if (s.size() < sizeof(small)) {
      ptr = small;
    } else {
      heap_size = s.size() + 1;
      Allocator h = heap();
      ptr = static_cast<char*>(h.alloc(heap_size, 1));
    }
    if (!s.empty()) std::memcpy(ptr, s.data(), s.size());
    ptr[s.size()] = '\0';
  }
  ~CStr() {
    if (heap_size) {
      Allocator h = heap();
      h.free(ptr, heap_size);
    }
  }
  CStr(const CStr&) = delete;
  CStr& operator=(const CStr&) = delete;

  const char* get() const { return ptr; }
};

// A copy of `s` from `a`, NUL-terminated, with '\' turned into '/'.
Str copy_path(Str s, Allocator& a);

} // namespace oat::os::detail
