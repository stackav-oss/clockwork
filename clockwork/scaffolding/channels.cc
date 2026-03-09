// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/channels.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/logging/channel_publisher_config_clk_cc.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"

#include <wise_enum.h>
#include <xxh3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <thread>
#include <utility>

namespace clockwork::scaffolding
{

jewels::expected<ChannelMap, jewels::MonoError> setup_channels(
  std::span<const Tappy<common::PublishEndpoint<>>> descs,
  jewels::memory::MemoryResource memres,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  pinion::ShmChannelFactory& factory)
{
  using Role = pinion::ShmChannel::Role;
  ChannelMap channels(descs.size(), memres);

  std::pmr::list<std::reference_wrapper<const Tappy<common::PublishEndpoint<>>>> pending(memres);
  for (const auto& config : descs)
  {
    pending.push_back(std::ref(config));
  }

  while (true)
  {
    for (auto config_it = pending.begin(); config_it != pending.end();)
    {
      const auto& config = config_it->get();
      auto layout = pinion::BufferLayout{
        .num_slots = config.get_buffer_layout().get_num_slots(),
        .message_size = config.get_buffer_layout().get_message_size(),
        .is_published_once = config.get_buffer_layout().get_is_published_once(),
      };
      auto role = (config.get_process_id() == process_id ? Role::publisher : Role::subscriber);
      auto uuid_str = config.get_publisher_id().to_string(memres);
      auto channel = factory.open(role, uuid_str, config.get_channel_name(), layout, config.get_num_subscribers());
      if (channel)
      {
        channels.emplace(config.get_publisher_id(), std::move(channel.value()));
        config_it = pending.erase(config_it);
      }
      else if (channel.error() == pinion::ShmChannel::Error::missing && role == pinion::ShmChannel::Role::subscriber)
      {
        ++config_it;
      }
      else
      {
        jewels::log_cerr_error(
          "Could not create channel '{}': {}", config.get_channel_name(), wise_enum::to_string(channel.error()));
        return jewels::unexpected(jewels::MonoError());
      }
    }
    if (pending.empty())
    {
      break;
    }
    jewels::log_cerr_info("Waiting for publisher creation ({} channels remaining)", pending.size());
    std::this_thread::sleep_for(channel_connect_sleep_time);
  }
  return std::move(channels);
}

jewels::expected<ChannelMap, jewels::MonoError> setup_deterministic_channels(
  std::span<const Tappy<common::PublishEndpoint<>>> descs,
  std::span<const Tappy<clockwork_logging::PublishedChannelConfig<>>> published_channels,
  jewels::memory::MemoryResource memres,
  pinion::ShmChannelFactory& factory)
{
  using Role = pinion::ShmChannel::Role;
  ChannelMap channels(descs.size(), memres);

  bool error = false;
  std::mutex mutex;
  auto config_it = descs.begin();
  auto next_config = [&config_it, &descs, &mutex]() -> const Tappy<common::PublishEndpoint<>>*
  {
    const std::scoped_lock lock(mutex);
    if (config_it != descs.end())
    {
      return &*(config_it++);
    }
    return nullptr;
  };
  auto worker = [&next_config, &error, &mutex, &channels, &factory, &memres]()
  {
    for (const auto* config = next_config(); !error && config != nullptr; config = next_config())
    {
      auto layout = pinion::BufferLayout{
        .num_slots = config->get_buffer_layout().get_num_slots(),
        .message_size = config->get_buffer_layout().get_message_size(),
        .is_published_once = config->get_buffer_layout().get_is_published_once(),
      };
      auto uuid_str = config->get_publisher_id().to_string(memres);
      auto channel =
        factory.open(Role::publisher, uuid_str, config->get_channel_name(), layout, config->get_num_subscribers());
      if (channel)
      {
        const std::scoped_lock lock(mutex);
        channels.emplace(config->get_publisher_id(), std::move(channel.value()));
      }
      else
      {
        jewels::log_cerr_error(
          "Could not create channel '{}': {}", config->get_channel_name(), wise_enum::to_string(channel.error()));
        error = true;
      }
    }
  };

  // Create channels in parallel.  This is an I/O bound operation (mostly ftruncate) so just pick a decent number of
  // threads.
  constexpr size_t num_threads = 8;
  std::array<std::thread, num_threads> threads;
  for (auto& thread : threads)
  {
    thread = std::thread(worker);
  }
  for (auto& thread : threads)
  {
    thread.join();
  }

  // This is to handle the case where a channel is meant to be published by the log publisher and consumed only by the
  // deterministic log writer. In that case the channel will not be described in the PublishEndpoint so we add any
  // such channels below.
  for (const auto& channel_config : published_channels)
  {
    auto channel_uuid = jewels::Uuid<::clockwork::common::EndpointInstanceId>(channel_config.get_uuid().uuid);
    if (channel_uuid == jewels::Uuid<::clockwork::common::EndpointInstanceId>{})
    {
      // These are here as FirstMessage data sources, not connected to any Pinion channel.
      continue;
    }
    if (!channels.contains(channel_uuid))
    {
      auto layout = pinion::BufferLayout{
        .num_slots = channel_config.get_num_slots(),
        .message_size = channel_config.get_message_size(),
        .is_published_once = channel_config.get_is_published_once(),
      };
      auto uuid_str = channel_config.get_uuid().to_string(memres);
      // For this case the only subscriber should be the deterministic log writer.
      auto channel = factory.open(Role::publisher, uuid_str, channel_config.get_channel_name(), layout, 1);
      if (channel)
      {
        channels.emplace(channel_uuid, std::move(channel.value()));
      }
      else
      {
        jewels::log_cerr_error(
          "Could not create channel '{}': {}",
          channel_config.get_channel_name(),
          wise_enum::to_string(channel.error()));
        return jewels::unexpected(jewels::MonoError());
      }
    }
  }
  return channels;
}

jewels::expected<ChannelMap, jewels::MonoError> setup_non_connected_channels(
  std::span<const Tappy<common::NotConnectedEndpoint>> endpoints,
  AbstractCasing& casing,
  jewels::memory::MemoryResource memres,
  pinion::ShmChannelFactory& factory)
{
  using Role = pinion::ShmChannel::Role;
  ChannelMap channels(memres);
  for (const auto& endpoint : endpoints)
  {
    auto layout = pinion::BufferLayout{
      .num_slots = endpoint.get_buffer_layout().get_num_slots(),
      .message_size = endpoint.get_buffer_layout().get_message_size(),
      .is_published_once = endpoint.get_buffer_layout().get_is_published_once(),
    };
    if (endpoint.get_endpoint_type() == common::NotConnectedEndpointType::subscriber)
    {
      auto subscriber_result = casing.set_subscriber(endpoint.get_endpoint_id());
      if (!subscriber_result)
      {
        jewels::log_cerr_error(
          "Could not create non-connected subscriber channel '{}': {}",
          endpoint.get_endpoint_id(),
          subscriber_result.error());
        return jewels::unexpected(jewels::MonoError());
      }
      continue;
    }
    auto channel = factory.open(
      Role::publisher,
      endpoint.get_endpoint_id().to_string(memres),
      endpoint.get_endpoint_id().to_string(memres),
      layout,
      0);
    if (channel)
    {
      channels.emplace(endpoint.get_endpoint_id(), std::move(channel.value()));
    }
    else
    {
      jewels::log_cerr_error(
        "Could not create channel '{}': {}", endpoint.get_endpoint_id(), wise_enum::to_string(channel.error()));
      return jewels::unexpected(jewels::MonoError());
    }

    auto* publisher_ptr = dynamic_cast<pinion::ShmPublisher*>(channels[endpoint.get_endpoint_id()].get());
    if (publisher_ptr == nullptr)
    {
      jewels::log_cerr_error("supposed publisher id '{}' is actually a subscriber", endpoint.get_endpoint_id());
      return jewels::unexpected(jewels::MonoError());
    }
    auto publisher_handle = publisher_ptr->extract_publisher();
    if (!publisher_handle)
    {
      jewels::log_cerr_error("publisher '{}' was already used", endpoint.get_endpoint_id());
      return jewels::unexpected(jewels::MonoError());
    }
    auto result = casing.set_publisher_handle(endpoint.get_endpoint_id(), std::move(*publisher_handle));
    if (!result)
    {
      jewels::log_cerr_error("set_publisher_handle '{}' failed: {}", endpoint.get_endpoint_id(), result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return channels;
}

jewels::expected<std::pmr::vector<std::shared_ptr<pinion::Observer>>, jewels::MonoError> connect_subscribers(
  std::span<const Tappy<common::PubSubConnection>> connections,
  jewels::memory::MemoryResource memres,
  ChannelMap& channels,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  AbstractCasing& casing)
{
  std::pmr::vector<std::shared_ptr<pinion::Observer>> observers(memres);
  for (const auto& connection : connections)
  {
    if (connection.get_subscriber_process_id() == process_id)
    {
      auto channel = channels.find(connection.get_publisher_id());
      if (channel == channels.end())
      {
        jewels::log_cerr_error("pubsub graph has unknown publisher id '{}'", connection.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      auto observer = casing.try_connect_subscriber(connection.get_subscriber_id(), channel->second->make_subscriber());
      if (!observer)
      {
        jewels::log_cerr_error(
          "try_connect_subscriber '{}' failed: {}", connection.get_publisher_id(), observer.error());
        return jewels::unexpected(jewels::MonoError());
      }
      if (observer.value() == nullptr)
      {
        jewels::log_cerr_error("Got nullptr observer for '{}'", connection.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      if (!channel->second->add_observer(jewels::memory::make_non_null_from_ref(**observer)))
      {
        jewels::log_cerr_error("Too many observers for '{}'", connection.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      observers.push_back(std::move(*observer));
    }
  }
  return std::move(observers);
}

jewels::expected<void, jewels::MonoError> connect_publishers(
  std::span<const Tappy<common::PublishEndpoint<>>> endpoints,
  ChannelMap& channels,
  const jewels::Uuid<common::ProcessInstanceId>& process_id,
  AbstractCasing& casing)
{
  for (const auto& endpoint : endpoints)
  {
    if (endpoint.get_process_id() == process_id)
    {
      auto channel = channels.find(endpoint.get_publisher_id());
      if (channel == channels.end())
      {
        jewels::log_cerr_error("pubsub graph has unknown publisher id '{}'", endpoint.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      auto* publisher_ptr = dynamic_cast<pinion::ShmPublisher*>(channel->second.get());
      if (publisher_ptr == nullptr)
      {
        jewels::log_cerr_error("supposed publisher id '{}' is actually a subscriber", endpoint.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      auto publisher_handle = publisher_ptr->extract_publisher();
      if (!publisher_handle)
      {
        jewels::log_cerr_error("publisher '{}' was already used", endpoint.get_publisher_id());
        return jewels::unexpected(jewels::MonoError());
      }
      auto result = casing.try_connect_publisher(endpoint.get_publisher_id(), std::move(*publisher_handle));
      if (!result)
      {
        jewels::log_cerr_error("try_connect_publisher '{}' failed: {}", endpoint.get_publisher_id(), result.error());
        return jewels::unexpected(jewels::MonoError());
      }
    }
  }
  return {};
}

void bind_channel_to_epoll(const std::shared_ptr<pinion::ShmChannel>& channel, AbstractEPollManager& epoll)
{
  uint32_t events = 0;
  if (std::dynamic_pointer_cast<pinion::ShmSubscriber>(channel))
  {
    events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;
  }
  else if (std::dynamic_pointer_cast<pinion::ShmPublisher>(channel))
  {
    events = EPOLLIN;
  }
  else
  {
    throw std::runtime_error("internal error: unknown channel type");
  }
  if (!epoll.add(channel->socket(), events, channel))
  {
    throw std::runtime_error("internal error: could not add event to epoll");
  }
}

void bind_channels_to_epoll(const ChannelMap& channels, AbstractEPollManager& epoll)
{
  for (const auto& [_, channel] : channels)
  {
    bind_channel_to_epoll(channel, epoll);
  }
}

} // namespace clockwork::scaffolding
