cmake_minimum_required(VERSION 3.28)

project(ndtbl_install_consumer
  LANGUAGES CXX
)

if(NDTBL_REQUIRED_VERSION)
  find_package(ndtbl ${NDTBL_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
else()
  find_package(ndtbl CONFIG REQUIRED)
endif()

add_executable(ndtbl_install_consumer
  "${CMAKE_CURRENT_LIST_DIR}/../consumer/main.cpp"
)

target_link_libraries(ndtbl_install_consumer
  PRIVATE
    ndtbl::ndtbl
)

target_compile_features(ndtbl_install_consumer
  PRIVATE
    cxx_std_14
)

# CXX_EXTENSIONS OFF verifies that public headers compile as portable
# ISO C++14 and do not rely on GNU extensions.
set_target_properties(ndtbl_install_consumer
  PROPERTIES
    CXX_EXTENSIONS OFF
)
