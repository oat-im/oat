# Compile-fail tests: every static-assert precondition has a file here that must
# be rejected by the compiler. Each file is compiled twice at configure time:
# without OAT_EXPECT_FAIL it must build (so a typo cannot pass as a "failure"),
# and with it the build must stop on a static assertion.

set(oat_saved_try_compile_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY) # compile only, nothing to link

file(GLOB oat_compile_fail_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
set(oat_compile_fail_count 0)
foreach(case IN LISTS oat_compile_fail_cases)
  get_filename_component(name "${case}" NAME_WE)
  set(common
    SOURCES "${case}"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_LIST_DIR}/../../include"
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    NO_CACHE)

  try_compile(control_ok ${common} OUTPUT_VARIABLE control_output)
  if(NOT control_ok)
    message(SEND_ERROR "compile-fail test ${name}: the control build failed:\n${control_output}")
    continue()
  endif()

  try_compile(compiled ${common} COMPILE_DEFINITIONS -DOAT_EXPECT_FAIL OUTPUT_VARIABLE output)
  if(compiled)
    message(SEND_ERROR "compile-fail test ${name}: compiled, but must be rejected")
  elseif(NOT output MATCHES "static assertion|static_assert|C2338")
    message(SEND_ERROR "compile-fail test ${name}: failed, but not on a static assertion:\n${output}")
  else()
    math(EXPR oat_compile_fail_count "${oat_compile_fail_count} + 1")
  endif()
endforeach()

# Rules enforced by compiler flags rather than static asserts: the files in
# flags/ are built with the flags every oat consumer inherits, plus warnings
# as errors.
if(MSVC)
  set(oat_rule_flags /GR- /EHs-c- -D_HAS_EXCEPTIONS=0 /W3 /WX)
else()
  set(oat_rule_flags -fno-exceptions -fno-rtti -Wall -Werror)
endif()
file(GLOB oat_flag_cases CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/flags/*.cpp")
foreach(case IN LISTS oat_flag_cases)
  get_filename_component(name "${case}" NAME_WE)
  set(common
    SOURCES "${case}"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CMAKE_CURRENT_LIST_DIR}/../../include"
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    NO_CACHE)
  try_compile(control_ok ${common} COMPILE_DEFINITIONS ${oat_rule_flags} OUTPUT_VARIABLE control_output)
  if(NOT control_ok)
    message(SEND_ERROR "compile-fail test ${name}: the control build failed:\n${control_output}")
    continue()
  endif()
  try_compile(compiled ${common} COMPILE_DEFINITIONS ${oat_rule_flags} -DOAT_EXPECT_FAIL)
  if(compiled)
    message(SEND_ERROR "compile-fail test ${name}: compiled, but must be rejected")
  else()
    math(EXPR oat_compile_fail_count "${oat_compile_fail_count} + 1")
  endif()
endforeach()

message(STATUS "oat compile-fail tests: ${oat_compile_fail_count} rejected as expected")

set(CMAKE_TRY_COMPILE_TARGET_TYPE "${oat_saved_try_compile_type}")
