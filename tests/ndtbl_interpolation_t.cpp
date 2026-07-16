// SPDX-License-Identifier: MIT

#include "ndtbl/ndtbl.hpp"

#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template<std::size_t Dim>
std::array<double, Dim>
coefficients_a()
{
  std::array<double, Dim> coeffs = {};
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    coeffs[axis] = 0.5 + static_cast<double>(axis);
  }
  return coeffs;
}

template<std::size_t Dim>
std::array<double, Dim>
coefficients_b()
{
  std::array<double, Dim> coeffs = {};
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    coeffs[axis] = -1.25 + 0.75 * static_cast<double>(axis);
  }
  return coeffs;
}

template<std::size_t Dim>
void
require_linear_recovery(const std::array<ndtbl::Axis, Dim>& axes,
                        const std::array<double, Dim>& query)
{
  const std::array<double, Dim> coeffs0 = coefficients_a<Dim>();
  const std::array<double, Dim> coeffs1 = coefficients_b<Dim>();
  const double intercept0 = 1.25;
  const double intercept1 = -0.75;

  const ndtbl::FieldGroup<Dim, double> group(
    ndtbl::Grid<Dim>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coeffs0, intercept0, coeffs1, intercept1));

  const std::vector<double> values = group.evaluate_all_linear(query);
  std::array<double, 2> into_values = { 0.0, 0.0 };
  group.evaluate_all_linear_into(query, into_values.data());

  const double expected0 = ndtbl_test::linear_value(query, coeffs0, intercept0);
  const double expected1 = ndtbl_test::linear_value(query, coeffs1, intercept1);

  REQUIRE(values.size() == 2);
  REQUIRE(values[0] == Catch::Approx(expected0));
  REQUIRE(values[1] == Catch::Approx(expected1));
  REQUIRE(into_values[0] == Catch::Approx(expected0));
  REQUIRE(into_values[1] == Catch::Approx(expected1));
}

template<std::size_t Dim>
void
require_clamped_linear_recovery(const std::array<ndtbl::Axis, Dim>& axes,
                                const std::array<double, Dim>& query)
{
  const std::array<double, Dim> clamped_query =
    ndtbl_test::clamp_to_axes(axes, query);
  require_linear_recovery(axes, clamped_query);

  const std::array<double, Dim> coeffs0 = coefficients_a<Dim>();
  const std::array<double, Dim> coeffs1 = coefficients_b<Dim>();
  const double intercept0 = 1.25;
  const double intercept1 = -0.75;

  const ndtbl::FieldGroup<Dim, double> group(
    ndtbl::Grid<Dim>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coeffs0, intercept0, coeffs1, intercept1));

  std::array<double, 2> values = { 0.0, 0.0 };
  group.evaluate_all_linear_into(query, values.data());

  REQUIRE(values[0] == Catch::Approx(ndtbl_test::linear_value(
                         clamped_query, coeffs0, intercept0)));
  REQUIRE(values[1] == Catch::Approx(ndtbl_test::linear_value(
                         clamped_query, coeffs1, intercept1)));
}

template<std::size_t Dim>
void
require_loaded_linear_recovery(const std::array<ndtbl::Axis, Dim>& axes,
                               const std::array<double, Dim>& query)
{
  const std::array<double, Dim> coeffs0 = coefficients_a<Dim>();
  const std::array<double, Dim> coeffs1 = coefficients_b<Dim>();
  const double intercept0 = 1.25;
  const double intercept1 = -0.75;

  const ndtbl::FieldGroup<Dim, double> group(
    ndtbl::Grid<Dim>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coeffs0, intercept0, coeffs1, intercept1));

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::RuntimeFieldGroup<Dim> loaded =
    ndtbl::read_runtime_field_group<Dim>(path);
  std::array<double, 2> values = { 0.0, 0.0 };
  loaded.evaluate_all_linear_into(query, values.data());

  REQUIRE(values[0] ==
          Catch::Approx(ndtbl_test::linear_value(query, coeffs0, intercept0)));
  REQUIRE(values[1] ==
          Catch::Approx(ndtbl_test::linear_value(query, coeffs1, intercept1)));

  std::remove(path.c_str());
}

