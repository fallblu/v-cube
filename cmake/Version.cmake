# Writes OUTPUT, a header defining VCUBE_VERSION, from `git describe` in
# SOURCE_DIR (e.g. "v0.2.0-14-gabc1234-dirty"), or FALLBACK when there is
# no git checkout. Run at every build; the file is only rewritten when the
# version changes, so nothing is rebuilt otherwise.
set(version "${FALLBACK}")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE described
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE result)
  if(result EQUAL 0 AND described)
    # Without tags, describe gives only the commit: put the project version
    # in front of it.
    if(described MATCHES "^[0-9a-f]+(-dirty)?$")
      set(version "${FALLBACK}-g${described}")
    else()
      string(REGEX REPLACE "^v" "" version "${described}")
    endif()
  endif()
endif()
set(content "#pragma once\n#define VCUBE_VERSION \"${version}\"\n")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" old)
endif()
if(NOT old STREQUAL content)
  file(WRITE "${OUTPUT}" "${content}")
endif()
