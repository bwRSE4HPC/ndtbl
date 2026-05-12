#pragma once

#include <cstdint>
#include <type_traits>

namespace ndtbl {

/**
 * @brief Describes how one interpolation axis is represented.
 */
enum class axis_kind : std::uint8_t
{
  /// Axis coordinates are generated from minimum, maximum, and size.
  uniform = 1,
  /// Axis coordinates are stored explicitly in ascending order.
  explicit_coordinates = 2
};

/**
 * @brief Scalar payload type stored in a table file.
 */
enum class scalar_type : std::uint8_t
{
  /// IEEE-754 single-precision floating-point payload values.
  float32 = 1,
  /// IEEE-754 double-precision floating-point payload values.
  float64 = 2
};

/**
 * @brief Controls how interpolation queries outside the grid domain behave.
 */
enum class bounds_policy : std::uint8_t
{
  /// Clamp out-of-domain interpolation queries to the table bounds.
  clamp = 1,
  /// Reject out-of-domain interpolation queries with an exception.
  throw_error = 2
};

/**
 * @brief Current ndtbl binary file format version.
 */
static constexpr std::uint8_t current_format_version = 1u;

/**
 * @brief Map a supported C++ scalar type to the ndtbl on-disk type tag.
 *
 * Only `float` and `double` are supported in the current implementation.
 *
 * @tparam Stored Supported scalar payload type.
 * @return Corresponding ndtbl scalar type tag.
 */
template<class Stored>
constexpr scalar_type
scalar_type_of() noexcept
{
  static_assert(std::is_same<Stored, float>::value ||
                  std::is_same<Stored, double>::value,
                "ndtbl supports only float and double payloads");

  return std::is_same<Stored, float>::value ? scalar_type::float32
                                            : scalar_type::float64;
}

} // namespace ndtbl
