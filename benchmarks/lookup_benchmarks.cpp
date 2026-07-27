// SPDX-License-Identifier: MIT

#include "ndtbl/ndtbl.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {

/// Field count used by field-independent preparation benchmarks.
constexpr std::size_t prepare_field_count = 2;
/// Number of precomputed query points in the ring cycled by timed loops.
constexpr std::size_t query_count = 1024;

/**
 * @brief Return the per-axis extent used for a benchmark dimension.
 *
 * The extents keep memory use modest while covering 2D, 4D, and 6D stencil
 * sizes.
 *
 * @tparam Dim Benchmark dimensionality.
 * @return Extent used for every axis in the selected dimensionality.
 */
template<std::size_t Dim>
constexpr std::size_t
default_extent()
{
  if constexpr (Dim == 2) {
    return 256;
  } else if constexpr (Dim == 4) {
    return 16;
  } else {
    static_assert(Dim == 6, "unsupported benchmark dimension");
    return 6;
  }
}

/**
 * @brief Shared table data reused by one benchmark family.
 *
 * The context owns the grid, typed group, runtime-erased group, query ring, and
 * one prepared stencil so benchmark loops do not include setup allocation. The
 * field count is part of the context identity for field-dependent benchmarks.
 *
 * @tparam Dim Benchmark dimensionality.
 */
template<std::size_t Dim>
struct LookupContext
{
  ndtbl::Grid<Dim> grid;
  ndtbl::FieldGroup<Dim, double> group;
  ndtbl::RuntimeFieldGroup<Dim> runtime_group;
  std::vector<std::array<double, Dim>> queries;
  ndtbl::LinearStencil<Dim> prepared;

  LookupContext(const ndtbl::Grid<Dim>& grid_in,
                const ndtbl::FieldGroup<Dim, double>& group_in,
                const std::vector<std::array<double, Dim>>& queries_in)
    : grid(grid_in)
    , group(group_in)
    , runtime_group(group)
    , queries(queries_in)
    , prepared(grid.prepare_linear(queries.front()))
  {
  }
};

/**
 * @brief Build a hypercube shape with the same extent on every axis.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param extent Number of support points per axis.
 * @return Shape array with `Dim` entries.
 */
template<std::size_t Dim>
std::array<std::size_t, Dim>
filled_shape(std::size_t extent)
{
  std::array<std::size_t, Dim> shape = {};
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    shape[axis] = extent;
  }
  return shape;
}

/**
 * @brief Construct either uniform or explicit axes for one benchmark family.
 *
 * Explicit axes use a curved coordinate distribution to exercise the
 * binary-search bracketing path instead of the uniform-axis arithmetic path.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param shape Number of support points on each axis.
 * @param axis_kind Axis representation to construct.
 * @return Axis descriptors in dimension order.
 */
template<std::size_t Dim>
std::array<ndtbl::Axis, Dim>
make_axes(const std::array<std::size_t, Dim>& shape, ndtbl::axis_kind axis_kind)
{
  std::array<ndtbl::Axis, Dim> axes = {};
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    const double min_value = -1.0 - static_cast<double>(axis);
    const double max_value = 1.0 + 0.5 * static_cast<double>(axis);
    if (axis_kind == ndtbl::axis_kind::uniform) {
      axes[axis] = ndtbl::Axis::uniform(min_value, max_value, shape[axis]);
    } else {
      std::vector<double> coordinates(shape[axis], min_value);
      for (std::size_t index = 0; index < shape[axis]; ++index) {
        const double fraction =
          shape[axis] == 1
            ? 0.0
            : static_cast<double>(index) / static_cast<double>(shape[axis] - 1);
        const double curved = fraction * fraction;
        coordinates[index] = std::lerp(min_value, max_value, curved);
      }
      axes[axis] = ndtbl::Axis::from_coordinates(coordinates);
    }
  }
  return axes;
}

/**
 * @brief Construct synthetic field names for a benchmark table.
 *
 * @tparam Dim Benchmark dimensionality, unused but kept for call-site symmetry.
 * @param field_count Number of field names to create.
 * @return Names in payload storage order.
 */
template<std::size_t Dim>
std::vector<std::string>
make_field_names(std::size_t field_count)
{
  std::vector<std::string> names;
  names.reserve(field_count);
  for (std::size_t field = 0; field < field_count; ++field) {
    names.push_back("field" + std::to_string(field));
  }
  return names;
}

