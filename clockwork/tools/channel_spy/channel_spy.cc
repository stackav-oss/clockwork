// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/channel_spy.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/format.h>

#include <cstddef>
#include <map>
#include <memory_resource>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace clockwork::tools
{

ChannelSpy::ChannelSpy(std::string_view shm_root_dir, std::string_view socket_ns)
  : memory_resource_(std::pmr::new_delete_resource()), shm_root_dir_(shm_root_dir), socket_ns_(socket_ns)
{
}

[[nodiscard]] std::vector<SpyChannelMetadata> ChannelSpy::channels()
{
  read_channel_spy_config();
  std::map<std::string, SpyChannelMetadata> channel_map{};
  for (const auto& channel_metadata : config_ptr_->get_channels())
  {
    channel_map.emplace(
      channel_metadata.get_channel_name(),
      SpyChannelMetadata{
        .channel_name = std::string{channel_metadata.get_channel_name()},
        .schema_name = std::string{channel_metadata.get_schema_name()},
        .schema_definition = std::vector<std::byte>(
          channel_metadata.get_schema_definition().begin(), channel_metadata.get_schema_definition().end()),
      });
  }
  std::vector<SpyChannelMetadata> channels{};
  channels.reserve(channel_map.size());
  for (auto& channel : std::views::values(channel_map))
  {
    channels.emplace_back(std::move(channel));
  }
  return channels;
}

[[nodiscard]] const ChannelSpyConfigTap& ChannelSpy::spy_config()
{
  read_channel_spy_config();
  return *config_ptr_;
}

void ChannelSpy::subscribe(std::string_view channel_name, const RawMessageCallback& callback_fn)
{
  read_channel_spy_config();
  bool subscribed = false;
  for (const auto& channel : config_ptr_->get_channels())
  {
    if (channel.get_channel_name() == channel_name)
    {
      subscribers_.emplace_back(ChannelSpySubscriber::make_subscriber(
        shm_root_dir_,
        socket_ns_,
        channel.get_uuid().to_string(),
        channel.get_num_slots(),
        channel.get_message_size(),
        callback_fn));
      subscribed = true;
    }
  }
  if (!subscribed)
  {
    const auto msg = fmt::format("Channel {} is not published on the local machine", channel_name);
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
}

void ChannelSpy::subscribe(std::string_view channel_name, const PythonCallback& callback_fn)
{
  read_channel_spy_config();
  bool subscribed = false;
  for (const auto& channel : config_ptr_->get_channels())
  {
    if (channel.get_channel_name() == channel_name)
    {
      subscribers_.emplace_back(ChannelSpySubscriber::make_subscriber(
        shm_root_dir_,
        socket_ns_,
        channel.get_uuid().to_string(),
        channel.get_num_slots(),
        channel.get_message_size(),
        callback_fn));
      subscribed = true;
    }
  }
  if (!subscribed)
  {
    const auto msg = fmt::format("Channel {} is not published on the local machine", channel_name);
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
}

void ChannelSpy::run(std::chrono::nanoseconds polling_interval)
{
  while (true)
  {
    run_once();
    std::this_thread::sleep_for(polling_interval);
  }
}

void ChannelSpy::run_once()
{
  for (auto& subscriber : subscribers_)
  {
    subscriber->poll();
  }
}

void ChannelSpy::read_channel_spy_config()
{
  if (config_ptr_)
  {
    return;
  }
  const auto config_path = socket_ns_.empty()
                             ? fmt::format(default_channel_spy_config_path_format, shm_root_dir_)
                             : fmt::format(namespace_channel_spy_config_path_format, shm_root_dir_, socket_ns_);
  jewels::filesystem::File config_file{config_path};
  const auto read_result = config_file.read_all(memory_resource_);
  if (!read_result)
  {
    const auto msg = fmt::format("Failed to read spy configuration file: {}", read_result.error().message());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  if (read_result->size() != sizeof(ChannelSpyConfigTap))
  {
    const auto msg = fmt::format("Spy configuration file has invalid size: {}", read_result->size());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  config_ptr_ =
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Cast to deserialize channel spy configuration
    std::make_unique<ChannelSpyConfigTap>(*reinterpret_cast<const ChannelSpyConfigTap*>(read_result->data()));
}

} // namespace clockwork::tools
