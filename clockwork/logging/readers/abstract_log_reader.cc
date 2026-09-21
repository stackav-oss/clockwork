// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/abstract_log_reader.hh"

#include "jewels/std/expected.hh"

namespace clockwork_logging
{

AbstractLogReader::AbstractLogReader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval)
  : log_uri_(log_uri), maybe_log_interval_(maybe_log_interval), maybe_relative_interval_(maybe_relative_interval)
{
}

AbstractLogReader::~AbstractLogReader() = default;

[[nodiscard]] std::string_view AbstractLogReader::log_uri() const
{
  return log_uri_;
}

[[nodiscard]] std::optional<LogInterval> AbstractLogReader::maybe_log_interval() const
{
  return maybe_log_interval_;
}

[[nodiscard]] std::optional<RelativeInterval> AbstractLogReader::maybe_relative_interval() const
{
  return maybe_relative_interval_;
}

void AbstractLogReader::set_sequence_number_filter(
  const std::function<bool(std::string_view, uint32_t)>& sequence_number_filter)
{
  sequence_number_filter_ = sequence_number_filter;
}

LogExpected<LogMetrics> AbstractLogReader::get_metrics()
{
  return jewels::unexpected(LogError::not_implemented);
}

std::optional<LoggedMessage> AbstractLogReader::next_message()
{
  while (auto maybe_msg = next_message_impl())
  {
    if (!sequence_number_filter_ || sequence_number_filter_(maybe_msg->topic, maybe_msg->sequence_number))
    {
      return maybe_msg;
    }
  }
  return std::nullopt;
}

std::optional<ZeroCopyLoggedMessage> AbstractLogReader::zero_copy_next_message()
{
  while (auto maybe_msg = zero_copy_next_message_impl())
  {
    if (!sequence_number_filter_ || sequence_number_filter_(maybe_msg->topic, maybe_msg->sequence_number))
    {
      return maybe_msg;
    }
  }
  return std::nullopt;
}

std::optional<ZeroCopyLoggedMessage> AbstractLogReader::zero_copy_next_message_impl()
{
  const auto maybe_msg = next_message_impl();
  if (!maybe_msg)
  {
    return std::nullopt;
  }
  zero_copy_data_span_ = maybe_msg->data;
  return ZeroCopyLoggedMessage{
    .topic = maybe_msg->topic,
    .sequence_number = maybe_msg->sequence_number,
    .publish_time = maybe_msg->publish_time,
    .log_time = maybe_msg->log_time,
    .header = maybe_msg->header,
    .data = std::span{&zero_copy_data_span_, 1U},
    .is_repeated_persistent = maybe_msg->is_repeated_persistent,
    .message_encoding = maybe_msg->message_encoding,
    .is_lite_compressed = maybe_msg->is_lite_compressed,
  };
}

} // namespace clockwork_logging
