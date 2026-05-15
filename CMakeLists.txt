cmake_minimum_required(VERSION 3.23)

# Set a name and a version number for the project:
project(ndtbl
  VERSION 0.0.1
  LANGUAGES CXX
)

# Initialize some default paths
include(GNUInstallDirs)

# Define build options
option(ndtbl_BUILD_BENCHMARKS "Enable building of benchmarks" OFF)
option(ndtbl_BUILD_DOCS "Enable building of documentation" OFF)
option(ndtbl_BUILD_TESTING "Enable building of tests" OFF)

# Option for enabling POSIX mmap support for payload reads
option(ndtbl_ENABLE_MMAP "Enable POSIX mmap-backed ndtbl payload reads" OFF)

if(ndtbl_ENABLE_MMAP AND NOT UNIX)
  message(FATAL_ERROR "ndtbl_ENABLE_MMAP requires a POSIX platform")
endif()

# Add an interface target for the header-only library
add_library(ndtbl INTERFACE)

# Add the public headers that belong to this library
target_sources(ndtbl
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/ndtbl/types.hpp
      include/ndtbl/axis.hpp
      include/ndtbl/grid.hpp
      include/ndtbl/field_group.hpp
      include/ndtbl/runtime_field_group.hpp
      include/ndtbl/metadata.hpp
      include/ndtbl/payload.hpp
      include/ndtbl/io.hpp
      include/ndtbl/ndtbl.hpp
      include/ndtbl/detail/size_math.hpp
      include/ndtbl/detail/binary_io.hpp
      include/ndtbl/detail/mapped_payload.hpp
)

# Compile flag for enabling mmap support
target_compile_definitions(ndtbl
  INTERFACE
    NDTBL_ENABLE_MMAP=$<BOOL:${ndtbl_ENABLE_MMAP}>
)

# Request a minimum C++ standard for this target
target_compile_features(ndtbl
  INTERFACE
    cxx_std_14
)

# Compile the application executable
if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory(app)
endif()

# Compile the benchmarks
if(ndtbl_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# Add the documentation
if(ndtbl_BUILD_DOCS)
  add_subdirectory(doc)
endif()

# Compile the tests
if(ndtbl_BUILD_TESTING)
  include(CTest)
  add_subdirectory(tests)
endif()

# Add an alias target for use if this project is included as a subproject in another project
add_library(ndtbl::ndtbl ALIAS ndtbl)

# Install the target and headers
install(
  TARGETS ndtbl
  EXPORT ndtblTargets
  FILE_SET HEADERS
)

# Install the exported targets (creates ndtblTargets.cmake)
install(
  EXPORT ndtblTargets
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ndtbl
  NAMESPACE ndtbl::
)

# Include helpers for calling configure_package_config_file
include(CMakePackageConfigHelpers)

# Generate the ndtblConfig.cmake file
configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/ndtblConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ndtblConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ndtbl
)

# Install the generated Config file
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ndtblConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ndtbl
)

# Generate a version file
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ndtblConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

# Install the version file
install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ndtblConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ndtbl
)

# Print a summary of found dependencies
include(FeatureSummary)
feature_summary(WHAT ALL)
