# The version from `git describe` in SOURCE_DIR (e.g.
# "0.2.0-14-gabc1234-dirty" for a checkout 14 commits after the tag
# v0.2.0), or FALLBACK when there is no git checkout.
#
# Included, it defines vcube_describe_version() for configure time (package
# names, the Windows version resource). Run as a script with -P, it writes
# OUTPUT, a header defining VCUBE_VERSION; that runs at every build and only
# rewrites the file when the version changes, so nothing is rebuilt
# otherwise.
function(vcube_describe_version out source_dir fallback)
  set(version "${fallback}")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
      WORKING_DIRECTORY "${source_dir}"
      OUTPUT_VARIABLE described
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE result)
    if(result EQUAL 0 AND described)
      # Without tags, describe gives only the commit: put the project
      # version in front of it.
      if(described MATCHES "^[0-9a-f]+(-dirty)?$")
        set(version "${fallback}-g${described}")
      else()
        string(REGEX REPLACE "^v" "" version "${described}")
      endif()
    endif()
  endif()
  set(${out} "${version}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
  vcube_describe_version(version "${SOURCE_DIR}" "${FALLBACK}")
  set(content "#pragma once\n#define VCUBE_VERSION \"${version}\"\n")
  if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" old)
  endif()
  if(NOT old STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
  endif()
endif()
