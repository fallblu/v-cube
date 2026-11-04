# Everything but main(), so tools and tests can link the widgets.
add_library(vcube_gui STATIC
  app_settings.cpp
  colours.cpp
  cube_painter.cpp
  cube_view.cpp
  keymap.cpp
  main_window.cpp
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
