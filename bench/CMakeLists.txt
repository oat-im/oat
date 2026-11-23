# Benchmarks: oat against the standard library and EASTL, at -O0 and -O2.
# Not a CI gate; the numbers go in the README.
#
#   cmake -S . -B build/bench -DCMAKE_BUILD_TYPE=Release -DOAT_BUILD_BENCH=ON
#   cmake --build build/bench --target oat_bench_O0 oat_bench_O2
#   ./build/bench/bench/oat_bench_O0 && ./build/bench/bench/oat_bench_O2

option(OAT_BENCH_EASTL "Benchmark against EASTL too (downloads it from GitHub)" ON)

if(OAT_BENCH_EASTL)
  include(FetchContent)
  # EABase still asks for a CMake older than current releases accept.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(EASTL
    GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
    GIT_TAG 3.27.01
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "")
  FetchContent_MakeAvailable(EASTL)
endif()

foreach(opt O0 O2)
  add_executable(oat_bench_${opt} bench.cpp)
  target_link_libraries(oat_bench_${opt} PRIVATE oat)
  if(MSVC)
    string(REPLACE "O0" "Od" msvc_opt "${opt}")
    target_compile_options(oat_bench_${opt} PRIVATE /${msvc_opt})
  else()
    target_compile_options(oat_bench_${opt} PRIVATE -${opt})
  endif()
  target_compile_definitions(oat_bench_${opt} PRIVATE OAT_BENCH_OPT="-${opt}")
  if(OAT_BENCH_EASTL)
    target_link_libraries(oat_bench_${opt} PRIVATE EASTL)
    target_compile_definitions(oat_bench_${opt} PRIVATE OAT_BENCH_EASTL=1)
  endif()
endforeach()
