// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/cog_diagnostics.hh"
#include "clockwork/cog/cog_publishers.hh"
#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

namespace clockwork::testing
{

/// Handle returned by UnitTestCogInput::publish() containing the sequence number of the published message
struct MessageHandle
{
  uint64_t seqno;
};

/// Memory resources wrapper for the generated unit test cog wrappers
/// @tparam MemoryResourcesType Cog memory resources
template <typename MemoryResourcesType>
class UnitTestCogMemoryResources
{
public:
  /// Constructor
  /// @param[in] memory_resources Wrapped memory resources
  explicit UnitTestCogMemoryResources(jewels::memory::ObjectPtr<MemoryResourcesType> memory_resources);

  ~UnitTestCogMemoryResources() noexcept = default;

  UnitTestCogMemoryResources(const UnitTestCogMemoryResources&) noexcept = default;
  UnitTestCogMemoryResources& operator=(const UnitTestCogMemoryResources&) noexcept = default;
  UnitTestCogMemoryResources(UnitTestCogMemoryResources&&) noexcept = default;
  UnitTestCogMemoryResources& operator=(UnitTestCogMemoryResources&&) noexcept = default;

  /// Test whether a memory resource handle has been set
  /// @tparam[index] Memory resource index
  /// @return True if the memory resource handle has been set
  template <size_t index>
  [[nodiscard]] bool is_memory_resource_set() const;

  /// Memory resource value accessor, requires that memory resource has been set or the cog has been initialized
  /// @tparam index Memory resource index
  /// @return Memory resource value
  /// @throws runtime_error if the memory resource has not been set
  template <size_t index>
  [[nodiscard]] const typename std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::
    MemoryResourceType&
    get_memory_resource();

  /// Set the memory resource at the specified index
  /// @tparam index Memory resource index
  /// @param[in] memory_resource Memory resource to set
  /// @throws runtime_error if the memory resource has already been set
  template <size_t index>
  void set_memory_resource(
    typename std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::MemoryResourceType
      memory_resource);

  /// Initialize any unset memory resources by default constructing an instance
  void initialize_memory_resources();

private:
  /// Initialize the memory resource at the specified index if it has not been set
  /// @tparam index Memory resource index
  template <size_t index>
  void initialize_memory_resource_if_not_set();

  /// Wrapped memory resources
  jewels::memory::ObjectPtr<MemoryResourcesType> memory_resources_;
};

/// Configs wrapper for the generated unit test cog wrappers
/// @tparam ConfigsType Cog configs type
template <typename ConfigsType>
class UnitTestCogConfigs
{
public:
  /// Constructor
  /// @param[in] configs Wrapped configs
  explicit UnitTestCogConfigs(jewels::memory::ObjectPtr<ConfigsType> configs);

  ~UnitTestCogConfigs() noexcept = default;

  UnitTestCogConfigs(const UnitTestCogConfigs&) noexcept = default;
  UnitTestCogConfigs& operator=(const UnitTestCogConfigs&) noexcept = default;
  UnitTestCogConfigs(UnitTestCogConfigs&&) noexcept = default;
  UnitTestCogConfigs& operator=(UnitTestCogConfigs&&) noexcept = default;

  /// Test whether a config handle has been set
  /// @tparam[index] Config index
  /// @return True if the config handle has been set
  template <size_t index>
  [[nodiscard]] bool is_config_set() const;

  /// Config value accessor, requires that config has been set or the cog has been initialized
  /// @tparam index Config index
  /// @return Config value
  /// @throws runtime_error if the config has not been set
  template <size_t index>
  [[nodiscard]] typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType& get_config();

  /// Get the condfiguration handle at the specified index
  /// @tparam index Config index
  /// @return Config handle
  /// @throws runtime_error if the config has not been set
  template <size_t index>
  [[nodiscard]] std::shared_ptr<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType>
  get_config_handle() const;

  /// Set the configuration handle at the specified index
  /// @tparam index Config index
  /// @param[in] config Config to set
  /// @throws runtime_error if the config has already been set
  template <size_t index>
  void set_config_handle(
    std::shared_ptr<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType>
      config_handle);