template<std::size_t Dim, class Function>
std::vector<double>
build_single_field_payload(const std::array<ndtbl::Axis, Dim>& axes,
                           Function function)
{
  std::vector<double> payload;
  std::array<double, Dim> coordinates = {};

  const auto append_point_value = [&](const auto& self,
                                      std::size_t axis) -> void {
    if (axis == Dim) {
      payload.push_back(function(coordinates));
      return;
    }

    for (std::size_t index = 0; index < axes[axis].size(); ++index) {
      coordinates[axis] = axes[axis].coordinate(index);
      self(self, axis + 1);
    }
  };

  append_point_value(append_point_value, 0);
  return payload;
}

double
cubic_1d(double x)
{
  return 1.25 + 0.75 * x - 2.0 * x * x + 0.5 * x * x * x;
}

double
cubic_2d(double x, double y)
{
  return 1.0 + 0.5 * x - 1.25 * y + 0.25 * x * y + 0.125 * x * x * y * y -
         0.5 * x * x + 0.75 * y * y * y - 0.125 * x * x * x * y;
}

} // namespace

static_assert(ndtbl::LinearStencil<4>::points == 16,
              "4D linear interpolation should use 16 table points");
static_assert(ndtbl::CubicStencil<4>::points == 256,
              "4D cubic interpolation should use 256 table points");

TEST_CASE("grid rejects shape products exceeding supported size",
          "[grid][validation]")
{
  const std::size_t huge_extent =
    std::numeric_limits<std::size_t>::max() / 2u + 1u;
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
    ndtbl::Axis::uniform(0.0, 1.0, huge_extent),
  };

  REQUIRE_THROWS_AS(ndtbl::Grid<2>(axes), std::overflow_error);
}

TEST_CASE("field group rejects payload shape products exceeding supported size",
          "[field_group][validation]")
{
  const std::size_t huge_extent =
    std::numeric_limits<std::size_t>::max() / 2u + 1u;
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, huge_extent),
  };
  const ndtbl::Grid<1> grid(axes);

  REQUIRE_THROWS_AS(
    (ndtbl::FieldGroup<1, double>(grid, { "A", "B" }, std::vector<double>())),
    std::overflow_error);
}

TEST_CASE("payload view keeps checked access separate from typed fast access",
          "[payload]")
{
  const std::vector<double> payload = { 1.0, 2.0, 3.0 };
  const ndtbl::PayloadView<double> view = ndtbl::payload_view(payload);

  REQUIRE(view.typed_data() == payload.data());
  REQUIRE(view.at(1) == Catch::Approx(2.0));
  REQUIRE(view.unchecked(2) == Catch::Approx(3.0));
  REQUIRE_THROWS_AS(view.at(payload.size()), std::out_of_range);
}

TEST_CASE("field group evaluates unaligned byte-backed payloads",
          "[field_group][payload][interpolation]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const std::vector<double> payload = { 1.0, 3.0 };

  std::shared_ptr<std::vector<std::uint8_t>> storage =
    std::make_shared<std::vector<std::uint8_t>>(
      payload.size() * sizeof(double) + 1u);
  std::uint8_t* const unaligned_data = storage->data() + 1u;
  std::memcpy(unaligned_data, payload.data(), payload.size() * sizeof(double));

  const ndtbl::PayloadView<double> view(unaligned_data, payload.size());
  REQUIRE(view.typed_data() == nullptr);

  const std::shared_ptr<const std::uint8_t> owner(storage, unaligned_data);
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, view, owner);

  std::array<double, 1> values = { 0.0 };
  group.evaluate_all_linear_into({ 0.25 }, values.data());

  REQUIRE(values[0] == Catch::Approx(1.5));
}

TEST_CASE("field group evaluates float payloads into double output",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, float> group(
    ndtbl::Grid<1>(axes), { "A" }, { 16777216.0f, 16777218.0f });

  const std::vector<double> values =
    group.evaluate_all_linear_as<double>({ 0.5 });
  std::array<double, 1> into_values = { 0.0 };
  group.evaluate_all_linear_into_as({ 0.5 }, into_values.data());

  REQUIRE(values[0] == Catch::Approx(16777217.0));
  REQUIRE(into_values[0] == Catch::Approx(16777217.0));
}

TEST_CASE("field group exactly recovers linear fields on uniform axes in 2D",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(-1.0, 2.0, 4),
    ndtbl::Axis::uniform(0.5, 3.5, 5),
  };

  require_linear_recovery(axes, { 0.25, 2.75 });
}

