// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace ndtbl {
namespace detail {

/**
 * @brief Multiply two sizes after checking for overflow.
 *
 * @param lhs Left-hand factor.
 * @param rhs Right-hand factor.
 * @param what Description of the size used in the overflow message.
 *
 * @return The product of @p lhs and @p rhs.
 *
 * @throws std::overflow_error If the product cannot be represented by
 * `std::size_t`.
 */
inline std::size_t
checked_multiply_size(std::size_t lhs, std::size_t rhs, const std::string& what)
{
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw std::overflow_error("ndtbl " + what + " exceeds supported size");
  }
  return lhs * rhs;
}

/**
 * @brief Add two sizes after checking for overflow.
 *
 * @param lhs Left-hand addend.
 * @param rhs Right-hand addend.
 * @param what Description of the size used in the overflow message.
 *
 * @return The sum of @p lhs and @p rhs.
 *
 * @throws std::overflow_error If the sum cannot be represented by
 * `std::size_t`.
 */
inline std::size_t
checked_add_size(std::size_t lhs, std::size_t rhs, const std::string& what)
{
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    throw std::overflow_error("ndtbl " + what + " exceeds supported size");
  }
  return lhs + rhs;
}

/**
 * @brief Convert a 64-bit unsigned value to `std::size_t` safely.
 *
 * @param value Value to convert.
 * @param what Description of the size used in the overflow message.
 *
 * @return @p value represented as `std::size_t`.
 *
 * @throws std::overflow_error If @p value cannot be represented by
 * `std::size_t`.
 */
inline std::size_t
narrow_u64_to_size(std::uint64_t value, const std::string& what)
{
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::overflow_error("ndtbl " + what + " exceeds supported size");
  }
  return static_cast<std::size_t>(value);
}

} // namespace detail
} // namespace ndtbl
