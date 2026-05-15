#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace ndtbl {
namespace detail {

inline std::size_t
checked_multiply_size(std::size_t lhs, std::size_t rhs, const std::string& what)
{
  if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw std::runtime_error("ndtbl " + what + " exceeds supported size");
  }
  return lhs * rhs;
}

inline std::size_t
checked_add_size(std::size_t lhs, std::size_t rhs, const std::string& what)
{
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    throw std::runtime_error("ndtbl " + what + " exceeds supported size");
  }
  return lhs + rhs;
}

inline std::size_t
narrow_u64_to_size(std::uint64_t value, const std::string& what)
{
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("ndtbl " + what + " exceeds supported size");
  }
  return static_cast<std::size_t>(value);
}

} // namespace detail
} // namespace ndtbl
