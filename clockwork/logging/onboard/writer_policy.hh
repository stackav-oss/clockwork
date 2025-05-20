// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/onboard/async_write_request.hh"
#include "clockwork/logging/onboard/async_writer.hh"
#include "clockwork/logging/onboard/clockwork_message_handle.hh"
#include "jewels/math/constants.hh"
#include "jewels/shared_pool/shared_buffer_pool.hh"
#include "jewels/shared_pool/shared_object_pool.hh"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace clockwork_logging::onboard
{

namespace detail
{

/// Default buffer size in bytes
constexpr size_t default_buffer_size = 256U * jewels::math::constants::bytes_per_kib<size_t>;

/// Default buffer alignment in bytes
constexpr size_t default_buffer_alignment = 2048U;

} // namespace detail

/// Onboard writer policy
/// @tparam MessageHandle Message handle type
/// @tparam BufferPoolT Buffer pool type
template <
  typename MessageHandle,
  typename BufferPoolT = jewels::SharedBufferPool<detail::default_buffer_size, detail::default_buffer_alignment>>
struct WriterPolicy
{
  /// Maximum write size in bytes
  static constexpr size_t max_write_size = 8U * jewels::math::constants::bytes_per_mib<size_t>;

  /// Buffer poll type
  using BufferPoolType = BufferPoolT;

  /// Data buffer size
  static constexpr size_t buffer_size = BufferPoolType::buffer_size;

  /// Data buffer alignment
  static constexpr size_t alignment = BufferPoolType::buffer_alignment;

  /// Minimum zero copy message size
  static constexpr size_t min_zero_copy_message_size =
    std::min(8U * jewels::math::constants::bytes_per_kib<size_t>, BufferPoolType::buffer_alignment);

  /// Maximum number of message handles per write request
  static constexpr size_t max_message_handles = max_write_size / min_zero_copy_message_size;

  /// Maximum number of buffers per write request
  static constexpr size_t max_buffers = max_write_size / buffer_size;

  /// I/O ring size
  static constexpr uint32_t io_ring_size = 512U;

  /// Maximum outstanding async operations
  static constexpr size_t max_async_requests = 1024U;

  /// Maximum time to hold data in buffers before writing to the log
  static constexpr auto flush_interval = std::chrono::milliseconds(100);

  /// Buffer space to reserve for writing channel and schema metadata to the log in MiB
  static constexpr size_t schema_reserve_mib = 8U;

  /// Buffer space to reserve for writing persistent messages to the log in MiB
  static constexpr size_t persistent_message_reserve_mib = 64U;

  /// Maximum write backlog to accept a new message into the log
  static constexpr auto max_write_backlog = std::chrono::seconds(5);

  /// Maximum log file size
  static constexpr size_t max_log_file_size = 2U * jewels::math::constants::bytes_per_gib<size_t>;

  /// Message handle type
  using MessageHandleType = MessageHandle;

  /// Buffer handle type
  using BufferReferenceType = BufferPoolType::SharedReference;

  /// Async write request type
  using AsyncWriteRequestType = onboard::AsyncWriteRequest<WriterPolicy>;

  /// Async write request handle type
  using AsyncWriteRequestHandleType = typename jewels::SharedObjectPool<AsyncWriteRequestType>::SharedReference;

  /// Async writer type
  using AsyncWriterType = onboard::AsyncWriter<WriterPolicy>;
};

} // namespace clockwork_logging::onboard
