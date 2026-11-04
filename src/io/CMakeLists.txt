find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_library(vcube_io
  algset_json.cpp
  library.cpp
  sessions.cpp
)
target_include_directories(vcube_io PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_io PUBLIC vcube_train Qt6::Core PRIVATE vcube_warnings)
