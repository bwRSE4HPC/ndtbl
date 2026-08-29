// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/detail/size_math.hpp"
#include "ndtbl/exceptions.hpp"
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
#include <utility>
#include <vector>

namespace ndtbl {
namespace detail {

/**
 * @brief Internal magic header written at the start of every ndtbl file.
 *
 * This constant is part of the binary file format implementation and is not
 * intended to be consumed directly by library users.
 */
static constexpr std::array<char, 8> file_magic = {
  { 'N', 'D', 'T', 'B', 'L', '\0', '\0', '\0' }
};

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
  try {
    os.write(data, static_cast<std::streamsize>(size));
  } catch (const std::ios_base::failure&) {
    throw IOError("failed to write ndtbl payload");
  }
  if (!os.good()) {
    throw IOError("failed to write ndtbl payload");
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
  try {
    is.read(data, static_cast<std::streamsize>(size));
  } catch (const std::ios_base::failure&) {
    if (is.eof()) {
      throw FormatError("unexpected end of ndtbl input");
    }
    throw IOError("failed to read ndtbl payload");
  }
  if (!is.good()) {
    if (is.eof()) {
      throw FormatError("unexpected end of ndtbl input");
    }
    throw IOError("failed to read ndtbl payload");
  }
}

inline bool
host_is_little_endian()
{
  const std::uint16_t marker = 1u;
  const auto* bytes = reinterpret_cast<const unsigned char*>(&marker);
  return bytes[0] == 1u;
}

template<class UInt>
void
write_uint_le(std::ostream& os, UInt value)
{
  static_assert(std::is_unsigned<UInt>::value,
                "write_uint_le requires an unsigned integer type");

  std::array<char, sizeof(UInt)> bytes = {};
  for (auto& byte : bytes) {
    byte = static_cast<char>(value & 0xffu);
    value >>= 8u;
  }
  write_bytes(os, bytes.data(), bytes.size());
}

template<class UInt>
UInt
read_uint_le(std::istream& is)
{
  static_assert(std::is_unsigned<UInt>::value,
                "read_uint_le requires an unsigned integer type");

  std::array<char, sizeof(UInt)> bytes = {};
  read_bytes(is, bytes.data(), bytes.size());

  UInt value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<UInt>(static_cast<unsigned char>(bytes[index]))
             << (index * 8u);
  }
  return value;
}

template<class Float, class UInt>
void
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
Float
read_float_le(std::istream& is)
{
  static_assert(std::is_floating_point<Float>::value,
                "read_float_le requires a floating-point type");
  static_assert(sizeof(Float) == sizeof(UInt),
                "read_float_le requires matching bit width");
  static_assert(std::numeric_limits<Float>::is_iec559,
                "ndtbl requires IEEE-754 floating-point types");

  const auto bits = read_uint_le<UInt>(is);
  Float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

template<class Stored>
using payload_uint_t =
  std::conditional_t<sizeof(Stored) == sizeof(std::uint32_t),
                     std::uint32_t,
                     std::uint64_t>;

/**
 * @brief Restrict metadata reads to the declared metadata byte range.
 */
class bounded_metadata_reader
{
public:
  /**
   * @brief Construct a reader with a fixed metadata byte budget.
   *
   * @param is Source stream positioned at the metadata region.
   * @param remaining Number of metadata bytes available to read.
   */
  bounded_metadata_reader(std::istream& is, std::size_t remaining)
    : is_(is)
    , remaining_(remaining)
  {
  }

  /**
   * @brief Require a byte sequence to fit within the metadata region.
   *
   * @param size Number of bytes required.
   * @param what Description used in the format-error message.
   *
   * @throws FormatError If fewer than @p size metadata bytes remain.
   */
  void require_bytes(std::size_t size, const std::string& what) const
  {
    if (size > remaining_) {
      throw FormatError("ndtbl " + what + " exceeds metadata boundary");
    }
  }

  /**
   * @brief Require a count of fixed-size records to fit in the metadata.
   *
   * @param count Number of records declared by the input.
   * @param encoded_size Minimum encoded byte size of one record.
   * @param what Description used in the format-error message.
   *
   * @throws FormatError If the records exceed the remaining metadata.
   */
  void require_count(std::size_t count,
                     std::size_t encoded_size,
                     const std::string& what) const
  {
    if (encoded_size != 0 && count > remaining_ / encoded_size) {
      throw FormatError("ndtbl " + what + " exceeds metadata boundary");
    }
  }

  /**
   * @brief Read bytes and consume them from the metadata budget.
   *
   * @param data Destination byte buffer.
   * @param size Number of bytes to read.
   * @param what Description used in the format-error message.
   *
   * @throws FormatError If the read crosses the metadata boundary.
   * @throws IOError If the underlying stream read fails.
   */
  void read(char* data, std::size_t size, const std::string& what)
  {
    require_bytes(size, what);
    read_bytes(is_, data, size);
    remaining_ -= size;
  }

  /**
   * @brief Read one little-endian unsigned integer from the metadata.
   *
   * @tparam UInt Unsigned integer type to decode.
   * @param what Description used in the format-error message.
   *
   * @return Decoded integer value.
   * @throws FormatError If the read crosses the metadata boundary.
   * @throws IOError If the underlying stream read fails.
   */
  template<class UInt>
  UInt read_uint_le(const std::string& what)
  {
    static_assert(std::is_unsigned<UInt>::value,
                  "read_uint_le requires an unsigned integer type");

    std::array<char, sizeof(UInt)> bytes = {};
    read(bytes.data(), bytes.size(), what);

    UInt value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
      value |= static_cast<UInt>(static_cast<unsigned char>(bytes[index]))
               << (index * 8u);
    }
    return value;
  }

  /**
   * @brief Read one little-endian IEEE-754 value from the metadata.
   *
   * @tparam Float Floating-point type to decode.
   * @tparam UInt Unsigned integer type with the same width as @c Float.
   * @param what Description used in the format-error message.
   *
   * @return Decoded floating-point value.
   * @throws FormatError If the read crosses the metadata boundary.
   * @throws IOError If the underlying stream read fails.
   */
  template<class Float, class UInt>
  Float read_float_le(const std::string& what)
  {
    static_assert(std::is_floating_point<Float>::value,
                  "read_float_le requires a floating-point type");
    static_assert(sizeof(Float) == sizeof(UInt),
                  "read_float_le requires matching bit width");
    static_assert(std::numeric_limits<Float>::is_iec559,
                  "ndtbl requires IEEE-754 floating-point types");

    const auto bits = read_uint_le<UInt>(what);
    Float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  /**
   * @brief Return the unread metadata byte count.
   *
   * @return Number of bytes remaining before the payload boundary.
   */
  std::size_t remaining() const { return remaining_; }

private:
  std::istream& is_;
  std::size_t remaining_;
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
  const auto size = static_cast<std::uint64_t>(value.size());
  write_uint_le(os, size);
  write_bytes(os, value.data(), value.size());
}

/**
 * @brief Read a length-prefixed string from a binary stream.
 *
 * @param is Source stream in binary mode.
 *
 * @return Decoded string value.
 */
inline std::string
read_string(bounded_metadata_reader& reader)
{
  const auto encoded_size =
    reader.read_uint_le<std::uint64_t>("field name length");
  if (encoded_size >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw FormatError("ndtbl string length exceeds supported size");
  }
  const auto size = static_cast<std::size_t>(encoded_size);
  reader.require_bytes(size, "field name");
  if (size == 0) {
    return std::string();
  }

  std::string value;
  if (size > value.max_size()) {
    throw FormatError("ndtbl string length exceeds supported size");
  }
  value.resize(size);
  reader.read(&value[0], size, "field name");
  return value;
}

inline void
require_zero(std::uint64_t value, const std::string& what)
{
  if (value != 0u) {
    throw FormatError("ndtbl " + what + " must be zero");
  }
}

constexpr std::size_t
fixed_header_size()
{
  return file_magic.size() +     // File magic
         sizeof(std::uint8_t) +  // Format version
         sizeof(std::uint8_t) +  // Scalar type
         sizeof(std::uint16_t) + // Reserved
         sizeof(std::uint64_t) + // Payload offset
         sizeof(std::uint64_t) + // Dimension
         sizeof(std::uint64_t) + // Field count
         sizeof(std::uint64_t);  // Point count
}

inline std::size_t
input_size(std::istream& is)
{
  std::istream::pos_type original_position = std::istream::pos_type(-1);
  std::istream::pos_type end_position = std::istream::pos_type(-1);
  try {
    original_position = is.tellg();
    is.seekg(0, std::ios::end);
    end_position = is.tellg();
    is.seekg(original_position);
  } catch (const std::ios_base::failure&) {
    throw IOError("failed to determine ndtbl input size");
  }

  if (original_position < 0 || end_position < 0 || !is.good()) {
    throw IOError("failed to determine ndtbl input size");
  }

  const auto end_offset = static_cast<std::streamoff>(end_position);
  if (end_offset < 0 ||
      static_cast<std::uintmax_t>(end_offset) >
        static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
    throw FormatError("ndtbl input size exceeds supported size");
  }
  return static_cast<std::size_t>(end_offset);
}

inline std::size_t
narrow_format_size(std::uint64_t value, const std::string& what)
{
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw FormatError("ndtbl " + what + " exceeds supported size");
  }
  return static_cast<std::size_t>(value);
}

inline std::size_t
checked_format_multiply(std::size_t lhs,
                        std::size_t rhs,
                        const std::string& what)
{
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw FormatError("ndtbl " + what + " exceeds supported size");
  }
  return lhs * rhs;
}

