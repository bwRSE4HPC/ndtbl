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
  bool available = false;
  std::size_t page_size = 0;
  std::size_t total_pages = 0;
  std::size_t resident_pages = 0;
  std::size_t resident_bytes = 0;
  double resident_fraction = 0.0;

  bool smaps_available = false;
  std::size_t smaps_mapping_bytes = 0;
  std::size_t smaps_rss_bytes = 0;
  std::size_t smaps_pss_bytes = 0;
  std::size_t smaps_shared_clean_bytes = 0;
  std::size_t smaps_shared_dirty_bytes = 0;
  std::size_t smaps_private_clean_bytes = 0;
  std::size_t smaps_private_dirty_bytes = 0;
};

} // namespace ndtbl