/**
 * @brief Create deterministic point-major field payload values.
 *
 * The payload is not part of the measured setup. It provides stable values for
 * interpolation loops and stores `field_count` values for each grid point.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param grid Grid whose support points define the payload layout.
 * @param field_count Number of fields per support point.
 * @return Point-major interleaved payload.
 */
template<std::size_t Dim>
std::vector<double>
make_payload(const ndtbl::Grid<Dim>& grid, std::size_t field_count)
{
  std::vector<double> payload(grid.point_count() * field_count, 0.0);
  std::array<std::size_t, Dim> indices = {};

  for (std::size_t point = 0; point < grid.point_count(); ++point) {
    std::size_t remainder = point;
    double coordinate_sum = 0.0;
    for (std::size_t axis = 0; axis < Dim; ++axis) {
      indices[axis] = remainder / grid.stride(axis);
      remainder %= grid.stride(axis);
      coordinate_sum += (1.0 + static_cast<double>(axis)) *
                        grid.axis(axis).coordinate(indices[axis]);
    }

    const std::size_t base = point * field_count;
    for (std::size_t field = 0; field < field_count; ++field) {
      payload[base + field] =
        coordinate_sum + 0.25 * static_cast<double>(field);
    }
  }

  return payload;
}

/**
 * @brief Create deterministic query points inside the table domain.
 *
 * Queries are precomputed and cycled in the benchmark loops to avoid measuring
 * query generation while avoiding one constant lookup point.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param axes Axis descriptors used to bound generated queries.
 * @param count Number of query points to generate.
 * @return Query coordinates in axis order.
 */
template<std::size_t Dim>
std::vector<std::array<double, Dim>>
make_queries(const std::array<ndtbl::Axis, Dim>& axes, std::size_t count)
{
  std::vector<std::array<double, Dim>> queries;
  queries.reserve(count);

  for (std::size_t query = 0; query < count; ++query) {
    std::array<double, Dim> coordinates = {};
    for (std::size_t axis = 0; axis < Dim; ++axis) {
      const auto fraction =
        static_cast<double>(((query + 17u * axis) % 997u) + 1u) / 998.0;
      coordinates[axis] =
        axes[axis].min() + fraction * (axes[axis].max() - axes[axis].min());
    }
    queries.push_back(coordinates);
  }

  return queries;
}

/**
 * @brief Construct all reusable state for one benchmark family.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to store at each grid point.
 * @return Fully initialized lookup context.
 */
template<std::size_t Dim>
LookupContext<Dim>
make_context(std::size_t extent,
             ndtbl::axis_kind axis_kind,
             std::size_t field_count)
{
  const std::array<std::size_t, Dim> shape = filled_shape<Dim>(extent);
  const std::array<ndtbl::Axis, Dim> axes = make_axes(shape, axis_kind);
  const ndtbl::Grid<Dim> grid(axes);
  const ndtbl::FieldGroup<Dim, double> group(
    grid, make_field_names<Dim>(field_count), make_payload(grid, field_count));
  return LookupContext<Dim>(grid, group, make_queries(axes, query_count));
}

/**
 * @brief Return cached context state for one dimension and axis kind.
 *
 * Static storage keeps setup outside benchmark timing and ensures each
 * benchmark family constructs its table only once per process.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param extent Expected extent for the selected dimensionality.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to store at each grid point.
 * @return Reusable lookup context.
 */
template<std::size_t Dim>
const LookupContext<Dim>&
context(std::size_t extent, ndtbl::axis_kind axis_kind, std::size_t field_count)
{
  const std::size_t context_extent =
    extent == default_extent<Dim>() ? extent : default_extent<Dim>();

  if (axis_kind == ndtbl::axis_kind::uniform) {
    if (field_count == 2) {
      static const LookupContext<Dim> uniform_2 =
        make_context<Dim>(context_extent, ndtbl::axis_kind::uniform, 2);
      return uniform_2;
    }
    if (field_count == 4) {
      static const LookupContext<Dim> uniform_4 =
        make_context<Dim>(context_extent, ndtbl::axis_kind::uniform, 4);
      return uniform_4;
    }
    static const LookupContext<Dim> uniform_8 =
      make_context<Dim>(context_extent, ndtbl::axis_kind::uniform, 8);
    return uniform_8;
  }

  if (field_count == 2) {
    static const LookupContext<Dim> explicit_2 = make_context<Dim>(
      context_extent, ndtbl::axis_kind::explicit_coordinates, 2);
    return explicit_2;
  }
  if (field_count == 4) {
    static const LookupContext<Dim> explicit_4 = make_context<Dim>(
      context_extent, ndtbl::axis_kind::explicit_coordinates, 4);
    return explicit_4;
  }
  static const LookupContext<Dim> explicit_8 = make_context<Dim>(
    context_extent, ndtbl::axis_kind::explicit_coordinates, 8);
  return explicit_8;
}

