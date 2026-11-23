cmake_minimum_required(VERSION 3.29)
project(oat CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OAT_BUILD_OS  "Build oat_os (needs SDL3)" ON)
option(OAT_BUILD_NET "Build oat_net (needs SDL3_net)" OFF)
option(OAT_BUILD_TESTS "Build the oat tests" ${PROJECT_IS_TOP_LEVEL})
option(OAT_BUILD_EXAMPLES "Build the sample consumer project" ${PROJECT_IS_TOP_LEVEL})
option(OAT_BUILD_BENCH "Build the benchmarks (downloads EASTL unless OAT_BENCH_EASTL is OFF)" OFF)
option(OAT_OPTIMIZE_IN_DEBUG "Build the oat libraries with -O2 even in Debug (GCC and Clang)" ON)
option(OAT_WERROR "Treat warnings in oat and its tests as errors" OFF)
set(OAT_SANITIZE "" CACHE STRING "Sanitizers for a top-level build, e.g. address,undefined or thread")

if(PROJECT_IS_TOP_LEVEL)
  if(MSVC)
    # CMake adds /EHsc by default; oat builds with exceptions off.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  endif()
  if(OAT_SANITIZE)
    add_compile_options(-fsanitize=${OAT_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${OAT_SANITIZE})
  endif()
endif()

# Warnings for oat's own targets. Not propagated to consumers.
function(oat_internal_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(OAT_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(OAT_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(oat_internal_library target)
  oat_internal_warnings(${target})
  if(OAT_OPTIMIZE_IN_DEBUG AND NOT MSVC)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:-O2>)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Lint
# ---------------------------------------------------------------------------

set(OAT_LINT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_oat.cmake" CACHE INTERNAL "")

# oat_enable_lint(<target> <dir>): checks every .h and .cpp under <dir> against
# the oat rules each time <target> is built.
function(oat_enable_lint target dir)
  add_custom_target(${target}_oat_lint
    COMMAND ${CMAKE_COMMAND} -DOAT_LINT_DIR=${dir} -DOAT_LINT_MODE=project -P ${OAT_LINT_SCRIPT}
    COMMENT "oat lint: ${dir}"
    VERBATIM)
  add_dependencies(${target} ${target}_oat_lint)
endfunction()

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------

add_library(oat STATIC src/oat.cpp)
target_include_directories(oat PUBLIC include)
if(MSVC)
  target_compile_options(oat PUBLIC /GR-)
  target_compile_definitions(oat PUBLIC _HAS_EXCEPTIONS=0)
else()
  target_compile_options(oat PUBLIC -fno-exceptions -fno-rtti)
endif()
oat_internal_library(oat)

# oat's own headers are linted with a wider allowlist.
add_custom_target(oat_self_lint
  COMMAND ${CMAKE_COMMAND} -DOAT_LINT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/include/oat -DOAT_LINT_MODE=oat
          -P ${OAT_LINT_SCRIPT}
  COMMENT "oat lint: include/oat"
  VERBATIM)
add_dependencies(oat oat_self_lint)

if(OAT_BUILD_OS)
  find_package(SDL3 REQUIRED CONFIG)
  add_library(oat_os STATIC src/oat_os.cpp)
  target_link_libraries(oat_os PUBLIC oat PRIVATE SDL3::SDL3)
  oat_internal_library(oat_os)
endif()

if(OAT_BUILD_NET)
  if(NOT OAT_BUILD_OS)
    message(FATAL_ERROR "OAT_BUILD_NET needs OAT_BUILD_OS")
  endif()
  find_package(SDL3_net REQUIRED CONFIG)
  add_library(oat_net STATIC src/oat_net.cpp)
  target_link_libraries(oat_net PUBLIC oat_os PRIVATE SDL3_net::SDL3_net SDL3::SDL3)
  oat_internal_library(oat_net)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(OAT_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)

  add_executable(oat_tests
    tests/test_main.cpp
    tests/test_base.cpp
    tests/test_memory.cpp
    tests/test_array.cpp
    tests/test_algo.cpp
    tests/test_str.cpp
    tests/test_map.cpp
    tests/test_pool.cpp)
  target_link_libraries(oat_tests PRIVATE oat Threads::Threads)
  oat_internal_warnings(oat_tests)
  add_test(NAME oat_tests COMMAND oat_tests)

  if(OAT_BUILD_OS)
    add_executable(oat_os_tests
      tests/test_main.cpp
      tests/test_os_base.cpp
      tests/test_os_path.cpp
      tests/test_os_file.cpp
      tests/test_os_thread.cpp)
    target_link_libraries(oat_os_tests PRIVATE oat_os)
    oat_internal_warnings(oat_os_tests)
    add_test(NAME oat_os_tests COMMAND oat_os_tests)

    # A tiny shared library for the dylib test to load.
    add_library(oat_test_plugin SHARED tests/plugin/plugin.cpp)
    set_target_properties(oat_test_plugin PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET default)
    add_dependencies(oat_os_tests oat_test_plugin)
    target_compile_definitions(oat_os_tests PRIVATE
      OAT_TEST_PLUGIN_PATH="$<TARGET_FILE:oat_test_plugin>")
  endif()

  if(OAT_BUILD_NET)
    add_executable(oat_net_tests tests/test_main.cpp tests/test_net.cpp)
    target_link_libraries(oat_net_tests PRIVATE oat_net)
    oat_internal_warnings(oat_net_tests)
    add_test(NAME oat_net_tests COMMAND oat_net_tests)
  endif()

  # One of each container, for trying the debugger visualizers in debug/.
  add_executable(oat_visualizer_demo debug/visualizer_demo.cpp)
  target_link_libraries(oat_visualizer_demo PRIVATE oat)
  target_compile_options(oat_visualizer_demo PRIVATE $<IF:$<BOOL:${MSVC}>,/Zi,-g>)

  include(tests/compile_fail/compile_fail.cmake)

  # The lint itself is tested: one directory it must accept, four it must reject.
  add_test(NAME oat_lint_accepts_good
    COMMAND ${CMAKE_COMMAND} -DOAT_LINT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/lint_cases/good
            -P ${OAT_LINT_SCRIPT})
  foreach(case bad_include bad_std bad_std_move bad_value)
    add_test(NAME oat_lint_rejects_${case}
      COMMAND ${CMAKE_COMMAND} -DOAT_LINT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/lint_cases/${case}
              -P ${OAT_LINT_SCRIPT})
    set_tests_properties(oat_lint_rejects_${case} PROPERTIES WILL_FAIL TRUE)
  endforeach()
endif()

if(OAT_BUILD_EXAMPLES)
  add_subdirectory(examples/consumer)
endif()

if(OAT_BUILD_BENCH)
  add_subdirectory(bench)
endif()
