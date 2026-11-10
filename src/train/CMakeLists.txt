add_library(vcube_train
  alg_follow.cpp
  algset.cpp
  bld.cpp
  case_logic.cpp
  families.cpp
  move_count.cpp
  reduction.cpp
  solve_review.cpp
  stats.cpp
  timer.cpp
  srs.cpp
  today.cpp
)
target_include_directories(vcube_train PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_train PUBLIC vcube_cube PRIVATE vcube_strict)

# The step solver's searches and tables need optimisation in debug builds
# too (see src/cube for the MSVC object library).
if(MSVC)
  add_library(vcube_step_solver OBJECT step_solver.cpp)
  target_include_directories(vcube_step_solver PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(vcube_step_solver PRIVATE vcube_strict)
  set_target_properties(vcube_step_solver PROPERTIES VCUBE_OPTIMIZE ON)
  target_sources(vcube_train PRIVATE $<TARGET_OBJECTS:vcube_step_solver>)
else()
  target_sources(vcube_train PRIVATE step_solver.cpp)
  set_source_files_properties(step_solver.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()
