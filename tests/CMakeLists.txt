# An installed Catch2 (a distribution package, vcpkg, or CMAKE_PREFIX_PATH)
# if there is one, on every platform; otherwise a fetched copy.
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
  test_hold.cpp
  test_case_logic.cpp
  test_alg_follow.cpp
  test_timer_stats.cpp
  test_drill_flow.cpp
  test_lab_flow.cpp
  test_learn.cpp
  test_srs.cpp
  test_io.cpp
  test_move_count.cpp
  test_step_solver.cpp
  test_solve_review.cpp
  test_families.cpp
  test_reduction.cpp
  test_data_file.cpp
  test_today.cpp
  test_backup.cpp
  test_timed_flow.cpp
  test_attempt_flow.cpp
  test_alg_search.cpp
  test_alg_lab.cpp
  test_grips.cpp
  test_eo_cross.cpp
  test_f2l.cpp
  test_practice.cpp
  test_tsle.cpp
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
if(VCUBE_SANITIZE AND NOT MSVC)
  # Leaks inside Qt that LeakSanitizer can't tell from ours.
  catch_discover_tests(vcube_tests PROPERTIES ENVIRONMENT
    "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
else()
  catch_discover_tests(vcube_tests)
endif()

# The app's pages, driven through the main window on the offscreen
# platform (the test sets it, and its own temporary settings and data
# folders, itself).
if(TARGET vcube_gui)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(vcube_gui_tests test_gui.cpp)
  target_link_libraries(vcube_gui_tests PRIVATE vcube_gui Qt6::Test
    vcube_warnings)
  set_target_properties(vcube_gui_tests PROPERTIES AUTOMOC ON)
  target_compile_definitions(vcube_gui_tests PRIVATE
    VCUBE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  if(WIN32)
    # Qt's libraries are copied next to the test, away from its plugins.
    target_compile_definitions(vcube_gui_tests PRIVATE
      VCUBE_QT_PLUGIN_DIR="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
    add_custom_command(TARGET vcube_gui_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:vcube_gui_tests>
        $<TARGET_FILE_DIR:vcube_gui_tests>
      COMMAND_EXPAND_LISTS)
  endif()
  add_test(NAME gui COMMAND vcube_gui_tests)
  # Qt's messages go to stderr, not to a debugger, on Windows too.
  set(gui_test_env "QT_QPA_PLATFORM=offscreen" "QT_FORCE_STDERR_LOGGING=1")
  if(VCUBE_SANITIZE AND NOT MSVC)
    list(APPEND gui_test_env
      "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
  endif()
  set_tests_properties(gui PROPERTIES ENVIRONMENT "${gui_test_env}"
    TIMEOUT 120)
endif()
