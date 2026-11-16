# Everything but main(), so tools and tests can link the widgets.
add_library(vcube_gui STATIC
  alg_html.cpp
  alglab_common.cpp
  alglab_dialogs.cpp
  alglab_candidates.cpp
  alglab_generator.cpp
  alglab_page.cpp
  app_data.cpp
  app_settings.cpp
  attempt_page.cpp
  case_filter.cpp
  case_history_dialog.cpp
  case_list_panel.cpp
  case_page.cpp
  case_stage.cpp
  colours.cpp
  context_practice.cpp
  command_palette.cpp
  cube_painter.cpp
  cube_view.cpp
  drill_history_dialog.cpp
  drill_list.cpp
  drill_attempt.cpp
  drill_page.cpp
  drill_settings.cpp
  eo_cross_check.cpp
  grip_view.cpp
  flow_layout.cpp
  help_panel.cpp
  help_text.cpp
  eo_cross_page.cpp
  eo_cross_page_results.cpp
  f2l_page.cpp
  tsle_page.cpp
  tsle_page_results.cpp
  f2l_page_results.cpp
  key_router.cpp
  lab_chart.cpp
  lab_test_panel.cpp
  keymap.cpp
  learn_list.cpp
  learn_page.cpp
  learn_practice.cpp
  learn_solve.cpp
  library_editor.cpp
  library_algs.cpp
  library_cases.cpp
  library_page.cpp
  library_sets.cpp
  page_parts.cpp
  paste_dialog.cpp
  main_window.cpp
  move_count_page.cpp
  mode_page.hpp
  mode_table.cpp
  platform.cpp
  review_dialog.cpp
  scramble_source.cpp
  session_panel.cpp
  set_properties_dialog.cpp
  settings_dialog.cpp
  single_instance.cpp
  sounds.cpp
  timer_page.cpp
  timer_sessions.cpp
  today_page.cpp
  ui_state.cpp
  untimed_page.cpp
  widgets.cpp
)
target_include_directories(vcube_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_gui
  PUBLIC vcube_io Qt6::Widgets Qt6::Concurrent Qt6::Network
  PRIVATE vcube_warnings)
set_target_properties(vcube_gui PROPERTIES AUTOMOC ON)
# The built-in alg sets (read from :/algsets) and the window icon, in the
# library so that anything linking it, the tests too, has them.
qt_add_resources(vcube_gui resources
  PREFIX "/"
  BASE ${PROJECT_SOURCE_DIR}/resources
  FILES
    ${PROJECT_SOURCE_DIR}/resources/algsets/pll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/oll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/2look-oll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/2look-pll.json
    ${PROJECT_SOURCE_DIR}/resources/icons/v-cube.svg
    ${PROJECT_SOURCE_DIR}/resources/icons/v-cube-256.png
)
# The inspection calls play a sound with Qt Multimedia when it is installed,
# else the system beep.
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(Qt6Multimedia_FOUND)
  target_link_libraries(vcube_gui PRIVATE Qt6::Multimedia)
  target_compile_definitions(vcube_gui PRIVATE VCUBE_HAVE_MULTIMEDIA)
endif()

# The version shown in About: `git describe` at each build, else the
# project version.
set(vcube_version_header ${CMAKE_CURRENT_BINARY_DIR}/generated/vcube_version.hpp)
add_custom_target(vcube_version
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DOUTPUT=${vcube_version_header}
    -DFALLBACK=${PROJECT_VERSION}
    -P ${PROJECT_SOURCE_DIR}/cmake/Version.cmake
  BYPRODUCTS ${vcube_version_header}
  COMMENT "Checking the version"
  VERBATIM)

# The version at configure time, for package names and the version
# resource of the .exe.
include(${PROJECT_SOURCE_DIR}/cmake/Version.cmake)
vcube_describe_version(VCUBE_VERSION_FULL ${PROJECT_SOURCE_DIR}
  ${PROJECT_VERSION})
message(STATUS "v-cube version: ${VCUBE_VERSION_FULL}")

if(WIN32)
  # The icon and version of the .exe in Explorer and the taskbar.
  set(VCUBE_ICO ${PROJECT_SOURCE_DIR}/resources/icons/v-cube.ico)
  set(rc_version "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
  if(VCUBE_VERSION_FULL MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(rc_version "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
  endif()
  set(VCUBE_RC_VERSION ${rc_version})
  set(vcube_rc ${CMAKE_CURRENT_BINARY_DIR}/generated/v-cube.rc)
  configure_file(${PROJECT_SOURCE_DIR}/resources/icons/v-cube.rc.in
    ${vcube_rc} @ONLY)
endif()
qt_add_executable(vcube main.cpp ${vcube_rc})
add_dependencies(vcube vcube_version)
target_include_directories(vcube PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(vcube PRIVATE vcube_gui vcube_warnings)
set_target_properties(vcube PROPERTIES OUTPUT_NAME v-cube WIN32_EXECUTABLE ON)
include(GNUInstallDirs)
install(TARGETS vcube RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/v-cube)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/docs/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/v-cube/docs)

# Linux: a desktop entry and icons, so that the installed app (and the
# AppImage built from the install tree) shows up in menus.
if(UNIX AND NOT APPLE)
  install(FILES ${PROJECT_SOURCE_DIR}/resources/linux/v-cube.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/v-cube.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/v-cube-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME v-cube.png)
endif()

# Windows: `cmake --install` produces a self-contained folder with the Qt
# DLLs and plugins (windeployqt) and the MSVC runtime.
if(WIN32)
  qt_generate_deploy_app_script(
    TARGET vcube
    OUTPUT_SCRIPT vcube_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
  install(SCRIPT ${vcube_deploy_script})
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  include(InstallRequiredSystemLibraries)
endif()

# `cpack` packs the install tree: on Windows a zip of the self-contained
# folder, elsewhere a tarball (the Linux AppImage is made with linuxdeploy,
# see .github/workflows/release.yml).
set(CPACK_PACKAGE_NAME v-cube)
set(CPACK_PACKAGE_VENDOR v-cube)
set(CPACK_PACKAGE_VERSION ${VCUBE_VERSION_FULL})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
  set(CPACK_PACKAGE_FILE_NAME v-cube-${VCUBE_VERSION_FULL}-windows-x64)
else()
  set(CPACK_GENERATOR TGZ)
  set(CPACK_PACKAGE_FILE_NAME
    v-cube-${VCUBE_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR})
endif()
include(CPack)
