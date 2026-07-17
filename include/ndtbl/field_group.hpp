// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/detail/mapped_payload.hpp"
#include "ndtbl/detail/size_math.hpp"
#include "ndtbl/grid.hpp"
#include "ndtbl/payload.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ndtbl {

/**
 * @brief One grid plus one or more named fields stored in interleaved flat
 * memory.
 *
 * The storage layout is point-major in row-major grid order:
 * `point0.field0, point0.field1, ..., point1.field0, ...` where the last grid
 * axis varies fastest before stepping to the next field tuple. One prepared
 * interpolation stencil can accumulate all fields together.
 *
 * @tparam Dim Grid dimensionality of the group.
 * @tparam Stored Scalar payload type stored in the group.
 */
template<std::size_t Dim, class Stored>
class FieldGroup
{
public:
  /**
   * @brief Construct a field group on a shared grid.
   *
   * @param grid Shared interpolation grid.
   * @param field_names Logical names of the stored fields.
   * @param interleaved_values Flat point-major payload.
   */
  FieldGroup(const Grid<Dim>& grid,
             const std::vector<std::string>& field_names,
             const std::vector<Stored>& interleaved_values)
    : FieldGroup(grid, field_names, std::vector<Stored>(interleaved_values))
  {
  }

  /**
   * @brief Construct a field group from owned payload storage.
   *
   * @param grid Shared interpolation grid.
   * @param field_names Logical names of the stored fields.
   * @param interleaved_values Flat point-major payload whose ownership is moved
   *                           into the group.
   */
  FieldGroup(const Grid<Dim>& grid,
             const std::vector<std::string>& field_names,
             std::vector<Stored>&& interleaved_values)
    : grid_(grid)
    , field_names_(field_names)
  {
    adopt_owned_payload(std::move(interleaved_values));
    validate_payload_shape();
  }

  /**
   * @brief Construct a field group from externally managed payload storage.
   *
   * @param grid Shared interpolation grid.
   * @param field_names Logical names of the stored fields.
   * @param interleaved_values View over a contiguous point-major payload.
   * @param payload_owner Shared owner keeping the viewed payload alive.
   */
  FieldGroup(const Grid<Dim>& grid,
             const std::vector<std::string>& field_names,
             PayloadView<Stored> interleaved_values,
             std::shared_ptr<const std::uint8_t> payload_owner,
             bool payload_is_mmap = false)
    : grid_(grid)
    , field_names_(field_names)
    , interleaved_values_(std::move(interleaved_values))
    , payload_owner_(std::move(payload_owner))
    , payload_is_mmap_(payload_is_mmap)
  {
    if (interleaved_values_.size() != 0 && !payload_owner_) {
      throw std::invalid_argument("field group payload owner is empty");
    }
    validate_payload_shape();
  }

  /**
   * @brief Return the shared grid metadata.
   *
   * @return Shared interpolation grid.
   */
  const Grid<Dim>& grid() const { return grid_; }

  /**
   * @brief Return the number of stored fields.
   *
   * @return Number of named fields.
   */
  std::size_t field_count() const { return field_names_.size(); }

  /**
   * @brief Return all field names in storage order.
   *
   * @return Field names in payload order.
   */
  const std::vector<std::string>& field_names() const { return field_names_; }

  /**
   * @brief Return the number of support points in the shared grid.
   *
   * @return Number of grid support points.
   */
  std::size_t point_count() const { return grid_.point_count(); }

  /**
   * @brief Return a read-only view of the raw interleaved field payload.
   *
   * @return Point-major interleaved payload view.
   */
  PayloadView<Stored> interleaved_values() const { return interleaved_values_; }

  /**
   * @brief Return OS page residency information for mmap-backed payloads.
   *
   * The returned value has `available=false` unless this group was loaded
   * through ndtbl's mmap path and mmap diagnostics were enabled at compile
   * time.
   *
   * @return Page residency diagnostic information.
   */
  residency_info payload_residency() const
  {
    if (!payload_is_mmap_) {
      return detail::unavailable_residency();
    }
    return detail::query_residency(interleaved_values_.byte_data(),
                                   interleaved_values_.byte_size());
  }

