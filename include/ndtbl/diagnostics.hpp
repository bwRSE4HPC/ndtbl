// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

namespace ndtbl {

/**
 * @brief Page residency information for an ndtbl payload.
 *
 * The mincore fields describe the queried payload range. The smaps fields
 * describe the Linux virtual-memory mapping that contains the payload address,
 * and the process VmLck fields describe all locked memory in the process.
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

  bool smaps_locked_available = false;
  std::size_t smaps_locked_bytes = 0;

  bool smaps_vm_flags_available = false;
  bool smaps_lock_enabled = false;
  bool smaps_lock_on_fault = false;

  bool process_vmlck_available = false;
  std::size_t process_vmlck_bytes = 0;
};

} // namespace ndtbl
