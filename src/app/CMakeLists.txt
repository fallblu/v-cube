# Everything but main(), so tools and tests can link the widgets.
add_library(vcube_gui STATIC
  colours.cpp
  cube_painter.cpp
  cube_view.cpp
)
target_include_directories(vcube_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_gui
  PUBLIC vcube_io Qt6::Widgets Qt6::Concurrent
  PRIVATE vcube_warnings)
set_target_properties(vcube_gui PROPERTIES AUTOMOC ON)
