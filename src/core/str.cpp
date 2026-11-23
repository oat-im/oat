#include <oat/str.h>

#include <cerrno>
#include <charconv>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace oat {

// ---------------------------------------------------------------------------
// StrBuf
// ---------------------------------------------------------------------------

// Makes room for `extra` more characters plus the terminator.
static char* strbuf_grow(StrBuf* s, u64 extra, Allocator& a) {
  u64 need = static_cast<u64>(s->len) + extra + 1;
  if (need > s->cap) s->data = static_cast<char*>(detail::array_grow(s->data, &s->cap, need, 1, 1, a));
  return s->data + s->len;
}

void StrBuf::reserve(u32 chars, Allocator& a) {
  if (chars > len || !data) strbuf_grow(this, chars > len ? chars - len : 0, a);
  data[len] = '\0';
}

void StrBuf::append(Str s, Allocator& a) {
  OAT_ASSERT_MSG(s.empty() || !detail::points_into(s.data(), data, cap),
                 "appending a StrBuf to itself");
  char* dst = strbuf_grow(this, s.size(), a);
  if (!s.empty()) std::memcpy(dst, s.data(), s.size());
  len += static_cast<u32>(s.size());
  data[len] = '\0';
}

void StrBuf::append_char(char c, Allocator& a) {
  char* dst = strbuf_grow(this, 1, a);
  dst[0] = c;
  dst[1] = '\0';
  ++len;
}

void StrBuf::append_uint(u64 v, Allocator& a) {
  char digits[20];
  u32 n = 0;
  do {
    digits[n++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v);
  char* dst = strbuf_grow(this, n, a);
  for (u32 i = 0; i < n; ++i) dst[i] = digits[n - 1 - i];
  len += n;
  data[len] = '\0';
}

void StrBuf::append_int(i64 v, Allocator& a) {
  if (v < 0) {
    append_char('-', a);
    append_uint(0 - static_cast<u64>(v), a); // well defined for the most negative value
  } else {
    append_uint(static_cast<u64>(v), a);
  }
}

void StrBuf::append_float(f64 v, Allocator& a) {
  char text[40];
  std::to_chars_result r = std::to_chars(text, text + sizeof(text), v);
  append(Str(text, static_cast<usize>(r.ptr - text)), a);
}

void StrBuf::appendf(Allocator& a, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int n = std::vsnprintf(nullptr, 0, fmt, copy);
  va_end(copy);
  if (n > 0) {
    char* dst = strbuf_grow(this, static_cast<u64>(n), a);
    std::vsnprintf(dst, static_cast<usize>(n) + 1, fmt, args);
    len += static_cast<u32>(n);
  }
  va_end(args);
}

// ---------------------------------------------------------------------------
// string_view helpers
// ---------------------------------------------------------------------------

static bool str_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

Str trim(Str s) {
  usize begin = 0;
  usize end = s.size();
  while (begin < end && str_is_space(s[begin])) ++begin;
  while (end > begin && str_is_space(s[end - 1])) --end;
  return s.substr(begin, end - begin);
}

bool split_once(Str s, char sep, Str* left, Str* right) {
  for (usize i = 0; i < s.size(); ++i) {
    if (s[i] == sep) {
      *left = s.substr(0, i);
      *right = s.substr(i + 1);
      return true;
    }
  }
  *left = s;
  *right = Str();
  return false;
}

Result<i64> parse_int(Str s) {
  usize i = 0;
  bool negative = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
    negative = s[i] == '-';
    ++i;
  }
  if (i == s.size()) return err(Error::InvalidArgument);

  // Accumulate as a magnitude so the most negative value parses.
  const u64 limit = negative ? (u64{1} << 63) : (u64{1} << 63) - 1;
  u64 value = 0;
  bool overflow = false;
  for (; i < s.size(); ++i) {
    char c = s[i];
    if (c < '0' || c > '9') return err(Error::InvalidArgument);
    u64 digit = static_cast<u64>(c - '0');
    if (value > (limit - digit) / 10) overflow = true;
    else value = value * 10 + digit;
  }
  if (overflow) return err(Error::OutOfRange);
  return static_cast<i64>(negative ? 0 - value : value);
}

