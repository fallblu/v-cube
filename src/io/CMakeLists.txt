add_library(vcube_io
  algset_json.cpp
  alglab_store.cpp
  backup.cpp
  blind_store.cpp
  data_file.cpp
  f2l_store.cpp
  inspection_store.cpp
  library.cpp
  move_counts.cpp
  sessions.cpp
)
target_include_directories(vcube_io PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(vcube_io PUBLIC vcube_train Qt6::Core PRIVATE vcube_warnings)
