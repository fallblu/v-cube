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
  cube_painter.cpp
  cube_view.cpp
  drill_history_dialog.cpp
  drill_page.cpp
  drill_settings.cpp
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
  timer_page.cpp
  widgets.cpp
)
target_include_directories(vcube_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_gui
  PUBLIC vcube_io Qt6::Widgets Qt6::Concurrent
  PRIVATE vcube_warnings)
set_target_properties(vcube_gui PROPERTIES AUTOMOC ON)

qt_add_executable(vcube main.cpp)
qt_add_resources(vcube resources
  PREFIX "/"
  BASE ${PROJECT_SOURCE_DIR}/resources
  FILES
    ${PROJECT_SOURCE_DIR}/resources/algsets/pll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/oll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/2look-oll.json
    ${PROJECT_SOURCE_DIR}/resources/algsets/2look-pll.json
)
target_compile_definitions(vcube PRIVATE VCUBE_VERSION="${PROJECT_VERSION}")
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
