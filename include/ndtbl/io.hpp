// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/detail/binary_io.hpp"
#include "ndtbl/detail/mapped_payload.hpp"
#include "ndtbl/exceptions.hpp"
#include "ndtbl/runtime_field_group.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace ndtbl {

/**
 * @brief Write a typed field group to an already opened binary stream.
 *
 * @tparam Stored Scalar payload type stored in the group.
 * @tparam Dim Grid dimensionality of the group.
 * @param os Destination stream in binary mode.
 * @param group Field group to serialize.
 * @see write_group(const std::string&, const FieldGroup<Dim, Stored>&)
 */
template<class Stored, std::size_t Dim>
inline void
write_group_stream(std::ostream& os, const FieldGroup<Dim, Stored>& group)
{
  detail::write_group_stream_impl(os, group);
}

/**
 * @brief Write a raw ndtbl payload with explicit metadata to a binary stream.
 *
 * This overload is useful when the caller already has metadata and an
 * interleaved point-major payload in row-major axis order, but not a typed
 * `FieldGroup`.
 *
 * @tparam Stored Scalar payload type stored in the payload vector.
 * @param os Destination stream in binary mode.
 * @param metadata Group metadata to encode into the file header.
 * @param interleaved_values Point-major field payload to serialize in row-major
 *                           axis order.
 * @see write_group_stream(std::ostream&, const FieldGroup<Dim, Stored>&)
 */
template<class Stored>
inline void
write_group_stream(std::ostream& os,
                   const GroupMetadata& metadata,
                   const std::vector<Stored>& interleaved_values)
{
  detail::write_group_stream_impl(os, metadata, interleaved_values);
}

/**
 * @brief Write a typed field group to a binary ndtbl file.
 *
 * @tparam Stored Scalar payload type stored in the group.
 * @tparam Dim Grid dimensionality of the group.
 * @param path Output file path.
 * @param group Field group to serialize.
 * @see write_group_stream(std::ostream&, const FieldGroup<Dim, Stored>&)
 */
template<class Stored, std::size_t Dim>
inline void
write_group(const std::string& path, const FieldGroup<Dim, Stored>& group)
{
  std::ofstream os(path.c_str(), std::ios::binary);
  if (!os.is_open()) {
    throw IOError("failed to open ndtbl output file: " + path);
  }
  write_group_stream(os, group);
}

/**
 * @brief Write a raw ndtbl payload with explicit metadata to a file.
 *
 * @tparam Stored Scalar payload type stored in the payload vector.
 * @param path Output file path.
 * @param metadata Group metadata to encode into the file header.
 * @param interleaved_values Point-major field payload to serialize in row-major
 *                           axis order.
 * @see write_group_stream(std::ostream&, const GroupMetadata&,
 *                         const std::vector<Stored>&)
 */
template<class Stored>
inline void
write_group(const std::string& path,
            const GroupMetadata& metadata,
            const std::vector<Stored>& interleaved_values)
{
  std::ofstream os(path.c_str(), std::ios::binary);
  if (!os.is_open()) {
    throw IOError("failed to open ndtbl output file: " + path);
  }
  write_group_stream(os, metadata, interleaved_values);
}

/**
 * @brief Write a runtime-erased field group to a binary ndtbl file.
 *
 * @tparam Dim Grid dimensionality of the group.
 * @tparam Output Scalar output type used by runtime-erased interpolation.
 * @param path Output file path.
 * @param group Runtime-erased field group to serialize.
 * @see RuntimeFieldGroup
 */
template<std::size_t Dim, class Output>
inline void
write_group(const std::string& path,
            const RuntimeFieldGroup<Dim, Output>& group)
{
  std::ofstream os(path.c_str(), std::ios::binary);
  if (!os.is_open()) {
    throw IOError("failed to open ndtbl output file: " + path);
  }
  group.write(os);
}

/**
 * @brief Read only the metadata header of an ndtbl file.
 *
 * This function validates the file header and axis descriptors without reading
 * the field payload.
 *
 * @param path Input file path.
 * @return Parsed metadata describing the stored group.
 * @see read_field_group
 * @see read_runtime_field_group
 */
inline GroupMetadata
read_group_metadata(const std::string& path)
{
  std::ifstream is(path.c_str(), std::ios::binary);
  if (!is.is_open()) {
    throw IOError("failed to open ndtbl input file: " + path);
  }

  return detail::read_group_metadata_impl(is);
}

/**
 * @brief Read an ndtbl file into a typed field group.
 *
 * The file dimension and scalar payload type must match the compile-time
 * `Dim` and `Stored` arguments.
 *
 * @tparam Dim Expected grid dimensionality of the file.
 * @tparam Stored Expected scalar payload type stored in the file.
 * @param path Input file path.
 * @return Typed field group with payload storage of type `Stored`.
 * @see read_group_metadata
 * @see FieldGroup
 */
