// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/axis.hpp"
#include "ndtbl/detail/size_math.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ndtbl {

namespace detail {

/**
 * @brief Compute an integer power for `std::size_t` values.
 *
 * Computes `base` raised to the power `exponent` by repeated multiplication.
 * Intended for small compile-time constants such as stencil sizes.
 *
 * @param base Base value.
 * @param exponent Non-negative exponent.
 * @return `base` raised to `exponent`.
 */
constexpr std::size_t
pow_size(std::size_t base, std::size_t exponent)
{
  std::size_t result = 1;
  for (std::size_t index = 0; index < exponent; ++index) {
    result *= base;
  }
  return result;
}

} // namespace detail

/**
 * @brief Fixed-size tensor-product interpolation stencil for one query point.
 *
 * A stencil stores linearized point indices and corresponding weights for one
 * interpolation point so that multiple fields on the same grid can reuse the
 * same lookup work.
 *
 * @tparam Dim Number of dimensions.
 * @tparam PointsPerAxis Number of interpolation points along each axis.
 */
template<std::size_t Dim, std::size_t PointsPerAxis>
class TensorStencil
{
  static_assert(Dim > 0, "ndtbl grids must have at least one dimension");
  static_assert(
    PointsPerAxis > 0,
    "ndtbl tensor stencils must contain at least one point per axis");

public:
  /// Number of dimensions.
  static constexpr std::size_t dimensions = Dim;
  /// Number of stencil points along each axis.
  static constexpr std::size_t points_per_axis = PointsPerAxis;
  /// Total number of interpolation points in the tensor-product stencil.
  static constexpr std::size_t points = detail::pow_size(PointsPerAxis, Dim);

  /**
   * @brief Return the flat storage indices of all interpolation points.
   *
   * @return Flat point indices for all interpolation points.
   */
  const std::array<std::size_t, points>& point_indices() const
  {
    return point_indices_;
  }

  /**
   * @brief Return the flat storage index by stencil point index.
   *
   * @param index Stencil point index.
   * @return Flat point index of the selected stencil point.
   */
  std::size_t point_index(std::size_t index) const
  {
    return point_indices_[index];
  }

  /**
   * @brief Return the interpolation weights of all stencil points.
   *
   * @return Interpolation weights for all stencil points.
   */
  const std::array<double, points>& weights() const { return weights_; }

  /**
   * @brief Return the interpolation weight by stencil point index.
   *
   * @param index Stencil point index.
   * @return Interpolation weight for the selected stencil point.
   */
  double weight(std::size_t index) const { return weights_[index]; }

private:
  template<std::size_t>
  friend class Grid;

  TensorStencil() = default;

  std::array<std::size_t, points> point_indices_;
  std::array<double, points> weights_;
};

/**
 * @brief Multilinear interpolation stencil with two points per axis.
 */
template<std::size_t Dim>
using LinearStencil = TensorStencil<Dim, 2>;

/**
 * @brief Local tensor-product cubic Lagrange interpolation stencil with four
 * points per axis.
 */
template<std::size_t Dim>
using CubicStencil = TensorStencil<Dim, 4>;

/**
 * @brief N-dimensional grid metadata with stride and query preparation logic.

 * A grid is defined by one axis descriptor per dimension. It provides the
 * logic to prepare interpolation stencils for query points and to validate
 * compatibility with field groups.
 *
 * @tparam Dim Number of dimensions.
 */
template<std::size_t Dim>
class Grid
{
  static_assert(Dim > 0, "ndtbl grids must have at least one dimension");

public:
  /// Number of dimensions.
  static constexpr std::size_t dimensions = Dim;

  /**
   * @brief Construct an empty grid descriptor.
   *
   * The default-constructed grid has zero points and is mainly useful as a
   * placeholder before assigning a fully constructed grid.
   */
  Grid()
  {
    point_count_ = 0;
    extents_.fill(0);
    strides_.fill(0);
  }

