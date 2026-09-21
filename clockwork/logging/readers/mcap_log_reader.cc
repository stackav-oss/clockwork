// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/readers/mcap_log_reader.hh"

#include "clockwork/logging/channel_type_clk_cc.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding_clk_cc.hh"
#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/schema_encoding_clk_cc.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <mcap/reader.hpp>
#include <mcap/types.hpp>
#include <wise_enum.h>

#include <algorithm>
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace clockwork_logging
{

McapLogReader::McapLogReader(
  std::string_view log_uri,
  std::optional<LogInterval> maybe_log_interval,
  std::optional<RelativeInterval> maybe_relative_interval)
  : AbstractLogReader(log_uri, maybe_log_interval, maybe_relative_interval),
    reader_(std::make_unique<mcap::McapReader>())
{
}

McapLogReader::~McapLogReader() = default;

std::string McapLogReader::type() const
{
  return "mcap";
}

LogExpected<void> McapLogReader::open(const std::function<bool(std::string_view)>& topic_filter)
{
  if (is_open_)
  {
    return jewels::unexpected(LogError::already_open);
  }

  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    return open_result;
  }

  topic_filter_ = topic_filter;
  is_open_ = true;
  return {};
}

LogExpected<void> McapLogReader::close()
{
  if (!is_open_)
  {
    return jewels::unexpected(LogError::not_open);
  }

  reader_->close();
  is_mcap_reader_open_ = false;
  is_open_ = false;
  return {};
}

std::vector<std::string> McapLogReader::get_channels()
{
  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    return {};
  }
  std::vector<std::string> channels;
  for (const auto& [_, channel] : reader_->channels())
  {
    channels.emplace_back(channel->topic);
  }

  std::ranges::sort(channels, [](const auto& lhs, const auto& rhs) { return lhs < rhs; });

  return channels;
}

std::vector<TopicMetadata> McapLogReader::get_metadata()
{
  load_metadata();
  if (!maybe_metadata_)
  {
    return {};
  }
  return maybe_metadata_.value();
}

[[nodiscard]] LogExpected<TopicMetadata> McapLogReader::get_channel_metadata(std::string_view channel_name)
{
  load_metadata();
  const auto map_iter = channel_metadata_map_.find(channel_name);
  if (map_iter == channel_metadata_map_.end())
  {
    return jewels::unexpected(LogError::unknown_channel);
  }
  return *map_iter->second;
}

[[nodiscard]] LogExpected<LogMetrics> McapLogReader::get_metrics()
{
  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  const auto& maybe_statistics = reader_->statistics();
  if (!maybe_statistics.has_value())
  {
    jewels::log_cerr_error("Failed to load statistics from {}", log_uri());
    return jewels::unexpected(LogError::failed_to_load_metrics);
  }
  const auto& statistics = maybe_statistics.value();
  LogMetrics log_metrics{
    .transmit_time_interval =
      {LogTimestamp{static_cast<int64_t>(statistics.messageStartTime)},
       LogTimestamp{static_cast<int64_t>(statistics.messageEndTime)}},
    .message_count = statistics.messageCount,
    .byte_count = 0U,
    .topic_metrics = {},
  };
  log_metrics.topic_metrics.reserve(statistics.channelMessageCounts.size());
  for (const auto& [channel_id, message_count] : statistics.channelMessageCounts)
  {
    const auto channel_ptr = reader_->channel(channel_id);
    if (!channel_ptr)
    {
      continue;
    }
    log_metrics.topic_metrics.emplace_back(channel_ptr->topic, LogInterval{}, message_count, 0U);
  }
  std::ranges::sort(
    log_metrics.topic_metrics,

    [](const auto& lhs, const auto& rhs) { return lhs.topic < rhs.topic; });
  return {std::move(log_metrics)};
}

LogExpected<LogTimestamp> McapLogReader::start_time()
{
  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  const auto& statistics = reader_->statistics();
  if (!statistics.has_value())
  {
    jewels::log_cerr_error("Failed to load statistics from {}", log_uri());
    return jewels::unexpected(LogError::failed_to_load_metrics);
  }
  return LogTimestamp{static_cast<int64_t>(statistics->messageStartTime)};
}

LogExpected<LogTimestamp> McapLogReader::end_time()
{
  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    return jewels::unexpected(open_result.error());
  }
  const auto& statistics = reader_->statistics();
  if (!statistics.has_value())
  {
    jewels::log_cerr_error("Failed to load statistics from {}", log_uri());
    return jewels::unexpected(LogError::failed_to_load_metrics);
  }
  return LogTimestamp{static_cast<int64_t>(statistics->messageEndTime)};
}

