# Prefer an installed or parent-provided Catch2 package, and only fetch it as
# a fallback when the test dependency is not available yet.
find_package(Catch2 3 QUIET)

if(NOT TARGET Catch2::Catch2WithMain)
  include(FetchContent)

  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.14.0)

  FetchContent_MakeAvailable(Catch2)
endif()

# Include Catch2's CMake helper functions (e.g., catch_discover_tests).
include(Catch)
find_package(Threads REQUIRED)

# Create a single test binary that compiles all test files.
add_executable(ndtbl_tests)

# Add the implementation file
target_sources(ndtbl_tests
  PRIVATE
    ndtbl_interpolation_t.cpp
    ndtbl_io_t.cpp
)

# Link the test binary
target_link_libraries(ndtbl_tests
  PUBLIC
    ndtbl
    Catch2::Catch2WithMain
    Threads::Threads
)

# allow user to run tests with `make test` or `ctest`
catch_discover_tests(ndtbl_tests
  TEST_PREFIX "ndtbl."
)
