// IWYU pragma: private, include "clockwork/cog/cog_memory_resources.hh"
#pragma once

#include "clockwork/cog/cog_memory_resources.hh"

#include "clockwork/cog/detail.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <tuple>
#include <type_traits>

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

} // namespace clockwork
