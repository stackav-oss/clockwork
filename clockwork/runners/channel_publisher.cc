// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/channel_publisher.hh"

#include "clockwork/logging/channel_publisher_config.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <xxh3.h>

#include <cstring>
#include <memory_resource>
#include <span>
#include <unordered_set>
#include <utility>

namespace clockwork
{
ChannelPublisher::ChannelPublisher(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::ObjectPtr<const clockwork_logging::ChannelPublisherConfigTap> channel_publisher_config,
  const jewels::memory::NonNullSharedPtr<MessageFetcher>& message_fetcher,
  ShmPublisherMap channels,
  bool suppress_schema_mismatch_errors)
  : memory_resource_(std::move(memory_resource)),
    message_fetcher_(message_fetcher),
    channel_publisher_config_(std::move(channel_publisher_config)),
    channels_(std::move(channels), memory_resource_),
    channel_publishers_(memory_resource_),
    suppress_schema_mismatch_errors_(suppress_schema_mismatch_errors)
{
  mismatched_channels_logged_ = std::pmr::unordered_set<std::pmr::string>(memory_resource_);
}

jewels::expected<void, jewels::MonoError> ChannelPublisher::initialize()
{
  for (const auto& channel_config : channel_publisher_config_->get_channels())
  {
    auto channel_name = channel_config.get_channel_name();

    auto endpoint_uuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>(channel_config.get_uuid().uuid);

    if (auto channel = channels_.find(endpoint_uuid); channel != channels_.end())
    {
      channel_publishers_.emplace(std::pmr::string(channel_name, memory_resource_), channel->second);
    }
    else
    {
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  message_fetcher_->initialize(); // NOLINT(cert-err33-c) False positive
  next_message_ = get_next_message_info();
  return {};
}

jewels::expected<void, jewels::MonoError> ChannelPublisher::publish_next_message()
{
  if (!next_message_ || next_message_->msgs.empty())
  {
    jewels::log_cerr_error("Attempted to publish a message but the log file has been exhausted.");
    return jewels::unexpected(jewels::MonoError{});
  }

  // Exit early if this channel has a schema mismatch
  if (mismatched_channels_logged_.contains(next_message_->channel))
  {
    // Move on to the next message
    next_message_ = get_next_message_info();
    return {};
  }

  if (auto shm_publisher_it = channel_publishers_.find(std::pmr::string(next_message_->channel, memory_resource_));
      shm_publisher_it != channel_publishers_.end())
  {
    auto& shm_publisher = shm_publisher_it->second;
    auto& publisher_handle = shm_publisher->publisher();

    auto reserve_result = publisher_handle.reserve();
    if (!reserve_result)
    {
      jewels::log_cerr_error("Failed to reserve a slot for {}", next_message_->channel);
      return jewels::unexpected(jewels::MonoError{});
    }
    auto& reserved_slot = reserve_result.value();
    auto slot = reserved_slot.slot();
    const auto& msg_data = next_message_->msgs.front();
    if (slot.message().size() != msg_data.size())
    {
      auto channel = next_message_->channel;

      // If we aren't suppressing schema mismatch errors, hard fail.
      if (!suppress_schema_mismatch_errors_)
      {
        jewels::log_cerr_error(
          "data size on channel '{}' is not correct (actual {} != expected {}).",
          channel,
          msg_data.size(),
          slot.message().size());
        return jewels::unexpected(jewels::MonoError{});
      }

      jewels::log_cerr_warn(
        " Logged data size on channel '{}' is not correct (actual {} != expected {}). Messages on this channel "
        "will be ignored.",
        channel,
        msg_data.size(),
        slot.message().size());

      // We don't want to revisit this channel, mark it so that we can catch it up top.
      mismatched_channels_logged_.insert(channel);

      // Move on to the next message
      next_message_ = get_next_message_info();
      return {};
    }
    std::memcpy(slot.message().data(), msg_data.data(), slot.message().size());
    if (const auto result = reserved_slot.commit(next_message_->time_to_publish); !result)
    {
      jewels::log_cerr_error("Failed to commit slot for {}: {}", next_message_->channel, result.error());
      return jewels::unexpected(jewels::MonoError{});
    }

    next_message_->msgs.pop_front();
    if (next_message_->msgs.empty())
    {
      next_message_ = get_next_message_info();
    }

    return {};
  }
  jewels::log_cerr_error("Unable to find configured publisher channel for channel: {}", next_message_->channel);
  return jewels::unexpected(jewels::MonoError{});
}

std::optional<jewels::time::SyncTime> ChannelPublisher::try_next_message_time()
{
  if (next_message_)
  {
    return next_message_->time_to_publish;
  }
  return std::nullopt;
}

bool ChannelPublisher::messages_remaining()
{
  return next_message_.has_value();
}

std::optional<MultiMessageInfoData> ChannelPublisher::get_next_message_info()
{
  while (auto next = message_fetcher_->try_fetch_message())
  {
    if (!next)
    {
      return std::nullopt;
    }

    return next;
  }

  return std::nullopt;
}
} // namespace clockwork
