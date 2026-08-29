// SPDX-License-Identifier: MIT

#include "ndtbl/ndtbl.hpp"

#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if NDTBL_ENABLE_MMAP_LOCK && defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static_assert(std::is_base_of<std::runtime_error, ndtbl::Error>::value,
              "ndtbl errors must expose the standard runtime-error API");
static_assert(std::is_base_of<ndtbl::Error, ndtbl::FormatError>::value,
              "format errors must derive from the ndtbl error base");
static_assert(std::is_base_of<ndtbl::Error, ndtbl::IOError>::value,
              "I/O errors must derive from the ndtbl error base");
static_assert(std::is_base_of<ndtbl::Error, ndtbl::StateError>::value,
              "state errors must derive from the ndtbl error base");

#if NDTBL_ENABLE_MMAP_LOCK && defined(__linux__)

namespace {

std::size_t
locked_memory_kib()
{
  std::ifstream status("/proc/self/status");
  if (!status.is_open()) {
    throw std::runtime_error("failed to open /proc/self/status");
  }

  std::string line;
  while (std::getline(status, line)) {
    if (line.compare(0, 6, "VmLck:") != 0) {
      continue;
    }

    std::istringstream value_stream(line.substr(6));
    std::size_t value_kib = 0;
    if (!(value_stream >> value_kib)) {
      throw std::runtime_error("failed to parse VmLck");
    }
    return value_kib;
  }

  throw std::runtime_error("VmLck is missing from /proc/self/status");
}

} // namespace

#endif

TEST_CASE("ndtbl exceptions preserve messages and support base catches",
          "[exceptions]")
{
  REQUIRE_THROWS_AS([]() { throw ndtbl::FormatError("invalid table"); }(),
                    ndtbl::Error);
  REQUIRE_THROWS_AS([]() { throw ndtbl::IOError("failed read"); }(),
                    std::exception);

  const ndtbl::StateError error("empty group");
  REQUIRE(std::string(error.what()) == "empty group");
}

TEST_CASE("file and stream failures use ndtbl I/O errors", "[exceptions][io]")
{
  const std::string missing_path = ndtbl_test::temporary_path();
  std::remove(missing_path.c_str());
  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(missing_path), ndtbl::IOError);

  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });
  std::ostringstream failed_output;
  failed_output.setstate(std::ios::badbit);
  REQUIRE_THROWS_AS(ndtbl::write_group_stream(failed_output, group),
                    ndtbl::IOError);
}

TEST_CASE("empty runtime field groups use ndtbl state errors",
          "[exceptions][field_group]")
{
  const ndtbl::RuntimeFieldGroup<1> group;
  std::array<double, 1> values = { 0.0 };
  std::ostringstream output;

  REQUIRE_THROWS_AS(group.field_count(), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.value_type(), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.field_names(), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.axes(), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.field_index("A"), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.evaluate_all_linear_into({ 0.5 }, values.data()),
                    ndtbl::StateError);
  REQUIRE_THROWS_AS(group.evaluate_all_cubic_into({ 0.5 }, values.data()),
                    ndtbl::StateError);
  REQUIRE_THROWS_AS(group.payload_residency(), ndtbl::StateError);
  REQUIRE_THROWS_AS(group.write(output), ndtbl::StateError);
}

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
  REQUIRE(info.smaps_available);
  REQUIRE(info.smaps_locked_available);
  REQUIRE(info.smaps_vm_flags_available);
  REQUIRE(info.process_vmlck_available);
#if NDTBL_ENABLE_MMAP_LOCK
  REQUIRE(info.smaps_lock_enabled);
#else
  REQUIRE_FALSE(info.smaps_lock_enabled);
  REQUIRE_FALSE(info.smaps_lock_on_fault);
  REQUIRE(info.smaps_locked_bytes == 0);
#endif

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

#if NDTBL_ENABLE_MMAP

TEST_CASE("mmap loader rejects payload ranges whose end would overflow",
          "[io][mmap]")
{
  const std::string path = ndtbl_test::temporary_path();
  ndtbl_test::write_file_bytes(path, { 'x' });

  REQUIRE_THROWS_AS(ndtbl::detail::map_payload_bytes(
                      path, 1, std::numeric_limits<std::size_t>::max()),
                    ndtbl::FormatError);

  std::remove(path.c_str());
}

