// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_playback/log_message_fetcher.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/log_writer_config.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <fmt10/format.h>

#include <cstddef>
#include <exception>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace clockwork_logging
{
LogMessageFetcher::LogMessageFetcher(
  std::string_view log_uri,
  jewels::memory::ObjectPtr<const LogWriterConfigTap> log_publisher_config,
  LogInterval log_interval,
  jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    log_publisher_config_(std::move(log_publisher_config)),
    log_uri_(log_uri),
    log_interval_(log_interval)
{
}

std::optional<::clockwork::MultiMessageInfoData> LogMessageFetcher::try_fetch_message()
{
  auto logged_message = reader_->next_message();
  if (!logged_message)
  {
    return std::nullopt;
  }
  if (logged_message->message_encoding != MessageEncoding::tachyon)
  {
    throw std::runtime_error(fmt::format(
      "Attempted to read log data encoding '{}'. Only 'tachyon' log data is supported.",
      logged_message->message_encoding));
  }
  return clockwork::MultiMessageInfoData{
    .time_to_publish = logged_message->publish_time.get_time(),
    .msgs =
      {{std::pmr::vector<std::byte>{logged_message->data.begin(), logged_message->data.end(), memory_resource_}},
       memory_resource_},
    .channel = std::pmr::string(logged_message->topic, memory_resource_),
  };
}

jewels::expected<void, jewels::MonoError> LogMessageFetcher::initialize()
{
  // Attempt to create the appropriate AbstractLogReader for the log uri
  try
  {
    reader_ = make_reader(log_uri_, log_interval_, {});
  }
  catch (std::exception& err)
  {
    jewels::log_cerr_error("Could not create a log reader for {}: {}", log_uri_, err.what());
    return jewels::unexpected(jewels::MonoError{});
  }

  for (const auto& channel : log_publisher_config_->get_channels())
  {
    topics_.insert(std::pmr::string(channel.get_channel_name(), memory_resource_));
  }

  auto open_status = reader_->open(
    [this](auto topic)
    {
      const std::pmr::string topic_str{topic, memory_resource_};
      return topics_.contains(topic_str);
    });
  if (!open_status)
  {
    jewels::log_cerr_error("Error opening log reader: {}", open_status.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return {};
}

} // namespace clockwork_logging
