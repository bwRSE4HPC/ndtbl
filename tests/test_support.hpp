// SPDX-License-Identifier: MIT

#pragma once

#include "ndtbl/axis.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace ndtbl_test {

inline std::string
temporary_path()
{
#if defined(_WIN32)
  char path_buffer[L_tmpnam];
  if (std::tmpnam(path_buffer) == nullptr) {
    throw std::runtime_error("failed to create temporary path for ndtbl test");
  }

  const std::string path = std::string(path_buffer) + ".ndtbl";
  std::remove(path.c_str());
  return path;
#else
  char path_buffer[] = "/tmp/ndtbl-test-XXXXXX";
  const int fd = mkstemp(path_buffer);
  if (fd < 0) {
    throw std::runtime_error("failed to create temporary path for ndtbl test");
  }

  close(fd);

  const std::string path = std::string(path_buffer) + ".ndtbl";
  if (std::rename(path_buffer, path.c_str()) != 0) {
    std::remove(path_buffer);
    throw std::runtime_error("failed to reserve temporary ndtbl test path");
  }
  return path;
#endif
}

inline std::vector<char>
read_file_bytes(const std::string& path)
{
  std::ifstream input(path.c_str(), std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open test file for reading");
  }

  return std::vector<char>((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
}

inline void
write_file_bytes(const std::string& path, const std::vector<char>& bytes)
{
  std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error("failed to open test file for writing");
  }

  if (!bytes.empty()) {
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  if (!output.good()) {
    throw std::runtime_error("failed to write test file bytes");
  }
}

template<class UInt>
void
append_uint_le(std::vector<char>& bytes, UInt value)
{
  for (std::size_t index = 0; index < sizeof(UInt); ++index) {
    bytes.push_back(static_cast<char>((value >> (index * 8u)) & 0xffu));
  }
}

template<class UInt>
void
overwrite_uint_le(std::vector<char>& bytes, std::size_t offset, UInt value)
{
  if (offset > bytes.size() || sizeof(UInt) > bytes.size() - offset) {
    throw std::out_of_range("encoded integer exceeds test buffer");
  }
  for (std::size_t index = 0; index < sizeof(UInt); ++index) {
    bytes[offset + index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
  }
}

inline void
append_double_le(std::vector<char>& bytes, double value)
{
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  append_uint_le(bytes, bits);
}

inline void
append_float_le(std::vector<char>& bytes, float value)
{
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  append_uint_le(bytes, bits);
}

template<std::size_t Dim>
double
linear_value(const std::array<double, Dim>& coordinates,
             const std::array<double, Dim>& coefficients,
             double intercept)
{
  double value = intercept;
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    value += coefficients[axis] * coordinates[axis];
  }
  return value;
}

template<std::size_t Dim>
std::array<double, Dim>
clamp_to_axes(const std::array<ndtbl::Axis, Dim>& axes,
              const std::array<double, Dim>& coordinates)
{
  std::array<double, Dim> clamped = coordinates;
  for (std::size_t axis = 0; axis < Dim; ++axis) {
    if (clamped[axis] < axes[axis].min()) {
      clamped[axis] = axes[axis].min();
    } else if (clamped[axis] > axes[axis].max()) {
      clamped[axis] = axes[axis].max();
    }
  }
  return clamped;
}

template<std::size_t Dim>
std::vector<double>
build_linear_payload(const std::array<ndtbl::Axis, Dim>& axes,
                     const std::array<double, Dim>& coeffs_a,
                     double intercept_a,
                     const std::array<double, Dim>& coeffs_b,
                     double intercept_b)
{
  std::vector<double> payload;
  std::array<double, Dim> coordinates;

  const auto append_point_values = [&](const auto& self,
                                       std::size_t axis) -> void {
    if (axis == Dim) {
      payload.push_back(linear_value(coordinates, coeffs_a, intercept_a));
      payload.push_back(linear_value(coordinates, coeffs_b, intercept_b));
      return;
    }

    for (std::size_t index = 0; index < axes[axis].size(); ++index) {
      coordinates[axis] = axes[axis].coordinate(index);
      self(self, axis + 1);
    }
  };

  append_point_values(append_point_values, 0);
  return payload;
}

} // namespace ndtbl_test
