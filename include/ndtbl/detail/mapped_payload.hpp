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

/**
 * @brief Return the default result used when residency diagnostics are
 * disabled.
 *
 * @return Residency information with all availability flags clear.
 */
inline residency_info
unavailable_residency()
{
  return residency_info{};
}

#if NDTBL_ENABLE_MMAP

/**
 * @brief Append the current errno description to an operation-specific
 * message.
 *
 * @param[in] prefix Description of the operation that failed.
 *
 * @return The supplied prefix followed by the current errno description.
 */
inline std::string
system_error_message(const std::string& prefix)
{
  return prefix + ": " + std::strerror(errno);
}

#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS

/**
 * @brief Parse the address range at the beginning of a Linux smaps mapping
 * header.
 *
 * @param[in] line Candidate line from `/proc/self/smaps`.
 * @param[out] start Receives the inclusive mapping start address.
 * @param[out] end Receives the exclusive mapping end address.
 *
 * @return True when the line begins with a valid address range.
 */
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

/**
 * @brief Parse a KiB-valued Linux proc field and convert it to bytes.
 *
 * A valid zero is distinguished from a missing or malformed field through the
 * return value. The output is left unchanged when parsing fails.
 *
 * @param[in] line Candidate line from a proc status file.
 * @param[in] key Field name including its trailing colon, for example `"Rss:"`.
 * @param[out] value_bytes Receives the converted byte count.
 *
 * @return True when the requested field was parsed without overflow.
 */
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

/**
 * @brief Extract the Linux mapping lock flags from an smaps `VmFlags` line.
 *
 * `lo` denotes a lock-enabled mapping and `lf` denotes lock-on-fault. A
 * matching `VmFlags` line without either token is still a successful parse.
 *
 * @param[in] line Candidate line from `/proc/self/smaps`.
 * @param[out] lock_enabled Receives whether the `lo` token is present.
 * @param[out] lock_on_fault Receives whether the `lf` token is present.
 *
 * @return True when @p line is a `VmFlags` line.
 */
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

/**
 * @brief Add mapping-scoped Linux smaps measurements for the supplied address.
 *
 * This locates the single virtual-memory mapping containing @p address and
 * records its size, RSS/PSS breakdown, locked bytes, and lock flags. Failure
 * to open smaps or locate the mapping is represented by availability fields;
 * it is not an error in the independent mincore query.
 *
 * @param[in] address Address whose containing mapping is inspected.
 * @param[in,out] info Residency result to extend with mapping information.
 */
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

/**
 * @brief Add the process-wide locked-memory total from `/proc/self/status`.
 *
 * `VmLck` includes all lock-enabled memory in the process, not only the table
 * mapping being inspected. Unavailable or malformed proc data leaves the
 * corresponding availability flag clear.
 *
 * @param[in,out] info Residency result to extend with process information.
 */
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

/**
 * @brief Query page residency and related Linux memory information for a byte
 * range.
 *
 * The range is expanded to whole pages for mincore. Mapping-scoped smaps data
 * and process-scoped VmLck data are then attached when their proc files are
 * available.
 *
 * @param[in] address First byte in the payload range.
 * @param[in] length Number of payload bytes to inspect.
 *
 * @return Page-, mapping-, and process-scoped residency information.
 *
 * @throws std::invalid_argument for a null nonempty range.
 * @throws std::overflow_error when page-range arithmetic would overflow.
 * @throws IOError when the page size or mincore query fails.
 */
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

/**
 * @brief Return an unavailable result in builds without mmap diagnostics.
 *
 * @param[in] address Unused payload address.
 * @param[in] length Unused payload length.
 *
 * @return Residency information with all availability flags clear.
 */
inline residency_info
query_residency(const void* address, std::size_t length)
{
  static_cast<void>(address);
  static_cast<void>(length);
  return unavailable_residency();
}

#endif

#if NDTBL_ENABLE_MMAP

/**
 * @brief RAII owner that unmaps a payload mapping when its shared lifetime
 * ends.
 */
class mapped_payload_owner
{
public:
  /**
   * @brief Take ownership of an existing mmap mapping.
   *
   * @param[in] mapping Start address returned by mmap.
   * @param[in] mapping_length Length of the complete mapping in bytes.
   */
  mapped_payload_owner(void* mapping, std::size_t mapping_length)
    : mapping_(mapping)
    , mapping_length_(mapping_length)
  {
  }

  mapped_payload_owner(const mapped_payload_owner&) = delete;
  mapped_payload_owner& operator=(const mapped_payload_owner&) = delete;

  /** @brief Unmap the owned range, if any. */
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

/**
 * @brief Apply the configured locking policy to a mapped payload.
 *
 * Linux lazy-loading builds use MLOCK_ONFAULT; eager Linux builds and other
 * POSIX platforms use mlock. The return value and errno follow the selected
 * system call.
 *
 * @param[in] mapping Start address of the mapping to lock.
 * @param[in] mapping_length Number of mapped bytes to lock.
 *
 * @return Zero on success and -1 on failure.
 */
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

/**
 * @brief Map a payload subrange and return shared ownership of its first byte.
 *
 * The file offset is rounded down to a page boundary for mmap. The returned
 * aliasing shared pointer retains the complete mapping through
 * mapped_payload_owner, while pointing past any alignment prefix. Optional
 * population and locking policies are applied before the pointer is returned.
 *
 * @param[in] path Path of the ndtbl file to map.
 * @param[in] payload_offset Byte offset of the payload within the file.
 * @param[in] payload_size Number of payload bytes to map.
 *
 * @return An empty pointer for an empty payload.
 *
 * @throws IOError for file, mapping, page-size, or locking failures.
 * @throws FormatError when the payload range exceeds the file.
 */
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

  if (status.st_size < 0) {
    close(fd);
    throw IOError("ndtbl input file reports a negative size");
  }

  const auto file_size = static_cast<std::uintmax_t>(status.st_size);
  const auto offset = static_cast<std::uintmax_t>(payload_offset);
  const auto size = static_cast<std::uintmax_t>(payload_size);
  if (offset > file_size || size > file_size - offset) {
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
  if (payload_size > std::numeric_limits<std::size_t>::max() - delta) {
    close(fd);
    throw FormatError("ndtbl payload mapping range exceeds supported size");
  }
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
