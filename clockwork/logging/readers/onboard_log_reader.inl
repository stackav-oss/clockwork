// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/logging/readers/onboard_log_reader.hh"
#pragma once

#include "clockwork/logging/readers/onboard_log_reader.hh"

#include "clockwork/logging/decompress_option.hh"
#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/onboard/reader.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/readers/abstract_log_reader.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::detail
{

template <typename BufferedReaderType>
OnboardLogReaderImpl<BufferedReaderType>::OnboardLogReaderImpl(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option,
  std::shared_ptr<BufferedReaderType> buffered_reader)
  : AbstractLogReader(log_uri, maybe_log_interval, maybe_relative_interval),
    memory_resource_(std::pmr::new_delete_resource()),
    reader_(memory_resource_, log_uri, buffered_reader, onboard::MetadataMapOption::enable),
    decompress_option_(decompress_option),
    buffered_reader_(std::move(buffered_reader))
{
}

template <typename BufferedReaderType>
std::string OnboardLogReaderImpl<BufferedReaderType>::type() const
{
  return "onboard";
}

template <typename BufferedReaderType>
LogExpected<void>
OnboardLogReaderImpl<BufferedReaderType>::open(const std::function<bool(std::string_view)>& topic_filter)
{
  if (is_open_)
  {
    return jewels::unexpected(LogError::already_open);
  }
  load_topic_metadata();
  const auto& interval = maybe_log_interval();
  const auto& relative_interval = maybe_relative_interval();
  if (interval && relative_interval)
  {
    throw std::invalid_argument("Using interval with relative interval is not supported");
  }
  log_interval_ = {LogTimestamp{0}, LogTimestamp{std::numeric_limits<int64_t>::max()}};
  if (interval)
  {
    log_interval_ = *interval;
  }
  if (relative_interval)
  {
    log_interval_ = {
      first_message_timestamp_ + relative_interval->start_offset,
      relative_interval->end_offset == std::chrono::nanoseconds{std::numeric_limits<int64_t>::max()}
        ? LogTimestamp{std::numeric_limits<int64_t>::max()}
        : first_message_timestamp_ + relative_interval->end_offset};
  }
  topic_filter_ = topic_filter;
  if (const auto open_result = reader_.open(log_interval_, onboard::TimeFilterOption::message_time, decompress_option_);
      !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  is_open_ = true;
  return {};
}

template <typename BufferedReaderType>
LogExpected<void> OnboardLogReaderImpl<BufferedReaderType>::close()
{
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  reader_.close();
  topic_metadata_ptr_.reset();
  return {};
}

template <typename BufferedReaderType>
std::vector<std::string> OnboardLogReaderImpl<BufferedReaderType>::get_channels()
{
  load_topic_metadata();
  std::vector<std::string> channels;
  for (const auto& metadata : *topic_metadata_ptr_)
  {
    channels.emplace_back(metadata.name);
  }
  return channels;
}

template <typename BufferedReaderType>
std::vector<TopicMetadata> OnboardLogReaderImpl<BufferedReaderType>::get_metadata()
{
  load_topic_metadata();
  return *topic_metadata_ptr_;
}

template <typename BufferedReaderType>
[[nodiscard]] LogExpected<TopicMetadata>
OnboardLogReaderImpl<BufferedReaderType>::get_channel_metadata(std::string_view channel_name)
{
  load_topic_metadata();
  for (const auto& metadata : *topic_metadata_ptr_)
  {
    if (metadata.name == channel_name)
    {
      return metadata;
    }
  }
  return jewels::unexpected(LogError::unknown_channel);
}

template <typename BufferedReaderType>
LogExpected<LogTimestamp> OnboardLogReaderImpl<BufferedReaderType>::start_time()
{
  load_topic_metadata();
  return first_message_timestamp_;
}

template <typename BufferedReaderType>
LogExpected<LogTimestamp> OnboardLogReaderImpl<BufferedReaderType>::end_time()
{
  load_topic_metadata();
  return last_message_timestamp_;
}

template <typename BufferedReaderType>
std::optional<LoggedMessage> OnboardLogReaderImpl<BufferedReaderType>::next_message_impl()
{
  if (!reader_)
  {
    return std::nullopt;
  }
  while (true)
  {
    const auto read_result = reader_.read_next();
    if (!read_result)
    {
      break;
    }
    if (topic_filter_ && !topic_filter_(read_result->channel_name))
    {
      continue;
    }
    return LoggedMessage{
      .topic = read_result->channel_name,
      .sequence_number = read_result->sequence_number,
      .publish_time = read_result->message_time,
      .log_time = read_result->log_time,
      .header = read_result->header,
      .data = read_result->data,
      .is_repeated_persistent = read_result->message_type == onboard::LoggedMessageType::repeated_persistent,
      .message_encoding = read_result->message_encoding,
      .is_lite_compressed = read_result->is_lite_compressed,
    };
  }
  return std::nullopt;
}

template <typename BufferedReaderType>
std::optional<ZeroCopyLoggedMessage> OnboardLogReaderImpl<BufferedReaderType>::zero_copy_next_message_impl()
{
  if (!reader_)
  {
    return std::nullopt;
  }
  while (true)
  {
    const auto read_result = reader_.zero_copy_read_next();
    if (!read_result)
    {
      break;
    }
    if (topic_filter_ && !topic_filter_(read_result->channel_name))
    {
      continue;
    }
    return ZeroCopyLoggedMessage{
      .topic = read_result->channel_name,
      .sequence_number = read_result->sequence_number,
      .publish_time = read_result->message_time,
      .log_time = read_result->log_time,
      .header = read_result->header,
      .data = read_result->data,
      .is_repeated_persistent = read_result->message_type == onboard::LoggedMessageType::repeated_persistent,
      .message_encoding = read_result->message_encoding,
      .is_lite_compressed = read_result->is_lite_compressed,
    };
  }
  return std::nullopt;
}

template <typename BufferedReaderType>
void OnboardLogReaderImpl<BufferedReaderType>::load_topic_metadata()
{
  if (topic_metadata_ptr_)
  {
    return;
  }
  topic_metadata_ptr_ = std::make_unique<std::vector<TopicMetadata>>();
  const auto metadata_result = reader_.get_channel_metadata_map();
  if (!metadata_result)
  {
    return;
  }
  topic_metadata_ptr_->reserve(metadata_result.value().size());
  for (const auto& [channel_name, channel_metadata] : metadata_result.value())
  {
    topic_metadata_ptr_->emplace_back(
      TopicMetadata{
        .name = std::string{channel_metadata.channel_name},
        .type = std::string{channel_metadata.schema_name},
        .message_encoding = channel_metadata.message_encoding,
        .channel_type = channel_metadata.channel_type,
        .schema_encoding = channel_metadata.schema_encoding,
        .schema_definition = std::string(channel_metadata.schema_definition),
        .is_amended = false,
      });
  }
  std::sort(
    topic_metadata_ptr_->begin(),
    topic_metadata_ptr_->end(),
    [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  const auto list_result = buffered_reader_->list_log_files(log_uri());
  if (!list_result)
  {
    return;
  }
  for (const auto& log_file : list_result.value())
  {
    const auto time_range_result = onboard::Reader<BufferedReaderType>::get_file_log_interval(
      memory_resource_, log_file, onboard::TimeFilterOption::message_time, buffered_reader_);
    if (time_range_result)
    {
      if (first_message_timestamp_.get_nanoseconds() > 0)
      {
        first_message_timestamp_ = std::min(first_message_timestamp_, time_range_result->get_start_timestamp());
      }
      else
      {
        first_message_timestamp_ = time_range_result->get_start_timestamp();
      }

      last_message_timestamp_ = std::max(last_message_timestamp_, time_range_result->get_end_timestamp());
    }
  }
}

} // namespace clockwork_logging::detail