  /// Initialize any unset configs by default constructing an instance
  void initialize_configs();

private:
  /// Initialize the config at the specified index to a default constructed value if it has not been set
  /// @tparam index Index sequence over the elements in the records tuple
  template <size_t index>
  void initialize_config_if_not_set();

  /// Wrapped configs
  jewels::memory::ObjectPtr<ConfigsType> configs_;
};

/// States wrapper for the generated unit test cog wrappers
/// @tparam StatesType Cog states type
template <typename StatesType>
class UnitTestCogStates
{
public:
  /// Constructor
  /// @param[in] states Wrapped states
  /// @param[in] channel_factory Shared memory channel factory
  UnitTestCogStates(
    jewels::memory::ObjectPtr<StatesType> states,
    jewels::memory::ObjectPtr<pinion::AbstractChannelFactory> channel_factory,
    std::span<std::shared_ptr<pinion::AbstractPublisher>, StatesType::policy_count> state_publishers);

  ~UnitTestCogStates() noexcept = default;

  UnitTestCogStates(const UnitTestCogStates&) noexcept = default;
  UnitTestCogStates& operator=(const UnitTestCogStates&) noexcept = default;
  UnitTestCogStates(UnitTestCogStates&&) noexcept = default;
  UnitTestCogStates& operator=(UnitTestCogStates&&) noexcept = default;

  /// Test whether a state handle has been set
  /// @tparam[index] State index
  /// @return True if the state handle has been set
  template <size_t index>
  [[nodiscard]] bool is_state_set() const;

  /// State value accessor, requires that state has been set or the cog has been initialized
  /// @tparam index State index
  /// @return State value
  /// @throws runtime_error if the state has not been set
  template <size_t index>
  [[nodiscard]] typename StatesType::template SchemaType<index>& get_state();

  /// Get the state handle at the specified index
  /// @tparam index State index
  /// @return State handle
  /// @throws runtime_error if the state has not been set
  template <size_t index>
  typename StatesType::template RecordPtrType<index> get_state_handle() const;

  /// Set the state handle at the specified index
  /// @tparam index State index
  /// @param[in] state_handle State handle to set
  /// @throws runtime_error if the state has already been set
  template <size_t index>
  void set_state_handle(typename StatesType::template RecordPtrType<index> state_handle);

  /// Initialize any unset states by default constructing an instance
  /// @param[in] memory_resource Memory resource
  void initialize_states(const jewels::memory::MemoryResource& memory_resource);

private:
  /// Initialize the state at the specified index to a default constructed value if it has not been set
  /// @tparam index Index sequence over the elements in the records tuple
  /// @param[in] memory_resource Memory resource
  /// @{
  template <size_t index>
  void initialize_state_if_not_set(const jewels::memory::MemoryResource& memory_resource)
    requires TappyType<typename StatesType::template SchemaType<index>>;

  template <size_t index>
  void initialize_state_if_not_set(const jewels::memory::MemoryResource& memory_resource)
    requires(!TappyType<typename StatesType::template SchemaType<index>>);
  /// @}

  /// Wrapped states
  jewels::memory::ObjectPtr<StatesType> states_;

  /// Shared memory channel factory
  jewels::memory::ObjectPtr<pinion::AbstractChannelFactory> channel_factory_;

  /// Shared memory publishers backing the state handles
  std::span<std::shared_ptr<pinion::AbstractPublisher>, StatesType::policy_count> state_publishers_;
};

/// Publishable handle used to publish messages to the inputs of generated unit test cog wrappers
/// @tparam MsgType Input message type
template <typename MsgType>
class UnitTestCogInputPublishable
{
public:
  /// Contructor
  /// @param[in] publish_time Message publish time
  /// @param[in] reserved_slot Message reserved slot
  UnitTestCogInputPublishable(jewels::time::SyncTime publish_time, pinion::PublisherReservation&& reserved_slot);

  /// Destructor publishes the message if it has not been explicitly discarded
  ~UnitTestCogInputPublishable();