TEST_CASE("field group exactly recovers linear fields on explicit axes in 2D",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, -0.5, 1.0, 3.5 }),
    ndtbl::Axis::from_coordinates({ 1.0, 1.75, 4.0, 6.0 }),
  };

  require_linear_recovery(axes, { 0.5, 3.25 });
}

TEST_CASE("field group exactly recovers linear fields on uniform axes in 4D",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 4> axes = {
    ndtbl::Axis::uniform(-1.0, 1.0, 3),
    ndtbl::Axis::uniform(0.0, 3.0, 4),
    ndtbl::Axis::uniform(10.0, 14.0, 3),
    ndtbl::Axis::uniform(-5.0, -1.0, 3),
  };

  require_linear_recovery(axes, { -0.25, 1.5, 11.25, -3.0 });
}

TEST_CASE("field group exactly recovers linear fields on explicit axes in 4D",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 4> axes = {
    ndtbl::Axis::from_coordinates({ -1.0, -0.25, 2.0 }),
    ndtbl::Axis::from_coordinates({ 0.0, 1.0, 1.5, 4.0 }),
    ndtbl::Axis::from_coordinates({ 3.0, 4.0, 7.0 }),
    ndtbl::Axis::from_coordinates({ -3.0, -2.5, 0.0, 2.0 }),
  };

  require_linear_recovery(axes, { 0.5, 1.25, 5.5, -1.0 });
}

TEST_CASE("field group returns exact vertex values on support points",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, 0.0, 1.5 }),
    ndtbl::Axis::from_coordinates({ 1.0, 3.5, 6.0 }),
  };

  require_linear_recovery(axes, { 0.0, 3.5 });
}

TEST_CASE("field group clamps uniform-axis queries outside the domain",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(-1.0, 2.0, 4),
    ndtbl::Axis::uniform(0.0, 6.0, 4),
  };

  require_clamped_linear_recovery(axes, { -1.0, 6.0 });
  require_clamped_linear_recovery(axes, { -3.0, 9.5 });
}

TEST_CASE("field group clamps explicit-axis queries outside the domain",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, -0.5, 1.0, 3.5 }),
    ndtbl::Axis::from_coordinates({ 1.0, 1.75, 4.0, 6.0 }),
  };

  require_clamped_linear_recovery(axes, { -2.0, 6.0 });
  require_clamped_linear_recovery(axes, { -5.0, 8.0 });
}

TEST_CASE("single-point axes reject out-of-domain coordinates in throw mode",
          "[axis][interpolation]")
{
  const ndtbl::Axis uniform = ndtbl::Axis::uniform(2.0, 10.0, 1);
  REQUIRE_NOTHROW(uniform.bracket(2.0, ndtbl::bounds_policy::throw_error));
  REQUIRE_THROWS_AS(uniform.bracket(2.1, ndtbl::bounds_policy::throw_error),
                    std::out_of_range);

  const ndtbl::Axis explicit_axis = ndtbl::Axis::from_coordinates({ 3.0 });
  REQUIRE_NOTHROW(
    explicit_axis.bracket(3.0, ndtbl::bounds_policy::throw_error));
  REQUIRE_THROWS_AS(
    explicit_axis.bracket(2.9, ndtbl::bounds_policy::throw_error),
    std::out_of_range);
}

TEST_CASE("axes reject non-finite query coordinates", "[axis][interpolation]")
{
  const ndtbl::Axis uniform = ndtbl::Axis::uniform(0.0, 1.0, 3);
  const ndtbl::Axis explicit_axis =
    ndtbl::Axis::from_coordinates({ 0.0, 0.5, 1.0 });
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();

  REQUIRE_THROWS_AS(uniform.bracket(nan), std::out_of_range);
  REQUIRE_THROWS_AS(uniform.bracket(nan, ndtbl::bounds_policy::throw_error),
                    std::out_of_range);
  REQUIRE_THROWS_AS(explicit_axis.bracket(nan), std::out_of_range);
  REQUIRE_THROWS_AS(
    explicit_axis.bracket(nan, ndtbl::bounds_policy::throw_error),
    std::out_of_range);

  REQUIRE_THROWS_AS(uniform.bracket(infinity), std::out_of_range);
  REQUIRE_THROWS_AS(explicit_axis.bracket(-infinity), std::out_of_range);
}

