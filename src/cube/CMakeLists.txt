add_library(vcube_cube
  move.cpp
)
target_include_directories(vcube_cube PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_cube PRIVATE vcube_strict)
