#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ndtbl {

/**
 * @brief Lightweight read-only view over a contiguous ndtbl payload.
 *
 * The payload is addressed in logical `Stored` elements, but is stored as bytes
 * internally so the view remains valid for both aligned heap-backed storage and
 * potentially unaligned file-backed memory mappings.
 *
 * @tparam Stored Scalar payload type addressed through the view.
 */
template<class Stored>
class PayloadView
{
public:
  /**
   * @brief Construct an empty payload view.
   */
  PayloadView()
    : data_(nullptr)
    , size_(0)
  {
  }

  /**
   * @brief Construct a payload view from raw bytes.
   *
   * @param data Pointer to the first payload byte.
   * @param size Number of logical `Stored` entries in the payload.
   */
  PayloadView(const std::uint8_t* data, std::size_t size)
    : data_(data)
    , size_(size)
  {
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
   * @brief Read one payload value by index.
   *
   * This uses `memcpy` rather than typed pointer dereferencing so the same
   * implementation stays valid for memory-mapped payloads whose file offset is
   * not guaranteed to satisfy `alignof(Stored)`.
   *
   * @param index Zero-based payload index.
   * @return Deserialized payload value.
   */
  Stored operator[](std::size_t index) const
  {
    if (index >= size_) {
      throw std::out_of_range("ndtbl payload index out of range");
    }

    Stored value;
    std::memcpy(&value, data_ + index * sizeof(Stored), sizeof(Stored));
    return value;
  }

private:
  const std::uint8_t* data_;
  std::size_t size_;
};

/**
 * @brief Build a read-only payload view over an existing vector.
 *
 * @tparam Stored Scalar payload type stored in the vector.
 * @param values Contiguous payload storage to view.
 * @return Read-only view into `values`.
 */
template<class Stored>
inline PayloadView<Stored>
payload_view(const std::vector<Stored>& values)
{
  const std::uint8_t* data =
    values.empty() ? nullptr
                   : reinterpret_cast<const std::uint8_t*>(values.data());
  return PayloadView<Stored>(data, values.size());
}

} // namespace ndtbl
