find_package(benchmark CONFIG QUIET)

if(NOT benchmark_FOUND)
  include(FetchContent)

  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.5
  )

  FetchContent_MakeAvailable(benchmark)
endif()

add_executable(ndtbl_lookup_benchmarks)

target_sources(ndtbl_lookup_benchmarks
  PRIVATE
    lookup_benchmarks.cpp
)

target_link_libraries(ndtbl_lookup_benchmarks
  PRIVATE
    ndtbl
    benchmark::benchmark
    benchmark::benchmark_main
)

target_compile_features(ndtbl_lookup_benchmarks
  PRIVATE
    cxx_std_20
)
