# ndtbl

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Build](https://github.com/thomasisensee/ndtbl/actions/workflows/ci.yml/badge.svg)](https://github.com/thomasisensee/ndtbl/actions)
[![Documentation Status](https://readthedocs.org/projects/ndtbl/badge/)](https://ndtbl.readthedocs.io/)
[![codecov](https://codecov.io/gh/thomasisensee/ndtbl/graph/badge.svg?flag=cpp&token=5N4GQ0YP7I)](https://codecov.io/gh/thomasisensee/ndtbl)
[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/thomasisensee/ndtbl/main.svg)](https://results.pre-commit.ci/latest/github/thomasisensee/ndtbl/main)
[![C++ standard](https://img.shields.io/badge/C%2B%2B-14-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)

`ndtbl` is an n-dimensional table format and toolkit.

This repository currently contains two user-facing parts:

- a header-only C++14 library in `include/ndtbl/`
- a separate [pure-Python package](https://pypi.org/project/ndtbl/) in `python/ndtbl/` for reading,
writing, inspecting, querying, and generating `.ndtbl` files

The C++ reader can also be built with optional POSIX `mmap` memory mapping support so payloads
can stay file-backed instead of always being copied into heap memory entirely.

## 🗂️ Layout

- `app/`: C++ command-line tool for inspecting `.ndtbl` files
- `benchmarks/`: C++ benchmarks
- `cmake/`: CMake modules
- `doc/`: Sphinx and Doxygen documentation
- `include/ndtbl/`: public C++ headers
- `python/ndtbl/`: pure-Python package and `ndtbl` CLI
- `tests/`: Catch2-based C++ test suite

## 🖥️ Which interface to use

Use the C++ library when you want to integrate `ndtbl`'s lookup logic directly into a C++ application.

Use the Python package when you want a pip-installable Python interface for creating `.ndtbl` files from NumPy arrays, inspecting existing `.ndtbl` files, or running queries against them. The Python package also provides a convenient CLI for inspecting, querying, and generating `.ndtbl` files. See [`python/ndtbl/README.md`](https://github.com/thomasisensee/ndtbl/tree/main/python/ndtbl) for Python package details.

If you want to inspect `.ndtbl`files with a C++ command-line tool, use the C++ `ndtbl-inspect` executable built from `app/`. It is not prebuilt; it becomes available only after running the local CMake build.


## 📋 Prerequisites

Building the C++ project requires:

- a C++14-compliant compiler for the header-only library interface
- a compiler with C++20 support for the executables in `app/` and tests in `tests/`
- CMake `>= 3.23`
- Doxygen only if you want to build the documentation
- Catch2 only if you want to build the optional C++ test suite

## 🛠️ Build

From the top-level `ndtbl/` directory:

```bash
cmake -B build -Dndtbl_BUILD_TESTING=OFF -Dndtbl_BUILD_DOCS=OFF
cmake --build build
```

This produces the C++ command-line tool in `build/app/`:

- `build/app/ndtbl-inspect`

Relevant CMake options:

- `ndtbl_BUILD_TESTING`: build the C++ test suite, default `OFF`
- `ndtbl_BUILD_BENCHMARKS`: build developer lookup benchmarks, default `OFF`
- `ndtbl_BUILD_DOCS`: build the documentation, default `ON` for top-level builds
- `ndtbl_ENABLE_MMAP`: enable POSIX-only `mmap`-backed payload reads, default `OFF`

When `ndtbl_ENABLE_MMAP=OFF` (the default), `read_group()` reads payload data
into owned heap storage. When `ndtbl_ENABLE_MMAP=ON`, supported POSIX builds
use read-only memory mapping instead, which can reduce heap usage for large
tables and enables shared memory access in multi-process environments.

If you want to install the C++ headers and CMake package metadata:

```bash
cmake --install build --prefix /desired/prefix
```

To enable optional `mmap`-backed reads on supported POSIX platforms:

```bash
cmake -B build -Dndtbl_ENABLE_MMAP=ON
cmake --build build
```

## ⚙️ C++ Tool Workflow

Inspect existing `.ndtbl` files:

```bash
./build/app/ndtbl-inspect output.ndtbl
```

## 📐 Interpolation

The standard C++ lookup path uses multilinear interpolation through explicit
`evaluate_all_linear()` and `Grid::prepare_linear()` calls. This path uses
`2^Dim` table points per query and keeps the hot path allocation-free.

The C++ API also exposes local tensor-product cubic interpolation through
explicit `evaluate_all_cubic()` and `Grid::prepare_cubic()` calls. Cubic
interpolation uses `4^Dim` table points, can be much more expensive in high
dimensions, and may overshoot smooth-looking table data enough to produce
unwanted values. Bounds handling is independent of interpolation order:
queries outside the table domain can either clamp or throw according to the
selected `bounds_policy`.

## 🐍 Python Package

The repository also ships a separate Python package in [`python/ndtbl/`](https://github.com/thomasisensee/ndtbl/tree/main/python/ndtbl).

That package installs a different CLI executable named `ndtbl`, with the
subcommands:

- `inspect`
- `query`
- `generate`

Install it from the package directory:

```bash
cd python/ndtbl
python -m pip install .
```

After that, the Python CLI is available:

```bash
ndtbl --help
```

See [`python/ndtbl/README.md`](https://github.com/thomasisensee/ndtbl/tree/main/python/ndtbl) for usage examples and Python API details.

## 🧪 Testing

Enable the C++ test suite during configuration:

```bash
cmake -B build -Dndtbl_BUILD_TESTING=ON
cmake --build build
```

Then run:

```bash
cd build
ctest --output-on-failure
```

## ⏱️ Benchmarks

The lookup-time benchmarks use [Google Benchmark](https://github.com/google/benchmark) and measure
query preparation, prepared evaluation, typed combined lookup, and
runtime-erased combined lookup for representative 2D, 4D, and 6D tables. See
[`benchmarks/README.md`](https://github.com/thomasisensee/ndtbl/tree/main/benchmarks) for the benchmark case definitions and interpretation.

Build the benchmark target:

```bash
cmake -B build -Dndtbl_BUILD_BENCHMARKS=ON
cmake --build build --target ndtbl_lookup_benchmarks
```

Run a benchmark:

```bash
./build/benchmarks/ndtbl_lookup_benchmarks
```

## 📖 Documentation

Online documentation is available at
[ndtbl.readthedocs.io](https://ndtbl.readthedocs.io/).

To build the docs locally, first install the documentation requirements from
the top-level `ndtbl/` directory:

```bash
python -m pip install -r doc/requirements.txt
```

Then build the Sphinx target:

```bash
cmake -B build -Dndtbl_BUILD_DOCS=ON -Dndtbl_BUILD_TESTING=OFF
cmake --build build --target sphinx-doc
```

Open `build/doc/sphinx/index.html` in a browser to inspect the generated site.
