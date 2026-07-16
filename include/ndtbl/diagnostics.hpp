// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

namespace ndtbl {

/**
 * @brief Page residency information for an ndtbl payload.
 *
 * The byte count is page-granular because OS residency is reported per page.
 */
struct residency_info
{
  bool available;
  std::size_t page_size;
  std::size_t total_pages;
  std::size_t resident_pages;
  std::size_t resident_bytes;
  double resident_fraction;
};

} // namespace ndtbl