inline std::size_t
checked_format_add(std::size_t lhs, std::size_t rhs, const std::string& what)
{
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    throw FormatError("ndtbl " + what + " exceeds supported size");
  }
  return lhs + rhs;
}

inline std::size_t
metadata_size(const GroupMetadata& metadata)
{
  std::size_t total = fixed_header_size();

  for (const auto& axis_spec : metadata.axes) {
    total = checked_add_size(total,
                             sizeof(std::uint8_t) +    // Axis kind
                               sizeof(std::uint8_t) +  // Reserved byte
                               sizeof(std::uint16_t) + // Reserved field
                               sizeof(std::uint64_t),  // Axis extent
                             "metadata size");

    if (axis_spec.kind() == axis_kind::uniform) {
      total = checked_add_size(total,
                               sizeof(double) +  // Minimum coordinate
                                 sizeof(double), // Maximum coordinate
                               "metadata size");
    } else {
      total = checked_add_size(
        total,
        checked_multiply_size(axis_spec.size(),
                              sizeof(double), // Axis coordinate
                              "axis payload"),
        "metadata size");
    }
  }

  for (const auto& field_name : metadata.field_names) {
    total = checked_add_size(total,
                             sizeof(std::uint64_t), // Field name length
                             "metadata size");
    total = checked_add_size(total, field_name.size(), "metadata size");
  }

  return total;
}