  /**
   * @brief Construct a grid from one axis descriptor per dimension.
   *
   * @param axes Axis descriptors in dimension order.
   */
  explicit Grid(const std::array<Axis, Dim>& axes)
    : axes_(axes)
  {
    point_count_ = 1;
    strides_[Dim - 1] = 1;
    for (std::size_t axis = Dim; axis-- > 0;) {
      extents_[axis] = axes_[axis].size();
      point_count_ = detail::checked_multiply_size(
        point_count_, extents_[axis], "point count");
      if (axis + 1 < Dim) {
        strides_[axis] = detail::checked_multiply_size(
          strides_[axis + 1], extents_[axis + 1], "grid stride");
      }
    }
  }

  /**
   * @brief Return all axis descriptors.
   *
   * @return Axis descriptors in dimension order.
   */
  const std::array<Axis, Dim>& axes() const { return axes_; }

  /**
   * @brief Return one axis descriptor by dimension index.
   *
   * @param index Dimension index.
   * @return Axis descriptor for the selected dimension.
   */
  const Axis& axis(std::size_t index) const { return axes_[index]; }

  /**
   * @brief Return the extent of each dimension.
   *
   * @return Grid extents in dimension order.
   */
  const std::array<std::size_t, Dim>& extents() const { return extents_; }

  /**
   * @brief Return the extent by dimension index.
   *
   * @param index Dimension index.
   * @return Number of support points along the selected dimension.
   */
  std::size_t extent(std::size_t index) const { return extents_[index]; }

  /**
   * @brief Return the flat-memory strides of each dimension.
   *
   * @return Flat-memory strides in dimension order.
   */
  const std::array<std::size_t, Dim>& strides() const { return strides_; }

  /**
   * @brief Return the flat-memory stride by dimension index.
   *
   * @param index Dimension index.
   * @return Flat-memory stride for the selected dimension.
   */
  std::size_t stride(std::size_t index) const { return strides_[index]; }

  /**
   * @brief Return the total number of support points in the grid.
   *
   * @return Total point count across all dimensions.
   */
  std::size_t point_count() const { return point_count_; }

  /**
   * @brief Precompute the multilinear interpolation stencil for one query
   * point.
   *
   * This is the key operation to reuse when several fields share a grid.
   *
   * @param coordinates Query coordinates in axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Linear stencil containing point indices and weights.
   */
  LinearStencil<Dim> prepare_linear(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    std::array<std::size_t, Dim> lower_indices;
    std::array<std::size_t, Dim> upper_indices;
    std::array<double, Dim> upper_weights;

    for (std::size_t axis = 0; axis < Dim; ++axis) {
      const std::pair<std::size_t, double> bracket =
        axes_[axis].bracket(coordinates[axis], policy);
      lower_indices[axis] = bracket.first;
      upper_indices[axis] =
        std::min(lower_indices[axis] + 1, extents_[axis] - 1);
      upper_weights[axis] = bracket.second;
    }

    LinearStencil<Dim> prepared;
    // Enumerate all corners of the bracketing cell. Bit `axis` selects the
    // lower (0) or upper (1) endpoint on that axis.
    for (std::size_t corner_mask = 0; corner_mask < LinearStencil<Dim>::points;
         ++corner_mask) {
      double weight = 1.0;
      std::size_t linear_index = 0;

      for (std::size_t axis = 0; axis < Dim; ++axis) {
        const bool use_upper = (corner_mask & (std::size_t(1) << axis)) != 0;
        const std::size_t index =
          use_upper ? upper_indices[axis] : lower_indices[axis];
        const double axis_weight =
          use_upper ? upper_weights[axis] : (1.0 - upper_weights[axis]);
        linear_index += index * strides_[axis];
        weight *= axis_weight;
      }

      prepared.point_indices_[corner_mask] = linear_index;
      prepared.weights_[corner_mask] = weight;
    }

    return prepared;
  }

