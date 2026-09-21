// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/lite_compressor.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/message_chunk_reader.hh"
#include "clockwork/logging/offboard/types.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace clockwork_logging::offboard::tests
{

/// Message logged in a message chunk
struct TestLoggedMessage
{
  /// Channel name
  std::string_view channel_name;

  /// Sequence number
  uint32_t sequence_number{};

  /// Log timestamp
  LogTimestamp log_time{};

  /// Logged transmit timestamp
  LogTimestamp logged_transmit_time{};

  /// Expected transmit timestamp
  LogTimestamp expected_transmit_time{};

  /// Message header
  std::vector<std::byte> header;

  /// Message data
  std::span<const std::span<const std::byte>> data;

  /// Flag indicating a repeated persistent message from a prior log
  bool is_repeated_persistent{};

  /// Flag indicating a message is compressed with lite compression
  bool is_lite_compressed{};
};

/// Validate the contents of a message in a message chunk
/// @param[in] expected_message Expected message contents
/// @param[in] chunk Message chunk
/// @param[in] chunk_offset Message chunk offset
/// @return True iff the message matches the expected contents
[[nodiscard]] bool validate_message(const TestLoggedMessage& expected_message, LoggedMessage message);

/// Validate the contents of a message in a message chunk
/// @param[in] expected_message Expected message contents
/// @param[in] chunk Message chunk
/// @param[in] chunk_offset Message chunk offset
/// @return True iff the message matches the expected contents
[[nodiscard]] bool validate_message(const TestLoggedMessage& expected_message, ZeroCopyLoggedMessage message);

/// Check that the message reader returns the expected messages
/// @param[in] expected_message Expected message contents
/// @param[in] message_reader Message chunk reader
/// @return True iff the reader returns the expected messages
[[nodiscard]] bool
check_read_result(const std::vector<TestLoggedMessage>& expected_messages, MessageChunkReader& message_reader);

/// Compress a message with lite compression
/// @param[in] data Data to compress
/// @param[in,out] lite_compressor Lite compressor
/// @return Compressed data vector
[[nodiscard]] std::vector<std::byte> lite_compress(std::span<const std::byte> data, LiteCompressor& lite_compressor);

/// Compress a message with lite compression for zero copy writes
/// @param[in] data Data to compress
/// @param[in,out] lite_compressor Lite compressor
/// @return Compressed data spans
[[nodiscard]] std::span<const std::span<const std::byte>>
zero_copy_lite_compress(std::span<const std::byte> data, LiteCompressor& lite_compressor);

/// Utility to get the size of a span of spans
/// @param[in] data_spans Span of data spans
/// @return Total size of the data spans
[[nodiscard]] size_t spans_size(std::span<const std::span<const std::byte>> data_spans);

} // namespace clockwork_logging::offboard::tests