  UnitTestCogInputPublishable(const UnitTestCogInputPublishable&) noexcept = default;
  UnitTestCogInputPublishable& operator=(const UnitTestCogInputPublishable&) noexcept = default;
  UnitTestCogInputPublishable(UnitTestCogInputPublishable&&) noexcept = default;
  UnitTestCogInputPublishable& operator=(UnitTestCogInputPublishable&&) noexcept = default;

  /// Message accessor
  /// @return Reserved slot message
  MsgType& message();

private:
  /// Message publish time
  jewels::time::SyncTime publish_time_;

  /// Reserved pinion slot
  pinion::PublisherReservation reserved_slot_;

  /// Publishable for the reserved slot
  pinion::Publishable<MsgType> publishable_;
};

/// Input wrapper for the generated unit test cog wrappers
/// @tparam InputType Cog input type
template <typename InputType>
class UnitTestCogInput
{
  template <typename OtherInputType>
  friend class UnitTestCogInput;

public:
  /// Constructor
  /// @param[in] is_initialized Flag set when the cog has been initialized
  /// @param[in] slot_count Input channel slot count
  /// @param[in] input_publisher Input channel publisher
  /// @param[in] publish_count Shared publish counter for this input channel
  UnitTestCogInput(
    jewels::memory::ObjectPtr<bool> is_initialized,
    jewels::memory::ObjectPtr<uint32_t> slot_count,
    jewels::memory::ObjectPtr<std::shared_ptr<pinion::AbstractPublisher>> input_publisher,
    jewels::memory::ObjectPtr<uint64_t> publish_count);

  ~UnitTestCogInput() noexcept = default;

  UnitTestCogInput(const UnitTestCogInput&) noexcept = default;
  UnitTestCogInput& operator=(const UnitTestCogInput&) noexcept = default;
  UnitTestCogInput(UnitTestCogInput&&) noexcept = default;
  UnitTestCogInput& operator=(UnitTestCogInput&&) noexcept = default;

  /// Constructor used for multi-connect input elements
  template <typename OtherInputType>
  explicit UnitTestCogInput(const UnitTestCogInput<OtherInputType>& other) noexcept;

  /// Get the number of slots for the input
  /// @return Slot count
  [[nodiscard]] uint32_t get_num_slots() const;

  /// Set the number of slots for the input
  /// @param[in] num_slots Slot count
  /// @throws runtime_error if the cog has been initialized
  void set_num_slots(uint32_t num_slots);

  /// Publish a message on the input channel
  /// @param[in] message Message to publish
  /// @param[in] publish_time Message publish time
  /// @return Handle containing the sequence number of the published message
  /// @throws runtime_error if the slot cannot be reserved
  testing::MessageHandle publish(const typename InputType::MsgType& message, jewels::time::SyncTime publish_time);

  /// Publish a message on the input channel using an initializer function
  /// @param[in] init_fn Function to initialize the message to publish
  /// @param[in] publish_time Message publish time
  /// @return Handle containing the sequence number of the published message
  /// @throws runtime_error if the slot cannot be reserved
  testing::MessageHandle
  publish(const std::function<void(typename InputType::MsgType&)>& init_fn, jewels::time::SyncTime publish_time);

private:
  /// Get a handle for publishing a message to the input channel
  /// @param[in] publish_time Message publish time
  /// @throws runtime_error if the slot cannot be reserved
  [[nodiscard]] UnitTestCogInputPublishable<typename InputType::MsgType> reserve(jewels::time::SyncTime publish_time);

  /// Flag set when the cog has been initialized
  jewels::memory::ObjectPtr<bool> is_initialized_;

  /// Number slots for the input channel
  jewels::memory::ObjectPtr<uint32_t> slot_count_;

  /// Input shared memory publisher
  jewels::memory::ObjectPtr<std::shared_ptr<pinion::AbstractPublisher>> input_publisher_;

