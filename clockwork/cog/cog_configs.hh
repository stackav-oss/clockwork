// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/interface.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

namespace clockwork
{

/// Object that contains the data for a loaded config
template <typename ConfigType>
struct CogConfigDataImpl : CogConfigData
{
  CogConfigDataImpl() = default;
  explicit CogConfigDataImpl(std::shared_ptr<ConfigType> input)
    : data(std::move(input))
  {
  }
  std::shared_ptr<ConfigType> data;
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
  template <size_t index>
  using ConfigRefType = std::reference_wrapper<typename std::tuple_element_t<index, PoliciesTuple>::ConfigType>;
  template <size_t index>
  using ConfigHandleType = std::shared_ptr<typename std::tuple_element_t<index, PoliciesTuple>::ConfigType>;
  template <size_t index>
  using ConfigType = typename std::tuple_element_t<index, PoliciesTuple>::ConfigType;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the config handle.
  /// @tparam ConfigType The config type setting.
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, std::shared_ptr<CogConfigData> config);

  /// Store a publisher handle for later snapshot configuration
  /// @param[in] endpoint_id The endpoint id
  /// @param[in] publisher The publisher handle to store
  /// @return Success if the endpoint matches a config, failure otherwise
  jewels::BinaryOutcome
  set_publisher_handle(jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& publisher);

  /// Publish a snapshot immediately (SnapshotOnce)
  /// @param[in] endpoint_id The endpoint id to snapshot
  jewels::BinaryOutcome publish_snapshot(jewels::Uuid<common::EndpointClassId> endpoint_id);

  /// Construct the configs tuple.
  /// @return Tuple of configuration objects.
  [[nodiscard]] ConfigsTuple make_configs();

  /// Test whether the config at the specified index has been set
  /// @tparam index Record tuple index
  /// @return True if the config has been set
  template <size_t index>
  [[nodiscard]] bool is_config_set() const;

  /// Get the config at the specified index
  /// @tparam index
  /// @param[out] config Config reference
  /// @return Success or failure if the config has not been set
  template <size_t index>
  jewels::BinaryOutcome get_config(jewels::FactoryOut<ConfigRefType<index>> config);

  /// Get the config handle at the specified index
  /// @tparam index
  /// @param[out] config_handle Config handle
  /// @return Success or failure if the config has not been set
  template <size_t index>
  jewels::BinaryOutcome get_config_handle(jewels::Out<ConfigHandleType<index>> config_handle) const;

  /// Set the config handle at the specified index
  /// @tparam index
  /// @param[in] config_handle Config handle
  /// @return Success or failure if the config has already been set
  template <size_t index>
  jewels::BinaryOutcome set_config_handle(ConfigHandleType<index> config_handle);

private:
  template <typename Policy>
  struct Record
  {
    using PolicyType = Policy;
    using ConfigType = typename Policy::ConfigType;
    std::shared_ptr<CogConfigDataImpl<ConfigType>> config;
    std::optional<pinion::PublisherHandle> snapshot_publisher;
  };

  using RecordsTuple = std::tuple<Record<Policies>...>;

  /// The config records
  RecordsTuple records_;
};

} // namespace clockwork

#include "clockwork/cog/cog_configs.inl"