#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS

TEST_CASE("Linux proc memory diagnostic fields are parsed explicitly",
          "[io][mmap][diagnostics]")
{
  std::size_t value_bytes = 1;
  REQUIRE(ndtbl::detail::parse_proc_kib(
    "Locked:                0 kB", "Locked:", value_bytes));
  REQUIRE(value_bytes == 0);
  REQUIRE(ndtbl::detail::parse_proc_kib(
    "VmLck:\t      17 kB", "VmLck:", value_bytes));
  REQUIRE(value_bytes == 17 * 1024);
  REQUIRE_FALSE(
    ndtbl::detail::parse_proc_kib("Rss: 4 kB", "Locked:", value_bytes));

  bool lock_enabled = false;
  bool lock_on_fault = false;
  REQUIRE(ndtbl::detail::parse_smaps_vm_flags(
    "VmFlags: rd wr mr mw me lo lf ac sd", lock_enabled, lock_on_fault));
  REQUIRE(lock_enabled);
  REQUIRE(lock_on_fault);

  REQUIRE(ndtbl::detail::parse_smaps_vm_flags(
    "VmFlags: rd mr me lo sd", lock_enabled, lock_on_fault));
  REQUIRE(lock_enabled);
  REQUIRE_FALSE(lock_on_fault);

  REQUIRE(ndtbl::detail::parse_smaps_vm_flags(
    "VmFlags: rd mr me sd", lock_enabled, lock_on_fault));
  REQUIRE_FALSE(lock_enabled);
  REQUIRE_FALSE(lock_on_fault);
  REQUIRE_FALSE(ndtbl::detail::parse_smaps_vm_flags(
    "Locked: 4 kB", lock_enabled, lock_on_fault));
}

#endif

#if NDTBL_ENABLE_MMAP_LOCK && defined(__linux__)

TEST_CASE("mmap locking follows the configured population policy",
          "[io][mmap][lock]")
{
  const long page_size = sysconf(_SC_PAGESIZE);
  REQUIRE(page_size > 0);
  const auto mapping_length = static_cast<std::size_t>(page_size) * 2;
  void* const mapping = mmap(nullptr,
                             mapping_length,
                             PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1,
                             0);
  REQUIRE(mapping != MAP_FAILED);

  std::array<unsigned char, 2> resident = { 0, 0 };
  REQUIRE(mincore(mapping, mapping_length, resident.data()) == 0);
  REQUIRE((resident[0] & 1U) == 0U);
  REQUIRE((resident[1] & 1U) == 0U);

  REQUIRE(ndtbl::detail::lock_mapping_pages(mapping, mapping_length) == 0);
  REQUIRE(mincore(mapping, mapping_length, resident.data()) == 0);
#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
  ndtbl::residency_info lock_info =
    ndtbl::detail::query_residency(mapping, mapping_length);
  REQUIRE(lock_info.smaps_available);
  REQUIRE(lock_info.smaps_locked_available);
  REQUIRE(lock_info.smaps_vm_flags_available);
  REQUIRE(lock_info.smaps_lock_enabled);
  REQUIRE(lock_info.process_vmlck_available);
  REQUIRE(lock_info.process_vmlck_bytes >= mapping_length);
#endif
#if NDTBL_ENABLE_MMAP_POPULATE
  REQUIRE((resident[0] & 1U) != 0U);
  REQUIRE((resident[1] & 1U) != 0U);
#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
  REQUIRE_FALSE(lock_info.smaps_lock_on_fault);
  REQUIRE(lock_info.smaps_locked_bytes >= mapping_length);
#endif
#else
  REQUIRE((resident[0] & 1U) == 0U);
  REQUIRE((resident[1] & 1U) == 0U);
#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
  REQUIRE(lock_info.smaps_lock_on_fault);
  REQUIRE(lock_info.smaps_locked_bytes == 0);
#endif

  static_cast<volatile unsigned char*>(mapping)[0] = 1;
  REQUIRE(mincore(mapping, mapping_length, resident.data()) == 0);
  REQUIRE((resident[0] & 1U) != 0U);
  REQUIRE((resident[1] & 1U) == 0U);
#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
  lock_info = ndtbl::detail::query_residency(mapping, mapping_length);
  REQUIRE(lock_info.smaps_locked_bytes >= static_cast<std::size_t>(page_size));
  REQUIRE(lock_info.smaps_locked_bytes < mapping_length);
#endif
#endif

  REQUIRE(munmap(mapping, mapping_length) == 0);
}