std::optional<LoggedMessage> McapLogReader::next_message_impl()
{
  if (!is_open_)
  {
    throw std::runtime_error("Attempted to read next message before opening the log.");
  }

  // Create the message iterator if this is the first call.
  if (!view_ || !msg_iter_)
  {
    // Create the message iterator.
    auto options = mcap::ReadMessageOptions();
    const auto& interval = maybe_log_interval();
    const auto& relative_interval = maybe_relative_interval();
    uint64_t start_time_ns{0};
    uint64_t end_time_ns{std::numeric_limits<int64_t>::max()};
    if (interval && relative_interval)
    {
      throw std::invalid_argument("Using interval with relative interval is not supported");
    }
    if (interval)
    {
      start_time_ns = static_cast<uint64_t>(interval->get_start_timestamp().get_nanoseconds());
      end_time_ns = static_cast<uint64_t>(interval->get_end_timestamp().get_nanoseconds());
    }
    if (relative_interval)
    {
      if (const auto maybe_statistics = reader_->statistics(); maybe_statistics)
      {
        start_time_ns =
          maybe_statistics->messageStartTime + static_cast<uint64_t>(relative_interval->start_offset.count());
        end_time_ns = maybe_statistics->messageStartTime + static_cast<uint64_t>(relative_interval->end_offset.count());
      }
      else
      {
        throw std::runtime_error("Failed to get statistics from MCAP reader, unable to use a relative interval");
      }
    }
    options.startTime = start_time_ns;
    options.endTime = end_time_ns;
    options.topicFilter = topic_filter_;
    options.readOrder = mcap::ReadMessageOptions::ReadOrder::LogTimeOrder;

    view_ = std::make_unique<mcap::LinearMessageView>(
      reader_->readMessages([this](const auto& status) { problem_callback(status); }, options));
    msg_iter_ = view_->begin();

    if (*msg_iter_ != view_->end())
    {
      return to_logged_message(**msg_iter_);
    }

    return {};
  }

  // If we are already at the end return nullopt.
  if (*msg_iter_ == view_->end())
  {
    return {};
  }

  // Increment the iterator and return the message if we are not at the end.
  ++(*msg_iter_);

  if (*msg_iter_ == view_->end())
  {
    return {};
  }

  return to_logged_message(**msg_iter_);
}

LogExpected<void> McapLogReader::open_mcap_reader()
{
  if (is_mcap_reader_open_)
  {
    return {};
  }

  // Open the log.
  if (const auto status = reader_->open(std::string{log_uri()}); !status.ok())
  {
    jewels::log_cerr_error("Failed to open {}: {}", log_uri(), status.message);
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }

  if (auto status = reader_->readSummary(
        mcap::ReadSummaryMethod::AllowFallbackScan, [this](const auto& problem) { problem_callback(problem); });
      !status.ok())
  {
    jewels::log_cerr_error("Failed to read the log summary: {}", log_uri(), status.message);
    return jewels::unexpected(LogError::failed_to_open_log_file);
  }

  is_mcap_reader_open_ = true;
  return {};
}

void McapLogReader::load_metadata()
{
  if (maybe_metadata_)
  {
    return;
  }
  if (const auto open_result = open_mcap_reader(); !open_result)
  {
    maybe_metadata_.emplace();
    return;
  }
  std::vector<TopicMetadata> topics;
  for (const auto& [_, channel] : reader_->channels())
  {
    const auto maybe_message_encoding = wise_enum::from_string<MessageEncoding>(channel->messageEncoding);
    decltype(wise_enum::from_string<SchemaEncoding>(std::string{})) maybe_schema_encoding;
    if (channel->schemaId != 0)
    {
      maybe_schema_encoding = wise_enum::from_string<SchemaEncoding>(reader_->schema(channel->schemaId)->encoding);
    }
    topics.emplace_back(
      TopicMetadata{
        .name = channel->topic,
        .type = (channel->schemaId == 0) ? "" : reader_->schema(channel->schemaId)->name,
        .message_encoding = maybe_message_encoding ? *maybe_message_encoding : MessageEncoding::undefined,
        .channel_type = ChannelType::regular,
        .schema_encoding = maybe_schema_encoding ? *maybe_schema_encoding : SchemaEncoding::undefined,
        .schema_definition = (channel->schemaId == 0) ? std::string{}
                                                      : std::string{nolint_helper::byte_span_to_string_view(
                                                          reader_->schema(channel->schemaId)->data)},
        .is_amended = false,
      });
  }
  std::ranges::sort(topics, [](const auto& lhs, const auto& rhs) { return lhs.name < rhs.name; });
  for (const auto& metadata : topics)
  {
    channel_metadata_map_.emplace(metadata.name, jewels::memory::make_non_null_from_ref(metadata));
  }
  maybe_metadata_.emplace(std::move(topics));
}

[[nodiscard]] MessageEncoding McapLogReader::get_channel_message_encoding(std::string_view channel)
{
  load_metadata();
  const auto map_iter = channel_metadata_map_.find(channel);
  if (map_iter == channel_metadata_map_.end())
  {
    return MessageEncoding::undefined;
  }
  return map_iter->second->message_encoding;
}

LoggedMessage McapLogReader::to_logged_message(const mcap::MessageView& view)
{
  return LoggedMessage{
    .topic = view.channel->topic,
    .sequence_number = view.message.sequence,
    .publish_time = LogTimestamp(static_cast<int64_t>(view.message.publishTime)),
    .log_time = LogTimestamp(static_cast<int64_t>(view.message.logTime)),
    .header = {},
    .data = std::span(view.message.data, view.message.dataSize),
    .is_repeated_persistent = false,
    .message_encoding = get_channel_message_encoding(view.channel->topic),
    .is_lite_compressed = false,
  };
}

void McapLogReader::problem_callback(const mcap::Status& /*status*/)
{
  // TODO(OI-2713): Propogate issues up through the interfaces.
}

} // namespace clockwork_logging
