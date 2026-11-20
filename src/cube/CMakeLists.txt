add_library(vcube_cube STATIC
  move.cpp
  facelet.cpp
  cubie.cpp
  scramble.cpp
  hold.cpp
)
target_include_directories(vcube_cube PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Headers use C++23; dependents need it too.
target_compile_features(vcube_cube PUBLIC cxx_std_23)
target_link_libraries(vcube_cube PRIVATE vcube_strict)

# The solver builds ~4 MB of tables on first use.
vcube_optimize_in_debug(vcube_cube solver.cpp)
