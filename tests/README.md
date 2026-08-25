# ndtbl C++ Tests

This directory contains the Catch2-based C++ test suite for the header-only `ndtbl` library. The tests focus on correctness and binary-format behavior.

## Running The Tests

Enable tests when configuring the project:

```bash
cmake -B build -Dndtbl_BUILD_TESTING=ON
cmake --build build
```

Then run the discovered tests with CTest:

```bash
cd build
ctest --output-on-failure
```

CMake builds one test executable, `ndtbl_tests`. Individual Catch2 test cases are discovered through `catch_discover_tests` and registered with the `ndtbl` CTest prefix.

The test build first tries to find an installed or parent-provided Catch2 package with `find_package(Catch2 3 QUIET)`. If `Catch2::Catch2WithMain` is not available, CMake falls back to `FetchContent`.

## Test Files

- `ndtbl_interpolation_t.cpp` checks linear and experimental cubic interpolation behavior for typed `FieldGroup` objects and runtime-erased `RuntimeFieldGroup` objects.
- `ndtbl_io_t.cpp` checks binary read/write behavior, metadata handling, and rejection of malformed files.
- `test_support.hpp` provides shared helpers for temporary files, byte-level layout checks, and synthetic linear payload generation.

## What Is Covered

- Exact recovery of synthetic linear fields on uniform and explicit axes.
- Exact recovery of synthetic cubic fields on uniform and explicit axes.
- 2D and 4D interpolation cases.
- Vertex queries on support points.
- Clamp and throw-error bounds policies for out-of-domain coordinates.
- Runtime-erased lookup through `RuntimeFieldGroup`.
- Compile-time stencil sizes for 4D linear and cubic interpolation.
- Round trips through `write_group`, `read_group`, and `read_group_metadata`.
- Little-endian binary layout for a small documented file.
- Rejection of mismatched dimensions, truncated payloads, invalid reserved fields, and inconsistent payload offsets.

The interpolation tests use polynomial fields because linear and cubic interpolation should recover matching low-order polynomials exactly. This makes correctness failures easier to interpret than using arbitrary tabulated data.

## Not Currently Covered

- Optional `mmap` behavior in realistic access patterns.
