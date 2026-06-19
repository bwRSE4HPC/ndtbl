#include "ndtbl/ndtbl.hpp"

#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("typed loader round-trips metadata and float payloads", "[io]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<2, float> group(
    ndtbl::Grid<2>(axes),
    { "A", "B" },
    { 0.0f, 10.0f, 1.0f, 11.0f, 2.0f, 12.0f, 3.0f, 13.0f });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::GroupMetadata metadata = ndtbl::read_group_metadata(path);
  REQUIRE(metadata.format_version == 1u);
  REQUIRE(metadata.dimension == 2);
  REQUIRE(metadata.field_count == 2);
  REQUIRE(metadata.point_count == 4);
  REQUIRE(metadata.value_type == ndtbl::scalar_type::float32);
  REQUIRE(metadata.field_names == std::vector<std::string>({ "A", "B" }));

  const ndtbl::RuntimeFieldGroup<2> loaded =
    ndtbl::read_runtime_field_group<2>(path);
  std::array<double, 2> values = { 0.0, 0.0 };
  loaded.evaluate_all_linear_into({ 0.5, 0.5 }, values.data());
  REQUIRE(values[0] == Catch::Approx(1.5));
  REQUIRE(values[1] == Catch::Approx(11.5));

  std::remove(path.c_str());
}

TEST_CASE("typed field group loader reads known double payloads", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A", "B" }, { 2.0, 10.0, 4.0, 14.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::FieldGroup<1, double> loaded =
    ndtbl::read_field_group<1, double>(path);
  std::array<double, 2> values = { 0.0, 0.0 };
  loaded.evaluate_all_linear_into({ 0.25 }, values.data());

  REQUIRE(values[0] == Catch::Approx(2.5));
  REQUIRE(values[1] == Catch::Approx(11.0));

  std::remove(path.c_str());
}

TEST_CASE("typed field group loader reads known float payloads", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, float> group(
    ndtbl::Grid<1>(axes), { "A" }, { 1.0f, 3.0f });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::FieldGroup<1, float> loaded =
    ndtbl::read_field_group<1, float>(path);
  std::array<float, 1> values = { 0.0f };
  loaded.evaluate_all_linear_into({ 0.5 }, values.data());

  REQUIRE(values[0] == Catch::Approx(2.0f));

  std::remove(path.c_str());
}

TEST_CASE("payload residency reports availability for supported storage",
          "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });
  const ndtbl::RuntimeFieldGroup<1> runtime(group);

  REQUIRE_FALSE(group.payload_residency().available);
  REQUIRE_FALSE(runtime.payload_residency().available);

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::FieldGroup<1, double> loaded =
    ndtbl::read_field_group<1, double>(path);
  const ndtbl::RuntimeFieldGroup<1> loaded_runtime =
    ndtbl::read_runtime_field_group<1>(path);

  const ndtbl::residency_info info = loaded.payload_residency();
  const ndtbl::residency_info runtime_info = loaded_runtime.payload_residency();

#if NDTBL_ENABLE_MMAP && NDTBL_ENABLE_MMAP_DIAGNOSTICS
  REQUIRE(info.available);
  REQUIRE(info.page_size > 0);
  REQUIRE(info.total_pages > 0);
  REQUIRE(info.resident_pages <= info.total_pages);
  REQUIRE(info.resident_bytes == info.resident_pages * info.page_size);
  REQUIRE(info.resident_fraction >= 0.0);
  REQUIRE(info.resident_fraction <= 1.0);

  REQUIRE(runtime_info.available);
  REQUIRE(runtime_info.page_size > 0);
  REQUIRE(runtime_info.total_pages > 0);
  REQUIRE(runtime_info.resident_pages <= runtime_info.total_pages);
  REQUIRE(runtime_info.resident_fraction >= 0.0);
  REQUIRE(runtime_info.resident_fraction <= 1.0);
#else
  REQUIRE_FALSE(info.available);
  REQUIRE_FALSE(runtime_info.available);
#endif

  std::remove(path.c_str());
}

TEST_CASE("typed field group loader rejects wrong scalar type", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  REQUIRE_THROWS_AS((ndtbl::read_field_group<1, float>(path)),
                    std::runtime_error);

  std::remove(path.c_str());
}

