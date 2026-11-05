add_library(vcube_train
  algset.cpp
  case_logic.cpp
  move_count.cpp
  stats.cpp
  timer.cpp
  srs.cpp
)
target_include_directories(vcube_train PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_train PUBLIC vcube_cube PRIVATE vcube_strict)