  /**
   * @brief Resolve a field name to its local field index.
   *
   * @param name Field name to look up.
   * @return Zero-based field index in storage order.
   */
  std::size_t field_index(const std::string& name) const
  {
    const auto found =
      std::find(field_names_.begin(), field_names_.end(), name);
    if (found == field_names_.end()) {
      throw std::out_of_range("field not found in ndtbl field group");
    }
    return static_cast<std::size_t>(std::distance(field_names_.begin(), found));
  }

  /**
   * @brief Evaluate all fields using a previously prepared interpolation
   * stencil.
   *
   * @tparam Stencil Fixed-size interpolation stencil type.
   * @param stencil Prepared stencil to reuse across fields.
   * @return Interpolated field values in storage order.
   * @see Grid::prepare_linear
   * @see Grid::prepare_cubic
   * @see evaluate_all_linear(const std::array<double, Dim>&)
   */
  template<class Stencil>
  std::vector<Stored> evaluate_all(const Stencil& stencil) const
  {
    return evaluate_all_as<Stored>(stencil);
  }

  /**
   * @brief Evaluate all fields using a previously prepared interpolation
   * stencil and caller-selected output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @tparam Stencil Fixed-size interpolation stencil type.
   * @param stencil Prepared stencil to reuse across fields.
   * @return Interpolated field values in storage order.
   * @see evaluate_all(const Stencil&)
   */
  template<class Output, class Stencil>
  std::vector<Output> evaluate_all_as(const Stencil& stencil) const
  {
    static_assert(std::is_floating_point<Output>::value,
                  "FieldGroup output type must be floating point");

    std::vector<Output> results(field_count(), Output(0));
    evaluate_all_into_as<Output>(stencil, results.data());
    return results;
  }

  /**
   * @brief Evaluate all fields using a previously prepared interpolation
   * stencil into caller-provided storage.
   *
   * @tparam Stencil Fixed-size interpolation stencil type.
   * @param stencil Prepared stencil to reuse across fields.
   * @param results Output buffer with space for `field_count()` values.
   * @see evaluate_all(const Stencil&)
   */
  template<class Stencil>
  void evaluate_all_into(const Stencil& stencil, Stored* results) const
  {
    evaluate_all_into_as(stencil, results);
  }