  /// Shared monotonic counter tracking published messages (mirrors SHM publisher seqno)
  jewels::memory::ObjectPtr<uint64_t> publish_count_;
};

/// Inputs wrapper for the generated unit test cog wrappers
/// @tparam InputsType Cog inputs type
template <typename InputsType>
class UnitTestCogInputs
{
public:
  /// Constructor
  /// @param[in] is_initialized Flag set when the cog has been initialized
  /// @param[in] slot_counts Input channel slot counts
  /// @param[in] input_publishers Input channel publishers
  /// @param[in] slot_counts Input channel publish counts
  UnitTestCogInputs(
    jewels::memory::ObjectPtr<bool> is_initialized,
    std::span<uint32_t, InputsType::policy_count> slot_counts,
    std::span<std::shared_ptr<pinion::AbstractPublisher>, InputsType::policy_count> input_publishers,
    std::span<uint64_t, InputsType::policy_count> publish_counts);

  ~UnitTestCogInputs() noexcept = default;

  UnitTestCogInputs(const UnitTestCogInputs&) noexcept = default;
  UnitTestCogInputs& operator=(const UnitTestCogInputs&) noexcept = default;
  UnitTestCogInputs(UnitTestCogInputs&&) noexcept = default;
  UnitTestCogInputs& operator=(UnitTestCogInputs&&) noexcept = default;

  /// Get the input at the specified index
  /// @tparam[in] index Input index
  /// @return Cog input handle
  template <size_t index>
  [[nodiscard]] UnitTestCogInput<typename InputsType::template PolicyType<index>> get_input();

private:
  /// Flag set when the cog has been initialized
  jewels::memory::ObjectPtr<bool> is_initialized_;

  /// Number slots for each input channel
  std::span<uint32_t, InputsType::policy_count> slot_counts_;

  /// Input channel publishers
  std::span<std::shared_ptr<pinion::AbstractPublisher>, InputsType::policy_count> input_publishers_;

  /// Per-channel publish counters (shadows SHM publisher seqno)
  std::span<uint64_t, InputsType::policy_count> publish_counts_;
};

/// Output wrapper for the generated unit test cog wrappers
/// @tparam OutputViewPolicy Output view policy type
template <typename OutputViewPolicy>
class UnitTestCogOutput
{
public:
  /// Constructor
  /// @param[in] is_initialized Flag set when the cog has been initialized
  /// @param[in] slot_count Output channel slot count
  /// @param[in] output_view View to the messages published on the output channel
  UnitTestCogOutput(
    jewels::memory::ObjectPtr<bool> is_initialized,
    jewels::memory::ObjectPtr<std::shared_ptr<InputView<OutputViewPolicy>>> output_view);

  ~UnitTestCogOutput() noexcept = default;

  UnitTestCogOutput(const UnitTestCogOutput&) noexcept = default;
  UnitTestCogOutput& operator=(const UnitTestCogOutput&) noexcept = default;
  UnitTestCogOutput(UnitTestCogOutput&&) noexcept = default;
  UnitTestCogOutput& operator=(UnitTestCogOutput&&) noexcept = default;

  /// Get the next message from the output channel
  ///
  /// The message reference remains valid until the next cog execution.
  ///
  /// @return The next message from the output channel
  /// @throws runtime_error if the cog has not been initialized or no message is available
  [[nodiscard]] const typename OutputViewPolicy::MsgType& get_next_message();

  /// Get the next message from the output channel if available
  ///
  ///
  /// The message reference remains valid until the next cog execution.
  ///
  /// @return The next message from the output channel or nullopt if no message is available
  /// @throws runtime_error if the cog has not been initialized
  std::optional<std::reference_wrapper<const typename OutputViewPolicy::MsgType>> try_get_next_message();

private:
  /// Get the next message from the output channel in a view
  ///
  /// The messages in the view remain valid until the next cog execution.
  ///
  /// @return View containing the next message or an empty view if no messages are available
  /// @throws runtime_error if the cog has not been initialized or error occurs
  [[nodiscard]] auto get_next_view();

  /// Flag set when the cog has been initialized
  jewels::memory::ObjectPtr<bool> is_initialized_;

