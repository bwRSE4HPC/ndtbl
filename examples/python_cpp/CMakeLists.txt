cmake_minimum_required(VERSION 3.28)

project(ndtbl_python_cpp_example
  LANGUAGES CXX
)

add_subdirectory(../.. ndtbl-source EXCLUDE_FROM_ALL)

add_executable(ndtbl_example evaluate.cpp)
target_link_libraries(ndtbl_example
  PRIVATE ndtbl::ndtbl
)

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
  add_test(NAME ndtbl.python_cpp_example
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_example.py"
      "$<TARGET_FILE:ndtbl_example>"
  )
  set_tests_properties(ndtbl.python_cpp_example PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/../../python/ndtbl/src"
    TIMEOUT 60
  )
endif()
