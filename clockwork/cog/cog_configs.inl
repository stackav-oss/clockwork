// IWYU pragma: private, include "clockwork/cog/cog_configs.hh"
#pragma once

#include "clockwork/cog/cog_configs.hh"

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

template <typename... Policies>
bool CogConfigs<Policies...>::validate() const
{
  return detail::validate_helper<Record>(records_, [](const auto& record) { return record.config != nullptr; });
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError> CogConfigs<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, std::shared_ptr<CogConfigData> config)
{
  auto try_set = [&endpoint_id, &config](auto& record) -> bool
  {
    using PolicyType = typename std::decay_t<decltype(record)>::PolicyType;
    if (endpoint_id == PolicyType::endpoint_id)
    {
      using ConfigType = typename PolicyType::ConfigType;
      if (auto ptr = std::dynamic_pointer_cast<CogConfigDataImpl<ConfigType>>(std::move(config)); ptr)
      {
        record.config = std::move(ptr);
        return true;
      }
    }
    return false;
  };

  auto is_set = std::apply([&try_set](auto&... record) -> bool { return (try_set(record) || ...); }, records_);

  if (!is_set)
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  return {};
}

template <typename... Policies>
jewels::BinaryOutcome CogConfigs<Policies...>::set_publisher_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& publisher)
{
  auto try_set = [&endpoint_id, &publisher]<typename Record>(Record& record) -> bool
  {
    using PolicyType = typename Record::PolicyType;
    if (endpoint_id == PolicyType::endpoint_id)
    {
      if (record.snapshot_publisher)
      {
        jewels::log_cerr_error("Snapshot publisher already set for config endpoint '{}'", endpoint_id.to_string());
        return false;
      }
      record.snapshot_publisher.emplace(std::move(publisher));
      return true;
    }
    return false;
  };

  if (std::apply([&try_set](auto&... record) -> bool { return (try_set(record) || ...); }, records_))
  {
    return jewels::success;
  }

  return jewels::failure;
}

template <typename... Policies>
jewels::BinaryOutcome CogConfigs<Policies...>::publish_snapshot(jewels::Uuid<common::EndpointClassId> endpoint_id)
{
  auto try_snapshot = [&endpoint_id]<typename Record>(Record& record) -> jewels::BinaryOutcome
  {
    using PolicyType = typename Record::PolicyType;
    if (endpoint_id != PolicyType::endpoint_id)
    {
      return jewels::failure;
    }

    if (!record.config)
    {
      jewels::log_cerr_error("Config not loaded for endpoint '{}'", endpoint_id);
      return jewels::failure;
    }

    if (!record.snapshot_publisher)
    {
      jewels::log_cerr_error("Snapshot publisher not set for config endpoint '{}'", endpoint_id);
      return jewels::failure;
    }

    using ConfigType = typename PolicyType::ConfigType;

    auto slot = record.snapshot_publisher->reserve();
    if (!slot)
    {
      jewels::log_cerr_error("Failed to reserve slot for config snapshot");
      return jewels::failure;
    }

    auto publishable = pinion::Publishable<ConfigType>::try_make(jewels::memory::make_non_null_from_ref(*slot));
    if (!publishable)
    {
      jewels::log_cerr_error("Failed to create publishable for config snapshot");
      return jewels::failure;
    }

    publishable->message() = *(record.config->data);
    publishable->mark_for_publish();

    auto result = slot->process(jewels::time::SyncClock::now());
    if (!result)
    {
      jewels::log_cerr_error("Failed to publish config snapshot: {}", result.error());
      return jewels::failure;
    }

    return jewels::success;
  };

  bool matched = std::apply([&try_snapshot](auto&... record) { return (ok(try_snapshot(record)) || ...); }, records_);

  return matched ? jewels::success : jewels::failure;
}

template <typename... Policies>
auto CogConfigs<Policies...>::make_configs() -> ConfigsTuple
{
  return std::apply([](const auto&... record) { return ConfigsTuple(*(record.config->data)...); }, records_);
}

template <typename... Policies>
template <size_t index>
[[nodiscard]] bool CogConfigs<Policies...>::is_config_set() const
{
  return static_cast<bool>(std::get<index>(records_).config);
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogConfigs<Policies...>::get_config(jewels::FactoryOut<ConfigRefType<index>> config)
{
  if (!is_config_set<index>())
  {
    return jewels::failure;
  }
  *config = *std::get<index>(records_).config->data;
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome
CogConfigs<Policies...>::get_config_handle(jewels::Out<ConfigHandleType<index>> config_handle) const
{
  if (!is_config_set<index>())
  {
    return jewels::failure;
  }
  *config_handle = std::get<index>(records_).config->data;
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogConfigs<Policies...>::set_config_handle(ConfigHandleType<index> config_handle)
{
  if (is_config_set<index>())
  {
    return jewels::failure;
  }
  std::get<index>(records_).config = std::make_shared<CogConfigDataImpl<ConfigType<index>>>(std::move(config_handle));
  return jewels::success;
}

} // namespace clockwork