TEST_CASE("runtime field group can be rewritten after reading", "[io]")
{
  const std::array<ndtbl::Axis, 2> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<2, double> group(
    ndtbl::Grid<2>(axes),
    { "A", "B" },
    { 0.0, 10.0, 1.0, 11.0, 2.0, 12.0, 3.0, 13.0 });

  const std::string input_path = ndtbl_test::temporary_path();
  const std::string output_path = ndtbl_test::temporary_path();
  ndtbl::write_group(input_path, group);

  const ndtbl::RuntimeFieldGroup<2> loaded =
    ndtbl::read_runtime_field_group<2>(input_path);
  ndtbl::write_group(output_path, loaded);

  const ndtbl::RuntimeFieldGroup<2> rewritten =
    ndtbl::read_runtime_field_group<2>(output_path);
  std::array<double, 2> values = { 0.0, 0.0 };
  rewritten.evaluate_all_linear_into({ 0.5, 0.5 }, values.data());
  REQUIRE(values[0] == Catch::Approx(1.5));
  REQUIRE(values[1] == Catch::Approx(11.5));

  std::remove(input_path.c_str());
  std::remove(output_path.c_str());
}

TEST_CASE("runtime field group supports caller-selected output precision",
          "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::Grid<1> grid(axes);

  const ndtbl::FieldGroup<1, float> float_group(
    grid, { "A", "B" }, { 0.0f, 10.0f, 1.0f, 12.0f });
  const ndtbl::RuntimeFieldGroup<1, float> float_runtime(float_group);
  std::array<float, 2> float_values = { 0.0f, 0.0f };
  float_runtime.evaluate_all_linear_into({ 0.5 }, float_values.data());
  REQUIRE(float_values[0] == Catch::Approx(0.5f));
  REQUIRE(float_values[1] == Catch::Approx(11.0f));

  const ndtbl::FieldGroup<1, double> double_group(
    grid, { "A", "B" }, { 0.0, 10.0, 1.0, 12.0 });
  const ndtbl::RuntimeFieldGroup<1, float> converted_runtime(double_group);
  converted_runtime.evaluate_all_linear_into({ 0.25 }, float_values.data());
  REQUIRE(float_values[0] == Catch::Approx(0.25f));
  REQUIRE(float_values[1] == Catch::Approx(10.5f));

  const ndtbl::RuntimeFieldGroup<1, long double> long_double_runtime(
    double_group);
  const std::vector<long double> long_double_values =
    long_double_runtime.evaluate_all_linear({ 0.75 });
  REQUIRE(static_cast<double>(long_double_values[0]) == Catch::Approx(0.75));
  REQUIRE(static_cast<double>(long_double_values[1]) == Catch::Approx(11.5));
}

TEST_CASE("typed loader supports caller-selected runtime output precision",
          "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 2.0, 4.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const ndtbl::RuntimeFieldGroup<1, float> loaded =
    ndtbl::read_runtime_field_group<1, float>(path);
  std::array<float, 1> values = { 0.0f };
  loaded.evaluate_all_linear_into({ 0.5 }, values.data());
  REQUIRE(values[0] == Catch::Approx(3.0f));

  std::remove(path.c_str());
}

TEST_CASE("raw writer round-trips explicit metadata", "[io]")
{
  const ndtbl::GroupMetadata metadata = {
    ndtbl::scalar_type::float32,           1,           2, 3,
    { ndtbl::Axis::uniform(0.0, 2.0, 3) }, { "A", "B" }
  };
  const std::vector<float> payload = { 0.0f, 10.0f, 1.0f, 11.0f, 2.0f, 12.0f };

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, metadata, payload);

  const ndtbl::RuntimeFieldGroup<1> loaded =
    ndtbl::read_runtime_field_group<1>(path);
  std::array<double, 2> values = { 0.0, 0.0 };
  loaded.evaluate_all_linear_into({ 0.5 }, values.data());
  REQUIRE(values[0] == Catch::Approx(0.5));
  REQUIRE(values[1] == Catch::Approx(10.5));

  std::remove(path.c_str());
}

TEST_CASE("raw writer rejects metadata scalar type mismatches", "[io]")
{
  const ndtbl::GroupMetadata metadata = {
    ndtbl::scalar_type::float64,           1,      1, 2,
    { ndtbl::Axis::uniform(0.0, 1.0, 2) }, { "A" }
  };
  std::ostringstream os;

  REQUIRE_THROWS_AS(
    ndtbl::write_group_stream(os, metadata, std::vector<float>({ 0.0f, 1.0f })),
    std::invalid_argument);
}

