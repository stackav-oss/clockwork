// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/tests/support/test_support.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/types.hh"
#include "clockwork/logging/wrapping_counter.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <ranges>
#include <span>
#include <vector>

namespace clockwork_logging::offboard::tests
{

[[nodiscard]] bool validate_message(const TestLoggedMessage& expected_message, LoggedMessage message)
{
  return validate_message(
    expected_message,
    ZeroCopyLoggedMessage{
      .channel_name = message.channel_name,
      .sequence_number = message.sequence_number,
      .log_time = message.log_time,
      .transmit_time = message.transmit_time,
      .header = message.header,
      .data = std::span{&message.data, 1U},
      .is_repeated_persistent = message.is_repeated_persistent,
      .message_encoding = message.message_encoding,
      .is_lite_compressed = message.is_lite_compressed,
    });
}

[[nodiscard]] bool validate_message(const TestLoggedMessage& expected_message, ZeroCopyLoggedMessage message)
{
  if (message.sequence_number != expected_message.sequence_number)
  {
    jewels::log_cerr_error(
      "invalid sequence number: actual {}, expected {}", message.sequence_number, expected_message.sequence_number);
    return false;
  }
  if (message.log_time != expected_message.log_time)
  {
    jewels::log_cerr_error(
      "invalid log time: actual {}, expected {}",
      message.log_time.get_nanoseconds(),
      expected_message.log_time.get_nanoseconds());
    return false;
  }
  if (message.transmit_time != expected_message.expected_transmit_time)
  {
    jewels::log_cerr_error(
      "invalid transmit time: actual {}, expected {}",
      message.transmit_time.get_nanoseconds(),
      expected_message.expected_transmit_time.get_nanoseconds());
    return false;
  }
  if (message.is_repeated_persistent != expected_message.is_repeated_persistent)
  {
    jewels::log_cerr_error(
      "invalid is_repeated_persistent flag: actual {}, expected {}",
      message.is_repeated_persistent,
      expected_message.is_repeated_persistent);
    return false;
  }
  if (message.is_lite_compressed != expected_message.is_lite_compressed)
  {
    jewels::log_cerr_error(
      "invalid is_lite_compressed flag: actual {}, expected {}",
      message.is_lite_compressed,
      expected_message.is_lite_compressed);
    return false;
  }
  if (message.header.size() != expected_message.header.size())
  {
    jewels::log_cerr_error(
      "invalid header size: actual {}, expected {}", message.header.size(), expected_message.header.size());
    return false;
  }
  if (std::memcmp(message.header.data(), expected_message.header.data(), expected_message.header.size()) != 0)
  {
    jewels::log_cerr_error("message header mismatch");
    return false;
  }
  const auto data_size = std::accumulate(
    message.data.begin(), message.data.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  const auto expected_data_size = std::accumulate(
    expected_message.data.begin(),
    expected_message.data.end(),
    size_t{0U},
    [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
  if (data_size != expected_data_size)
  {
    jewels::log_cerr_error("invalid data size: actual {}, expected {}", data_size, expected_data_size);
    return false;
  }
  if (!std::ranges::equal(message.data | std::views::join, expected_message.data | std::views::join))
  {
    jewels::log_cerr_error("message data mismatch");
    return false;
  }
  return true;
}

[[nodiscard]] bool
check_read_result(const std::vector<TestLoggedMessage>& expected_messages, MessageChunkReader& message_reader)
{
  for (const auto& expected_message : expected_messages)
  {
    const auto message_result = message_reader.get_next_message_identifier();
    if (!message_result)
    {
      jewels::log_cerr_error("Failed to get next message identidier: {}", message_result.error());
      return false;
    }
    if (message_result->transmit_time != expected_message.logged_transmit_time)
    {
      jewels::log_cerr_error(
        "Next transmit time mismatch: actual: {}, expected: {}",
        message_result->transmit_time.get_nanoseconds(),
        expected_message.logged_transmit_time.get_nanoseconds());
    }
    if (message_result->sequence_number.value() != expected_message.sequence_number)
    {
      jewels::log_cerr_error(
        "Next sequence number mismatch: actual: {}, expected: {}",
        message_result->sequence_number.value(),
        expected_message.sequence_number);
    }
    const auto read_result = message_reader.read_next();
    if (!read_result)
    {
      jewels::log_cerr_error("Failed to read next message: {}", read_result.error());
      return false;
    }
    if (!validate_message(expected_message, read_result.value()))
    {
      return false;
    }
  }
  if (!message_reader.is_empty())
  {
    jewels::log_cerr_error("Unexpected messages at end of chunk");
    return false;
  }
  return true;
}

[[nodiscard]] std::vector<std::byte> lite_compress(std::span<const std::byte> data, LiteCompressor& lite_compressor)
{
  const auto data_spans = lite_compressor.compress(data);
  std::vector<std::byte> compressed_data;
  compressed_data.resize(
    std::accumulate(
      data_spans.begin(), data_spans.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); }));
  size_t bytes_copied = 0U;
  for (const auto data_span : data_spans)
  {
    std::memcpy(&compressed_data.at(bytes_copied), data_span.data(), data_span.size());
    bytes_copied += data_span.size();
  }
  return compressed_data;
}

[[nodiscard]] std::span<const std::span<const std::byte>>
zero_copy_lite_compress(std::span<const std::byte> data, LiteCompressor& lite_compressor)
{
  return lite_compressor.compress(data);
}

[[nodiscard]] size_t spans_size(std::span<const std::span<const std::byte>> data_spans)
{
  return std::accumulate(
    data_spans.begin(), data_spans.end(), size_t{0U}, [](size_t lhs, auto& rhs) { return lhs + rhs.size(); });
}

} // namespace clockwork_logging::offboard::tests