  /// Input view to the messages published on the output channel
  jewels::memory::ObjectPtr<std::shared_ptr<InputView<OutputViewPolicy>>> output_view_;
};

/// Outputs wrapper for the generated unit test cog wrappers
/// @tparam OutputsType Cog outputs type
template <typename PublishersType>
class UnitTestCogOutputs
{
public:
  /// Constructor
  /// @param[in] is_initialized Flag set when the cog has been initialized
  /// @param[in] output_views Views to the messages published on the output channels
  UnitTestCogOutputs(
    jewels::memory::ObjectPtr<bool> is_initialized,
    jewels::memory::ObjectPtr<typename PublishersType::UnitTestCogOutputViewTuple> output_views);

  ~UnitTestCogOutputs() noexcept = default;

  UnitTestCogOutputs(const UnitTestCogOutputs&) noexcept = default;
  UnitTestCogOutputs& operator=(const UnitTestCogOutputs&) noexcept = default;
  UnitTestCogOutputs(UnitTestCogOutputs&&) noexcept = default;
  UnitTestCogOutputs& operator=(UnitTestCogOutputs&&) noexcept = default;

  /// Get the output at the specified index
  /// @tparam[in] index Output index
  /// @return Cog output handle
  template <size_t index>
  [[nodiscard]] UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<index>> get_output();

private:
  /// Flag set when the cog has been initialized
  jewels::memory::ObjectPtr<bool> is_initialized_;

  /// Views to the messages published on the output channels
  jewels::memory::ObjectPtr<typename PublishersType::UnitTestCogOutputViewTuple> output_views_;
};

/// Diagnostics wrapper for the generated unit test cog wrappers
/// @tparam DiagnosticsType Cog diagnostics type
template <typename DiagnosticsType>
class UnitTestCogDiagnostics
{
public:
  /// Constructor
  /// @param[in] is_initialized Flag set when the cog has been initialized
  /// @param[in] report_views Views to the messages published on the diagnostics channels
  UnitTestCogDiagnostics(
    jewels::memory::ObjectPtr<bool> is_initialized,
    jewels::memory::ObjectPtr<typename DiagnosticsType::UnitTestCogOutputViewTuple> report_views);

  ~UnitTestCogDiagnostics() noexcept = default;

  UnitTestCogDiagnostics(const UnitTestCogDiagnostics&) noexcept = default;
  UnitTestCogDiagnostics& operator=(const UnitTestCogDiagnostics&) noexcept = default;
  UnitTestCogDiagnostics(UnitTestCogDiagnostics&&) noexcept = default;
  UnitTestCogDiagnostics& operator=(UnitTestCogDiagnostics&&) noexcept = default;

  /// Get the diagnostic at the specified index
  /// @tparam[in] index Diagnostic index
  /// @return Cog diagnostic handle
  template <size_t index>
  [[nodiscard]] UnitTestCogOutput<typename DiagnosticsType::template UnitTestOutputViewPolicyType<index>>
  get_diagnostic();

private:
  /// Flag set when the cog has been initialized
  jewels::memory::ObjectPtr<bool> is_initialized_;

  /// Views to the reports published on the diagnostics channels
  jewels::memory::ObjectPtr<typename DiagnosticsType::UnitTestCogOutputViewTuple> report_views_;
};

/// Cog class used by the generated cog unit test wrappers
template <typename Policy>
class UnitTestCog : public CogBase
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
  explicit UnitTestCog(
    jewels::memory::MemoryResource resource,
    const jewels::Uuid<common::CogInstanceId>& instance_id,
    jewels::memory::ObjectPtr<AbstractCogQueue> queue);

  /// Destructor.
  ~UnitTestCog() override = default;

  UnitTestCog(const UnitTestCog&) = delete;
  UnitTestCog& operator=(const UnitTestCog&) = delete;
  UnitTestCog(UnitTestCog&&) = delete;
  UnitTestCog& operator=(UnitTestCog&&) = delete;

  /// @see AbstractCog::get_name
  [[nodiscard]] std::string_view get_name() const override;

  /// @see AbstractCog::get_instance_id
  [[nodiscard]] const jewels::Uuid<common::CogInstanceId>& get_instance_id() const override;

