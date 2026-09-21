// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <span>

namespace clockwork
{

struct CogConfigData
{
  CogConfigData() = default;

  virtual ~CogConfigData();

  CogConfigData(const CogConfigData&) = delete;
  CogConfigData(CogConfigData&&) = delete;
  CogConfigData& operator=(const CogConfigData&) = delete;
  CogConfigData& operator=(CogConfigData&&) = delete;
};

struct CogStateData
{
  CogStateData() = default;

  virtual ~CogStateData();

  CogStateData(const CogStateData&) = delete;
  CogStateData(CogStateData&&) = delete;
  CogStateData& operator=(const CogStateData&) = delete;
  CogStateData& operator=(CogStateData&&) = delete;

  /// Set the state data from a byte span.
  /// This is only supported for schema-based (serialized) states.
  /// For C++ states, this will return failure.
  /// @param[in] data Byte span containing the state data to copy
  /// @return Success if data was copied, failure otherwise
  virtual jewels::BinaryOutcome set_from_bytes(std::span<const std::byte> data) noexcept;
};

class CogBase : public AbstractCog
{
public:
  using AbstractCog::AbstractCog;

  ~CogBase() override;

  CogBase(const CogBase&) = delete;
  CogBase& operator=(const CogBase&) = delete;
  CogBase(CogBase&&) = delete;
  CogBase& operator=(CogBase&&) = delete;

  /// Get the cog name.
  /// @return The name of the cog.
  [[nodiscard]] virtual const jewels::Uuid<common::CogInstanceId>& get_instance_id() const = 0;

  /// Set the memory resource
  /// @param[in] uuid The id of the config endpoint
  /// @param[in] config The underlying config
  /// @return The observer to associate with the config on success
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, jewels::memory::MemoryResource memory_resource) = 0;

  /// Set the config
  /// @param[in] uuid The id of the config endpoint
  /// @param[in] config The underlying config
  /// @return The observer to associate with the config on success
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogConfigData> config) = 0;

  /// Set the state
  /// @param[in] uuid The id of the state endpoint
  /// @param[in] state The underlying state
  /// @return The observer to associate with the state on success
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogStateData> state, bool is_shared) = 0;

  /// Set the timer
  /// @param[in] uuid The id of the timer endpoint
  /// @param[in] timer The underlying timer
  /// @return The observer to associate with the timer on success
  [[nodiscard]] virtual jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<AbstractTimer> timer) = 0;

  /// Set the subscriber
  /// @param[in] uuid The id of the subscriber endpoint
  /// @param[in] handle The underlying subscriber
  /// @return The observer to associate with the subscriber on success
  [[nodiscard]] virtual jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<pinion::AbstractChannel> channel) = 0;

  /// Set up a subscriber endpoint without a handle for non-connected endpoints
  /// @param[in] uuid The id of the subscriber endpoint to set up
  /// @return Success if endpoint was set up successfully, error otherwise
  virtual jewels::expected<void, jewels::MonoError> set_subscriber(jewels::Uuid<common::EndpointClassId> uuid) = 0;

  /// Set the publisher
  /// @param[in] uuid The id of the publisher endpoint
  /// @param[in] handle The underlying publisher
  /// @return True on success
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::PublisherHandle&& handle, bool connected) = 0;

  /// Set a snapshot configuration for a state or config endpoint
  /// @param[in] uuid The id of the endpoint (state or config) to snapshot
  /// @param[in] snapshot_config The snapshot configuration (interval/cycles/etc)
  /// @return Success or failure
  virtual jewels::BinaryOutcome set_snapshot_config(
    jewels::Uuid<common::EndpointClassId> uuid, const Tappy<common::SnapshotConfig>& snapshot_config) = 0;

  /// Validate that all the internal handles have been set.
  /// @return Unexpected if any required handles are unset
  [[nodiscard]] virtual jewels::expected<void, jewels::MonoError> validate() = 0;
};

} // namespace clockwork
