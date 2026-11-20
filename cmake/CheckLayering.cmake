# Checks that each layer includes nothing from the layers above it:
# src/cube (model) <- src/train (logic, no Qt) <- src/io (QtCore only)
# <- src/app. Every target exports the same include root, so the compiler
# alone would not catch an upward include.
#
#   cmake -DSOURCE_DIR=<repo>/src [-DQTCORE_INCLUDE_DIR=<dir>] -P CheckLayering.cmake
#
# With QTCORE_INCLUDE_DIR, every Qt header that src/io includes must be a
# QtCore header; without it, only the QtGui and QtWidgets module paths are
# refused there.

if(NOT SOURCE_DIR)
  message(FATAL_ERROR "Pass -DSOURCE_DIR=<repo>/src")
endif()

set(violations 0)

# Reports each include in src/<layer> that the layering forbids.
function(check_layer layer)
  file(GLOB_RECURSE files "${SOURCE_DIR}/${layer}/*.cpp"
    "${SOURCE_DIR}/${layer}/*.hpp" "${SOURCE_DIR}/${layer}/*.h")
  foreach(file IN LISTS files)
    file(RELATIVE_PATH rel "${SOURCE_DIR}" "${file}")
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS lines)
      if(NOT line MATCHES "include[ \t]*[<\"]([^>\"]+)[>\"]")
        continue()
      endif()
      set(header "${CMAKE_MATCH_1}")
      set(reason "")
      if(layer STREQUAL "cube")
        if(header MATCHES "^(train|io|app)/")
          set(reason "an upper layer")
        elseif(header MATCHES "^Q")
          set(reason "Qt")
        endif()
      elseif(layer STREQUAL "train")
        if(header MATCHES "^(io|app)/")
          set(reason "an upper layer")
        elseif(header MATCHES "^Q")
          set(reason "Qt")
        endif()
      elseif(layer STREQUAL "io")
        if(header MATCHES "^app/")
          set(reason "the app layer")
        elseif(header MATCHES "^Qt(Gui|Widgets)/")
          set(reason "a Qt module other than QtCore")
        elseif(QTCORE_INCLUDE_DIR AND header MATCHES "^Q"
            AND NOT header MATCHES "^QtCore/"
            AND NOT EXISTS "${QTCORE_INCLUDE_DIR}/${header}")
          set(reason "a Qt module other than QtCore")
        endif()
      endif()
      if(reason)
        message(SEND_ERROR "${rel} includes <${header}>, from ${reason}")
        math(EXPR violations "${violations} + 1")
      endif()
    endforeach()
  endforeach()
  set(violations ${violations} PARENT_SCOPE)
endfunction()

check_layer(cube)
check_layer(train)
check_layer(io)

if(violations GREATER 0)
  message(FATAL_ERROR "${violations} include(s) break the layering")
endif()
