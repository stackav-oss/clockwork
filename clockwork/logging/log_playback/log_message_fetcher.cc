// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_playback/log_message_fetcher.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/log_playback/end_of_log.hh"
#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/message_encoding.hh"
#include "clockwork/logging/readers/log_reader_factory.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <fmt10/format.h>

#include <cstddef>
#include <exception>
#include <functional>
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
  jewels::memory::ObjectPtr<const ChannelPublisherConfigTap> channel_publisher_config,
  std::optional<LogInterval> maybe_log_interval,
  jewels::memory::MemoryResource memory_resource)
  : memory_resource_(std::move(memory_resource)),
    channel_publisher_config_(std::move(channel_publisher_config)),
    log_uri_(log_uri),
    maybe_log_interval_(maybe_log_interval)
{
}

std::optional<clockwork::MultiMessageInfoData> LogMessageFetcher::get_end_of_log_message()
{
  if (end_of_log_sent_ || !last_message_time_ || !topics_.contains(end_of_log_channel_name))
  {
    return std::nullopt;
  }
  end_of_log_sent_ = true;

  clockwork::Tappy<EndOfLog> end_of_log_tap;
  // The message is really a signal, but tachyon requires at least one field. Since it exists we set it so we can
  // validate in tests that the message was received correctly.
  end_of_log_tap.set_end_of_log(true);
  auto end_of_log_bytes = std::as_writable_bytes(jewels::as_single_item_span(end_of_log_tap));

  std::pmr::vector<std::byte> end_of_log_data(end_of_log_bytes.begin(), end_of_log_bytes.end(), memory_resource_);

  clockwork::MultiMessageInfoData message_data{
    .time_to_publish = *last_message_time_,
    .msgs = {{std::move(end_of_log_data)}, memory_resource_},
    .channel = std::pmr::string(end_of_log_channel_name, memory_resource_),
  };
  return message_data;
}

std::optional<::clockwork::MultiMessageInfoData> LogMessageFetcher::try_fetch_message()
{
  while (true)
  {
    auto logged_message = reader_->next_message();
    if (!logged_message)
    {
      return end_of_log_sent_ ? std::nullopt : get_end_of_log_message();
    }
    if (logged_message->message_encoding != MessageEncoding::tachyon)
    {
      throw std::runtime_error(
        fmt::format(
          "Attempted to read log data encoding '{}'. Only 'tachyon' log data is supported.",
          logged_message->message_encoding));
    }
    last_message_time_ = logged_message->publish_time.get_time();
    clockwork::MultiMessageInfoData message_data{
      .time_to_publish = *last_message_time_,
      .msgs = {{std::pmr::vector<std::byte>{memory_resource_}}, memory_resource_},
      .channel = std::pmr::string(logged_message->topic, memory_resource_),
    };
    if (!upgrader_->upgrade_message(logged_message->topic, logged_message->data, message_data.msgs.front()))
    {
      continue;
    }
    return message_data;
  }
}

jewels::expected<void, jewels::MonoError> LogMessageFetcher::initialize()
{
  // Attempt to create the appropriate AbstractLogReader for the log uri
  try
  {
    reader_ = make_reader(log_uri_, maybe_log_interval_, {});
  }
  catch (std::exception& err)
  {
    jewels::log_cerr_error("Could not create a log reader for {}: {}", log_uri_, err.what());
    return jewels::unexpected(jewels::MonoError{});
  }

  for (const auto& channel : channel_publisher_config_->get_channels())
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

  upgrader_ = std::make_unique<TachyonUpgrader>(memory_resource_, *channel_publisher_config_, reader_->get_metadata());

  return {};
}

} // namespace clockwork_logging