template<std::size_t Dim, class Stored>
inline FieldGroup<Dim, Stored>
read_field_group(const std::string& path)
{
  std::ifstream is(path.c_str(), std::ios::binary);
  if (!is.is_open()) {
    throw IOError("failed to open ndtbl input file: " + path);
  }

  const detail::parsed_group_layout layout = detail::read_group_layout_impl(is);
  const GroupMetadata& metadata = layout.metadata;
  if (metadata.dimension != Dim) {
    throw FormatError("ndtbl file dimension does not match typed loader");
  }
  if (metadata.value_type != scalar_type_of<Stored>()) {
    throw FormatError("ndtbl file scalar type does not match typed loader");
  }

  const std::array<Axis, Dim> axes = detail::fixed_axes<Dim>(metadata.axes);
  const Grid<Dim> grid(axes);
#if NDTBL_ENABLE_MMAP
  const std::shared_ptr<const std::uint8_t> payload_owner =
    detail::map_payload_bytes(path, layout.payload_offset, layout.payload_size);
  return FieldGroup<Dim, Stored>(
    grid,
    metadata.field_names,
    PayloadView<Stored>(payload_owner.get(), layout.value_count),
    payload_owner,
    true);
#else
  // Keep this non-const so the payload buffer can be moved into the read-only
  // FieldGroup storage instead of copied during load.
  std::vector<Stored> values =
    detail::read_payload<Stored>(is, layout.value_count);
  return FieldGroup<Dim, Stored>(grid, metadata.field_names, std::move(values));
#endif
}

/**
 * @brief Read an ndtbl file into a runtime-erased field group.
 *
 * The file dimension must match the compile-time `Dim` argument. The scalar
 * payload type is selected from file metadata at runtime.
 *
 * @tparam Dim Expected grid dimensionality of the file.
 * @tparam Output Output type used by runtime-erased interpolation.
 * @param path Input file path.
 * @return Runtime-erased field group with either float or double payload
 *         storage and `Output` interpolation results.
 * @see read_field_group
 * @see read_group_metadata
 * @see RuntimeFieldGroup
 */
template<std::size_t Dim, class Output = double>
inline RuntimeFieldGroup<Dim, Output>
read_runtime_field_group(const std::string& path)
{
  std::ifstream is(path.c_str(), std::ios::binary);
  if (!is.is_open()) {
    throw IOError("failed to open ndtbl input file: " + path);
  }

  const detail::parsed_group_layout layout = detail::read_group_layout_impl(is);
  const GroupMetadata& metadata = layout.metadata;
  if (metadata.dimension != Dim) {
    throw FormatError("ndtbl file dimension does not match typed loader");
  }

  const std::array<Axis, Dim> axes = detail::fixed_axes<Dim>(metadata.axes);
  const Grid<Dim> grid(axes);
  if (metadata.value_type == scalar_type::float32) {
#if NDTBL_ENABLE_MMAP
    const std::shared_ptr<const std::uint8_t> payload_owner =
      detail::map_payload_bytes(
        path, layout.payload_offset, layout.payload_size);
    return RuntimeFieldGroup<Dim, Output>(FieldGroup<Dim, float>(
      grid,
      metadata.field_names,
      PayloadView<float>(payload_owner.get(), layout.value_count),
      payload_owner,
      true));
#else
    // Keep this non-const so the payload buffer can be moved into the
    // read-only FieldGroup storage instead of copied during load.
    std::vector<float> values =
      detail::read_payload<float>(is, layout.value_count);
    return RuntimeFieldGroup<Dim, Output>(
      FieldGroup<Dim, float>(grid, metadata.field_names, std::move(values)));
#endif
  }

  if (metadata.value_type == scalar_type::float64) {
#if NDTBL_ENABLE_MMAP
    const std::shared_ptr<const std::uint8_t> payload_owner =
      detail::map_payload_bytes(
        path, layout.payload_offset, layout.payload_size);
    return RuntimeFieldGroup<Dim, Output>(FieldGroup<Dim, double>(
      grid,
      metadata.field_names,
      PayloadView<double>(payload_owner.get(), layout.value_count),
      payload_owner,
      true));
#else
    // Keep this non-const so the payload buffer can be moved into the
    // read-only FieldGroup storage instead of copied during load.
    std::vector<double> values =
      detail::read_payload<double>(is, layout.value_count);
    return RuntimeFieldGroup<Dim, Output>(
      FieldGroup<Dim, double>(grid, metadata.field_names, std::move(values)));
#endif
  }

  throw FormatError("unsupported ndtbl scalar type");
}

} // namespace ndtbl
