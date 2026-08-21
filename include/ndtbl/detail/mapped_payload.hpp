// SPDX-License-Identifier: MIT

#pragma once

#ifndef NDTBL_ENABLE_MMAP
#define NDTBL_ENABLE_MMAP 0
#endif

#ifndef NDTBL_ENABLE_MMAP_POPULATE
#define NDTBL_ENABLE_MMAP_POPULATE 0
#endif

#ifndef NDTBL_ENABLE_MMAP_LOCK
#define NDTBL_ENABLE_MMAP_LOCK 0
#endif

#ifndef NDTBL_ENABLE_MMAP_DIAGNOSTICS
#define NDTBL_ENABLE_MMAP_DIAGNOSTICS 0
#endif

#include "ndtbl/diagnostics.hpp"
#include "ndtbl/exceptions.hpp"

#include <cstddef>

#if NDTBL_ENABLE_MMAP
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>
#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
#if !NDTBL_ENABLE_MMAP
#error "NDTBL_ENABLE_MMAP_DIAGNOSTICS requires NDTBL_ENABLE_MMAP"
#endif
#endif

#if NDTBL_ENABLE_MMAP_LOCK
#if !NDTBL_ENABLE_MMAP
#error "NDTBL_ENABLE_MMAP_LOCK requires NDTBL_ENABLE_MMAP"
#endif
#endif

#if NDTBL_ENABLE_MMAP_POPULATE
#if !NDTBL_ENABLE_MMAP
#error "NDTBL_ENABLE_MMAP_POPULATE requires NDTBL_ENABLE_MMAP"
#endif
#if !defined(__linux__)
#error "NDTBL_ENABLE_MMAP_POPULATE requires Linux"
#endif
#if !defined(MAP_POPULATE)
#error "NDTBL_ENABLE_MMAP_POPULATE requires MAP_POPULATE"
#endif
#endif

namespace ndtbl {
namespace detail {

inline residency_info
unavailable_residency()
{
  return residency_info{};
}

#if NDTBL_ENABLE_MMAP

inline std::string
system_error_message(const std::string& prefix)
{
  return prefix + ": " + std::strerror(errno);
}

#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS

inline bool
parse_smaps_header(const std::string& line,
                   std::uintptr_t& start,
                   std::uintptr_t& end)
{
  unsigned long long parsed_start = 0;
  unsigned long long parsed_end = 0;
  if (std::sscanf(line.c_str(), "%llx-%llx", &parsed_start, &parsed_end) != 2) {
    return false;
  }

  start = static_cast<std::uintptr_t>(parsed_start);
  end = static_cast<std::uintptr_t>(parsed_end);
  return true;
}

inline bool
parse_proc_kib(const std::string& line,
               const char* key,
               std::size_t& value_bytes)
{
  const std::string key_string(key);
  if (line.compare(0, key_string.size(), key_string) != 0) {
    return false;
  }

  std::istringstream is(line.substr(key_string.size()));
  std::size_t value_kib = 0;
  if (!(is >> value_kib) ||
      value_kib > std::numeric_limits<std::size_t>::max() / 1024) {
    return false;
  }

  value_bytes = value_kib * 1024;
  return true;
}

inline bool
parse_smaps_vm_flags(const std::string& line,
                     bool& lock_enabled,
                     bool& lock_on_fault)
{
  const std::string key("VmFlags:");
  if (line.compare(0, key.size(), key) != 0) {
    return false;
  }

  lock_enabled = false;
  lock_on_fault = false;
  std::istringstream flags(line.substr(key.size()));
  std::string flag;
  while (flags >> flag) {
    lock_enabled = lock_enabled || flag == "lo";
    lock_on_fault = lock_on_fault || flag == "lf";
  }
  return true;
}

inline void
query_smaps_mapping(const void* address, residency_info& info)
{
  std::ifstream smaps("/proc/self/smaps");
  if (!smaps.is_open()) {
    return;
  }

  const std::uintptr_t target = reinterpret_cast<std::uintptr_t>(address);
  std::uintptr_t mapping_start = 0;
  std::uintptr_t mapping_end = 0;
  bool in_target_mapping = false;
  std::string line;

  while (std::getline(smaps, line)) {
    std::uintptr_t header_start = 0;
    std::uintptr_t header_end = 0;
    if (parse_smaps_header(line, header_start, header_end)) {
      if (in_target_mapping) {
        return;
      }

      in_target_mapping = header_start <= target && target < header_end;
      if (in_target_mapping) {
        mapping_start = header_start;
        mapping_end = header_end;
        info.smaps_available = true;
        info.smaps_mapping_bytes =
          static_cast<std::size_t>(mapping_end - mapping_start);
      }
      continue;
    }

    if (!in_target_mapping) {
      continue;
    }

    std::size_t value_bytes = 0;
    if (parse_proc_kib(line, "Rss:", value_bytes)) {
      info.smaps_rss_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Pss:", value_bytes)) {
      info.smaps_pss_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Shared_Clean:", value_bytes)) {
      info.smaps_shared_clean_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Shared_Dirty:", value_bytes)) {
      info.smaps_shared_dirty_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Private_Clean:", value_bytes)) {
      info.smaps_private_clean_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Private_Dirty:", value_bytes)) {
      info.smaps_private_dirty_bytes = value_bytes;
      continue;
    }

    if (parse_proc_kib(line, "Locked:", value_bytes)) {
      info.smaps_locked_available = true;
      info.smaps_locked_bytes = value_bytes;
      continue;
    }

    bool lock_enabled = false;
    bool lock_on_fault = false;
    if (parse_smaps_vm_flags(line, lock_enabled, lock_on_fault)) {
      info.smaps_vm_flags_available = true;
      info.smaps_lock_enabled = lock_enabled;
      info.smaps_lock_on_fault = lock_on_fault;
      continue;
    }
  }
}

inline void
query_process_vmlck(residency_info& info)
{
  std::ifstream status("/proc/self/status");
  if (!status.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(status, line)) {
    std::size_t value_bytes = 0;
    if (parse_proc_kib(line, "VmLck:", value_bytes)) {
      info.process_vmlck_available = true;
      info.process_vmlck_bytes = value_bytes;
      return;
    }
  }
}

inline residency_info
query_residency(const void* address, std::size_t length)
{
  if (length == 0) {
    residency_info info;
    info.available = true;
    return info;
  }
  if (address == nullptr) {
    throw std::invalid_argument("cannot query residency for a null payload");
  }

  const long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    throw IOError("failed to query system page size for mincore");
  }

