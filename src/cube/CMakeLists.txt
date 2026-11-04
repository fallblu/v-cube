add_library(vcube_cube
  move.cpp
  facelet.cpp
  cubie.cpp
  solver.cpp
  scramble.cpp
)
target_include_directories(vcube_cube PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Headers use C++23; dependents need it too.
target_compile_features(vcube_cube PUBLIC cxx_std_23)
target_link_libraries(vcube_cube PRIVATE vcube_strict)

# The solver builds ~4 MB of tables on first use; keep that fast in debug
# builds too.
if(NOT MSVC)
  set_source_files_properties(solver.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