TEST_CASE("mmap locking holds payload pages for the loaded group lifetime",
          "[io][mmap][lock]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 2.0, 4.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const std::size_t locked_before_kib = locked_memory_kib();
  {
    const ndtbl::FieldGroup<1, double> loaded =
      ndtbl::read_field_group<1, double>(path);
    std::array<double, 1> values = { 0.0 };
    loaded.evaluate_all_linear_into({ 0.5 }, values.data());

    REQUIRE(values[0] == Catch::Approx(3.0));
    REQUIRE(locked_memory_kib() > locked_before_kib);
  }
  REQUIRE(locked_memory_kib() == locked_before_kib);

  std::remove(path.c_str());
}

TEST_CASE("mmap locking reports memlock limit failures as I/O errors",
          "[io][mmap][lock]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 2.0, 4.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  const pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    struct rlimit limit;
    if (getrlimit(RLIMIT_MEMLOCK, &limit) != 0) {
      _exit(2);
    }
    limit.rlim_cur = 0;
    if (setrlimit(RLIMIT_MEMLOCK, &limit) != 0) {
      _exit(3);
    }

    try {
      const ndtbl::FieldGroup<1, double> loaded =
        ndtbl::read_field_group<1, double>(path);
      static_cast<void>(loaded);
    } catch (const ndtbl::IOError&) {
      _exit(0);
    } catch (...) {
      _exit(4);
    }
    _exit(5);
  }

  int child_status = 0;
  const pid_t waited_child = waitpid(child, &child_status, 0);
  std::remove(path.c_str());

  REQUIRE(waited_child == child);
  REQUIRE(WIFEXITED(child_status));
  REQUIRE(WEXITSTATUS(child_status) == 0);
}

#endif

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
                    ndtbl::FormatError);

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
    const auto coordinate = static_cast<float>(axes[0].coordinate(point));
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
        const auto coordinate = static_cast<double>((thread + iteration) % 16);
        runtime.evaluate_all_linear_into({ coordinate }, values.data());
        if (values[0] != Catch::Approx(coordinate) ||
            values[1] != Catch::Approx(10.0 + 2.0 * coordinate)) {
          failures[thread] = 1;
          return;
        }
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  for (const auto failure : failures) {
    REQUIRE(failure == 0);
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
                    ndtbl::FormatError);
  REQUIRE_THROWS_AS((ndtbl::read_field_group<2, double>(path)),
                    ndtbl::FormatError);

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
                    ndtbl::FormatError);

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

  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(path), ndtbl::FormatError);

  std::remove(path.c_str());
}

TEST_CASE("typed loader translates invalid file axes to format errors", "[io]")
{
  const std::array<ndtbl::Axis, 1> axes = {
    ndtbl::Axis::uniform(0.0, 1.0, 2),
  };
  const ndtbl::FieldGroup<1, double> group(
    ndtbl::Grid<1>(axes), { "A" }, { 0.0, 1.0 });

  const std::string path = ndtbl_test::temporary_path();
  ndtbl::write_group(path, group);

  std::vector<char> bytes = ndtbl_test::read_file_bytes(path);
  REQUIRE(bytes.size() > 55);
  for (std::size_t index = 48; index < 56; ++index) {
    bytes[index] = 0;
  }
  ndtbl_test::write_file_bytes(path, bytes);

  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(path), ndtbl::FormatError);

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

  REQUIRE_THROWS_AS(ndtbl::read_group_metadata(path), ndtbl::FormatError);

  std::remove(path.c_str());
}
