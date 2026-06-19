#pragma once

#ifndef NDTBL_ENABLE_MMAP
#define NDTBL_ENABLE_MMAP 0
#endif

#ifndef NDTBL_ENABLE_MMAP_POPULATE
#define NDTBL_ENABLE_MMAP_POPULATE 0
#endif

#ifndef NDTBL_ENABLE_MMAP_DIAGNOSTICS
#define NDTBL_ENABLE_MMAP_DIAGNOSTICS 0
#endif

#include "ndtbl/diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if NDTBL_ENABLE_MMAP || NDTBL_ENABLE_MMAP_DIAGNOSTICS
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS
#if !NDTBL_ENABLE_MMAP
#error "NDTBL_ENABLE_MMAP_DIAGNOSTICS requires NDTBL_ENABLE_MMAP"
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
  return residency_info{ false, 0, 0, 0, 0, 0.0 };
}

#if NDTBL_ENABLE_MMAP || NDTBL_ENABLE_MMAP_DIAGNOSTICS

inline std::string
system_error_message(const std::string& prefix)
{
  return prefix + ": " + std::strerror(errno);
}

#endif

#if NDTBL_ENABLE_MMAP_DIAGNOSTICS

inline residency_info
query_residency(const void* address, std::size_t length)
{
  if (length == 0) {
    return residency_info{ true, 0, 0, 0, 0, 0.0 };
  }
  if (address == nullptr) {
    throw std::invalid_argument("cannot query residency for a null payload");
  }

  const long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    throw std::runtime_error("failed to query system page size for mincore");
  }

  const std::size_t page_size = static_cast<std::size_t>(page_size_long);
  const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(address);
  const std::uintptr_t aligned_addr = addr - (addr % page_size);
  const std::size_t delta = static_cast<std::size_t>(addr - aligned_addr);
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
    throw std::runtime_error(system_error_message("mincore failed"));
  }

  std::size_t resident_pages = 0;
  for (std::size_t index = 0; index < vec.size(); ++index) {
    if ((vec[index] & 1U) != 0U) {
      ++resident_pages;
    }
  }

  return residency_info{ true,
                         page_size,
                         total_pages,
                         resident_pages,
                         resident_pages * page_size,
                         static_cast<double>(resident_pages) /
                           static_cast<double>(total_pages) };
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
    throw std::runtime_error(
      system_error_message("failed to open ndtbl input file for mmap"));
  }

  struct stat status;
  if (fstat(fd, &status) != 0) {
    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    throw std::runtime_error(
      system_error_message("failed to stat ndtbl input file for mmap"));
  }

  const std::uintmax_t file_size = static_cast<std::uintmax_t>(status.st_size);
  const std::uintmax_t payload_end =
    static_cast<std::uintmax_t>(payload_offset) + payload_size;
  if (payload_end > file_size) {
    close(fd);
    throw std::runtime_error("ndtbl file payload exceeds file size");
  }

  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    close(fd);
    throw std::runtime_error("failed to query system page size for mmap");
  }

  const std::size_t alignment = static_cast<std::size_t>(page_size);
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
    throw std::runtime_error(
      system_error_message("failed to map ndtbl payload"));
  }

  const std::shared_ptr<mapped_payload_owner> owner =
    std::make_shared<mapped_payload_owner>(mapping, mapping_length);
  const std::uint8_t* const data =
    reinterpret_cast<const std::uint8_t*>(mapping) + delta;
  return std::shared_ptr<const std::uint8_t>(owner, data);
}

#endif

} // namespace detail
} // namespace ndtbl
