// oat/base.h - fixed-width types, asserts, the relocation trait, Error and Result.
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// OAT_DEBUG turns on OAT_ASSERT and heap statistics. It must have the same
// value in the oat libraries and in the code that includes these headers.
#ifndef OAT_DEBUG
#  ifdef NDEBUG
#    define OAT_DEBUG 0
#  else
#    define OAT_DEBUG 1
#  endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#  define OAT_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define OAT_ASAN 1
#  endif
#endif
#ifndef OAT_ASAN
#  define OAT_ASAN 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#  define OAT_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#  define OAT_PRINTF_LIKE(fmt_index, args_index)
#  define OAT_IS_TRIVIALLY_DESTRUCTIBLE(T) __is_trivially_destructible(T)
#elif defined(__clang__)
#  define OAT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#  define OAT_PRINTF_LIKE(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#  define OAT_IS_TRIVIALLY_DESTRUCTIBLE(T) __is_trivially_destructible(T)
#else
#  define OAT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#  define OAT_PRINTF_LIKE(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#  define OAT_IS_TRIVIALLY_DESTRUCTIBLE(T) __has_trivial_destructor(T)
#endif

#define OAT_CONCAT_(a, b) a##b
#define OAT_CONCAT(a, b) OAT_CONCAT_(a, b)

namespace oat {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using isize = std::ptrdiff_t;

inline constexpr u32 U32_MAX = 0xffffffffu;

// ---------------------------------------------------------------------------
// Asserts
// ---------------------------------------------------------------------------

// Replaces the function called when an assert fails or memory runs out. The
// default prints to stderr. If the handler returns, the process aborts.
// Passing nullptr restores the default.
void set_assert_handler(void (*handler)(const char* expr, const char* file, int line));

[[noreturn]] void assert_fail(const char* expr, const char* file, int line);

} // namespace oat

// OAT_CHECK is always on. It guards the cold paths: 32-bit overflow and out of memory.
#define OAT_CHECK(cond)                                                                            \
  do {                                                                                             \
    if (!(cond)) [[unlikely]]                                                                      \
      ::oat::assert_fail(#cond, __FILE__, __LINE__);                                               \
  } while (0)

#if OAT_DEBUG
#  define OAT_ASSERT(cond) OAT_CHECK(cond)
#  define OAT_ASSERT_MSG(cond, msg)                                                                \
    do {                                                                                           \
      if (!(cond)) [[unlikely]]                                                                    \
        ::oat::assert_fail(msg, __FILE__, __LINE__);                                               \
    } while (0)
#  define OAT_UNREACHABLE() ::oat::assert_fail("unreachable", __FILE__, __LINE__)
#else
#  define OAT_ASSERT(cond) ((void)sizeof(!(cond)))
#  define OAT_ASSERT_MSG(cond, msg) ((void)sizeof(!(cond)))
#  if defined(_MSC_VER) && !defined(__clang__)
#    define OAT_UNREACHABLE() __assume(0)
#  else
#    define OAT_UNREACHABLE() __builtin_unreachable()
#  endif
#endif

namespace oat {

// ---------------------------------------------------------------------------
// Relocation
// ---------------------------------------------------------------------------

// True when a T can be moved to a new address with memcpy and the old bytes
// forgotten. Every container element must satisfy this.
//
// The default only knows how the type is copied. A trivially copyable type
// that holds a pointer into itself (`struct B { char buf[8]; char* p = buf; }`)
// passes and is still wrong in a container: that cannot be detected, so do
// not write one. A type that owns a
// pointer but is still safe to memcpy opts in:
//
//   template <> constexpr bool oat::is_relocatable<MyType> = true;
template <class T>
constexpr bool is_relocatable = __is_trivially_copyable(T);

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

enum class Error : u32 {
  OutOfRange,
  InvalidArgument,
  NotFound,
  AlreadyExists,
  PermissionDenied,
  Io,
  EndOfStream,
  Timeout,
  WouldBlock,
  Unsupported,
  Unknown,
};

const char* error_name(Error);

// ---------------------------------------------------------------------------
// The standard library, as oat spells it
//
// These are the only standard types oat is built on. They are aliases, not
// wrappers, so they cost nothing and convert freely; project code uses these
// names and never spells the standard namespace (the lint enforces that).
// ---------------------------------------------------------------------------

// A value or an error. Test with `if (r)`, read with `*r` or `r->`, and get
// the error with `r.error()`. Never call the checked value accessor: it aborts
// with exceptions off.
template <class T, class E>
using Expected = std::expected<T, E>;
template <class T>
using Result = Expected<T, Error>;

// The error return for any Result: `return err(Error::NotFound);`
constexpr std::unexpected<Error> err(Error e) { return std::unexpected<Error>(e); }

// A value or nothing: `Option<u32> found = none;`
template <class T>
using Option = std::optional<T>;
inline constexpr std::nullopt_t none = std::nullopt;

// A view of characters someone else owns. Not NUL-terminated.
using Str = std::string_view;

// A view of a contiguous run of T someone else owns.
template <class T>
using Span = std::span<T>;

// oat::move and oat::forward are the standard ones under another name, so
// compilers still treat them as built-ins and there is no overload to clash.
using std::forward;
using std::move;

} // namespace oat

// OAT_TRY(file, open(path));   declares `file` as a reference to the value, or
// returns the error from the enclosing function. Plain statements, so it
// cannot be used as the body of an unbraced `if`.
#define OAT_TRY(name, expr)                                                                        \
  auto&& OAT_CONCAT(oat_try_, __LINE__) = (expr);                                                  \
  if (!OAT_CONCAT(oat_try_, __LINE__))                                                             \
    return ::std::unexpected(OAT_CONCAT(oat_try_, __LINE__).error());                              \
  auto& name = *OAT_CONCAT(oat_try_, __LINE__)

#define OAT_TRY_VOID(expr)                                                                         \
  do {                                                                                             \
    auto&& oat_try_void_ = (expr);                                                                 \
    if (!oat_try_void_) return ::std::unexpected(oat_try_void_.error());                           \
  } while (0)

namespace oat {

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

template <class T>
constexpr const T& min(const T& a, const T& b) {
  return b < a ? b : a;
}

template <class T>
constexpr const T& max(const T& a, const T& b) {
  return a < b ? b : a;
}

template <class T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
  return v < lo ? lo : (hi < v ? hi : v);
}

template <class T>
constexpr void swap(T& a, T& b) {
  T tmp = static_cast<T&&>(a);
  a = static_cast<T&&>(b);
  b = static_cast<T&&>(tmp);
}

template <class T, usize N>
constexpr u32 array_count(const T (&)[N]) {
  return static_cast<u32>(N);
}

} // namespace oat
