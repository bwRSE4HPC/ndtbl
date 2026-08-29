// SPDX-License-Identifier: MIT

#include <ndtbl/ndtbl.hpp>

#include <array>

int
main()
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "value" }, { 1.0, 3.0 });

  const auto values = group.evaluate_all_linear({ 0.5 });
  static_cast<void>(values);

  return 0;
}
