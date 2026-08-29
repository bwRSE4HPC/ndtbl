cmake_minimum_required(VERSION 3.28)

project(ndtbl_add_subdirectory_consumer
  LANGUAGES CXX
)

set(NDTBL_SOURCE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../.."
  CACHE PATH
  "Path to the ndtbl source tree"
)

add_subdirectory(
  "${NDTBL_SOURCE_DIR}"
  ndtbl-source
  EXCLUDE_FROM_ALL
)

add_executable(ndtbl_add_subdirectory_consumer
  "${CMAKE_CURRENT_LIST_DIR}/../consumer/main.cpp"
)

target_link_libraries(ndtbl_add_subdirectory_consumer
  PRIVATE
    ndtbl::ndtbl
)

target_compile_features(ndtbl_add_subdirectory_consumer
  PRIVATE
    cxx_std_14
)

# CXX_EXTENSIONS OFF verifies that public headers compile as portable
# ISO C++14 and do not rely on GNU extensions.
set_target_properties(ndtbl_add_subdirectory_consumer
  PROPERTIES
    CXX_EXTENSIONS OFF
)
