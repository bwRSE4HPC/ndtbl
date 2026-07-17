// SPDX-License-Identifier: MIT

#include "ndtbl/ndtbl.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string_view
value_type_name(ndtbl::scalar_type value_type)
{
  return value_type == ndtbl::scalar_type::float32 ? "float32" : "float64";
}

void
print_axis(std::size_t axis_index, const ndtbl::Axis& axis)
{
  std::cout << "axis[" << axis_index << "]: ";
  if (axis.kind() == ndtbl::axis_kind::uniform) {
    std::cout << "uniform size=" << axis.size() << " min=" << axis.min()
              << " max=" << axis.max() << '\n';
  } else {
    std::cout << "explicit size=" << axis.size() << '\n';
  }
}

} // namespace

int
main(int argc, char** argv)
{
  try {
    if (argc != 2) {
      std::cerr << "Usage: ndtbl-inspect file.ndtbl\n";
      return 1;
    }

    const std::filesystem::path input_path = argv[1];
    const ndtbl::GroupMetadata metadata =
      ndtbl::read_group_metadata(input_path.string());

    std::cout << "file: " << input_path.string() << '\n';
    std::cout << "format_version: "
              << static_cast<unsigned>(metadata.format_version) << '\n';
    std::cout << "dimension: " << metadata.dimension << '\n';
    std::cout << "fields: " << metadata.field_count << '\n';
    std::cout << "value_type: " << value_type_name(metadata.value_type) << '\n';

    for (std::size_t axis = 0; axis < metadata.axes.size(); ++axis) {
      print_axis(axis, metadata.axes[axis]);
    }

    for (std::size_t field = 0; field < metadata.field_names.size(); ++field) {
      std::cout << "field[" << field << "]: " << metadata.field_names[field]
                << '\n';
    }

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ndtbl-inspect: " << error.what() << '\n';
    return 1;
  }
}
