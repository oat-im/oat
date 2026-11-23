// oat/str.h - StrBuf, string_view helpers, number parsing, UTF-8, interning.
#pragma once

#include "array.h"
#include "base.h"
#include "map.h"
#include "memory.h"

namespace oat {

// A growable character buffer that is always NUL-terminated.
struct StrBuf {
  char* data = nullptr;
  u32 len = 0; // not counting the terminator
  u32 cap = 0; // bytes allocated, including room for the terminator

  void append(Str s, Allocator& a);
  void append_char(char c, Allocator& a);
  void append_int(i64 v, Allocator& a);
  void append_uint(u64 v, Allocator& a);
  // Shortest text that parses back to the same value.
  void append_float(f64 v, Allocator& a);
  // printf-style; the compiler checks the arguments against the format.
  void appendf(Allocator& a, const char* fmt, ...) OAT_PRINTF_LIKE(3, 4);

  void reserve(u32 chars, Allocator& a);

  Str view() const { return Str(data ? data : "", len); }
  const char* c_str() const { return data ? data : ""; }
  bool is_empty() const { return len == 0; }

  void clear() {
    len = 0;
    if (data) data[0] = '\0';
  }
  void free(Allocator& a) {
    a.free(data, cap);
    *this = StrBuf{};
  }
};

// Removes ASCII whitespace from both ends.
Str trim(Str s);

// Splits at the first `sep`. False, with *left = s and *right empty, if there is none.
bool split_once(Str s, char sep, Str* left, Str* right);

// The whole string must be a number: no surrounding whitespace, no trailing
// characters. InvalidArgument if it is not, OutOfRange if it does not fit.
[[nodiscard]] Result<i64> parse_int(Str s);
[[nodiscard]] Result<f64> parse_float(Str s);

// A NUL-terminated copy.
const char* to_cstr(Str s, Allocator& a);

// Decodes the code point starting at s[*index] and advances *index past it.
// Malformed input yields U+FFFD and advances one byte. Requires *index < s.size().
u32 utf8_decode(Str s, u32* index);

// Writes the encoding of `codepoint` and returns its length in bytes (1-4),
// or 0 if the value is not a Unicode scalar value.
u32 utf8_encode(u32 codepoint, char out[4]);

// ---------------------------------------------------------------------------
// Interning
// ---------------------------------------------------------------------------

// An interned string. Two Syms from the same table are equal exactly when
// their strings are. Sym{0} is the empty string.
struct Sym {
  u32 id = 0;
  bool operator==(const Sym&) const = default;
};

// Strings are copied into an arena and never move, so the views returned by
// str() stay valid until destroy(). Not thread-safe.
struct StrTable {
  Arena arena;
  Allocator backing;
  Map<Str, u32> ids;
  Array<Str> strs; // strs[id - 1]

  static StrTable create(Allocator& backing);
  void destroy();

  Sym intern(Str s);
  // The existing Sym for s, without adding it.
  Option<Sym> find(Str s) const;
  // The view is NUL-terminated at view.data()[view.size()].
  Str str(Sym sym) const;
  u32 len() const { return strs.len; }
};

} // namespace oat