TEST_CASE("field group rejects uniform-axis queries outside the domain",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(-1.0, 2.0, 4),
    ndtbl::Axis::uniform(0.0, 6.0, 4),
  };
  const ndtbl::FieldGroup<2, double> group(
    ndtbl::Grid<2>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coefficients_a<2>(), 1.25, coefficients_b<2>(), -0.75));
  std::array<double, 2> values = { 0.0, 0.0 };

  REQUIRE_NOTHROW(group.evaluate_all_linear({ -1.0, 6.0 },
                                            ndtbl::bounds_policy::throw_error));
  REQUIRE_THROWS_AS(
    group.evaluate_all_linear({ -1.1, 3.0 }, ndtbl::bounds_policy::throw_error),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    group.evaluate_all_linear_into(
      { 0.0, 6.1 }, values.data(), ndtbl::bounds_policy::throw_error),
    std::out_of_range);
}

TEST_CASE("field group rejects explicit-axis queries outside the domain",
          "[field_group][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, -0.5, 1.0, 3.5 }),
    ndtbl::Axis::from_coordinates({ 1.0, 1.75, 4.0, 6.0 }),
  };
  const ndtbl::FieldGroup<2, double> group(
    ndtbl::Grid<2>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coefficients_a<2>(), 1.25, coefficients_b<2>(), -0.75));
  std::array<double, 2> values = { 0.0, 0.0 };

  REQUIRE_NOTHROW(group.evaluate_all_linear({ -2.0, 6.0 },
                                            ndtbl::bounds_policy::throw_error));
  REQUIRE_THROWS_AS(
    group.evaluate_all_linear({ -2.1, 3.0 }, ndtbl::bounds_policy::throw_error),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    group.evaluate_all_linear_into(
      { 0.0, 6.1 }, values.data(), ndtbl::bounds_policy::throw_error),
    std::out_of_range);
}

TEST_CASE("runtime field group forwards throw bounds policy",
          "[io][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(-1.0, 2.0, 4),
    ndtbl::Axis::uniform(0.0, 6.0, 4),
  };
  const ndtbl::FieldGroup<2, double> group(
    ndtbl::Grid<2>(axes),
    { "A", "B" },
    ndtbl_test::build_linear_payload(
      axes, coefficients_a<2>(), 1.25, coefficients_b<2>(), -0.75));
  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::RuntimeFieldGroup<2> loaded =
    ndtbl::read_runtime_field_group<2>(path);
  std::array<double, 2> values = { 0.0, 0.0 };
  REQUIRE_NOTHROW(
    loaded.evaluate_all_linear_into({ -2.0, 7.0 }, values.data()));
  REQUIRE_THROWS_AS(
    loaded.evaluate_all_linear_into(
      { -2.0, 7.0 }, values.data(), ndtbl::bounds_policy::throw_error),
    std::out_of_range);

  std::remove(path.c_str());
}

TEST_CASE("runtime field groups preserve exact interpolation on uniform axes",
          "[io][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(-1.0, 2.0, 4),
    ndtbl::Axis::uniform(0.5, 3.5, 5),
  };

  require_loaded_linear_recovery(axes, { 0.25, 2.75 });
}

TEST_CASE("runtime field groups preserve exact interpolation on explicit axes",
          "[io][interpolation]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, -0.5, 1.0, 3.5 }),
    ndtbl::Axis::from_coordinates({ 1.0, 1.75, 4.0, 6.0 }),
  };

  require_loaded_linear_recovery(axes, { 0.5, 3.25 });
}

TEST_CASE("field group cubic interpolation exactly recovers 1D cubic data",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(-2.0, 3.0, 6),
  };
  const ndtbl::Grid<1> grid(axes);
  const ndtbl::FieldGroup<1, double> group(
    grid,
    { "cubic" },
    build_single_field_payload(axes, [](const std::array<double, 1>& coords) {
      return cubic_1d(coords[0]);
    }));

  const ndtbl::CubicStencil<1> stencil = grid.prepare_cubic({ 0.4 });
  const std::vector<double> prepared_values = group.evaluate_all(stencil);
  const std::vector<double> direct_values = group.evaluate_all_cubic({ 0.4 });
  std::array<double, 1> into_values = { 0.0 };
  group.evaluate_all_cubic_into({ 0.4 }, into_values.data());

  REQUIRE(prepared_values[0] == Catch::Approx(cubic_1d(0.4)));
  REQUIRE(direct_values[0] == Catch::Approx(cubic_1d(0.4)));
  REQUIRE(into_values[0] == Catch::Approx(cubic_1d(0.4)));
}