inline std::size_t
axis_point_count(const std::vector<Axis>& axes)
{
  std::size_t point_count = 1;
  for (const auto& axis : axes) {
    point_count =
      checked_multiply_size(point_count, axis.size(), "point count");
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
void
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

  write_bytes(os, file_magic.data(), file_magic.size());
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

  for (const auto& axis_spec : metadata.axes) {
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
      for (const auto coordinate : coordinates) {
        write_float_le<double, std::uint64_t>(os, coordinate);
      }
    }
  }

  for (const auto& field_name : metadata.field_names) {
    write_string(os, field_name);
  }

  if (payload.size() != 0) {
    if (host_is_little_endian()) {
      write_bytes(os,
                  reinterpret_cast<const char*>(payload.byte_data()),
                  payload.byte_size());
    } else {
      for (std::size_t index = 0; index < payload.size(); ++index) {
        write_float_le<Stored, payload_uint_t<Stored>>(
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
void
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
void
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
  std::array<char, file_magic.size()> magic = {};
  read_bytes(is, magic.data(), magic.size());
  if (magic != file_magic) {
    throw FormatError("invalid ndtbl magic header");
  }
}

constexpr std::size_t
scalar_size(scalar_type type)
{
  if (type == scalar_type::float32) {
    return sizeof(float);
  }
  if (type == scalar_type::float64) {
    return sizeof(double);
  }
  throw FormatError("unsupported ndtbl scalar type");
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
 *
 * @return Parsed metadata record plus payload location details.
 */
inline parsed_group_layout
read_group_layout_impl(std::istream& is)
{
  const std::size_t file_size = input_size(is);
  verify_magic(is);
  if (file_size < fixed_header_size()) {
    throw FormatError("unexpected end of ndtbl input");
  }

  const auto version = read_uint_le<std::uint8_t>(is);
  if (version != current_format_version) {
    throw FormatError("unsupported ndtbl version");
  }

  GroupMetadata metadata;
  metadata.format_version = version;
  metadata.value_type =
    static_cast<scalar_type>(read_uint_le<std::uint8_t>(is));
  require_zero(read_uint_le<std::uint16_t>(is), "header reserved field");
  const std::size_t payload_offset =
    narrow_format_size(read_uint_le<std::uint64_t>(is), "payload offset");

  metadata.dimension =
    narrow_format_size(read_uint_le<std::uint64_t>(is), "dimension");
  metadata.field_count =
    narrow_format_size(read_uint_le<std::uint64_t>(is), "field count");
  metadata.point_count =
    narrow_format_size(read_uint_le<std::uint64_t>(is), "point count");

  const std::size_t value_count = checked_format_multiply(
    metadata.point_count, metadata.field_count, "payload value count");
  const std::size_t payload_size = checked_format_multiply(
    value_count, scalar_size(metadata.value_type), "payload byte size");

  if (payload_offset < fixed_header_size()) {
    throw FormatError("ndtbl payload offset does not match metadata");
  }
  if (payload_offset > file_size) {
    throw FormatError("ndtbl payload offset exceeds file size");
  }

  bounded_metadata_reader reader(is, payload_offset - fixed_header_size());
  constexpr std::size_t axis_header_size =
    sizeof(std::uint8_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t) +
    sizeof(std::uint64_t);
  constexpr std::size_t minimum_axis_size = axis_header_size + sizeof(double);

  reader.require_count(metadata.dimension, minimum_axis_size, "dimension");
  if (metadata.dimension > metadata.axes.max_size()) {
    throw FormatError("ndtbl dimension exceeds supported size");
  }
  metadata.axes.reserve(metadata.dimension);
  for (std::size_t axis = 0; axis < metadata.dimension; ++axis) {
    const auto kind =
      static_cast<axis_kind>(reader.read_uint_le<std::uint8_t>("axis kind"));
    require_zero(reader.read_uint_le<std::uint8_t>("axis reserved byte"),
                 "axis reserved byte");
    require_zero(reader.read_uint_le<std::uint16_t>("axis reserved field"),
                 "axis reserved field");
    const std::size_t extent = narrow_format_size(
      reader.read_uint_le<std::uint64_t>("axis extent"), "axis extent");

    try {
      if (kind == axis_kind::uniform) {
        reader.require_bytes(sizeof(double) * 2, "uniform axis coordinates");
        const auto min_value =
          reader.read_float_le<double, std::uint64_t>("axis minimum");
        const auto max_value =
          reader.read_float_le<double, std::uint64_t>("axis maximum");
        metadata.axes.push_back(Axis::uniform(min_value, max_value, extent));
      } else if (kind == axis_kind::explicit_coordinates) {
        const std::size_t coordinate_bytes = checked_format_multiply(
          extent, sizeof(double), "axis coordinate bytes");
        reader.require_bytes(coordinate_bytes, "axis coordinates");
        std::vector<double> coordinates;
        if (extent > coordinates.max_size()) {
          throw FormatError("ndtbl axis extent exceeds supported size");
        }
        coordinates.resize(extent);
        for (std::size_t i = 0; i < extent; ++i) {
          coordinates[i] =
            reader.read_float_le<double, std::uint64_t>("axis coordinate");
        }
        metadata.axes.push_back(Axis::from_coordinates(coordinates));
      } else {
        throw FormatError("unsupported ndtbl axis kind");
      }
    } catch (const std::invalid_argument& error) {
      throw FormatError(error.what());
    }
  }

  reader.require_count(
    metadata.field_count, sizeof(std::uint64_t), "field count");
  if (metadata.field_count > metadata.field_names.max_size()) {
    throw FormatError("ndtbl field count exceeds supported size");
  }
  metadata.field_names.reserve(metadata.field_count);
  for (std::size_t field = 0; field < metadata.field_count; ++field) {
    metadata.field_names.push_back(read_string(reader));
  }

  if (reader.remaining() != 0) {
    throw FormatError("ndtbl payload offset does not match metadata");
  }

  std::size_t parsed_point_count = 0;
  try {
    parsed_point_count = axis_point_count(metadata.axes);
  } catch (const std::overflow_error& error) {
    throw FormatError(error.what());
  }
  if (parsed_point_count != metadata.point_count) {
    throw FormatError("ndtbl point count does not match axis extents");
  }

  const std::size_t expected_file_size =
    checked_format_add(payload_offset, payload_size, "file size");
  if (expected_file_size != file_size) {
    throw FormatError("ndtbl file size does not match declared payload");
  }

  parsed_group_layout layout;
  layout.metadata = std::move(metadata);
  layout.payload_offset = payload_offset;
  layout.value_count = value_count;
  layout.payload_size = payload_size;
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
 *
 * @return Payload vector with `value_count` entries.
 */
template<class Stored>
std::vector<Stored>
read_payload(std::istream& is, std::size_t value_count)
{
  const std::size_t payload_size =
    checked_format_multiply(value_count, sizeof(Stored), "payload byte size");
  std::vector<Stored> values;
  if (value_count > values.max_size()) {
    throw FormatError("ndtbl payload value count exceeds supported size");
  }
  values.resize(value_count);
  if (value_count == 0) {
    return values;
  }

  if (host_is_little_endian()) {
    read_bytes(is, reinterpret_cast<char*>(values.data()), payload_size);
    return values;
  }

  for (std::size_t index = 0; index < value_count; ++index) {
    values[index] = read_float_le<Stored, payload_uint_t<Stored>>(is);
  }
  return values;
}

/**
 * @brief Convert a dynamic axis vector to a fixed-size array.
 *
 * @tparam Dim Expected number of axes.
 * @param axes Dynamic axis list to convert.
 *
 * @return Fixed-size axis array with `Dim` entries.
 */
template<std::size_t Dim>
std::array<Axis, Dim>
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