  const auto page_size = static_cast<std::size_t>(page_size_long);
  const auto addr = reinterpret_cast<std::uintptr_t>(address);
  const std::uintptr_t aligned_addr = addr - (addr % page_size);
  const auto delta = static_cast<std::size_t>(addr - aligned_addr);
  if (length > std::numeric_limits<std::size_t>::max() - delta) {
    throw std::overflow_error("payload residency range is too large");
  }
  const std::size_t aligned_length = delta + length;
  const std::size_t total_pages =
    aligned_length / page_size + (aligned_length % page_size == 0 ? 0 : 1);
  if (total_pages > std::numeric_limits<std::size_t>::max() / page_size) {
    throw std::overflow_error("payload residency page range is too large");
  }

  std::vector<unsigned char> vec(total_pages);
  if (mincore(reinterpret_cast<void*>(aligned_addr),
              total_pages * page_size,
              vec.data()) != 0) {
    throw IOError(system_error_message("mincore failed"));
  }

  std::size_t resident_pages = 0;
  for (const auto page : vec) {
    if ((page & 1U) != 0U) {
      ++resident_pages;
    }
  }

  residency_info info;
  info.available = true;
  info.page_size = page_size;
  info.total_pages = total_pages;
  info.resident_pages = resident_pages;
  info.resident_bytes = resident_pages * page_size;
  info.resident_fraction =
    static_cast<double>(resident_pages) / static_cast<double>(total_pages);

  query_smaps_mapping(address, info);
  query_process_vmlck(info);

  return info;
}

#else

inline residency_info
query_residency(const void*, std::size_t)
{
  return unavailable_residency();
}

#endif

#if NDTBL_ENABLE_MMAP

class mapped_payload_owner
{
public:
  mapped_payload_owner(void* mapping, std::size_t mapping_length)
    : mapping_(mapping)
    , mapping_length_(mapping_length)
  {
  }

  mapped_payload_owner(const mapped_payload_owner&) = delete;
  mapped_payload_owner& operator=(const mapped_payload_owner&) = delete;

  ~mapped_payload_owner()
  {
    if (mapping_ != nullptr && mapping_length_ != 0) {
      munmap(mapping_, mapping_length_);
    }
  }

private:
  void* mapping_;
  std::size_t mapping_length_;
};

#if NDTBL_ENABLE_MMAP_LOCK

inline int
lock_mapping_pages(const void* mapping, std::size_t mapping_length)
{
#if defined(__linux__) && !NDTBL_ENABLE_MMAP_POPULATE
  return mlock2(mapping, mapping_length, MLOCK_ONFAULT);
#else
  return mlock(mapping, mapping_length);
#endif
}

#endif

inline std::shared_ptr<const std::uint8_t>
map_payload_bytes(const std::string& path,
                  std::size_t payload_offset,
                  std::size_t payload_size)
{
  if (payload_size == 0) {
    return std::shared_ptr<const std::uint8_t>();
  }

  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw IOError(
      system_error_message("failed to open ndtbl input file for mmap"));
  }

  struct stat status;
  if (fstat(fd, &status) != 0) {
    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    throw IOError(
      system_error_message("failed to stat ndtbl input file for mmap"));
  }

  const auto file_size = static_cast<std::uintmax_t>(status.st_size);
  const auto payload_end =
    static_cast<std::uintmax_t>(payload_offset) + payload_size;
  if (payload_end > file_size) {
    close(fd);
    throw FormatError("ndtbl file payload exceeds file size");
  }

  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    close(fd);
    throw IOError("failed to query system page size for mmap");
  }

  const auto alignment = static_cast<std::size_t>(page_size);
  const std::size_t aligned_offset =
    payload_offset - (payload_offset % alignment);
  const std::size_t delta = payload_offset - aligned_offset;
  const std::size_t mapping_length = delta + payload_size;

  int mapping_flags = MAP_PRIVATE;
#if NDTBL_ENABLE_MMAP_POPULATE
  mapping_flags |= MAP_POPULATE;
#endif

  void* const mapping =
    mmap(nullptr, mapping_length, PROT_READ, mapping_flags, fd, aligned_offset);
  const int saved_errno = errno;
  close(fd);
  if (mapping == MAP_FAILED) {
    errno = saved_errno;
    throw IOError(system_error_message("failed to map ndtbl payload"));
  }

#if NDTBL_ENABLE_MMAP_LOCK
  if (lock_mapping_pages(mapping, mapping_length) != 0) {
    const int saved_lock_errno = errno;
    munmap(mapping, mapping_length);
    errno = saved_lock_errno;
    throw IOError(system_error_message("failed to lock ndtbl payload"));
  }
#endif

  const auto owner =
    std::make_shared<mapped_payload_owner>(mapping, mapping_length);
  const auto* const data =
    reinterpret_cast<const std::uint8_t*>(mapping) + delta;
  return std::shared_ptr<const std::uint8_t>(owner, data);
}

#endif

} // namespace detail
} // namespace ndtbl