TEST_CASE("field group cubic interpolation supports double output from floats",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 3.0, 4),
  };
  const ndtbl::FieldGroup<1, float> group(
    ndtbl::Grid<1>(axes),
    { "linear" },
    { 16777216.0f, 16777218.0f, 16777220.0f, 16777222.0f });

  const std::vector<double> values =
    group.evaluate_all_cubic_as<double>({ 0.5 });
  std::array<double, 1> into_values = { 0.0 };
  group.evaluate_all_cubic_into_as({ 0.5 }, into_values.data());

  REQUIRE(values[0] == Catch::Approx(16777217.0));
  REQUIRE(into_values[0] == Catch::Approx(16777217.0));
}

TEST_CASE("field group cubic interpolation exactly recovers explicit-axis data",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::from_coordinates({ -2.0, -0.75, 0.25, 1.5, 3.0 }),
    ndtbl::Axis::from_coordinates({ -1.0, -0.25, 0.5, 1.25, 2.5 }),
  };
  const ndtbl::Grid<2> grid(axes);
  const ndtbl::FieldGroup<2, double> group(
    grid,
    { "cubic" },
    build_single_field_payload(axes, [](const std::array<double, 2>& coords) {
      return cubic_2d(coords[0], coords[1]);
    }));
  const std::array<double, 2> query = { 0.6, 0.8 };

  const std::vector<double> values = group.evaluate_all_cubic(query);

  REQUIRE(values[0] == Catch::Approx(cubic_2d(query[0], query[1])));
}

TEST_CASE("field group cubic interpolation handles boundary windows",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 4.0, 5),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes),
    { "cubic" },
    build_single_field_payload(axes, [](const std::array<double, 1>& coords) {
      return cubic_1d(coords[0]);
    }));

  REQUIRE(group.evaluate_all_cubic({ 0.1 })[0] == Catch::Approx(cubic_1d(0.1)));
  REQUIRE(group.evaluate_all_cubic({ 3.9 })[0] == Catch::Approx(cubic_1d(3.9)));
  REQUIRE(group.evaluate_all_cubic({ -1.0 })[0] ==
          Catch::Approx(cubic_1d(0.0)));
  REQUIRE(group.evaluate_all_cubic({ 5.0 })[0] == Catch::Approx(cubic_1d(4.0)));
}

TEST_CASE("cubic interpolation requires four support points per axis",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 4),
    ndtbl::Axis::uniform(0.0, 1.0, 3),
  };
  const ndtbl::Grid<2> grid(axes);

  REQUIRE_THROWS_AS(grid.prepare_cubic({ 0.5, 0.5 }), std::invalid_argument);
}

TEST_CASE("cubic interpolation respects throw bounds policy",
          "[field_group][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::from_coordinates({ -1.0, 0.0, 1.0, 2.0 }),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes),
    { "cubic" },
    build_single_field_payload(axes, [](const std::array<double, 1>& coords) {
      return cubic_1d(coords[0]);
    }));
  std::array<double, 1> values = { 0.0 };

  REQUIRE_NOTHROW(
    group.evaluate_all_cubic({ -1.0 }, ndtbl::bounds_policy::throw_error));
  REQUIRE_THROWS_AS(
    group.evaluate_all_cubic({ -1.1 }, ndtbl::bounds_policy::throw_error),
    std::out_of_range);
  REQUIRE_THROWS_AS(
    group.evaluate_all_cubic_into(
      { 2.1 }, values.data(), ndtbl::bounds_policy::throw_error),
    std::out_of_range);
}

TEST_CASE("runtime field groups expose explicit cubic interpolation",
          "[io][interpolation][cubic]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(-2.0, 3.0, 6),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes),
    { "cubic" },
    build_single_field_payload(axes, [](const std::array<double, 1>& coords) {
      return cubic_1d(coords[0]);
    }));
  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::RuntimeFieldGroup<1> loaded =
    ndtbl::read_runtime_field_group<1>(path);
  const std::vector<double> values = loaded.evaluate_all_cubic({ 0.4 });

  REQUIRE(values[0] == Catch::Approx(cubic_1d(0.4)));

  std::remove(path.c_str());
}
