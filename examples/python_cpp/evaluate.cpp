// SPDX-FileCopyrightText: 2026 Thomas Isensee
// SPDX-License-Identifier: MIT

#include <ndtbl/ndtbl.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int
main()
{
  const ndtbl::RuntimeFieldGroup<1, double> group =
    ndtbl::read_runtime_field_group<1, double>("example1D.ndtbl");
  const std::vector<std::string> field_names = group.field_names();

  std::vector<double> values_linear(group.field_count(), 0.0);
  std::vector<double> values_cubic(group.field_count(), 0.0);
  constexpr std::size_t steps = 100;

  for (std::size_t step = 0; step <= steps; ++step) {
    const double x = static_cast<double>(step) / static_cast<double>(steps);
    group.evaluate_all_linear_into(std::array<double, 1>{ { x } },
                                   values_linear.data());
    group.evaluate_all_cubic_into(std::array<double, 1>{ { x } },
                                  values_cubic.data());
    std::cout << x;
    for (std::size_t field = 0; field < field_names.size(); ++field) {
      std::cout << " " << values_linear[field];
      std::cout << " " << values_cubic[field];
    }
    std::cout << '\n';
  }
  return EXIT_SUCCESS;
}
