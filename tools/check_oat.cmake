# The oat lint. Fails if any .h or .cpp under OAT_LINT_DIR breaks a rule:
#   - no #include <...> of a system or standard header, and no quoted include
#     of a name without an extension ("vector")
#   - no std:: at all: use oat's names (Str, Span, Option, none, Result, err,
#     Expected, Atomic, oat::move, oat::forward)
#   - no .value() or ->value() on a Result or Option (it aborts with
#     exceptions off)
# Comments are not skipped, so a banned name in a comment is flagged too.
#
#   cmake -DOAT_LINT_DIR=<dir> [-DOAT_LINT_MODE=project|oat] -P check_oat.cmake
#
# MODE=oat is for oat's own headers, which are where the standard names are
# given their oat spelling, so a short list of them is allowed there.

set(inc_ok "oat/[a-z_/]+\\.h")
set(bad "#[ \t]*include[ \t]*<|#[ \t]*include[ \t]*\"[^\".]*\"|std::|using[ \t]+namespace[ \t]+std|(\\.|->)value\\(\\)")
set(hint "use oat's names instead of std:: (Str, Span, Option, none, Result, err, Expected, Atomic, oat::move, oat::forward)")
if(OAT_LINT_MODE STREQUAL "oat")
  set(std_ok "expected|unexpected|optional|nullopt|nullopt_t|string_view|span|move|forward|atomic"
             "|size_t|ptrdiff_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t"
             "|memcpy|memmove|memset|memcmp|bit_width")
  string(CONCAT std_ok ${std_ok})
  string(APPEND inc_ok "|bit|cstdint|cstring|expected|new|optional|span|string_view|utility|atomic")
  set(hint "oat headers may only use: ${std_ok}")
endif()

file(GLOB_RECURSE files "${OAT_LINT_DIR}/*.h" "${OAT_LINT_DIR}/*.cpp")
set(failures 0)
foreach(file IN LISTS files)
  file(READ "${file}" text)
  string(REGEX REPLACE "#[ \t]*include[ \t]*<(${inc_ok})>" "" text "${text}")
  if(std_ok)
    string(REGEX REPLACE "std::(${std_ok})([^A-Za-z0-9_])" "\\2" text "${text}")
  endif()
  if(NOT text MATCHES "${bad}")
    continue()
  endif()
  # Something is left. Walk the lines to report where.
  string(REGEX REPLACE "[][;\\\\]" "_" text "${text}")
  string(REPLACE "\n" ";" lines "${text}")
  set(n 0)
  foreach(line IN LISTS lines)
    math(EXPR n "${n} + 1")
    if(line MATCHES "${bad}")
      message("${file}:${n}: oat lint: ${line}")
      math(EXPR failures "${failures} + 1")
    endif()
  endforeach()
endforeach()
if(failures)
  message(FATAL_ERROR "oat lint: ${failures} violation(s); ${hint}")
endif()
