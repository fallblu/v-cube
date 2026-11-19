cmake_minimum_required(VERSION 3.25)

project(vcube
  VERSION 0.1.0
  DESCRIPTION "3x3 alg trainer, scrambler and timer with a virtual cube"
  LANGUAGES CXX)

# The standard comes from the targets' compile features (C++23, see
# cmake/ProjectOptions.cmake); compile_commands.json from the presets.
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(VCUBE_BUILD_TESTS "Build the unit tests" ON)
option(VCUBE_BUILD_APP "Build the Qt application" ON)
option(VCUBE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VCUBE_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# Faster rebuilds and links, when asked for (both off by default so that a
# build doesn't depend on what happens to be on PATH).
option(VCUBE_USE_CCACHE "Compile through ccache" OFF)
option(VCUBE_USE_MOLD "Link with mold (CMake 3.29 or later)" OFF)
if(VCUBE_USE_CCACHE)
  find_program(VCUBE_CCACHE_PROGRAM ccache REQUIRED)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${VCUBE_CCACHE_PROGRAM})
endif()
if(VCUBE_USE_MOLD)
  if(CMAKE_VERSION VERSION_LESS 3.29)
    message(FATAL_ERROR "VCUBE_USE_MOLD needs CMake 3.29 or later")
  endif()
  set(CMAKE_LINKER_TYPE MOLD)
endif()

include(cmake/ProjectOptions.cmake)

# Qt Core is needed for storage; the GUI parts only for the app.
if(VCUBE_BUILD_APP)
  set(vcube_qt_components Core Gui Widgets Concurrent Network)
else()
  set(vcube_qt_components Core)
endif()
find_package(Qt6 6.5 QUIET COMPONENTS ${vcube_qt_components})
if(NOT Qt6_FOUND)
  # The windows-* presets find Qt through QTDIR only.
  if(WIN32)
    message(FATAL_ERROR "Qt 6.5 or later was not found. Set QTDIR to your "
      "Qt msvc2022_64 folder (for example "
      "set QTDIR=C:\\Qt\\6.10.0\\msvc2022_64) and configure again.")
  endif()
  find_package(Qt6 6.5 REQUIRED COMPONENTS ${vcube_qt_components})
endif()

add_subdirectory(src)

if(VCUBE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
