cmake_minimum_required(VERSION 3.25)

project(vcube
  VERSION 0.1.0
  DESCRIPTION "3x3 alg trainer, scrambler and timer with a virtual cube"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(VCUBE_BUILD_TESTS "Build the unit tests" ON)
option(VCUBE_BUILD_APP "Build the Qt application" ON)
option(VCUBE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VCUBE_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

find_program(VCUBE_CCACHE ccache)
if(VCUBE_CCACHE AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${VCUBE_CCACHE})
endif()
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
  find_program(VCUBE_MOLD mold)
  if(VCUBE_MOLD AND NOT DEFINED CMAKE_LINKER_TYPE)
    set(CMAKE_LINKER_TYPE MOLD)
  endif()
endif()

include(cmake/ProjectOptions.cmake)

add_subdirectory(src)

if(VCUBE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
