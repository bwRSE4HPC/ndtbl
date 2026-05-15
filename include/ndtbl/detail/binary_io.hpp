#pragma once

#include "ndtbl/detail/size_math.hpp"
#include "ndtbl/field_group.hpp"
#include "ndtbl/metadata.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ndtbl {
namespace detail {

/**
 * @brief Internal magic header written at the start of every ndtbl file.
 *
 * This constant is part of the binary file format implementation and is not
 * intended to be consumed directly by library users.
 */
static constexpr char file_magic[8] = { 'N', 'D',  'T',  'B',
                                        'L', '\0', '\0', '\0' };

/**
 * @brief Write one exact byte sequence to a binary stream.
 *
 * @param os Destination stream in binary mode.
 * @param data Source byte buffer.
 * @param size Number of bytes to write.
 */
inline void
write_bytes(std::ostream& os, const char* data, std::size_t size)
{
  os.write(data, static_cast<std::streamsize>(size));
  if (!os.good()) {
    throw std::runtime_error("failed to write ndtbl payload");
  }
}

/**
 * @brief Read one exact byte sequence from a binary stream.
 *
 * @param is Source stream in binary mode.
 * @param data Output byte buffer.
 * @param size Number of bytes to read.
 */
inline void
read_bytes(std::istream& is, char* data, std::size_t size)
{
  is.read(data, static_cast<std::streamsize>(size));
  if (!is.good()) {
    throw std::runtime_error("failed to read ndtbl payload");
  }
}

inline bool
host_is_little_endian()
{
  const std::uint16_t marker = 1u;
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&marker);
  return bytes[0] == 1u;
}

