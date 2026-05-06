# ndtbl Lookup Benchmarks

This directory contains lookup-time benchmarks for the C++ `ndtbl` library.
The benchmarks use [Google Benchmark](https://github.com/google/benchmark) and focus on
interpolation cost after a table has already been constructed in memory.

The suite does not measure file I/O, `.ndtbl` parsing, memory mapping, or table
generation time.

## Benchmark Data

Each benchmark case uses a deterministic synthetic table with:

- `2`, `4`, or `8` fields per grid point for field-dependent measurements
- `1024` precomputed query points in a ring, cycled during each timed loop
- uniform and explicit-coordinate axis variants
- 2D shape `256 x 256`
- 4D shape `16 x 16 x 16 x 16`
- 6D shape `6 x 6 x 6 x 6 x 6 x 6`

The 6D case is intentionally small in each axis so the payload stays modest
while still exercising a 64-corner interpolation stencil.

The query ring size is not the number of benchmark iterations. Google
Benchmark chooses the number of timed iterations; each iteration advances to
the next precomputed query modulo the ring size.

## Measured Operations

`bench_prepare` measures only `Grid::prepare_linear(query)`. This isolates axis
bracketing and interpolation-stencil construction. It is registered once per
dimension and axis layout because field count does not affect stencil
preparation.

`bench_prepared_evaluate` measures
`FieldGroup::evaluate_all_into(prepared, results)`. This reuses one prepared
stencil and isolates interpolation over all fields. It is registered with `2`,
`4`, and `8` fields.

`bench_typed_combined` measures
`FieldGroup::evaluate_all_linear_into(query, results)`. This is the typed
end-to-end path from query coordinates to interpolated field values. It is
registered with `2`, `4`, and `8` fields.

`bench_runtime_combined` measures
`RuntimeFieldGroup::evaluate_all_linear_into(query, results)`. This covers the
runtime-erased path, including its wrapper dispatch and scratch-buffer copy. It
is registered with `2`, `4`, and `8` fields.

`bench_typed_cubic_combined` measures one focused cubic case:
`FieldGroup::evaluate_all_cubic_into(query, results)` for a 4D uniform table
with `4` fields. Cubic interpolation uses `4^Dim` table points, so a 4D case
already exercises the important 256-point stencil cost. The suite intentionally
does not expand cubic across every dimension, axis layout, or field count.

## Build And Run

Configure and build the benchmark target:

```bash
cmake -B build -Dndtbl_BUILD_BENCHMARKS=ON
cmake --build build --target ndtbl_lookup_benchmarks
```

Run a short benchmark:

```bash
./build/benchmarks/ndtbl_lookup_benchmarks
```

For performance numbers, prefer a release build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -Dndtbl_BUILD_BENCHMARKS=ON
cmake --build build --target ndtbl_lookup_benchmarks
./build/benchmarks/ndtbl_lookup_benchmarks
```
