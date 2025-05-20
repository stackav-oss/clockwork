// IWYU pragma: private, include "clockwork/cog/cog_configs.hh"
#pragma once

#include "clockwork/cog/cog_configs.hh"

#include "clockwork/cog/detail.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
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
  jewels::Uuid<common::EndpointClassId> endpoint_id, std::shared_ptr<const CogConfigData> config)
{
  auto try_set = [&endpoint_id, &config](auto& record) -> bool
  {
    using PolicyType = typename std::decay_t<decltype(record)>::PolicyType;
    if (endpoint_id == PolicyType::endpoint_id)
    {
      using ConfigType = typename PolicyType::ConfigType;
      if (auto ptr = std::dynamic_pointer_cast<const CogConfigDataImpl<ConfigType>>(std::move(config)); ptr)
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
auto CogConfigs<Policies...>::make_configs() -> ConfigsTuple
{
  return std::apply([](const auto&... record) { return ConfigsTuple(record.config->data...); }, records_);
}

} // namespace clockwork
