// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace clockwork
{

template <typename Policy>
class SimpleCog : public CogBase
{
public:
  using MemoryResourcesType = typename Policy::MemoryResourcesType;
  using ConfigsType = typename Policy::ConfigsType;
  using StatesType = typename Policy::StatesType;
  using TimersType = typename Policy::TimersType;
  using InputsType = typename Policy::InputsType;
  using ConditionsType = typename Policy::ConditionsType;
  using PublishersType = typename Policy::PublishersType;
  using DiagnosticsType = typename Policy::DiagnosticsType;
  using InfraDiagnosticsType = typename Policy::InfraDiagnosticsType;
  using SignalApiType = typename Policy::SignalApiType;

  static constexpr auto cog_id = Policy::cog_id;
  static constexpr auto event_metrics_batch_size = Policy::event_metrics_batch_size;
  /// Constructor.
  /// @param[in] resource The memory resource
  /// @param[in] instance_id The uuid of this cog instance
  /// @param[in] queue The queue to use when the cog is ready to execute.
  explicit SimpleCog(
    jewels::memory::MemoryResource resource,
    const jewels::Uuid<common::CogInstanceId>& instance_id,
    jewels::memory::ObjectPtr<AbstractCogQueue> queue);

  /// Destructor.
  ~SimpleCog() override;

  SimpleCog(const SimpleCog&) = delete;
  SimpleCog& operator=(const SimpleCog&) = delete;
  SimpleCog(SimpleCog&&) = delete;
  SimpleCog& operator=(SimpleCog&&) = delete;

  /// Get the cog name.
  /// @return The name of the cog.
  [[nodiscard]] std::string_view get_name() const override;

  /// Get the cog name.
  /// @return The name of the cog.
  [[nodiscard]] const jewels::Uuid<common::CogInstanceId>& get_instance_id() const override;

  /// Set the config
  /// @param[in] uuid The id of the config endpoint
  /// @param[in] config The underlying config
  /// @return The observer to associate with the config on success
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, jewels::memory::MemoryResource memory_resource) override;

  /// Set the config
  /// @param[in] uuid The id of the config endpoint
  /// @param[in] config The underlying config
  /// @return The observer to associate with the config on success
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogConfigData> config) override;

  /// Set the state
  /// @param[in] uuid The id of the state endpoint
  /// @param[in] state The underlying state
  /// @return The observer to associate with the state on success
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogStateData> state, bool is_shared) override;

  /// Set the timer
  /// @param[in] uuid The id of the timer endpoint
  /// @param[in] timer The underlying timer
  /// @return The observer to associate with the timer on success
  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<AbstractTimer> timer) override;

  /// Set the subscriber
  /// @param[in] uuid The id of the subscriber endpoint
  /// @param[in] handle The underlying subscriber
  /// @return The observer to associate with the subscriber on success
  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::SubscriberHandle handle) override;

  /// Set the publisher
  /// @param[in] uuid The id of the publisher endpoint
  /// @param[in] handle The underlying publisher
  /// @param[in] connected Whether the publisher is connected to a channel
  /// @return True on success
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::PublisherHandle&& handle, bool connected) override;

  /// Set a snapshot configuration for a state or config endpoint
  /// @param[in] uuid The id of the endpoint (state or config) to snapshot
  /// @param[in] snapshot_config The snapshot configuration (interval/cycles/etc)
  /// @return Success or failure
  jewels::BinaryOutcome set_snapshot_config(
    jewels::Uuid<common::EndpointClassId> uuid, const Tappy<common::SnapshotConfig>& snapshot_config) override;

  /// Set up a subscriber endpoint without a handle for non-connected endpoints
  /// @param[in] uuid The id of the subscriber endpoint to set up
  /// @return Success if endpoint was set up successfully, error otherwise
  jewels::expected<void, jewels::MonoError> set_subscriber(jewels::Uuid<common::EndpointClassId> uuid) override;
  /// Validate that all the internal handles have been set.
  /// @return Unexpected if any required handles are unset
  [[nodiscard]] jewels::expected<void, jewels::MonoError> validate() override;

  /// Notify the cog on input changes.
  void notify(jewels::time::SyncTime current_time);

  /// Prime the cog. Perform any required startup tasks (i.e. prime timers).
  /// @return Unexpected if any required handles are unset
  [[nodiscard]] jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime start_time) override;

  /// Perform any necessary action to prepare this Cog for execution.
  /// E.g. Aquire any necessary locks (e.g. shared state mutexes).
  /// @note Every successfull call to prepare_for_exection __must__ be followed
  ///   by a call to execute.
  /// @return true if the Cog is is ready to be executed.
  [[nodiscard]] jewels::expected<void, CogExecutionError>
  prepare_for_execution(jewels::time::SyncTime current_time) override;

  /// Execute the cog and unlock any shared resoures.
  /// @pre prepare_for_execution() was called and returned true.
  /// @param[in] params Execution parameters (start time, etc).
  /// @return true if the Cog executed successfully.
  [[nodiscard]] jewels::expected<void, CogExecutionError> execute(CogExecuteParams params) override;

