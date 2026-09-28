# Python table generation and C++ interpolation

- `generate.py` samples an analytic function at 11 uniformly spaced points on `[0, 1]` and writes the float64 field `A` to `example1D.ndtbl`.
- `evaluate.cpp` reads that filename directly and prints 101 rows containing the coordinate, multilinear result, and cubic result.

## Build and run

From the repository root:

```sh
python -m pip install ./python/ndtbl
cmake -S examples/python_cpp -B build/python-cpp
cmake --build build/python-cpp
```

Run both programs from the same directory, for example:

```sh
cd build/python-cpp
python ../../examples/python_cpp/generate.py
./ndtbl_example
```

## Check the workflow

From the repository root:

```sh
ctest --test-dir build/python-cpp --output-on-failure
```

After building the C++ example, CTest runs `generate.py` followed by the reader in a temporary directory using the Python package from this checkout. The check
passes when both programs exit successfully. CI builds the example and runs this check using the default heap-loaded reader. Configure with `-DBUILD_TESTING=OFF` to disable the check and skip Python interpreter discovery.