  /**
   * @brief Evaluate all fields using a previously prepared interpolation
   * stencil into caller-provided storage with caller-selected output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @tparam Stencil Fixed-size interpolation stencil type.
   * @param stencil Prepared stencil to reuse across fields.
   * @param results Output buffer with space for `field_count()` values.
   * @see evaluate_all_into(const Stencil&, Stored*)
   */
  template<class Output, class Stencil>
  void evaluate_all_into_as(const Stencil& stencil, Output* results) const
  {
    static_assert(std::is_floating_point<Output>::value,
                  "FieldGroup output type must be floating point");

    const std::size_t fields = field_count();
    const Stored* const typed_values = interleaved_values_.typed_data();

    std::fill(results, results + fields, Output(0));
    if (typed_values != nullptr) {
      evaluate_all_into_typed(stencil, typed_values, fields, results);
      return;
    }

    evaluate_all_into_bytes(stencil, fields, results);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using
   * multilinear interpolation.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @return Interpolated field values in storage order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all(const Stencil&)
   */
  std::vector<Stored> evaluate_all_linear(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    return evaluate_all_linear_as<Stored>(coordinates, policy);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using
   * multilinear interpolation and caller-selected output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @param coordinates Query coordinates in grid axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Interpolated field values in storage order.
   * @see evaluate_all_linear(const std::array<double, Dim>&, bounds_policy)
   */
  template<class Output>
  std::vector<Output> evaluate_all_linear_as(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    return evaluate_all_as<Output>(grid_.prepare_linear(coordinates, policy));
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using
   * multilinear interpolation into caller-provided storage.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param results Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_into(const Stencil&, Stored*)
   */
  void evaluate_all_linear_into(
    const std::array<double, Dim>& coordinates,
    Stored* results,
    bounds_policy policy = bounds_policy::clamp) const
  {
    evaluate_all_linear_into_as(coordinates, results, policy);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using
   * multilinear interpolation into caller-provided storage with
   * caller-selected output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @param coordinates Query coordinates in grid axis order.
   * @param results Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_linear_into(const std::array<double, Dim>&, Stored*,
   *                               bounds_policy)
   */
  template<class Output>
  void evaluate_all_linear_into_as(
    const std::array<double, Dim>& coordinates,
    Output* results,
    bounds_policy policy = bounds_policy::clamp) const
  {
    evaluate_all_into_as(grid_.prepare_linear(coordinates, policy), results);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using local
   * cubic interpolation.
   *
   * Cubic interpolation uses four support points per axis and is therefore
   * intended for experiments where the additional cost and possible overshoot
   * are acceptable.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Cubically interpolated field values in storage order.
   * @see Grid::prepare_cubic
   */
  std::vector<Stored> evaluate_all_cubic(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    return evaluate_all_cubic_as<Stored>(coordinates, policy);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using local
   * cubic interpolation and caller-selected output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @param coordinates Query coordinates in grid axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Cubically interpolated field values in storage order.
   * @see evaluate_all_cubic(const std::array<double, Dim>&, bounds_policy)
   */
  template<class Output>
  std::vector<Output> evaluate_all_cubic_as(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    return evaluate_all_as<Output>(grid_.prepare_cubic(coordinates, policy));
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using local
   * cubic interpolation into caller-provided storage.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param results Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_cubic
   */
  void evaluate_all_cubic_into(
    const std::array<double, Dim>& coordinates,
    Stored* results,
    bounds_policy policy = bounds_policy::clamp) const
  {
    evaluate_all_cubic_into_as(coordinates, results, policy);
  }

  /**
   * @brief Evaluate all fields directly from query coordinates using local
   * cubic interpolation into caller-provided storage with caller-selected
   * output precision.
   *
   * @tparam Output Floating-point type produced by interpolation.
   * @param coordinates Query coordinates in grid axis order.
   * @param results Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_cubic_into(const std::array<double, Dim>&, Stored*,
   *                              bounds_policy)
   */
  template<class Output>
  void evaluate_all_cubic_into_as(
    const std::array<double, Dim>& coordinates,
    Output* results,
    bounds_policy policy = bounds_policy::clamp) const
  {
    evaluate_all_into_as(grid_.prepare_cubic(coordinates, policy), results);
  }

private:
  template<class Stencil, class Output>
  void evaluate_all_into_typed(const Stencil& stencil,
                               const Stored* values,
                               std::size_t fields,
                               Output* results) const
  {
    for (std::size_t point = 0; point < Stencil::points; ++point) {
      const auto weight = static_cast<Output>(stencil.weight(point));
      const std::size_t base = stencil.point_index(point) * fields;
      for (std::size_t field = 0; field < fields; ++field) {
        const Stored value = values[base + field];
        results[field] += weight * static_cast<Output>(value);
      }
    }
  }

  template<class Stencil, class Output>
  void evaluate_all_into_bytes(const Stencil& stencil,
                               std::size_t fields,
                               Output* results) const
  {
    for (std::size_t point = 0; point < Stencil::points; ++point) {
      const auto weight = static_cast<Output>(stencil.weight(point));
      const std::size_t base = stencil.point_index(point) * fields;
      for (std::size_t field = 0; field < fields; ++field) {
        const Stored value = interleaved_values_.unchecked(base + field);
        results[field] += weight * static_cast<Output>(value);
      }
    }
  }

  void adopt_owned_payload(std::vector<Stored>&& interleaved_values)
  {
    const auto storage =
      std::make_shared<std::vector<Stored>>(std::move(interleaved_values));
    const Stored* const data = storage->empty() ? nullptr : storage->data();
    interleaved_values_ = PayloadView<Stored>(data, storage->size());
    payload_owner_ = std::shared_ptr<const std::uint8_t>(
      storage,
      data == nullptr ? nullptr : reinterpret_cast<const std::uint8_t*>(data));
  }

  void validate_payload_shape() const
  {
    if (field_names_.empty()) {
      throw std::invalid_argument(
        "field group must contain at least one field");
    }

    const std::size_t expected_size = detail::checked_multiply_size(
      grid_.point_count(), field_names_.size(), "field payload value count");
    if (interleaved_values_.size() != expected_size) {
      throw std::invalid_argument(
        "field payload size does not match grid and field count");
    }
  }

private:
  Grid<Dim> grid_;
  std::vector<std::string> field_names_;
  PayloadView<Stored> interleaved_values_;
  std::shared_ptr<const std::uint8_t> payload_owner_;
  bool payload_is_mmap_ = false;
};

} // namespace ndtbl