/**
 * @brief Benchmark axis bracketing and interpolation-stencil preparation.
 *
 * Measures `Grid::prepare_linear(query)` only.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param state Google Benchmark state.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 */
template<std::size_t Dim>
void
bench_prepare(benchmark::State& state,
              std::size_t extent,
              ndtbl::axis_kind axis_kind)
{
  const LookupContext<Dim>& data =
    context<Dim>(extent, axis_kind, prepare_field_count);
  std::size_t query = 0;

  for (auto _ : state) {
    ndtbl::LinearStencil<Dim> prepared =
      data.grid.prepare_linear(data.queries[query]);
    benchmark::DoNotOptimize(prepared);
    query = (query + 1) % data.queries.size();
  }
}

/**
 * @brief Benchmark interpolation using a precomputed stencil.
 *
 * Measures `FieldGroup::evaluate_all_into(prepared, results)` only.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param state Google Benchmark state.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to evaluate at each lookup.
 */
template<std::size_t Dim>
void
bench_prepared_evaluate(benchmark::State& state,
                        std::size_t extent,
                        ndtbl::axis_kind axis_kind,
                        std::size_t field_count)
{
  const LookupContext<Dim>& data = context<Dim>(extent, axis_kind, field_count);
  std::vector<double> results(data.group.field_count(), 0.0);

  for (auto _ : state) {
    data.group.evaluate_all_into(data.prepared, results.data());
    benchmark::DoNotOptimize(results.data());
    benchmark::ClobberMemory();
  }
}

/**
 * @brief Benchmark typed end-to-end lookup from query coordinates.
 *
 * Measures `FieldGroup::evaluate_all_linear_into(query, results)`, including
 * stencil preparation and interpolation.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param state Google Benchmark state.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to evaluate at each lookup.
 */
template<std::size_t Dim>
void
bench_typed_combined(benchmark::State& state,
                     std::size_t extent,
                     ndtbl::axis_kind axis_kind,
                     std::size_t field_count)
{
  const LookupContext<Dim>& data = context<Dim>(extent, axis_kind, field_count);
  std::vector<double> results(data.group.field_count(), 0.0);
  std::size_t query = 0;

  for (auto _ : state) {
    data.group.evaluate_all_linear_into(data.queries[query], results.data());
    benchmark::DoNotOptimize(results.data());
    benchmark::ClobberMemory();
    query = (query + 1) % data.queries.size();
  }
}

/**
 * @brief Benchmark typed end-to-end cubic lookup from query coordinates.
 *
 * Measures `FieldGroup::evaluate_all_cubic_into(query, results)`, including
 * cubic stencil preparation and interpolation.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param state Google Benchmark state.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to evaluate at each lookup.
 */
template<std::size_t Dim>
void
bench_typed_cubic_combined(benchmark::State& state,
                           std::size_t extent,
                           ndtbl::axis_kind axis_kind,
                           std::size_t field_count)
{
  const LookupContext<Dim>& data = context<Dim>(extent, axis_kind, field_count);
  std::vector<double> results(data.group.field_count(), 0.0);
  std::size_t query = 0;

  for (auto _ : state) {
    data.group.evaluate_all_cubic_into(data.queries[query], results.data());
    benchmark::DoNotOptimize(results.data());
    benchmark::ClobberMemory();
    query = (query + 1) % data.queries.size();
  }
}