Result<f64> parse_float(Str s) {
  if (s.empty() || str_is_space(s[0])) return err(Error::InvalidArgument);
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  const char* first = s.data();
  const char* last = first + s.size();
  if (*first == '+') ++first; // from_chars rejects a leading plus
  if (first == last || *first == '+') return err(Error::InvalidArgument);
  f64 value = 0;
  std::from_chars_result r = std::from_chars(first, last, value);
  if (r.ec == std::errc::result_out_of_range) return err(Error::OutOfRange);
  if (r.ec != std::errc() || r.ptr != last) return err(Error::InvalidArgument);
  return value;
#else
  // Standard libraries without floating-point from_chars: strtod on a
  // terminated copy. strtod follows the C locale's decimal point.
  char text[128];
  if (s.size() >= sizeof(text)) return err(Error::InvalidArgument);
  std::memcpy(text, s.data(), s.size());
  text[s.size()] = '\0';
  if (std::strlen(text) != s.size()) return err(Error::InvalidArgument); // embedded NUL
  char* end = nullptr;
  errno = 0;
  f64 value = std::strtod(text, &end);
  if (end != text + s.size()) return err(Error::InvalidArgument);
  if (errno == ERANGE && (value > 1.0 || value < -1.0)) return err(Error::OutOfRange);
  return value;
#endif
}

const char* to_cstr(Str s, Allocator& a) {
  char* p = static_cast<char*>(a.alloc(s.size() + 1, 1));
  if (!s.empty()) std::memcpy(p, s.data(), s.size());
  p[s.size()] = '\0';
  return p;
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

u32 utf8_decode(Str s, u32* index) {
  constexpr u32 REPLACEMENT = 0xfffd;
  OAT_ASSERT(*index < s.size());
  usize i = *index;
  usize left = s.size() - i;
  u8 b0 = static_cast<u8>(s[i]);
  *index = static_cast<u32>(i + 1);
  if (b0 < 0x80) return b0;

  u32 count; // continuation bytes
  u32 cp;
  u32 min_cp; // smallest value that needs this many bytes; rejects overlong forms
  if ((b0 & 0xe0) == 0xc0) {
    count = 1;
    cp = b0 & 0x1f;
    min_cp = 0x80;
  } else if ((b0 & 0xf0) == 0xe0) {
    count = 2;
    cp = b0 & 0x0f;
    min_cp = 0x800;
  } else if ((b0 & 0xf8) == 0xf0) {
    count = 3;
    cp = b0 & 0x07;
    min_cp = 0x10000;
  } else {
    return REPLACEMENT;
  }
  if (left <= count) return REPLACEMENT;
  for (u32 k = 1; k <= count; ++k) {
    u8 b = static_cast<u8>(s[i + k]);
    if ((b & 0xc0) != 0x80) return REPLACEMENT;
    cp = (cp << 6) | (b & 0x3f);
  }
  if (cp < min_cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return REPLACEMENT;
  *index = static_cast<u32>(i + 1 + count);
  return cp;
}

u32 utf8_encode(u32 cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xc0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp < 0x10000) {
    if (cp >= 0xd800 && cp <= 0xdfff) return 0;
    out[0] = static_cast<char>(0xe0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out[2] = static_cast<char>(0x80 | (cp & 0x3f));
    return 3;
  }
  if (cp <= 0x10ffff) {
    out[0] = static_cast<char>(0xf0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out[3] = static_cast<char>(0x80 | (cp & 0x3f));
    return 4;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// StrTable
// ---------------------------------------------------------------------------

StrTable StrTable::create(Allocator& backing) {
  StrTable t;
  t.backing = backing;
  t.arena = Arena::create(backing, 16 * 1024);
  return t;
}

void StrTable::destroy() {
  ids.free(backing);
  strs.free(backing);
  arena.destroy();
}

Option<Sym> StrTable::find(Str s) const {
  if (s.empty()) return Sym{0};
  if (const u32* id = ids.find(s)) return Sym{*id};
  return none;
}

Sym StrTable::intern(Str s) {
  if (s.empty()) return Sym{0};
  if (const u32* id = ids.find(s)) return Sym{*id};

  char* copy = static_cast<char*>(arena.alloc(s.size() + 1, 1));
  std::memcpy(copy, s.data(), s.size());
  copy[s.size()] = '\0';
  Str stored(copy, s.size());

  strs.push(stored, backing);
  u32 id = strs.len;
  ids.insert(stored, id, backing);
  return Sym{id};
}

Str StrTable::str(Sym sym) const {
  if (sym.id == 0) return Str("", 0);
  OAT_ASSERT(sym.id <= strs.len);
  return strs[sym.id - 1];
}

} // namespace oat
