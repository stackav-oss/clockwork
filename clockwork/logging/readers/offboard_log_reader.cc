// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/offboard_log_reader.hh"

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/offboard/types.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory_resource>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging
{

namespace
{

/// Offboard log reader type
constexpr auto offboard_reader_type = "offboard";

} // namespace

OffboardLogReader::OffboardLogReader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval,
  DecompressOption decompress_option,
  std::shared_ptr<offboard::ChunkReaderWriterFactory<>> chunk_reader_factory)
  : AbstractLogReader(log_uri, maybe_log_interval, maybe_relative_interval),
    memory_resource_(std::pmr::new_delete_resource()),
    reader_(memory_resource_, log_uri, std::move(chunk_reader_factory)),
    decompress_option_(decompress_option)
{
}

OffboardLogReader::~OffboardLogReader() = default;

std::string OffboardLogReader::type() const
{
  return offboard_reader_type;
}

[[nodiscard]] LogExpected<void> OffboardLogReader::open(const std::function<bool(std::string_view)>& topic_filter)
{
  std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_desired_channels;
  if (topic_filter)
  {
    maybe_desired_channels.emplace(memory_resource_);
    const auto channels_result = reader_.get_channels();
    if (!channels_result)
    {
      return jewels::unexpected(channels_result.error());
    }
    for (const auto& channel_name : *channels_result.value())
    {
      if (topic_filter(channel_name))
      {
        maybe_desired_channels->insert(std::pmr::string{channel_name, memory_resource_});
      }
    }
  }
  auto maybe_log_interval = AbstractLogReader::maybe_log_interval();
  const auto maybe_relative_interval = AbstractLogReader::maybe_relative_interval();
  if (maybe_log_interval && maybe_relative_interval)
  {
    throw std::invalid_argument("Using interval with relative interval is not supported");
  }
  if (maybe_relative_interval)
  {
    const auto interval_result = reader_.get_log_interval();
    if (!interval_result)
    {
      return jewels::unexpected(interval_result.error());
    }
    maybe_log_interval.emplace(
      interval_result.value().get_start_timestamp() + maybe_relative_interval->start_offset,
      maybe_relative_interval->end_offset == std::chrono::nanoseconds(std::numeric_limits<int64_t>::max())
        ? LogTimestamp{std::numeric_limits<int64_t>::max()}
        : interval_result.value().get_start_timestamp() + maybe_relative_interval->end_offset);
  }
  return reader_.open(maybe_desired_channels, maybe_log_interval, decompress_option_);
}

LogExpected<void> OffboardLogReader::close()
{
  reader_.close();
  return {};
}

std::vector<std::string> OffboardLogReader::get_channels()
{
  const auto channels_result = reader_.get_channels();
  if (!channels_result)
  {
    return {};
  }
  std::vector<std::string> channels;
  channels.reserve(channels_result.value()->size());
  for (const auto& channel : *channels_result.value())
  {
    channels.emplace_back(channel);
  }
  std::ranges::sort(channels, [](const auto& lhs, const auto& rhs) { return lhs < rhs; });
  return channels;
}

std::vector<TopicMetadata> OffboardLogReader::get_metadata()
{
  const auto metadata_result = reader_.get_metadata();
  if (!metadata_result)
  {
    return {};
  }
  std::vector<TopicMetadata> topic_metadata;
  topic_metadata.reserve(metadata_result.value()->size());
  for (const auto& metadata : std::views::values(*metadata_result.value()))
  {
    topic_metadata.push_back(
      TopicMetadata{
        .name = std::string{metadata.channel_name},
        .type = std::string{metadata.schema_name},
        .message_encoding = metadata.message_encoding,
        .channel_type = metadata.channel_type,
        .schema_encoding = metadata.schema_encoding,
        .schema_definition = std::string{metadata.schema_definition},
        .is_amended = metadata.is_amended,
      });
  }
  std::ranges::sort(topic_metadata, [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  return topic_metadata;
}

[[nodiscard]] LogExpected<TopicMetadata> OffboardLogReader::get_channel_metadata(std::string_view channel_name)
{
  const auto metadata_result = reader_.get_channel_metadata(channel_name);
  if (!metadata_result)
  {
    return jewels::unexpected(metadata_result.error());
  }
  return TopicMetadata{
    .name = std::string{metadata_result->channel_name},
    .type = std::string{metadata_result->schema_name},
    .message_encoding = metadata_result->message_encoding,
    .channel_type = metadata_result->channel_type,
    .schema_encoding = metadata_result->schema_encoding,
    .schema_definition = std::string{metadata_result->schema_definition},
    .is_amended = metadata_result->is_amended,
  };
}

LogExpected<LogMetrics> OffboardLogReader::get_metrics()
{
  const auto metrics_result = reader_.get_metrics();
  if (!metrics_result)
  {
    return jewels::unexpected(metrics_result.error());
  }
  const auto& offboard_metrics = *metrics_result.value();
  LogMetrics log_metrics{
    .transmit_time_interval = offboard_metrics.transmit_time_interval,
    .message_count = offboard_metrics.message_count,
    .byte_count = offboard_metrics.byte_count,
    .topic_metrics = {},
  };
  log_metrics.topic_metrics.reserve(offboard_metrics.metrics_map.size());
  for (const auto& [channel_name, metrics] : offboard_metrics.metrics_map)
  {
    log_metrics.topic_metrics.push_back(
      LoggedTopicMetrics{
        .topic = std::string(channel_name),
        .transmit_time_interval = metrics.transmit_time_interval,
        .message_count = metrics.message_count,
        .byte_count = metrics.byte_count,
      });
  }
  std::ranges::sort(log_metrics.topic_metrics, [](const auto& lhs, const auto& rhs) { return lhs.topic < rhs.topic; });
  return {std::move(log_metrics)};
}

LogExpected<LogTimestamp> OffboardLogReader::start_time()
{
  const auto interval_result = reader_.get_log_interval();
  if (!interval_result)
  {
    return jewels::unexpected(interval_result.error());
  }
  return interval_result.value().get_start_timestamp();
}

LogExpected<LogTimestamp> OffboardLogReader::end_time()
{
  const auto interval_result = reader_.get_log_interval();
  if (!interval_result)
  {
    return jewels::unexpected(interval_result.error());
  }
  return interval_result.value().get_end_timestamp();
}

std::optional<LoggedMessage> OffboardLogReader::next_message_impl()
{
  if (!reader_)
  {
    return std::nullopt;
  }
  const auto read_result = reader_.read_next();
  if (!read_result)
  {
    return std::nullopt;
  }
  return LoggedMessage{
    .topic = read_result->channel_name,
    .sequence_number = read_result->sequence_number,
    .publish_time = read_result->transmit_time,
    .log_time = read_result->log_time,
    .header = read_result->header,
    .data = read_result->data,
    .is_repeated_persistent = read_result->is_repeated_persistent,
    .message_encoding = read_result->message_encoding,
    .is_lite_compressed = read_result->is_lite_compressed,
  };
}

} // namespace clockwork_logging
