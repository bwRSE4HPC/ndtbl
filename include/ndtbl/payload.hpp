// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ndtbl {

/**
 * @brief Lightweight read-only view over a contiguous ndtbl payload.
 *
 * The payload is addressed in logical `Stored` elements, but is stored as bytes
 * internally so the view remains valid for both aligned heap-backed storage and
 * potentially unaligned file-backed memory mappings. The view does not own the
 * referenced storage, which must remain alive for the lifetime of the view.
 *
 * @tparam Stored Trivially copyable scalar payload type addressed through the
 *                view.
 */
template<class Stored>
class PayloadView
{
  static_assert(std::is_trivially_copyable<Stored>::value,
                "ndtbl payload elements must be trivially copyable");

public:
  /**
   * @brief Construct an empty payload view.
   */
  PayloadView() = default;

  /**
   * @brief Construct a payload view from raw bytes.
   *
   * @param data Pointer to the first payload byte.
   * @param size Number of logical `Stored` entries in the payload.
   * @throws std::invalid_argument If `data` is null and `size` is nonzero.
   */
  PayloadView(const std::uint8_t* data, std::size_t size)
    : data_(data)
    , size_(size)
  {
    validate_data();
  }

  /**
   * @brief Construct a payload view from typed scalar storage.
   *
   * @param data Pointer to the first typed payload value.
   * @param size Number of logical `Stored` entries in the payload.
   * @throws std::invalid_argument If `data` is null and `size` is nonzero.
   */
  PayloadView(const Stored* data, std::size_t size)
    : data_(data == nullptr ? nullptr
                            : reinterpret_cast<const std::uint8_t*>(data))
    , typed_data_(data)
    , size_(size)
  {
    validate_data();
  }

  /**
   * @brief Return the number of logical scalar values in the payload.
   *
   * @return Element count.
   */
  std::size_t size() const { return size_; }

  /**
   * @brief Return the payload size in bytes.
   *
   * @return Number of occupied bytes.
   */
  std::size_t byte_size() const { return size_ * sizeof(Stored); }

  /**
   * @brief Return the underlying payload bytes.
   *
   * @return Pointer to the first stored byte, or `nullptr` for an empty view.
   */
  const std::uint8_t* byte_data() const { return data_; }

  /**
   * @brief Return typed payload storage when the view was built from it.
   *
   * @return Typed pointer, or `nullptr` when direct typed loads would not be
   *         well-defined.
   */
  const Stored* typed_data() const { return typed_data_; }

  /**
   * @brief Read one payload value by index with bounds checking.
   *
   * This uses `memcpy` rather than typed pointer dereferencing so the same
   * implementation stays valid for memory-mapped payloads whose file offset is
   * not guaranteed to satisfy `alignof(Stored)`.
   *
   * @param index Zero-based payload index.
   * @return Deserialized payload value.
   */
  Stored at(std::size_t index) const
  {
    if (index >= size_) {
      throw std::out_of_range("ndtbl payload index out of range");
    }

    return unchecked(index);
  }

  /**
   * @brief Read one payload value without bounds checking.
   *
   * @param index Zero-based payload index known to be valid.
   * @return Deserialized payload value.
   */
  Stored unchecked(std::size_t index) const
  {
    Stored value;
    std::memcpy(&value, data_ + index * sizeof(Stored), sizeof(Stored));
    return value;
  }

private:
  void validate_data() const
  {
    if (data_ == nullptr && size_ != 0) {
      throw std::invalid_argument("non-empty ndtbl payload has null data");
    }
  }

  const std::uint8_t* data_ = nullptr;
  const Stored* typed_data_ = nullptr;
  std::size_t size_ = 0;
};

/**
 * @brief Build a read-only payload view over an existing vector.
 *
 * @tparam Stored Scalar payload type stored in the vector.
 * @param values Contiguous payload storage to view.
 * @return Read-only view into `values`.
 *
 * The vector must remain alive and must not reallocate while the returned view
 * is in use. Rvalue vectors are rejected to prevent immediately dangling views.
 */
template<class Stored>
PayloadView<Stored>
payload_view(const std::vector<Stored>& values)
{
  const Stored* data = values.empty() ? nullptr : values.data();
  return PayloadView<Stored>(data, values.size());
}

template<class Stored>
PayloadView<Stored>
payload_view(const std::vector<Stored>&&) = delete;

} // namespace ndtbl