template<class UInt>
inline void
write_uint_le(std::ostream& os, UInt value)
{
  static_assert(std::is_unsigned<UInt>::value,
                "write_uint_le requires an unsigned integer type");

  char bytes[sizeof(UInt)] = {};
  for (std::size_t index = 0; index < sizeof(UInt); ++index) {
    bytes[index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
  }
  write_bytes(os, bytes, sizeof(bytes));
}

template<class UInt>
inline UInt
read_uint_le(std::istream& is)
{
  static_assert(std::is_unsigned<UInt>::value,
                "read_uint_le requires an unsigned integer type");

  char bytes[sizeof(UInt)] = {};
  read_bytes(is, bytes, sizeof(bytes));

  UInt value = 0;
  for (std::size_t index = 0; index < sizeof(UInt); ++index) {
    value |= static_cast<UInt>(static_cast<unsigned char>(bytes[index]))
             << (index * 8u);
  }
  return value;
}

template<class Float, class UInt>
inline void
write_float_le(std::ostream& os, Float value)
{
  static_assert(std::is_floating_point<Float>::value,
                "write_float_le requires a floating-point type");
  static_assert(sizeof(Float) == sizeof(UInt),
                "write_float_le requires matching bit width");
  static_assert(std::numeric_limits<Float>::is_iec559,
                "ndtbl requires IEEE-754 floating-point types");

  UInt bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  write_uint_le(os, bits);
}

template<class Float, class UInt>
inline Float
read_float_le(std::istream& is)
{
  static_assert(std::is_floating_point<Float>::value,
                "read_float_le requires a floating-point type");
  static_assert(sizeof(Float) == sizeof(UInt),
                "read_float_le requires matching bit width");
  static_assert(std::numeric_limits<Float>::is_iec559,
                "ndtbl requires IEEE-754 floating-point types");

  const UInt bits = read_uint_le<UInt>(is);
  Float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

template<class Stored>
struct payload_uint
{
  typedef typename std::conditional<sizeof(Stored) == sizeof(std::uint32_t),
                                    std::uint32_t,
                                    std::uint64_t>::type type;
};

/**
 * @brief Write a length-prefixed UTF-8 string to a binary stream.
 *
 * @param os Destination stream in binary mode.
 * @param value String to serialize.
 */
inline void
write_string(std::ostream& os, const std::string& value)
{
  const std::uint64_t size = static_cast<std::uint64_t>(value.size());
  write_uint_le(os, size);
  write_bytes(os, value.data(), value.size());
}

/**
 * @brief Read a length-prefixed string from a binary stream.
 *
 * @param is Source stream in binary mode.
 * @return Decoded string value.
 */
inline std::string
read_string(std::istream& is)
{
  const std::uint64_t size = read_uint_le<std::uint64_t>(is);
  if (size == 0) {
    return std::string();
  }
  if (size >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("ndtbl string length exceeds supported size");
  }

  std::string value(static_cast<std::size_t>(size), '\0');
  read_bytes(is, &value[0], static_cast<std::size_t>(size));
  return value;
}

inline void
require_zero(std::uint64_t value, const std::string& what)
{
  if (value != 0u) {
    throw std::runtime_error("ndtbl " + what + " must be zero");
  }
}

inline constexpr std::size_t
fixed_header_size()
{
  return sizeof(file_magic) + sizeof(std::uint8_t) + sizeof(std::uint8_t) +
         sizeof(std::uint16_t) + sizeof(std::uint64_t) * 4u;
}

inline std::size_t
metadata_size(const GroupMetadata& metadata)
{
  std::size_t total = fixed_header_size();

  for (std::size_t axis = 0; axis < metadata.axes.size(); ++axis) {
    const Axis& axis_spec = metadata.axes[axis];
    total = checked_add_size(total,
                             sizeof(std::uint8_t) + sizeof(std::uint8_t) +
                               sizeof(std::uint16_t) + sizeof(std::uint64_t),
                             "metadata size");

    if (axis_spec.kind() == axis_kind::uniform) {
      total = checked_add_size(total, sizeof(double) * 2u, "metadata size");
    } else {
      total = checked_add_size(
        total,
        checked_multiply_size(axis_spec.size(), sizeof(double), "axis payload"),
        "metadata size");
    }
  }

  for (std::size_t field = 0; field < metadata.field_names.size(); ++field) {
    total = checked_add_size(total, sizeof(std::uint64_t), "metadata size");
    total = checked_add_size(
      total, metadata.field_names[field].size(), "metadata size");
  }

  return total;
}

inline std::size_t
axis_point_count(const std::vector<Axis>& axes)
{
  std::size_t point_count = 1;
  for (std::size_t axis = 0; axis < axes.size(); ++axis) {
    point_count =
      checked_multiply_size(point_count, axes[axis].size(), "point count");
  }
  return point_count;
}

/**
 * @brief Internal implementation for writing raw metadata and payload data.
 *
 * @tparam Stored Scalar payload type stored in the payload vector.
 * @param os Destination stream in binary mode.
 * @param metadata File metadata describing the payload layout.
 * @param payload Point-major interleaved field payload.
 */
template<class Stored>
inline void
write_group_stream_impl(std::ostream& os,
                        const GroupMetadata& metadata,
                        const PayloadView<Stored>& payload)
{
  if (metadata.axes.size() != metadata.dimension) {
    throw std::invalid_argument(
      "ndtbl metadata axis count does not match dimension");
  }

  if (metadata.field_names.size() != metadata.field_count) {
    throw std::invalid_argument(
      "ndtbl metadata field count does not match field names");
  }

  if (metadata.value_type != scalar_type_of<Stored>()) {
    throw std::invalid_argument(
      "ndtbl metadata scalar type does not match payload type");
  }

  if (metadata.point_count != axis_point_count(metadata.axes)) {
    throw std::invalid_argument(
      "ndtbl point count does not match axis extents");
  }

  const std::size_t expected_values = checked_multiply_size(
    metadata.point_count, metadata.field_count, "payload value count");
  if (payload.size() != expected_values) {
    throw std::invalid_argument("ndtbl payload size does not match metadata");
  }

  const std::size_t payload_offset = metadata_size(metadata);

  write_bytes(os, file_magic, sizeof(file_magic));
  write_uint_le<std::uint8_t>(os, current_format_version);
  write_uint_le<std::uint8_t>(os,
                              static_cast<std::uint8_t>(metadata.value_type));
  write_uint_le<std::uint16_t>(os, 0u);
  write_uint_le<std::uint64_t>(os, static_cast<std::uint64_t>(payload_offset));
  write_uint_le<std::uint64_t>(os,
                               static_cast<std::uint64_t>(metadata.dimension));
  write_uint_le<std::uint64_t>(
    os, static_cast<std::uint64_t>(metadata.field_count));
  write_uint_le<std::uint64_t>(
    os, static_cast<std::uint64_t>(metadata.point_count));

  for (std::size_t axis = 0; axis < metadata.axes.size(); ++axis) {
    const Axis& axis_spec = metadata.axes[axis];
    write_uint_le<std::uint8_t>(os,
                                static_cast<std::uint8_t>(axis_spec.kind()));
    write_uint_le<std::uint8_t>(os, 0u);
    write_uint_le<std::uint16_t>(os, 0u);
    write_uint_le<std::uint64_t>(os,
                                 static_cast<std::uint64_t>(axis_spec.size()));
    if (axis_spec.kind() == axis_kind::uniform) {
      write_float_le<double, std::uint64_t>(os, axis_spec.min());
      write_float_le<double, std::uint64_t>(os, axis_spec.max());
    } else {
      const std::vector<double> coordinates = axis_spec.coordinates();
      for (std::size_t i = 0; i < coordinates.size(); ++i) {
        write_float_le<double, std::uint64_t>(os, coordinates[i]);
      }
    }
  }

  for (std::size_t field = 0; field < metadata.field_names.size(); ++field) {
    write_string(os, metadata.field_names[field]);
  }

  if (payload.size() != 0) {
    if (host_is_little_endian()) {
      write_bytes(os,
                  reinterpret_cast<const char*>(payload.byte_data()),
                  payload.byte_size());
    } else {
      for (std::size_t index = 0; index < payload.size(); ++index) {
        write_float_le<Stored, typename payload_uint<Stored>::type>(
          os, payload.unchecked(index));
      }
    }
  }
}

/**
 * @brief Internal implementation for writing a typed field group.
 *
 * @tparam Stored Scalar payload type stored in the group.
 * @tparam Dim Grid dimensionality of the group.
 * @param os Destination stream in binary mode.
 * @param group Typed field group to serialize.
 * @see write_group_stream_impl(std::ostream&, const GroupMetadata&,
 *                              const std::vector<Stored>&)
 */
template<class Stored, std::size_t Dim>
inline void
write_group_stream_impl(std::ostream& os, const FieldGroup<Dim, Stored>& group)
{
  GroupMetadata metadata = { scalar_type_of<Stored>(),
                             Dim,
                             group.field_count(),
                             group.point_count(),
                             std::vector<Axis>(group.grid().axes().begin(),
                                               group.grid().axes().end()),
                             group.field_names() };
  write_group_stream_impl(os, metadata, group.interleaved_values());
}

template<class Stored>
inline void
write_group_stream_impl(std::ostream& os,
                        const GroupMetadata& metadata,
                        const std::vector<Stored>& payload)
{
  write_group_stream_impl(os, metadata, payload_view(payload));
}

/**
 * @brief Validate that a stream begins with the ndtbl file magic header.
 *
 * @param is Source stream positioned at the file start.
 */
inline void
verify_magic(std::istream& is)
{
  char magic[sizeof(file_magic)] = {};
  read_bytes(is, magic, sizeof(magic));
  if (!std::equal(magic, magic + sizeof(file_magic), file_magic)) {
    throw std::runtime_error("invalid ndtbl magic header");
  }
}

inline constexpr std::size_t
scalar_size(scalar_type type)
{
  if (type == scalar_type::float32) {
    return sizeof(float);
  }
  if (type == scalar_type::float64) {
    return sizeof(double);
  }
  throw std::runtime_error("unsupported ndtbl scalar type");
}

struct parsed_group_layout
{
  GroupMetadata metadata;
  std::size_t payload_offset;
  std::size_t value_count;
  std::size_t payload_size;
};

/**
 * @brief Read metadata from a stream without reading the payload body.
 *
 * @param is Source stream positioned at the file start.
 * @return Parsed metadata record plus payload location details.
 */
inline parsed_group_layout
read_group_layout_impl(std::istream& is)
{
  verify_magic(is);

  const std::uint8_t version = read_uint_le<std::uint8_t>(is);
  if (version != current_format_version) {
    throw std::runtime_error("unsupported ndtbl version");
  }

  GroupMetadata metadata;
  metadata.format_version = version;
  metadata.value_type =
    static_cast<scalar_type>(read_uint_le<std::uint8_t>(is));
  require_zero(read_uint_le<std::uint16_t>(is), "header reserved field");
  const std::size_t payload_offset =
    narrow_u64_to_size(read_uint_le<std::uint64_t>(is), "payload offset");

  metadata.dimension =
    narrow_u64_to_size(read_uint_le<std::uint64_t>(is), "dimension");
  metadata.field_count =
    narrow_u64_to_size(read_uint_le<std::uint64_t>(is), "field count");
  metadata.point_count =
    narrow_u64_to_size(read_uint_le<std::uint64_t>(is), "point count");

  metadata.axes.reserve(metadata.dimension);
  for (std::size_t axis = 0; axis < metadata.dimension; ++axis) {
    const axis_kind kind =
      static_cast<axis_kind>(read_uint_le<std::uint8_t>(is));
    require_zero(read_uint_le<std::uint8_t>(is), "axis reserved byte");
    require_zero(read_uint_le<std::uint16_t>(is), "axis reserved field");
    const std::size_t extent =
      narrow_u64_to_size(read_uint_le<std::uint64_t>(is), "axis extent");

    if (kind == axis_kind::uniform) {
      const double min_value = read_float_le<double, std::uint64_t>(is);
      const double max_value = read_float_le<double, std::uint64_t>(is);
      metadata.axes.push_back(Axis::uniform(min_value, max_value, extent));
    } else if (kind == axis_kind::explicit_coordinates) {
      std::vector<double> coordinates(extent);
      for (std::size_t i = 0; i < extent; ++i) {
        coordinates[i] = read_float_le<double, std::uint64_t>(is);
      }
      metadata.axes.push_back(Axis::from_coordinates(coordinates));
    } else {
      throw std::runtime_error("unsupported ndtbl axis kind");
    }
  }

  metadata.field_names.reserve(metadata.field_count);
  for (std::size_t field = 0; field < metadata.field_count; ++field) {
    metadata.field_names.push_back(read_string(is));
  }

  if (axis_point_count(metadata.axes) != metadata.point_count) {
    throw std::runtime_error("ndtbl point count does not match axis extents");
  }

  const std::istream::pos_type payload_position = is.tellg();
  if (payload_position < 0) {
    throw std::runtime_error("failed to determine ndtbl payload offset");
  }
  const std::size_t actual_payload_offset =
    static_cast<std::size_t>(payload_position);
  if (actual_payload_offset != payload_offset) {
    throw std::runtime_error("ndtbl payload offset does not match metadata");
  }

  parsed_group_layout layout;
  layout.metadata = metadata;
  layout.payload_offset = actual_payload_offset;
  layout.value_count = checked_multiply_size(
    metadata.point_count, metadata.field_count, "payload value count");
  layout.payload_size = checked_multiply_size(
    layout.value_count, scalar_size(metadata.value_type), "payload byte size");
  return layout;
}

inline GroupMetadata
read_group_metadata_impl(std::istream& is)
{
  return read_group_layout_impl(is).metadata;
}

/**
 * @brief Read a contiguous payload block from a binary stream.
 *
 * @tparam Stored Scalar payload type to deserialize.
 * @param is Source stream positioned at the start of the payload.
 * @param value_count Number of scalar values to read.
 * @return Payload vector with `value_count` entries.
 */
template<class Stored>
inline std::vector<Stored>
read_payload(std::istream& is, std::size_t value_count)
{
  std::vector<Stored> values(value_count);
  if (value_count == 0) {
    return values;
  }

  if (host_is_little_endian()) {
    read_bytes(is,
               reinterpret_cast<char*>(values.data()),
               values.size() * sizeof(Stored));
    return values;
  }

  for (std::size_t index = 0; index < value_count; ++index) {
    values[index] =
      read_float_le<Stored, typename payload_uint<Stored>::type>(is);
  }
  return values;
}

/**
 * @brief Convert a dynamic axis vector to a fixed-size array.
 *
 * @tparam Dim Expected number of axes.
 * @param axes Dynamic axis list to convert.
 * @return Fixed-size axis array with `Dim` entries.
 */
template<std::size_t Dim>
inline std::array<Axis, Dim>
fixed_axes(const std::vector<Axis>& axes)
{
  if (axes.size() != Dim) {
    throw std::invalid_argument(
      "ndtbl axis count does not match typed dimension");
  }

  std::array<Axis, Dim> fixed = {};
  std::copy(axes.begin(), axes.end(), fixed.begin());
  return fixed;
}

} // namespace detail
} // namespace ndtbl
