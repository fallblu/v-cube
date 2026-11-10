# Everything but main(), so tools and tests can link the widgets.
add_library(vcube_gui STATIC
  app_settings.cpp
  attempt_page.cpp
  blind_page.cpp
  bld_scheme_dialog.cpp
  case_filter.cpp
  case_history_dialog.cpp
  case_page.cpp
  colours.cpp
  command_palette.cpp
  cube_painter.cpp
  cube_view.cpp
  drill_page.cpp
  flow_layout.cpp
  help_text.cpp
  inspection_page.cpp
  keymap.cpp
  learn_page.cpp
  library_page.cpp
  paste_dialog.cpp
  main_window.cpp
  mode_page.hpp
  platform.cpp
  review_dialog.cpp
  scramble_source.cpp
  set_properties_dialog.cpp
  settings_dialog.cpp
  single_instance.cpp
  sounds.cpp
  timer_page.cpp
  today_page.cpp
  widgets.cpp
)
target_include_directories(vcube_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_gui
  PUBLIC vcube_io Qt6::Widgets Qt6::Concurrent Qt6::Network
  PRIVATE vcube_warnings)
set_target_properties(vcube_gui PROPERTIES AUTOMOC ON)
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

if(WIN32)
  # The icon of the .exe in Explorer and the taskbar.
  set(vcube_rc ${PROJECT_SOURCE_DIR}/resources/icons/v-cube.rc)
endif()
qt_add_executable(vcube main.cpp ${vcube_rc})
qt_add_resources(vcube resources
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
add_dependencies(vcube vcube_version)
target_include_directories(vcube PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(vcube PRIVATE vcube_gui vcube_warnings)
set_target_properties(vcube PROPERTIES OUTPUT_NAME v-cube WIN32_EXECUTABLE ON)
install(TARGETS vcube RUNTIME DESTINATION bin)

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
