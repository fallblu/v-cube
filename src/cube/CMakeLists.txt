add_library(vcube_cube
  move.cpp
  facelet.cpp
  cubie.cpp
  scramble.cpp
)
target_include_directories(vcube_cube PUBLIC ${PROJECT_SOURCE_DIR}/src)
# Headers use C++23; dependents need it too.
target_compile_features(vcube_cube PUBLIC cxx_std_23)
target_link_libraries(vcube_cube PRIVATE vcube_strict)

# The solver builds ~4 MB of tables on first use; keep that fast in debug
# builds too.
if(MSVC)
  # MSVC can't combine /O2 with /RTC1 in one file, so the solver is built as
  # its own object library without runtime checks (see ProjectOptions.cmake).
  add_library(vcube_solver OBJECT solver.cpp)
  target_include_directories(vcube_solver PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(vcube_solver PRIVATE vcube_strict)
  set_target_properties(vcube_solver PROPERTIES VCUBE_OPTIMIZE ON)
  target_sources(vcube_cube PRIVATE $<TARGET_OBJECTS:vcube_solver>)
else()
  target_sources(vcube_cube PRIVATE solver.cpp)
  set_source_files_properties(solver.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
