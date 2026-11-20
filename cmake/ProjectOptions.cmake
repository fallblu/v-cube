# Warning and sanitizer settings shared by every v-cube target.
#
# vcube_strict: the pure C++ libraries (cube, train) build with the full
# conversion warnings. vcube_warnings: Qt-facing code, where qsizetype/int
# conversions are pervasive, keeps the core set only.

add_library(vcube_warnings INTERFACE)
target_compile_features(vcube_warnings INTERFACE cxx_std_23)
if(MSVC)
  # Sources are UTF-8 ("…", "→"); MSVC otherwise reads them as the ANSI code
  # page.
  target_compile_options(vcube_warnings INTERFACE /W4 /permissive- /utf-8
    $<$<BOOL:${VCUBE_WARNINGS_AS_ERRORS}>:/WX>)
  # MSVC rejects /O2 together with the debug /RTC1, so the debug
  # optimization flags move from the global flags to the targets, and a
  # target with the VCUBE_OPTIMIZE property gets /O2 instead (see
  # vcube_optimize_in_debug below).
  string(REGEX REPLACE "/RTC1|/Od|/Ob0" "" CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG}")
  set(vcube_debug "$<CONFIG:Debug>")
  set(vcube_optimize "$<BOOL:$<TARGET_PROPERTY:VCUBE_OPTIMIZE>>")
  target_compile_options(vcube_warnings INTERFACE
    "$<$<AND:${vcube_debug},${vcube_optimize}>:/O2>"
    "$<$<AND:${vcube_debug},$<NOT:${vcube_optimize}>>:/Od>"
    "$<$<AND:${vcube_debug},$<NOT:${vcube_optimize}>>:/Ob0>"
    "$<$<AND:${vcube_debug},$<NOT:${vcube_optimize}>>:/RTC1>")
else()
  target_compile_options(vcube_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<BOOL:${VCUBE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# <windows.h>, should anything include it, defines min and max as macros,
# which break std::min and std::max.
if(WIN32)
  target_compile_definitions(vcube_warnings INTERFACE NOMINMAX)
endif()

add_library(vcube_strict INTERFACE)
target_link_libraries(vcube_strict INTERFACE vcube_warnings)
if(NOT MSVC)
  target_compile_options(vcube_strict INTERFACE -Wconversion -Wsign-conversion)
endif()

# UBSan stops at the first undefined behaviour, so that it fails the test
# instead of printing a line in a long log.
if(VCUBE_SANITIZE AND NOT MSVC)
  target_compile_options(vcube_warnings INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer)
  target_link_options(vcube_warnings INTERFACE
    -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(VCUBE_SANITIZE)
  message(WARNING "VCUBE_SANITIZE has no effect with MSVC: the build is not sanitized")
endif()

# Adds sources to target, optimised in debug builds too (the solvers' tables
# and searches would be slow otherwise). MSVC can't combine /O2 with /RTC1 in
# one file, so there the sources form an object library without runtime
# checks.
function(vcube_optimize_in_debug target)
  if(MSVC)
    add_library(${target}_optimized OBJECT ${ARGN})
    target_include_directories(${target}_optimized PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${target}_optimized PRIVATE vcube_strict)
    set_target_properties(${target}_optimized PROPERTIES VCUBE_OPTIMIZE ON)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${target}_optimized>)
  else()
    target_sources(${target} PRIVATE ${ARGN})
    set_source_files_properties(${ARGN} PROPERTIES
      COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
  endif()
endfunction()
