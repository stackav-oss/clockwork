// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/state.hh"

#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
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

jewels::expected<std::pmr::vector<std::shared_ptr<pinion::ShmPublisher>>, jewels::MonoError> setup_states(
  std::span<const common::StateInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres_sys,
  const MemResMap& memres_map,
  pinion::ShmChannelFactory& factory,
  AbstractCasing& casing)
{
  std::pmr::vector<std::shared_ptr<pinion::ShmPublisher>> publishers(memres_sys);
  for (const auto& desc : descs)
  {
    std::optional<jewels::memory::MemoryResource> memres;
    if (desc.has_maybe_memory_resource())
    {
      auto memres_it = memres_map.find(desc.value_maybe_memory_resource());
      if (memres_it == memres_map.end())
      {
        jewels::log_cerr_error(
          "state '{}' references unknown memory resource '{}'",
          desc.get_instance_path_name(),
          desc.value_maybe_memory_resource());
        return jewels::unexpected(jewels::MonoError());
      }
      memres.emplace(memres_it->second.get());
    }
    std::shared_ptr<pinion::ShmPublisher> publisher;
    if (desc.has_maybe_buffer_layout())
    {
      constexpr size_t state_publisher_subscriber_limit = 2;
      auto result = factory.open_publisher(
        desc.get_state_instance_id().to_string(memres_sys),
        desc.get_instance_path_name(),
        pinion::BufferLayout{
          .num_slots = desc.value_maybe_buffer_layout().get_num_slots(),
          .message_size = desc.value_maybe_buffer_layout().get_message_size(),
        },
        state_publisher_subscriber_limit);
      if (!result)
      {
        jewels::log_cerr_error(
          "state '{}' failed to create backing buffer: {}",
          desc.get_instance_path_name(),
          wise_enum::to_string(result.error()));
        return jewels::unexpected(jewels::MonoError());
      }
      publisher = publishers.emplace_back(std::move(result).value());
    }
    jewels::expected<void, AbstractCasing::Error> result;
    if (publisher != nullptr && memres.has_value())
    {
      result = casing.try_instantiate_state(
        desc.get_state_instance_id(), desc.get_representation_id(), publisher->extract_publisher().value(), *memres);
    }
    else if (publisher != nullptr)
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
  return std::move(publishers);
}

[[nodiscard]] jewels::expected<void, jewels::MonoError> connect_states(
  std::span<const common::StateConnectionTap> connections,
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
