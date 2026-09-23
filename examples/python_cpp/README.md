# Python table generation and C++ interpolation

- `generate.py` samples an analytic function at 11 uniformly spaced points on `[0, 1]` and writes the float64 field `A` to `example1D.ndtbl`.
- `evaluate.cpp` reads that filename directly and prints 101 rows containing
 the coordinate, multilinear result, and cubic result.

## Build and run

Requirements: CMake 3.28 or newer, a C++14 compiler, and Python 3.11 or newer.
From the repository root, install the local Python package and build:

```sh
python -m pip install ./python/ndtbl
cmake -S examples/python_cpp -B build/python-cpp
cmake --build build/python-cpp
```

Run both programs from the same directory, for example on Linux/macOS:

```sh
cd build/python-cpp
python ../../examples/python_cpp/generate.py
./ndtbl_example
```

With a multi-configuration generator such as Visual Studio, the executable
is in `Release/ndtbl_example.exe` instead.

## Check the workflow

From the repository root:

```sh
ctest --test-dir build/python-cpp -C Release --output-on-failure
```

CTest runs the actual example programs in a temporary directory using the
Python package from this checkout. It checks the output shape and coordinates,
all linear results against NumPy interpolation, cubic results at the table
nodes, and cubic interpolation at interior and boundary queries against
independent polynomial fits. Tolerances account for the example's default
six-significant-digit C++ output. The check requires NumPy but not pytest or
Catch2. CI uses the default heap-loaded reader. Use `-DBUILD_TESTING=OFF` to
build only the C++ example.
