find_package(Catch2 3 QUIET)
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
  test_case_logic.cpp
  test_timer_stats.cpp
  test_srs.cpp
)
target_link_libraries(vcube_tests PRIVATE vcube_train vcube_warnings
  Catch2::Catch2WithMain)
catch_discover_tests(vcube_tests)
