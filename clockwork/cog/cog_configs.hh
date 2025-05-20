// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <tuple>
#include <utility>

namespace clockwork
{

/// Object that contains the data for a loaded config
template <typename ConfigType>
struct CogConfigDataImpl : CogConfigData
{
  CogConfigDataImpl() = default;
  explicit CogConfigDataImpl(ConfigType input)
    : data(std::move(input))
  {
  }
  ConfigType data;
};

/// Helper class to handle the set of cog configs. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies see MessageConfig::Policy
template <typename... Policies>
class CogConfigs
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  using PoliciesTuple = std::tuple<Policies...>;
  using ConfigsTuple = std::tuple<const typename Policies::ConfigType&...>;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the config handle.
  /// @tparam ConfigType The config type setting.
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, std::shared_ptr<const CogConfigData> config);

  /// Construct the configs tuple.
  /// @return Tuple of configuration objects.
  [[nodiscard]] ConfigsTuple make_configs();

private:
  template <typename Policy>
  struct Record
  {
    using PolicyType = Policy;
    using ConfigType = typename Policy::ConfigType;
    std::shared_ptr<const CogConfigDataImpl<ConfigType>> config;
  };

  using RecordsTuple = std::tuple<Record<Policies>...>;

  /// The config records
  RecordsTuple records_;
};

} // namespace clockwork

#include "clockwork/cog/cog_configs.inl"
