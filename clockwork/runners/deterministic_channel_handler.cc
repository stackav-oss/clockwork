// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/runners/deterministic_channel_handler.hh"

#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <xxh3.h>

#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace clockwork
{
DeterministicChannelHandler::DeterministicChannelHandler(
  jewels::memory::MemoryResource memory_resource,
  jewels::memory::NonNullSharedPtr<AbstractMessageWriter> message_writer,
  jewels::memory::ObjectPtr<const clockwork_logging::LogWriterConfigTap> log_writer_config,
  ChannelMap channels)
  : memory_resource_(std::move(memory_resource)),
    message_writer_(std::move(message_writer)),
    log_writer_config_(log_writer_config),
    observers_(memory_resource_),
    channels_(std::move(channels))
{
}

jewels::expected<void, jewels::MonoError> DeterministicChannelHandler::initialize()
{

  if (auto message_writer_status = message_writer_->initialize(); !message_writer_status)
  {
    jewels::log_cerr_error("Error initializing the message writer");
    return jewels::unexpected(jewels::MonoError{});
  }

  observers_.reserve(log_writer_config_->get_channels().size());
  for (const auto& channel_config : log_writer_config_->get_channels())
  {
    auto endpoint_uuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>(channel_config.get_uuid().uuid);

    if (auto channel = channels_.find(endpoint_uuid); channel != channels_.end())
    {
      if (!channel->second)
      {
        return jewels::unexpected{jewels::MonoError{}};
      }
      observers_.emplace_back(
        memory_resource_,
        channel->second->buffer(),
        jewels::memory::make_non_null_from_ref(*this),
        channel_config.get_channel_name(),
        channel_config.get_channel_type());
      auto& observer = observers_.back();
      observer.notify(pinion::Observer::Event{});
      auto observer_status = channel->second->add_observer(jewels::memory::make_non_null_from_ref(observer));
      if (!observer_status)
      {
        jewels::log_cerr_error("Error adding channel observer");
        return jewels::unexpected(jewels::MonoError{});
      }
      observer.notify({});
    }
    else
    {
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  return {};
}

void DeterministicChannelHandler::message_callback(
  jewels::time::SyncTime current_time,
  std::string_view channel_name,
  jewels::memory::ObjectPtr<const clockwork::pinion::Buffer> /*buffer_ptr*/,
  const clockwork::pinion::BufferIterator& buffer_iterator)
{
  auto slot = buffer_iterator.dereference();
  auto slot_header = slot.header();
  const auto& message_data = slot.message();

  message_writer_->message_received_callback(
    MessageInfoView{
      .sequence_number = static_cast<uint32_t>(slot_header->sequence_number),
      .time_to_publish = current_time,
      .data = message_data,
      .channel = std::pmr::string(channel_name, memory_resource_),
    });
}

void DeterministicChannelHandler::drop_callback(std::string_view channel_name, size_t drop_count)
{
  jewels::log_cerr_error("Dropped {} messages on {}", drop_count, channel_name);
}

} // namespace clockwork
