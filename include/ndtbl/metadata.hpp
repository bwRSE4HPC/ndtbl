// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/axis.hpp"
#include "ndtbl/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ndtbl {

/**
 * @brief Lightweight description of one serialized ndtbl field group.
 *
 * This metadata is sufficient to inspect the file layout, reconstruct a typed
 * grid, and validate payload sizes before reading values.
 */
struct GroupMetadata
{
  /// Scalar payload type stored in the file.
  scalar_type value_type;
  /// Number of dimensions.
  std::size_t dimension;
  /// Number of named fields stored per grid point.
  std::size_t field_count;
  /// Total number of support points in the grid.
  std::size_t point_count;
  /// One axis descriptor per dimension.
  std::vector<Axis> axes;
  /// Field names in payload storage order.
  std::vector<std::string> field_names;
  /// ndtbl binary file format version.
  std::uint8_t format_version = current_format_version;
};

} // namespace ndtbl
