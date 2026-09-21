// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/meta_channel_factory.hh"

#include "clockwork/pinion/channel_config_clk_cc.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace clockwork::pinion
{

jewels::expected<MetaChannelFactory, jewels::MonoError> MetaChannelFactory::make(
  jewels::memory::MemoryResource memres,
  const Tappy<common::ProcessDescription<>>& config,
  std::string_view nmsp,
  std::string_view root,
  AbstractChannel::ResumeBehavior resume_behavior)
{
  std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types;
  std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys;
  std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys;
  for (const auto& publisher : config.get_pubsub_graph().get_publish_endpoints())
  {
    if (!channel_types.emplace(publisher.get_publisher_id().to_string(memres), publisher.get_channel_type()).second)
    {
      jewels::log_cerr_error("Duplicate publisher found {}", publisher.get_publisher_id());
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  for (const auto& connection : config.get_pubsub_graph().get_connections())
  {
    auto publisher_id = connection.get_publisher_id().to_string(memres);
    auto subscriber_id = connection.get_subscriber_id().to_string(memres);
    if (!channel_types.emplace(subscriber_id, channel_types.at(publisher_id)).second)
    {
      jewels::log_cerr_error("Duplicate subscriber found {}", subscriber_id);
      return jewels::unexpected(jewels::MonoError{});
    }
    if (!connection.get_publisher_key().empty())
    {
      publisher_keys[publisher_id].emplace_back(connection.get_publisher_key());
    }
    // Currently there is no subscriber id; channels are created with the publisher id, which is really the channel
    // id. This is okay, since we don't make multiple subscribers per-process and thus will just register publisher_id
    subscriber_keys.emplace(publisher_id, connection.get_subscriber_key());
  }
  for (auto& publisher_key : publisher_keys)
  {
    auto& keys = publisher_key.second;
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  }
  return make(
    memres,
    std::move(channel_types),
    std::move(publisher_keys),
    std::move(subscriber_keys),
    nmsp,
    root,
    resume_behavior);
}

jewels::expected<MetaChannelFactory, jewels::MonoError> MetaChannelFactory::make(
  jewels::memory::MemoryResource memres,
  std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types,
  // NOLINTNEXTLINE(performance-unnecessary-value-param) used in conditional builds
  [[maybe_unused]] std::pmr::unordered_map<std::pmr::string, std::pmr::vector<std::pmr::string>> publisher_keys,
  // NOLINTNEXTLINE(performance-unnecessary-value-param) used in conditional builds
  [[maybe_unused]] std::pmr::unordered_map<std::pmr::string, std::pmr::string> subscriber_keys,
  std::string_view nmsp,
  std::string_view root,
  AbstractChannel::ResumeBehavior resume_behavior)
{
  std::map<ChannelType, std::shared_ptr<AbstractChannelFactory>> factories;
  // shm factory
  auto maybe_shm_factory = ShmChannelFactory::make(memres, nmsp, root, resume_behavior);
  if (!maybe_shm_factory)
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  auto shm_factory = jewels::memory::make_pmr_shared<ShmChannelFactory>(memres, *std::move(maybe_shm_factory));
  factories[ChannelType::shared_memory] = shm_factory;
  factories[ChannelType::unspecified] = std::move(shm_factory);
  return MetaChannelFactory(std::move(memres), std::move(channel_types), std::move(factories));
}

MetaChannelFactory::MetaChannelFactory(
  jewels::memory::MemoryResource memres,
  std::pmr::unordered_map<std::pmr::string, ChannelType> channel_types,
  std::map<ChannelType, std::shared_ptr<AbstractChannelFactory>> factories)
  : memres_(std::move(memres)), factories_(std::move(factories)), channel_types_(std::move(channel_types))
{
}

MetaChannelFactory::~MetaChannelFactory() = default;

jewels::expected<std::shared_ptr<AbstractPublisher>, AbstractChannel::Error> MetaChannelFactory::open_publisher(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return get_factory(uuid_str).and_then(
    [uuid_str, channel_name, &layout, max_subscribers](auto* factory)
    { return factory->open_publisher(uuid_str, channel_name, layout, max_subscribers); });
}

jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error> MetaChannelFactory::open_subscriber(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return get_factory(uuid_str).and_then(
    [uuid_str, channel_name, &layout, max_subscribers](auto* factory)
    { return factory->open_subscriber(uuid_str, channel_name, layout, max_subscribers); });
}

jewels::expected<std::shared_ptr<AbstractSubscriber>, AbstractChannel::Error> MetaChannelFactory::open_spy(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return get_factory(uuid_str).and_then([uuid_str, channel_name, &layout, max_subscribers](auto* factory)
                                        { return factory->open_spy(uuid_str, channel_name, layout, max_subscribers); });
}

jewels::expected<AbstractChannelFactory*, AbstractChannel::Error>
MetaChannelFactory::get_factory(std::string_view uuid_str)
{
  auto type = channel_types_.find(std::pmr::string(uuid_str, memres_));
  if (type == channel_types_.end())
  {
    // For unknown ids (things like states) fallback to the default
    return factories_.at(ChannelType::unspecified).get();
  }
  auto factory = factories_.find(type->second);
  if (factory == factories_.end())
  {
    jewels::log_cerr_error("Attempted to create channel of unknown type: {} -> {}", uuid_str, type->second);
    return jewels::unexpected(AbstractChannel::Error::fatal);
  }
  return factory->second.get();
}

} // namespace clockwork::pinion
