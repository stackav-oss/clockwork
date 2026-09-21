// Copyright 2025-2026 Stack AV Co.
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
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
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
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<pinion::AbstractChannel> channel) override;

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
  /// @param[out] throttled_until_out The publisher eligibility deadline when publisher throttled.
  /// @param[in] current_time The current synchronized time.
  /// @return The preparation result.
  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> throttled_until_out, jewels::time::SyncTime current_time) override;

  /// Check whether this Cog has rate-limited publishers.
  /// @return True when any publisher policy has rate-limit parameters.
  [[nodiscard]] bool has_rate_limited_publishers() const override;

  /// Execute the cog and unlock any shared resoures.
  /// @pre prepare_for_execution() was called and returned ready.
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

  /// Update the output metrics for the publishers
  /// @param resources The resources to use for updating metrics
  /// @tparam ResourcesType The type of the resources
  /// @tparam StatesType the type of the states
  template <typename ResourcesType, typename StatesType>
  void update_resource_metrics(ResourcesType& resources, StatesType& states);

  /// Create the dial by dispatching to Policy::make_dial, appending the dynamic
  /// timer handler argument when the policy uses dynamic timers.
  /// @tparam DialArgs Forwarded argument types for Policy::make_dial
  /// @param args Arguments to forward to Policy::make_dial
  /// @return The constructed dial object
  template <typename... DialArgs>
  auto make_dial_impl(DialArgs&&... args);

  /// Populate cog metrics report group signals from the current execution data.
  /// @param publishables The publishables from this execution
  /// @param exec_complete_time The time the execution completed
  /// @tparam PublishablesType The type of the publishables
  template <typename PublishablesType>
  void populate_cog_metrics_signals(PublishablesType& publishables, jewels::time::SyncTime exec_complete_time);

  /// Compute the conditions mask for the current execution.
  ///
  /// For cogs that publish structured metrics (`publish_metrics`), delegates to the
  /// policy's generated `get_conditions_mask`. For signals-only cogs
  /// (`has_cog_metrics_report_groups`), computes the mask generically using
  /// `is_active()` on each condition handle.
  /// @tparam TimerConds The timer conditions type
  /// @tparam InputConds The input conditions type
  /// @param timer_conditions The timer conditions for this execution
  /// @param input_conditions The input conditions for this execution
  /// @return The computed conditions mask
  template <typename TimerConds, typename InputConds>
  [[nodiscard]] uint64_t
  get_conditions_mask(const TimerConds& timer_conditions, const InputConds& input_conditions) const noexcept;

  /// Execute user code with start/end signals and metrics bookkeeping.
  /// @tparam DialType The type of the dial
  /// @tparam TimerConds The timer conditions type
  /// @tparam InputConds The input conditions type
  /// @param dial The constructed dial to pass to Policy::execute
  /// @param start_time The execution start time
  /// @param timer_conditions The prepared timer conditions
  /// @param input_conditions The prepared input conditions
  template <typename DialType, typename TimerConds, typename InputConds>
  void run_cog_execution(
    DialType& dial, jewels::time::SyncTime start_time, TimerConds& timer_conditions, InputConds& input_conditions);

  /// Handle alignment resolution miss (stale or pending).
  /// @param inputs The constructed dial inputs tuple
  /// @param guard The optional scope guard (reset on early return)
  /// @param start_time The execution start time
  /// @return true if execution should be skipped (miss handled), false if resolved
  template <typename InputDialTuple, typename GuardType>
  bool handle_alignment_miss(InputDialTuple& inputs, GuardType& guard, jewels::time::SyncTime start_time)
    requires(requires { &Policy::resolve_alignment; });

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

  /// The start time of the previous execution, for computing execution period
  /// for cog metrics signals. Tracked separately from CogMetrics because
  /// CogMetrics' internal previous_execution_start_time_ is overwritten during
  /// commit_metrics() before we can read it.
  std::optional<jewels::time::SyncTime> previous_exec_start_for_signals_;

  /// Stored conditions from prepare execution.
  std::optional<typename TimersType::ConditionsTuple> prepared_timer_conditions_;
  std::optional<typename ConditionsType::ConditionsTuple> prepared_input_conditions_;
};

} // namespace clockwork

#include "clockwork/cog/simple_cog.inl"
