// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/log_processor.hh"

#include <optional>
#include <ranges>
#include <utility>

namespace clockwork_logging
{

LogProcessor::LogProcessor(const LogReaderConfig& config)
  : log_uri_(config.uri),
    reader_(std::make_unique<LogReader>(config.uri, config.interval, config.relative_interval)),
    topic_filter_(config.topic_filter)
{
}

LogProcessor::LogProcessor(
  std::unique_ptr<AbstractLogReader> reader, std::function<bool(std::string_view)> topic_filter)
  : log_uri_(reader->log_uri()),
    reader_(std::make_unique<LogReader>(std::move(reader))),
    topic_filter_(std::move(topic_filter))
{
}

LogProcessor::~LogProcessor() = default;

LogProcessor& LogProcessor::add_raw_msg_callback(std::string_view topic, LoggedMessageCallback callback)
{
  callbacks_[std::string(topic)].push_back(std::move(callback));

  return *this;
}

bool LogProcessor::process()
{
  abort_flag_ = false;

  if (!current_message_iterator_)
  {
    const auto topic_filter = augment_topic_filter(topic_filter_, std::views::keys(callbacks_));
    if (const auto open_result = reader_->open(topic_filter); !open_result)
    {
      return false;
    }
    current_message_iterator_ = reader_->begin();
  }

  auto iterator = *current_message_iterator_;
  for (; iterator != reader_->end(); ++iterator)
  {
    const auto& msg = *iterator;
    if (auto iter = callbacks_.find(std::string(msg.topic)); iter != callbacks_.end())
    {
      for (auto& callback : iter->second)
      {
        callback(msg);
      }
    }

    if (abort_flag_)
    {
      ++iterator;
      break;
    }
  }
  current_message_iterator_ = iterator;

  return *current_message_iterator_ == reader_->end();
}

// Returns empty if done
std::optional<LoggedMessage> LogProcessor::next()
{
  if (!current_message_iterator_)
  {
    // Started reading
    const auto topic_filter = augment_topic_filter(topic_filter_, std::views::keys(callbacks_));
    if (const auto open_result = reader_->open(topic_filter); !open_result)
    {
      return {};
    }
    current_message_iterator_ = reader_->begin();

    // Delay moving the iterator to the following call so that the data is not invalidated
    return **current_message_iterator_;
  }

  if (*current_message_iterator_ == reader_->end())
  {
    return {};
  }
  ++*current_message_iterator_;
  if (*current_message_iterator_ == reader_->end())
  {
    return {};
  }

  // We keep reading
  // Delay moving the iterator to the following call so that the data is not invalidated
  return **current_message_iterator_;
}

std::vector<TopicMetadata> LogProcessor::topics()
{
  return reader_->get_metadata();
}

[[nodiscard]] jewels::expected<TopicMetadata, jewels::MonoError>
LogProcessor::try_get_topic_metadata(const std::string& topic)
{
  auto metadata_result = reader_->get_channel_metadata(topic);
  if (!metadata_result)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  return {std::move(metadata_result).value()};
}

void LogProcessor::abort()
{
  abort_flag_ = true;
}

[[nodiscard]] LogExpected<LogMetrics> LogProcessor::get_metrics()
{
  return reader_->get_metrics();
}

LogExpected<LogTimestamp> LogProcessor::start_time()
{
  return reader_->start_time();
}

LogExpected<LogTimestamp> LogProcessor::end_time()
{
  return reader_->end_time();
}

std::string_view LogProcessor::log_uri() const
{
  return log_uri_;
}

LogReader& LogProcessor::reader()
{
  return *reader_;
}

std::unordered_set<std::string>& LogProcessor::failed_topics()
{
  return failed_topics_;
}

} // namespace clockwork_logging