  /// @see AbstractCog::set_handle
  /// @{
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, jewels::memory::MemoryResource memory_resource) override;

  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogConfigData> config) override;

  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<CogStateData> state, bool is_shared) override;

  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<AbstractTimer> timer) override;

  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, std::shared_ptr<pinion::AbstractChannel> subscriber) override;

  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  set_handle(jewels::Uuid<common::EndpointClassId> uuid, pinion::PublisherHandle&& handle, bool connected) override;
  /// @}

  /// @see AbstractCog::set_snapshot_config
  jewels::BinaryOutcome set_snapshot_config(
    jewels::Uuid<common::EndpointClassId> uuid, const Tappy<common::SnapshotConfig>& snapshot_config) override;

  /// @see AbstractCog::set_subscriber
  jewels::expected<void, jewels::MonoError> set_subscriber(jewels::Uuid<common::EndpointClassId> uuid) override;

  /// @see AbstractCog::validate
  [[nodiscard]] jewels::expected<void, jewels::MonoError> validate() override;

  /// @see AbstractCog::notify
  void notify(jewels::time::SyncTime current_time);

  /// @see AbstractCog::prime
  [[nodiscard]] jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime start_time) override;

  /// Prime the cog. Perform any required startup tasks (i.e. prime timers).
  /// @param[in] start_time Cog start time
  /// @param[in] memory_resource Memory resource used for initializing states
  /// @return Unexpected if any required handles are unset
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  prime(jewels::time::SyncTime start_time, const jewels::memory::MemoryResource& memory_resource);

  /// @see AbstractCog::prepare_for_execution
  CogPrepareOutcome prepare_for_execution(
    jewels::Out<jewels::time::SyncTime> throttled_until_out, jewels::time::SyncTime current_time) override;

  /// Notify expired unit-test timers without preparing a new condition snapshot.
  ///
  /// This is useful for tests that need to model a timer expiring between
  /// condition preparation and execution.
  void notify_expired_timers_for_test(jewels::time::SyncTime current_time);

  /// @see AbstractCog::execute
  [[nodiscard]] jewels::expected<void, CogExecutionError> execute(CogExecuteParams params) override;

  /// Execute the cog
  /// @param[in] params Execution parameters (start time, etc).
  /// @param[in] exec_duration Execute duration
  /// @return true if the Cog executed successfully.
  [[nodiscard]] jewels::expected<void, CogExecutionError>
  execute(const CogExecuteParams& params, std::chrono::microseconds exec_duration);

  /// Get a handle for the cog memory resources
  /// @return Cog memory resource handle
  [[nodiscard]] UnitTestCogMemoryResources<MemoryResourcesType> get_memory_resources();

  /// Get a handle for the cog configs
  /// @return Cog configuration handle
  [[nodiscard]] UnitTestCogConfigs<ConfigsType> get_configs();

  /// Get a handle for the cog states
  /// @return Cog states handle
  [[nodiscard]] UnitTestCogStates<StatesType> get_states();

  /// Get a handle for the cog inputs
  /// @return Cog inputs handle
  [[nodiscard]] UnitTestCogInputs<InputsType> get_inputs();

  /// Get a handle for the cog outputs
  /// @return Cog outputs handle
  [[nodiscard]] UnitTestCogOutputs<PublishersType> get_outputs();

  /// Get a handle for the cog diagnostics
  /// @return Cog diagnostics handle
  [[nodiscard]] UnitTestCogDiagnostics<DiagnosticsType> get_diagnostics();

  /// Get a handle for the cog infrastructure diagnostics
  /// @return Cog diagnostics handle
  [[nodiscard]] UnitTestCogOutput<typename InfraDiagnosticsType::UnitTestOutputViewPolicyType> get_infra_diagnostics();

  /// Get the pinion shared memory root directory
  /// @return Pinion shm root directory
  [[nodiscard]] std::string_view get_pinion_shm_root() const;

  /// Get the pinion unix socket namespace
  /// @return Pinion namespace
  [[nodiscard]] std::string_view get_pinion_namespace() const;

  /// Get the shared memory channel factory used by the unit test wrapper
  /// @return Shared memory channel factory
  [[nodiscard]] pinion::AbstractChannelFactory& get_channel_factory();

  /// Get the SHM publisher backing the input channel at the specified index.
  /// @tparam index Input policy index
  /// @return Reference to the input publisher
  template <size_t index>
  [[nodiscard]] std::shared_ptr<pinion::AbstractPublisher>& get_input_publisher();

  /// Get the SHM publisher backing the output channel at the specified index.
  /// @tparam index Output policy index
  /// @return Reference to the output publisher
  template <size_t index>
  [[nodiscard]] std::shared_ptr<pinion::AbstractPublisher>& get_output_publisher();

  /// Replace an input's subscriber view and rewire all conditions matching
  /// this input's endpoint ID to point to the given publisher's buffer.
  /// Must be called after prime().
  /// @tparam index Input policy index
  /// @param publisher The new publisher to subscribe to
  template <size_t index>
  void rewire_input(const std::shared_ptr<pinion::AbstractPublisher>& publisher);

