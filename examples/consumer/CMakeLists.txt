# A sample consumer: how a game uses oat. The umbrella headers go in the
# precompiled header, so the sources include nothing, and the lint keeps it
# that way.
#
# In a real project this file would start with
#   add_subdirectory(third_party/oat)

add_executable(oat_consumer src/main.cpp src/inventory.cpp)
target_link_libraries(oat_consumer PRIVATE oat)
target_precompile_headers(oat_consumer PRIVATE <oat/oat.h>)
if(TARGET oat_os)
  target_link_libraries(oat_consumer PRIVATE oat_os)
  target_precompile_headers(oat_consumer PRIVATE <oat/os.h>)
  target_compile_definitions(oat_consumer PRIVATE CONSUMER_HAS_OS=1)
endif()
oat_enable_lint(oat_consumer ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(OAT_BUILD_TESTS)
  add_test(NAME oat_consumer COMMAND oat_consumer)
endif()
