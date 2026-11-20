add_library(vcube_train STATIC
  alg_follow.cpp
  alg_guide.cpp
  alg_lab.cpp
  algset.cpp
  attempt_flow.cpp
  case_logic.cpp
  case_picker.cpp
  drill_flow.cpp
  drill_session.cpp
  eo_cross.cpp
  ergonomics.cpp
  f2l.cpp
  families.cpp
  grips.cpp
  lab_flow.cpp
  learn_attempt.cpp
  learn_session.cpp
  move_count.cpp
  move_set.cpp
  optimal_cache.cpp
  practice.cpp
  practice_flow.cpp
  reduction.cpp
  solve_review.cpp
  srs.cpp
  set_scramble.cpp
  stats.cpp
  timed_flow.cpp
  timer.cpp
  today.cpp
  tsle.cpp
)
target_include_directories(vcube_train PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_train PUBLIC vcube_cube PRIVATE vcube_strict)

vcube_optimize_in_debug(vcube_train step_solver.cpp alg_search.cpp f2l_search.cpp)
