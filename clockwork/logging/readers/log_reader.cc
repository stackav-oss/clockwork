// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/log_reader.hh"

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <stdexcept>
#include <utility>

namespace clockwork_logging
{

LogReader::Iterator::Iterator() = default;

LogReader::Iterator::Iterator(LogReader* reader)
  : reader_(reader)
{
}

LogReader::Iterator::Iterator(LogReader* reader, std::optional<LoggedMessage> msg)
  : reader_(reader), msg_(msg)
{
}

auto LogReader::Iterator::operator*() const -> reference
{
  if (!msg_)
  {
    throw std::runtime_error("Log reader iterator message is not valid");
  }
  return *msg_;
}

auto LogReader::Iterator::operator->() const -> pointer
{
  if (!msg_)
  {
    throw std::runtime_error("Log reader iterator message is not valid");
  }
  return &(*msg_);
}

auto LogReader::Iterator::operator++() -> Iterator&
{
  msg_ = reader_->next_message();
  return *this;
}

void LogReader::Iterator::operator++(int)
{
  ++(*this);
}

LogReader::LogReader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval)
  : reader_(make_reader(log_uri, maybe_log_interval, maybe_relative_interval))
{
}

LogReader::LogReader(std::unique_ptr<AbstractLogReader> reader)
  : reader_(std::move(reader))
{
}

LogReader::~LogReader()
{
  if (opened_)
  {
    if (auto ret = reader_->close(); !ret)
    {
      jewels::log_cerr_error("Failed to close the log reader: {}", ret.error());
    }
  }
}

std::string_view LogReader::log_uri() const
{
  return reader_->log_uri();
}

std::optional<LogInterval> LogReader::maybe_log_interval() const
{
  return reader_->maybe_log_interval();
}

std::optional<RelativeInterval> LogReader::maybe_relative_interval() const
{
  return reader_->maybe_relative_interval();
}

std::vector<std::string> LogReader::get_channels()
{
  return reader_->get_channels();
}

std::vector<TopicMetadata> LogReader::get_metadata()
{
  return reader_->get_metadata();
}

LogExpected<TopicMetadata> LogReader::get_channel_metadata(std::string_view channel_name)
{
  return reader_->get_channel_metadata(channel_name);
}

[[nodiscard]] LogExpected<LogMetrics> LogReader::get_metrics()
{
  return reader_->get_metrics();
}

LogExpected<LogTimestamp> LogReader::start_time()
{
  const auto& interval = maybe_log_interval();
  const auto& relative_interval = maybe_relative_interval();
  if (interval && relative_interval)
  {
    jewels::log_cerr_error("Using interval with relative interval is not supported");
    return jewels::unexpected(LogError::invalid_argument);
  }
  if (interval)
  {
    return interval->get_start_timestamp();
  }
  auto start_time_result = reader_->start_time();
  if (!start_time_result)
  {
    return jewels::unexpected(start_time_result.error());
  }
  if (relative_interval)
  {
    return start_time_result.value() + relative_interval->start_offset;
  }
  return start_time_result;
}

LogExpected<LogTimestamp> LogReader::end_time()
{
  const auto& interval = maybe_log_interval();
  const auto& relative_interval = maybe_relative_interval();
  if (interval && relative_interval)
  {
    jewels::log_cerr_error("Using interval with relative interval is not supported");
    return jewels::unexpected(LogError::invalid_argument);
  }
  if (interval)
  {
    return interval->get_end_timestamp();
  }
  auto end_time_result = reader_->end_time();
  if (!end_time_result)
  {
    return jewels::unexpected(end_time_result.error());
  }
  if (relative_interval)
  {
    return end_time_result.value() - relative_interval->end_offset;
  }
  return end_time_result;
}

[[nodiscard]] LogExpected<void> LogReader::open(
  const std::function<bool(std::string_view)>& topic_filter,
  const std::function<bool(std::string_view, uint32_t)>& sequence_number_filter)
{
  reader_->set_sequence_number_filter(sequence_number_filter);
  if (const auto open_result = reader_->open(topic_filter); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  opened_ = true;
  return {};
}

auto LogReader::begin() -> iterator
{
  if (!opened_)
  {
    return end();
  }

  if (auto msg = next_message())
  {
    return Iterator{this, msg};
  }

  return end();
}

auto LogReader::end() -> iterator
{
  return Iterator(this);
}

std::optional<LoggedMessage> LogReader::next_message()
{
  auto msg = reader_->next_message();
  if (msg)
  {
    if (auto metadata = get_channel_metadata(msg->topic))
    {
      msg->message_encoding = metadata->message_encoding;
    }
  }
  return msg;
}

} // namespace clockwork_logging