private:
  /// Process any notifies that have been received
  /// @param[in] notify_guard Unique lock holding notify_mutex_
  void process_pending_notifies(std::unique_lock<std::mutex>& notify_guard);

  /// Check if telemetry metrics should be published
  /// @param current_time The current time
  /// @return true if telemetry metrics should be published, false otherwise
  bool should_send_telemetry_metrics(jewels::time::SyncTime current_time);

  /// Check if event metrics should be published
  /// @param current_time The current time
  /// @return true if event metrics should be published, false otherwise
  bool should_send_event_metrics(jewels::time::SyncTime current_time);

  /// Publish the metrics to the publishers
  /// @param publishables The publishables to use for publishing metrics
  /// @param execution_start_time The start time of the execution
  /// @tparam PublishablesType The type of the publishables
  template <typename PublishablesType>
  void publish_metrics(PublishablesType& publishables, jewels::time::SyncTime execution_start_time);

  /// Update the output metrics for the publishers
  /// @param publishables The publishables to use for publishing metrics
  /// @tparam PublishablesType The type of the publishables
  template <typename PublishablesType>
  void update_output_metrics(PublishablesType& publishables);

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Id of this instance
  jewels::Uuid<common::CogInstanceId> instance_id_;

  /// Reentry mutex to protect against running the same instance concurrently.
  /// Also protects the conditions members.
  std::mutex reentry_mutex_;

  /// Mutex used to guard state for pending notifies
  std::mutex notify_mutex_;

  /// Latest time from a pending nofify
  std::optional<jewels::time::SyncTime> maybe_pending_notify_time_;

  /// Execution statistics
  CogStatistics statistics_;

  // The cog memory resources
  MemoryResourcesType memory_resources_;

  /// The cog configs
  ConfigsType configs_;

  /// The cog states
  StatesType states_;

  /// The cog timers
  TimersType timers_;

  /// The cog inputs
  InputsType inputs_;

  /// The cog conditions
  ConditionsType conditions_;

  /// The cog publishers
  PublishersType publishers_;

  /// The cog diagnostics
  DiagnosticsType diagnostics_;

  /// The cog infra diagnostics
  InfraDiagnosticsType infra_diagnostics_;

  /// The cog signal API
  SignalApiType signals_;

  /// The cog metrics
  CogMetrics metrics_;

  /// The last time telemetry metrics were sent
  std::optional<jewels::time::SyncTime> last_telemetry_sent_time_;

  /// The last time event metrics were sent
  std::optional<jewels::time::SyncTime> last_event_sent_time_;

  /// Stored conditions from prepare execution.
  std::optional<typename TimersType::ConditionsTuple> prepared_timer_conditions_;
  std::optional<typename ConditionsType::ConditionsTuple> prepared_input_conditions_;
};

} // namespace clockwork

#include "clockwork/cog/simple_cog.inl"