/**
 * @brief Benchmark runtime-erased end-to-end lookup from query coordinates.
 *
 * Measures `RuntimeFieldGroup::evaluate_all_linear_into(query, results)`,
 * including wrapper dispatch and scratch-buffer handling.
 *
 * @tparam Dim Benchmark dimensionality.
 * @param state Google Benchmark state.
 * @param extent Number of support points per axis.
 * @param axis_kind Axis representation to benchmark.
 * @param field_count Number of fields to evaluate at each lookup.
 */
template<std::size_t Dim>
void
bench_runtime_combined(benchmark::State& state,
                       std::size_t extent,
                       ndtbl::axis_kind axis_kind,
                       std::size_t field_count)
{
  const LookupContext<Dim>& data = context<Dim>(extent, axis_kind, field_count);
  std::vector<double> results(data.runtime_group.field_count(), 0.0);
  std::size_t query = 0;

  for (auto _ : state) {
    data.runtime_group.evaluate_all_linear_into(data.queries[query],
                                                results.data());
    benchmark::DoNotOptimize(results.data());
    benchmark::ClobberMemory();
    query = (query + 1) % data.queries.size();
  }
}

/**
 * @brief Register field-dependent lookup benchmarks for one field count.
 */
#define NDTBL_REGISTER_FIELD_BENCHMARKS(                                       \
  DIM, EXTENT, LAYOUT, NAME, FIELD_COUNT)                                      \
  BENCHMARK_CAPTURE(                                                           \
    bench_prepared_evaluate<DIM>, NAME, EXTENT, LAYOUT, FIELD_COUNT);          \
  BENCHMARK_CAPTURE(                                                           \
    bench_typed_combined<DIM>, NAME, EXTENT, LAYOUT, FIELD_COUNT);             \
  BENCHMARK_CAPTURE(                                                           \
    bench_runtime_combined<DIM>, NAME, EXTENT, LAYOUT, FIELD_COUNT)

/**
 * @brief Register one preparation benchmark for one dimension/axis-kind pair.
 */
#define NDTBL_REGISTER_PREPARE_BENCHMARK(DIM, EXTENT, LAYOUT, NAME)            \
  BENCHMARK_CAPTURE(bench_prepare<DIM>, NAME, EXTENT, LAYOUT)

/**
 * @brief Register all field-count variants for one dimension/axis-kind pair.
 */
#define NDTBL_REGISTER_LOOKUP_BENCHMARKS(DIM, EXTENT, LAYOUT, NAME)            \
  NDTBL_REGISTER_PREPARE_BENCHMARK(DIM, EXTENT, LAYOUT, NAME);                 \
  NDTBL_REGISTER_FIELD_BENCHMARKS(DIM, EXTENT, LAYOUT, NAME##_fields_2, 2);    \
  NDTBL_REGISTER_FIELD_BENCHMARKS(DIM, EXTENT, LAYOUT, NAME##_fields_4, 4);    \
  NDTBL_REGISTER_FIELD_BENCHMARKS(DIM, EXTENT, LAYOUT, NAME##_fields_8, 8)

NDTBL_REGISTER_LOOKUP_BENCHMARKS(2,
                                 default_extent<2>(),
                                 ndtbl::axis_kind::uniform,
                                 d2_uniform);
NDTBL_REGISTER_LOOKUP_BENCHMARKS(2,
                                 default_extent<2>(),
                                 ndtbl::axis_kind::explicit_coordinates,
                                 d2_explicit);
NDTBL_REGISTER_LOOKUP_BENCHMARKS(4,
                                 default_extent<4>(),
                                 ndtbl::axis_kind::uniform,
                                 d4_uniform);
BENCHMARK_CAPTURE(bench_typed_cubic_combined<4>,
                  d4_uniform_fields_4_cubic_combined,
                  default_extent<4>(),
                  ndtbl::axis_kind::uniform,
                  4);
NDTBL_REGISTER_LOOKUP_BENCHMARKS(4,
                                 default_extent<4>(),
                                 ndtbl::axis_kind::explicit_coordinates,
                                 d4_explicit);
NDTBL_REGISTER_LOOKUP_BENCHMARKS(6,
                                 default_extent<6>(),
                                 ndtbl::axis_kind::uniform,
                                 d6_uniform);
NDTBL_REGISTER_LOOKUP_BENCHMARKS(6,
                                 default_extent<6>(),
                                 ndtbl::axis_kind::explicit_coordinates,
                                 d6_explicit);

} // namespace
