// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/field_group.hpp"
#include "ndtbl/types.hpp"

#include <array>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ndtbl {

template<class Stored, std::size_t Dim>
void
write_group_stream(std::ostream& os, const FieldGroup<Dim, Stored>& group);

/**
 * @brief Runtime-erased wrapper around a typed `FieldGroup<Dim, Stored>`.
 *
 * The dimensionality remains part of the type. Only the stored scalar payload
 * type is selected from file metadata at runtime.
 *
 * @tparam Dim Grid dimensionality of the group.
 * @tparam Output Floating-point type produced by interpolation calls.
 */
template<std::size_t Dim, class Output = double>
class RuntimeFieldGroup
{
  static_assert(std::is_floating_point<Output>::value,
                "RuntimeFieldGroup output type must be floating point");

public:
  /**
   * @brief Construct an empty runtime-erased group handle.
   */
  RuntimeFieldGroup() {}

  /**
   * @brief Construct a runtime-erased wrapper from a typed field group.
   *
   * @tparam Stored Scalar payload type stored in the source group.
   * @param group Typed field group to wrap.
   * @see FieldGroup
   */
  template<class Stored>
  explicit RuntimeFieldGroup(const FieldGroup<Dim, Stored>& group)
    : impl_(std::make_shared<Model<Stored>>(group))
  {
  }

  /**
   * @brief Check whether this wrapper currently holds a group instance.
   *
   * @return `true` if no group is loaded, otherwise `false`.
   */
  bool empty() const { return !impl_; }

  /**
   * @brief Return the number of fields stored per grid point.
   *
   * @return Number of named fields in the wrapped group.
   */
  std::size_t field_count() const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->field_count();
  }

  /**
   * @brief Return the scalar payload type of the wrapped group.
   *
   * @return Runtime payload type tag.
   */
  scalar_type value_type() const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->value_type();
  }

  /**
   * @brief Return field names in storage order.
   *
   * @return Copy of the field name list.
   */
  std::vector<std::string> field_names() const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->field_names();
  }

  /**
   * @brief Return the axis descriptors of the wrapped grid.
   *
   * @return One axis descriptor per dimension.
   */
  std::array<Axis, Dim> axes() const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->axes();
  }

  /**
   * @brief Resolve a field name to its storage index.
   *
   * @param field_name Field name to look up.
   * @return Zero-based field index in storage order.
   */
  std::size_t field_index(const std::string& field_name) const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->field_index(field_name);
  }

  /**
   * @brief Evaluate all stored fields at one coordinate tuple using multilinear
   * interpolation.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Interpolated field values converted to `Output`.
   * @see evaluate_all_linear_into
   */
  std::vector<Output> evaluate_all_linear(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    std::vector<Output> values(field_count(), Output(0));
    evaluate_all_linear_into(coordinates, values.data(), policy);
    return values;
  }

  /**
   * @brief Evaluate all stored fields using multilinear interpolation into
   * caller-provided output storage.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param values Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_linear
   */
  void evaluate_all_linear_into(
    const std::array<double, Dim>& coordinates,
    Output* values,
    bounds_policy policy = bounds_policy::clamp) const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    impl_->evaluate_all_linear_into(coordinates, values, policy);
  }

  /**
   * @brief Evaluate all stored fields at one coordinate tuple using local cubic
   * interpolation.
   *
   * Cubic interpolation is an explicit opt-in because it uses `4^Dim` table
   * values and can overshoot.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @return Cubically interpolated field values converted to `Output`.
   * @see evaluate_all_cubic_into
   */
  std::vector<Output> evaluate_all_cubic(
    const std::array<double, Dim>& coordinates,
    bounds_policy policy = bounds_policy::clamp) const
  {
    std::vector<Output> values(field_count(), Output(0));
    evaluate_all_cubic_into(coordinates, values.data(), policy);
    return values;
  }

  /**
   * @brief Evaluate all stored fields using local cubic interpolation into
   * caller-provided output storage.
   *
   * @param coordinates Query coordinates in grid axis order.
   * @param values Output buffer with space for `field_count()` values.
   * @param policy Bounds handling behavior for out-of-domain coordinates.
   * @see evaluate_all_cubic
   */
  void evaluate_all_cubic_into(
    const std::array<double, Dim>& coordinates,
    Output* values,
    bounds_policy policy = bounds_policy::clamp) const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    impl_->evaluate_all_cubic_into(coordinates, values, policy);
  }

  /**
   * @brief Return OS page residency information for mmap-backed payloads.
   *
   * @return Page residency diagnostic information.
   */
  residency_info payload_residency() const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    return impl_->payload_residency();
  }

  /**
   * @brief Write the wrapped group to an already opened binary stream.
   *
   * @param os Destination stream in binary mode.
   * @see write_group_stream
   */
  void write(std::ostream& os) const
  {
    if (!impl_) {
      throw std::runtime_error("ndtbl field group is empty");
    }
    impl_->write(os);
  }

private:
  struct Concept
  {
    virtual ~Concept() {}
    virtual std::size_t field_count() const = 0;
    virtual scalar_type value_type() const = 0;
    virtual std::vector<std::string> field_names() const = 0;
    virtual std::array<Axis, Dim> axes() const = 0;
    virtual std::size_t field_index(const std::string& field_name) const = 0;
    virtual void evaluate_all_linear_into(
      const std::array<double, Dim>& coordinates,
      Output* values,
      bounds_policy policy) const = 0;
    virtual void evaluate_all_cubic_into(
      const std::array<double, Dim>& coordinates,
      Output* values,
      bounds_policy policy) const = 0;
    virtual residency_info payload_residency() const = 0;
    virtual void write(std::ostream& os) const = 0;
  };

  template<class Stored>
  struct Model : Concept
  {
    explicit Model(const FieldGroup<Dim, Stored>& group)
      : group_(group)
    {
    }

    std::size_t field_count() const override { return group_.field_count(); }

    scalar_type value_type() const override { return scalar_type_of<Stored>(); }

    std::vector<std::string> field_names() const override
    {
      return group_.field_names();
    }

    std::array<Axis, Dim> axes() const override { return group_.grid().axes(); }

    std::size_t field_index(const std::string& field_name) const override
    {
      return group_.field_index(field_name);
    }

    void evaluate_all_linear_into(const std::array<double, Dim>& coordinates,
                                  Output* values,
                                  bounds_policy policy) const override
    {
      group_.template evaluate_all_linear_into_as<Output>(
        coordinates, values, policy);
    }

    void evaluate_all_cubic_into(const std::array<double, Dim>& coordinates,
                                 Output* values,
                                 bounds_policy policy) const override
    {
      group_.template evaluate_all_cubic_into_as<Output>(
        coordinates, values, policy);
    }

    residency_info payload_residency() const override
    {
      return group_.payload_residency();
    }

    void write(std::ostream& os) const override
    {
      write_group_stream(os, group_);
    }

    FieldGroup<Dim, Stored> group_;
  };

  std::shared_ptr<const Concept> impl_;
};

} // namespace ndtbl