TEST_CASE("raw writer rejects point counts mismatching axis extents", "[io]")
{
  const ndtbl::GroupMetadata metadata = { ndtbl::scalar_type::float32,
                                          2,
                                          1,
                                          3,
                                          { ndtbl::Axis::uniform(0.0, 1.0, 2),
                                            ndtbl::Axis::uniform(0.0, 1.0, 2) },
                                          { "A" } };
  std::ostringstream os;

  REQUIRE_THROWS_AS(ndtbl::write_group_stream(
                      os, metadata, std::vector<float>({ 0.0f, 1.0f, 2.0f })),
                    std::invalid_argument);
}

TEST_CASE("runtime field group can be evaluated concurrently", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 15.0, 16),
  };
  std::vector<float> payload;
  for (std::size_t point = 0; point < axes[0].size(); ++point) {
    const float coordinate = static_cast<float>(axes[0].coordinate(point));
    payload.push_back(coordinate);
    payload.push_back(10.0f + 2.0f * coordinate);
  }

  const ndtbl::FieldGroup<1, float> group(
    ndtbl::Grid<1>(axes), { "A", "B" }, payload);
  const ndtbl::RuntimeFieldGroup<1> runtime(group);

  const std::size_t thread_count = 8;
  const std::size_t iterations = 1000;
  std::vector<int> failures(thread_count, 0);
  std::vector<std::thread> threads;

  for (std::size_t thread = 0; thread < thread_count; ++thread) {
    threads.push_back(std::thread([&, thread]() {
      std::array<double, 2> values = { 0.0, 0.0 };
      for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const double coordinate =
          static_cast<double>((thread + iteration) % 16);
        runtime.evaluate_all_linear_into({ coordinate }, values.data());
        if (values[0] != Catch::Approx(coordinate) ||
            values[1] != Catch::Approx(10.0 + 2.0 * coordinate)) {
          failures[thread] = 1;
          return;
        }
      }
    }));
  }

  for (std::size_t thread = 0; thread < threads.size(); ++thread) {
    threads[thread].join();
  }

  for (std::size_t thread = 0; thread < failures.size(); ++thread) {
    REQUIRE(failures[thread] == 0);
  }
}

TEST_CASE("typed loader rejects mismatched dimensions", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  REQUIRE_THROWS_AS(ndtbl::read_runtime_field_group<2>(path),
                    std::runtime_error);
  REQUIRE_THROWS_AS((ndtbl::read_field_group<2, double>(path)),
                    std::runtime_error);

  std::remove(path.c_str());
}

TEST_CASE("typed loader rejects truncated payload files", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  std::vector<char> bytes = ndtbl_test::read_file_bytes(path);
  REQUIRE(bytes.size() > 1);
  bytes.pop_back();
  ndtbl_test::write_file_bytes(path, bytes);

  REQUIRE_THROWS_AS(ndtbl::read_runtime_field_group<1>(path),
                    std::runtime_error);

  std::remove(path.c_str());
}

TEST_CASE("writer produces the documented little-endian layout", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(2.0, 2.0, 1),
  };
  const ndtbl::FieldGroup<1, float> group(
    ndtbl::Grid<1>(axes), { "A" }, { 1.5f });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  std::vector<char> expected;
  expected.insert(expected.end(),
                  { 'N', 'D', 'T', 'B', 'L', '\0', '\0', '\0' });
  ndtbl_test::append_uint_le<std::uint8_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint8_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint16_t>(expected, 0u);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 81u);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint8_t>(expected, 1u);
  ndtbl_test::append_uint_le<std::uint8_t>(expected, 0u);
  ndtbl_test::append_uint_le<std::uint16_t>(expected, 0u);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 1u);
  ndtbl_test::append_double_le(expected, 2.0);
  ndtbl_test::append_double_le(expected, 2.0);
  ndtbl_test::append_uint_le<std::uint64_t>(expected, 1u);
  expected.push_back('A');
  ndtbl_test::append_float_le(expected, 1.5f);

  REQUIRE(ndtbl_test::read_file_bytes(path) == expected);

  std::remove(path.c_str());
}

TEST_CASE("typed loader rejects nonzero reserved header fields", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  std::vector<char> bytes = ndtbl_test::read_file_bytes(path);
  REQUIRE(bytes.size() > 11);
  bytes[10] = 1;
  ndtbl_test::write_file_bytes(path, bytes);

  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(path), std::runtime_error);

  std::remove(path.c_str());
}

TEST_CASE("typed loader rejects mismatched payload offsets", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  std::vector<char> bytes = ndtbl_test::read_file_bytes(path);
  REQUIRE(bytes.size() > 19);
  for (std::size_t index = 12; index < 20; ++index) {
    bytes[index] = 0;
  }
  ndtbl_test::write_file_bytes(path, bytes);

  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(path), std::runtime_error);

  std::remove(path.c_str());
}
