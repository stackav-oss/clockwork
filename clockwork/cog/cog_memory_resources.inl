// IWYU pragma: private, include "clockwork/cog/cog_memory_resources.hh"
#pragma once

#include "clockwork/cog/cog_memory_resources.hh"

#include "clockwork/cog/detail.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

template <typename... Policies>
bool CogMemoryResources<Policies...>::validate() const
{
  return detail::validate_helper<Record>(
    records_,
    []<typename Policy>(const Record<Policy>& record)
    {
      auto valid = record.memory_resource.has_value();
      if (!valid)
      {
        jewels::log_cerr_error(
          "Memory resource for '{}' unset. Typically this is due to a missing memory resource connection to the cog "
          "instance in your box/casing.",
          Policy::name);
      }
      return valid;
    });
}

template <typename... Policies>
template <typename MemoryResourceType>
jewels::expected<void, jewels::MonoError> CogMemoryResources<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, MemoryResourceType memory_resource)
{
  auto try_set = [&endpoint_id, &memory_resource](auto& record) -> bool
  {
    using PolicyType = typename std::decay_t<decltype(record)>::PolicyType;
    if (endpoint_id == PolicyType::endpoint_id)
    {
      if constexpr (std::is_same_v<MemoryResourceType, typename PolicyType::MemoryResourceType>)
      {
        record.memory_resource.emplace(memory_resource);
        return true;
      }
      else
      {
        return false;
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
auto CogMemoryResources<Policies...>::make_memory_resources() -> MemoryResourcesTuple
{
  return std::apply([](const auto&... record) { return MemoryResourcesTuple(*record.memory_resource...); }, records_);
}

template <typename... Policies>
template <size_t index>
[[nodiscard]] bool CogMemoryResources<Policies...>::is_memory_resource_set() const
{
  return std::get<index>(records_).memory_resource.has_value();
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogMemoryResources<Policies...>::get_memory_resource(
  jewels::FactoryOut<MemoryResourceRefType<index>> memory_resource) const
{
  const auto& maybe_memory_resource = std::get<index>(records_).memory_resource;
  if (!maybe_memory_resource)
  {
    return jewels::failure;
  }
  *memory_resource = *maybe_memory_resource;
  return jewels::success;
}

template <typename... Policies>
template <size_t index>
jewels::BinaryOutcome CogMemoryResources<Policies...>::set_memory_resource(
  typename std::tuple_element_t<index, MemoryResourcesTuple> memory_resource)
{
  if (is_memory_resource_set<index>())
  {
    return jewels::failure;
  }
  std::get<index>(records_).memory_resource.emplace(std::move(memory_resource));
  return jewels::success;
}

} // namespace clockwork
