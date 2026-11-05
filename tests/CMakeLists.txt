# Windows has no system Catch2 to find, so it always uses the fetched copy.
if(NOT WIN32)
  find_package(Catch2 3 QUIET)
endif()
if(NOT Catch2_FOUND)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
include(Catch)

add_executable(vcube_tests
  test_move.cpp
  test_cube.cpp
  test_solver.cpp
  test_scramble.cpp
  test_hold.cpp
  test_case_logic.cpp
  test_timer_stats.cpp
  test_srs.cpp
  test_io.cpp
)
target_link_libraries(vcube_tests PRIVATE vcube_io vcube_warnings
  Catch2::Catch2WithMain)
target_compile_definitions(vcube_tests PRIVATE
  VCUBE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# Windows has no rpath: copy the Qt DLLs next to the tests so they run (and
# can be listed by catch_discover_tests) without Qt on PATH.
if(WIN32)
  add_custom_command(TARGET vcube_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_RUNTIME_DLLS:vcube_tests> $<TARGET_FILE_DIR:vcube_tests>
    COMMAND_EXPAND_LISTS)
endif()
catch_discover_tests(vcube_tests)
