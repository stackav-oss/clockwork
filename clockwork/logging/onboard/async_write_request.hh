// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/aligner/aligner.hh"
#include "jewels/time/sync_time.hh"

#include <boost/container/static_vector.hpp>
#include <wise_enum.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <sys/uio.h>

namespace clockwork_logging::onboard
{

/// Asynchronous write request sent to a log file
/// @tparam Policy Async write request policy
template <typename Policy>
class AsyncWriteRequest
{
public:
  /// Logged data type
  WISE_ENUM_CLASS_MEMBER(
    (DataType, uint8_t),
    // Message data
    message,
    // Repeated persistent message data
    repeated_persistent,
    // Channel metadata
    metadata)

  /// Write buffer size
  static constexpr size_t buffer_size = Policy::buffer_size;

  /// Write buffer alignment
  static constexpr size_t alignment = Policy::alignment;

  /// Maximum number of message handles per write request
  static constexpr size_t max_message_handles = Policy::max_message_handles;

  /// Maximum number of buffers per write request
  static constexpr size_t max_buffers = Policy::max_buffers;

  /// Number of buffers reserved for aligning to the write alignment
  static constexpr size_t num_reserve_buffers = 1U;

  /// Maximum write size in bytes
  static constexpr size_t max_write_size = Policy::max_write_size;

  /// Maximum number of I/O vectors per request
  static constexpr size_t max_io_vectors =
    std::min(std::max(max_buffers, max_message_handles * 2U), static_cast<size_t>(UIO_MAXIOV));

  /// Number of I/O vectors reserved for aligning to the write alignment
  static constexpr size_t num_reserve_io_vectors = 2U;

  /// Message handle type
  using MessageHandleType = typename Policy::MessageHandleType;

  /// Message handles type
  using MessageHandlesType = boost::container::static_vector<MessageHandleType, max_message_handles>;

  /// Buffer reference type
  using BufferReferenceType = typename Policy::BufferPoolType::SharedReference;

  /// Alignment helper type
  using AlignerType = jewels::Aligner<alignment>;

  AsyncWriteRequest() = default;
  ~AsyncWriteRequest() = default;

  AsyncWriteRequest(const AsyncWriteRequest&) = delete;
  AsyncWriteRequest& operator=(const AsyncWriteRequest&) = delete;
  AsyncWriteRequest(AsyncWriteRequest&&) = delete;
  AsyncWriteRequest& operator=(AsyncWriteRequest&&) = delete;

  /// Add a buffer to the async write request, the current buffer will be zero filled.
  /// @param[in] buffer_reference Reference to the buffer to be added
  /// @return True if the buffer was added, false if the write request is full
  [[nodiscard]] bool add_buffer(BufferReferenceType buffer_reference);

  /// Get the I/O vector for the write request, any remaining space in the current buffer is padded with zeros
  [[nodiscard]] std::span<iovec> get_io_vector();

  /// Pad the current buffer so that the buffer will be aligned when we write it to disk
  void pad_for_write();

  /// Pad the current buffer so that the buffer will be aligned after writing the given number of bytes
  /// @param[in] byte_count Number of bytes to be written
  /// @return True if the buffer has been padded, false if the current buffer is full.
  [[nodiscard]] bool pad_for_alignment(size_t byte_count);

  /// Copy data into the current buffer
  /// @param[in] timestamp Data timestamp
  /// @param[in] data Data to be copied
  /// @param[in] data_type Data type
  /// @return Number of byte copied into the write request
  [[nodiscard]] size_t
  copy_data(jewels::time::SteadyTime timestamp, std::span<const std::byte> data, DataType data_type);

  /// Zero copy add data to the write request
  /// @param[in] timestamp Data timestamp
  /// @param[in] data Data to be added to the request
  /// @param[in] data_type Data type
  /// @param[in] message_handle Message handle used to ensure that the data is valid
  /// @return Number of bytes added to the write request
  /// @pre The data must be aligned to the write alignment (std::terminate on violation)
  [[nodiscard]] size_t zero_copy_data(
    jewels::time::SteadyTime timestamp,
    std::span<const std::byte> data,
    DataType data_type,
    MessageHandleType message_handle);

  /// Test whether the write request is full
  /// @return True iff the request is full
  [[nodiscard]] bool is_full() const;

  /// Get the total size of the write request
  [[nodiscard]] size_t get_write_size() const;

  /// Get the number of message data bytes in the write request
  [[nodiscard]] size_t get_message_data_size() const;

  /// Get the timestamp of the oldest data contained in the write request
  /// @return Oldest data timestamp or nullopt if not set
  [[nodiscard]] std::optional<jewels::time::SteadyTime> try_get_oldest_data_timestamp() const;

  /// Accessor for the message handles backing the request
  /// @return Message handles
  [[nodiscard]] const MessageHandlesType& message_handles() const;

private:
  /// Add the current buffer to the I/O vector
  void add_current_buffer_to_io_vector();

  /// Current buffer, valid when there is space remaining in the current buffer
  std::optional<std::span<std::byte>> maybe_current_buffer_;

  /// Offset into the current buffer
  size_t current_buffer_offset_{0U};

  /// Write request size in bytes
  size_t write_size_{0U};

  /// Number of message data bytes in the write request
  size_t message_data_size_{0U};

  /// Oldest timestamp of data stored in the write request
  std::optional<jewels::time::SteadyTime> maybe_oldest_data_timestamp_;

  /// Buffer references
  boost::container::static_vector<BufferReferenceType, max_buffers> buffer_references_;

  /// Array of message handles
  MessageHandlesType message_handles_{};

  /// I/O vectors
  boost::container::static_vector<iovec, max_io_vectors> io_vectors_;
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/async_write_request.inl"
