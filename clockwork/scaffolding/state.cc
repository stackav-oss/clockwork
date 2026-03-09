// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/state.hh"

#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/data_source_loader.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"

#include <wise_enum.h>
#include <xxh3.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace clockwork::scaffolding
{

// Helper function to get memory resource for a state description
jewels::expected<std::optional<jewels::memory::MemoryResource>, jewels::MonoError>
get_memory_resource(const Tappy<common::StateInstanceDescription<>>& desc, const MemResMap& memres_map)
{
  if (!desc.has_maybe_memory_resource())
  {
    return std::optional<jewels::memory::MemoryResource>{};
  }
  auto memres_it = memres_map.find(desc.value_maybe_memory_resource());
  if (memres_it == memres_map.end())
  {
    jewels::log_cerr_error(
      "state '{}' references unknown memory resource '{}'",
      desc.get_instance_path_name(),
      desc.value_maybe_memory_resource());
    return jewels::unexpected(jewels::MonoError());
  }
  return std::optional<jewels::memory::MemoryResource>{memres_it->second.get()};
}

// Helper function to create publisher for a state description
jewels::expected<std::shared_ptr<pinion::ShmPublisher>, jewels::MonoError> create_publisher(
  const Tappy<common::StateInstanceDescription<>>& desc,
  pinion::ShmChannelFactory& factory,
  jewels::memory::MemoryResource memres_sys)
{
  if (!desc.has_maybe_buffer_layout())
  {
    return nullptr;
  }
  constexpr size_t state_publisher_subscriber_limit = 2;
  auto result = factory.open_publisher(
    desc.get_state_instance_id().to_string(memres_sys),
    desc.get_instance_path_name(),
    pinion::BufferLayout{
      .num_slots = desc.value_maybe_buffer_layout().get_num_slots(),
      .message_size = desc.value_maybe_buffer_layout().get_message_size(),
      .is_published_once = desc.value_maybe_buffer_layout().get_is_published_once(),
    },
    state_publisher_subscriber_limit);
  if (!result)
  {
    return jewels::unexpected(jewels::MonoError());
  }
  return std::move(result).value();
}

// Helper function to load initial data for a state description
jewels::expected<DataSourceLoadResult, jewels::MonoError> load_initial_data(
  const Tappy<common::StateInstanceDescription<>>& desc,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  jewels::memory::MemoryResource memres_sys,
  const FirstMessageCache& first_message_cache)
{
  DataSourceLoadResult load_result{memres_sys};
  if (jewels::fails(load_data_from_source(
        jewels::Out{load_result},
        desc.get_init_data_source(),
        data_sources,
        memres_sys,
        first_message_cache,
        desc.get_instance_path_name())))
  {
    jewels::log_cerr_error("Failed to load data for state '{}'", desc.get_instance_path_name());
    return jewels::unexpected(jewels::MonoError());
  }
  return load_result;
}

// Helper function to instantiate a state
jewels::expected<void, jewels::MonoError> instantiate_state(
  const Tappy<common::StateInstanceDescription<>>& desc,
  const std::shared_ptr<pinion::ShmPublisher>& publisher,
  std::optional<jewels::memory::MemoryResource> memres,
  const DataSourceLoadResult& load_result,
  AbstractCasing& casing)
{
  if (!load_result.should_default_construct)
  {
    if (!publisher)
    {
      jewels::log_cerr_error("State '{}' has init data source but no buffer layout", desc.get_instance_path_name());
      return jewels::unexpected(jewels::MonoError());
    }

    auto data = load_result.data;
    if (load_result.representation_id != desc.get_representation_id())
    {
      // Need to deserialize via the casing
      std::pmr::vector<std::byte> deserialized_data{data.get_allocator()};
      deserialized_data.resize(publisher->publisher().layout().message_size);
      if (const auto outcome =
            casing.try_deserialize_data(load_result.representation_id, load_result.data, deserialized_data);
          jewels::fails(outcome))
      {
        jewels::log_cerr_error(
          "error deserializing data for state '{}' with representation {}: {}",
          desc.get_instance_path_name(),
          load_result.representation_id,
          wise_enum::to_string(outcome.get()));
        return jewels::unexpected(jewels::MonoError());
      }
      data = std::move(deserialized_data);
    }

    const auto outcome = casing.try_instantiate_state(
      desc.get_state_instance_id(), desc.get_representation_id(), publisher->extract_publisher().value(), data);
    if (jewels::fails(outcome))
    {
      jewels::log_cerr_error(
        "error creating state '{}' with data: {}", desc.get_instance_path_name(), wise_enum::to_string(outcome.get()));
      return jewels::unexpected(jewels::MonoError());
    }
  }
  else
  {
    // Default construction
    jewels::expected<void, AbstractCasing::Error> result;
    if (publisher && memres.has_value())
    {
      result = casing.try_instantiate_state(
        desc.get_state_instance_id(), desc.get_representation_id(), publisher->extract_publisher().value(), *memres);
    }
    else if (publisher)
    {
      result = casing.try_instantiate_state(
        desc.get_state_instance_id(), desc.get_representation_id(), publisher->extract_publisher().value());
    }
    else if (memres.has_value())
    {
      result = casing.try_instantiate_state(desc.get_state_instance_id(), desc.get_representation_id(), *memres);
    }
    else
    {
      jewels::log_cerr_error("state '{}' lacks both a buffer and memory config", desc.get_instance_path_name());
      return jewels::unexpected(jewels::MonoError());
    }
    if (!result)
    {
      jewels::log_cerr_error("error creating state '{}': {}", desc.get_instance_path_name(), result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

jewels::expected<std::pmr::vector<std::shared_ptr<pinion::ShmPublisher>>, jewels::MonoError> setup_states(
  std::span<const Tappy<common::StateInstanceDescription<>>> descs,
  jewels::memory::MemoryResource memres_sys,
  const MemResMap& memres_map,
  pinion::ShmChannelFactory& factory,
  AbstractCasing& casing,
  std::span<const Tappy<common::DataSource<>>> data_sources,
  const FirstMessageCache& first_message_cache)
{
  std::pmr::vector<std::shared_ptr<pinion::ShmPublisher>> publishers(memres_sys);
  for (const auto& desc : descs)
  {
    auto memres_result = get_memory_resource(desc, memres_map);
    if (!memres_result)
    {
      return jewels::unexpected(memres_result.error());
    }
    auto memres = *memres_result;

    // TODO(OI-3562): Eliminate redundant publisher for state backing and snapshots
    auto publisher_result = create_publisher(desc, factory, memres_sys);
    if (!publisher_result)
    {
      return jewels::unexpected(publisher_result.error());
    }
    auto publisher = *publisher_result;

    auto load_result_result = load_initial_data(desc, data_sources, memres_sys, first_message_cache);
    if (!load_result_result)
    {
      return jewels::unexpected(load_result_result.error());
    }
    auto load_result = *load_result_result;

    if (publisher)
    {
      publishers.emplace_back(publisher);
    }

    auto instantiate_result = instantiate_state(desc, publisher, memres, load_result, casing);
    if (!instantiate_result)
    {
      return jewels::unexpected(instantiate_result.error());
    }
  }
  return std::move(publishers);
}

[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_states(
  std::span<const Tappy<common::StateConnection>> connections,
  jewels::memory::MemoryResource memres_sys,
  AbstractCasing& casing)
{
  std::pmr::unordered_map<jewels::Uuid<common::StateInstanceId>, size_t, jewels::UuidHasher<common::StateInstanceId>>
    is_shared(memres_sys);
  is_shared.reserve(connections.size());
  for (const auto& connection : connections)
  {
    is_shared[connection.get_state_id()]++;
  }
  for (const auto& connection : connections)
  {
    auto result = casing.try_connect_state(
      connection.get_endpoint_id(), connection.get_state_id(), (is_shared.at(connection.get_state_id()) > 1));
    if (!result)
    {
      jewels::log_cerr_error(
        "try_connect_state {} -> {} failed: {}",
        connection.get_state_id(),
        connection.get_endpoint_id(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

} // namespace clockwork::scaffolding
