# Warning and sanitizer settings shared by every v-cube target.
#
# vcube_strict: the pure C++ libraries (cube, train) build with the full
# conversion warnings. vcube_warnings: Qt-facing code, where qsizetype/int
# conversions are pervasive, keeps the core set only.

add_library(vcube_warnings INTERFACE)
target_compile_features(vcube_warnings INTERFACE cxx_std_23)
if(MSVC)
  target_compile_options(vcube_warnings INTERFACE /W4 /permissive-
    $<$<BOOL:${VCUBE_WARNINGS_AS_ERRORS}>:/WX>)
else()
  target_compile_options(vcube_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<BOOL:${VCUBE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_library(vcube_strict INTERFACE)
target_link_libraries(vcube_strict INTERFACE vcube_warnings)
if(NOT MSVC)
  target_compile_options(vcube_strict INTERFACE -Wconversion -Wsign-conversion)
endif()

if(VCUBE_SANITIZE AND NOT MSVC)
  target_compile_options(vcube_warnings INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(vcube_warnings INTERFACE -fsanitize=address,undefined)
endif()