  /**
   * @brief Precompute a local tensor-product cubic Lagrange interpolation
   * stencil for one query point.
   *
   * This method builds a local interpolation stencil using four support points
   * per axis. Along each axis, the one-dimensional weights are the cubic
   * Lagrange basis weights for the selected four axis coordinates:
   * \f[
   *   L_i(x) = prod_{j != i} (x - x_j) / (x_i - x_j)
   * \f]
   * The multidimensional stencil is formed as the tensor product of these
   * one-dimensional Lagrange weights. Consequently, the stencil contains
   * `4^Dim` table values.
   *
   * The basis weights are computed from the actual axis coordinates, so
   * non-uniform axes are supported. Near domain boundaries, the four-point
   * support window is shifted/clamped so that it remains inside the table. This
   * makes the cubic stencil one-sided near boundaries.
   *
   * Cubic interpolation is intended as an experimental higher-order path. The
   * linear path remains the recommended default for performance-critical
   * high-dimensional table lookup.
   *
   * @param coordinates Query coordinates in axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Cubic stencil containing flattened table point indices and
   * tensor-product weights.
   *
   * @throws std::invalid_argument if any axis contains fewer than four points.
   */
  CubicStencil<Dim> prepare_cubic(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    std::array<std::array<std::size_t, 4>, Dim> axis_indices;
    std::array<std::array<double, 4>, Dim> axis_weights;

    for (std::size_t axis = 0; axis < Dim; ++axis) {
      if (extents_[axis] < 4) {
        throw std::invalid_argument(
          "cubic interpolation requires at least four points per axis");
      }

      const std::pair<std::size_t, double> bracket =
        axes_[axis].bracket(coordinates[axis], policy);
      const std::size_t first =
        std::min(bracket.first > 0 ? bracket.first - 1 : std::size_t(0),
                 extents_[axis] - 4);
      const double coordinate = std::max(
        axes_[axis].min(), std::min(axes_[axis].max(), coordinates[axis]));

      for (std::size_t point = 0; point < 4; ++point) {
        axis_indices[axis][point] = first + point;
      }
      cubic_weights(axis, coordinate, axis_indices[axis], axis_weights[axis]);
    }

    CubicStencil<Dim> prepared;
    for (std::size_t stencil_point = 0;
         stencil_point < CubicStencil<Dim>::points;
         ++stencil_point) {
      double weight = 1.0;
      std::size_t linear_index = 0;
      std::size_t remainder = stencil_point;

      for (std::size_t axis = 0; axis < Dim; ++axis) {
        const std::size_t axis_point = remainder % 4;
        remainder /= 4;
        linear_index += axis_indices[axis][axis_point] * strides_[axis];
        weight *= axis_weights[axis][axis_point];
      }

      prepared.point_indices_[stencil_point] = linear_index;
      prepared.weights_[stencil_point] = weight;
    }

    return prepared;
  }

private:
  /**
   * @brief Compute one-dimensional cubic Lagrange basis weights for one axis.
   *
   * Given four support point indices on the selected axis and a query
   * coordinate, this function computes the four Lagrange basis weights
   *
   *   w_i = prod_{j != i} (x - x_j) / (x_i - x_j)
   *
   * using the actual axis coordinates `x_i`. The resulting weights define the
   * unique cubic polynomial, restricted to this local four-point stencil, that
   * interpolates the four support values exactly.
   *
   * @param axis Axis for which the weights are computed.
   * @param coordinate Query coordinate on that axis.
   * @param indices Four support point indices on the selected axis.
   * @param weights Output array receiving the four Lagrange basis weights.
   */
  void cubic_weights(std::size_t axis,
                     double coordinate,
                     const std::array<std::size_t, 4>& indices,
                     std::array<double, 4>& weights) const
  {
    for (std::size_t point = 0; point < 4; ++point) {
      const double point_coordinate = axes_[axis].coordinate(indices[point]);
      double weight = 1.0;
      for (std::size_t other = 0; other < 4; ++other) {
        if (other == point) {
          continue;
        }
        const double other_coordinate = axes_[axis].coordinate(indices[other]);
        weight *= (coordinate - other_coordinate) /
                  (point_coordinate - other_coordinate);
      }
      weights[point] = weight;
    }
  }

  std::size_t point_count_;
  std::array<Axis, Dim> axes_;
  std::array<std::size_t, Dim> extents_;
  std::array<std::size_t, Dim> strides_;
};

} // namespace ndtbl