private:
  /// Initialize the timers for the unit test
  void initialize_timers();

  /// Initialize the inputs for the unit test
  void initialize_inputs();

  /// Initialize the outputs for the unit test
  void initialize_outputs();

  /// Initialize the diagnostics for the unit test
  void initialize_diagnostics();

  /// Initialize the infrastructure diagnostics for the unit test
  void initialize_infra_diagnostics();

  /// Initialize the conditions for the inputs for the unit test that patch the entity_id
  /// @tparam endpoint_id Input endpoint ID
  /// @param[in] publisher Shared memory publisher for the input channel
  template <jewels::Uuid<::clockwork::common::EndpointClassId> endpoint_id>
  void initialize_conditions_for_input(const std::shared_ptr<pinion::AbstractPublisher>& channel);

  /// Execution statistics
  CogStatistics statistics_;

  /// Memory resource
  jewels::memory::MemoryResource memory_resource_;

  /// Id of this instance
  jewels::Uuid<common::CogInstanceId> instance_id_;

  /// Temporary unit test directory guard
  jewels::testing::TmpDirectoryGuard temp_dir_;

  /// Pinion shared memory root directory
  std::string pinion_shm_root_;

  /// Pinion unix domain socket namespace
  std::string pinion_namespace_;

  /// Shared memory channel factory
  std::shared_ptr<pinion::AbstractChannelFactory> channel_factory_;

  /// Shared memory channel publishers backing the state handles
  std::array<std::shared_ptr<pinion::AbstractPublisher>, StatesType::policy_count> state_publishers_{};

  /// Input channel slot counts
  std::array<uint32_t, InputsType::policy_count> input_slot_counts_ =
    InputsType::template get_default_unit_test_slot_counts<ConditionsType>();

  /// Input channel publish counts
  std::array<uint64_t, InputsType::policy_count> input_publish_counts_{};

  /// Shared memory channel publishers for the input channels
  std::array<std::shared_ptr<pinion::AbstractPublisher>, InputsType::policy_count> input_publishers_{};

  /// Shared memory channel publishers for the output channels
  std::array<std::shared_ptr<pinion::AbstractPublisher>, PublishersType::policy_count> output_publishers_{};

  /// Input views to the messages published on the output channels
  typename PublishersType::UnitTestCogOutputViewTuple output_views_;

  /// Shared memory channel publishers for diagnostics channels
  std::array<std::shared_ptr<pinion::AbstractPublisher>, DiagnosticsType::policy_count> diagnostics_publishers_{};

  /// Input views to the report messages published on the diagnostics channels
  typename DiagnosticsType::UnitTestCogOutputViewTuple report_views_;

  /// Shared memory channel publishers for diagnostics channels
  std::shared_ptr<pinion::AbstractPublisher> infra_diagnostics_publisher_{};

  /// Input view to the report messages published on the infra diagnostics channel
  std::shared_ptr<InputView<typename InfraDiagnosticsType::UnitTestOutputViewPolicyType>> infra_report_view_;

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

  /// Stored conditions from prepare execution.
  std::optional<typename TimersType::ConditionsTuple> prepared_timer_conditions_;
  std::optional<typename ConditionsType::ConditionsTuple> prepared_input_conditions_;

  /// Flag set when the cog has been initialized
  bool is_initialized_{false};
};

} // namespace clockwork::testing

#include "clockwork/cog/wrappers/unit_test_cog.inl"
