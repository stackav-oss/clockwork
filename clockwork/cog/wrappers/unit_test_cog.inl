// IWYU pragma: private, include "clockwork/cog/wrappers/unit_test_cog.hh"
// IWYU pragma: no_include <strings.h>
#pragma once

#include "clockwork/cog/wrappers/unit_test_cog.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/cog_state.hh"
#include "clockwork/cog/cog_statistics.hh"
#include "clockwork/cog/cog_timers.hh"
#include "clockwork/cog/input_view.hh"
#include "clockwork/cog/interface.hh"
#include "clockwork/cog/unit_test_support.hh"
#include "clockwork/cog/wrappers/dummy_timer.hh"
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/cog_execution_error_clk_cc.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/publisher_slot_ref.hh"
#include "clockwork/pinion/shm_channel_factory.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace clockwork::testing
{

namespace detail
{

/// Make a publishable from a reserved slot
/// @tparam MsgType Message type
/// @throws runtime_error on failure
template <typename MsgType>
[[nodiscard]] pinion::Publishable<MsgType> make_publishable(pinion::PublisherReservation& reserved_slot)
{
  auto publishable_result =
    pinion::Publishable<MsgType>::try_make(jewels::memory::make_non_null_from_ref(reserved_slot));
  if (!publishable_result)
  {
    throw std::runtime_error("Failed to make publishable for reserved slot");
  }
  return std::move(publishable_result).value();
}

/// Make a publisher for a pinion channel
/// @param[in] channel_factory Shared memory channel factory
/// @param[in] channel_name Channel name
/// @param[in] num_slots Number of channel slots
/// @param[in] message_size Message size
/// @param[in] channel_uuid Channel UUID string
/// @return Shared memory publisher
/// @throws runtime error on falure
[[nodiscard]] inline std::shared_ptr<pinion::AbstractPublisher> make_publisher(
  pinion::AbstractChannelFactory& channel_factory,
  std::string_view channel_name,
  size_t num_slots,
  size_t message_size,
  std::string_view channel_uuid = {})
{
  const jewels::memory::MemoryResource memres(std::pmr::get_default_resource());
  std::string uuid_str;
  if (channel_uuid.empty())
  {
    uuid_str = jewels::Uuid<void>::random_uuid().to_string();
    channel_uuid = uuid_str;
  }
  const pinion::BufferLayout layout{
    .num_slots = num_slots,
    .message_size = message_size,
    .is_published_once = false,
  };
  auto publisher = channel_factory.open_publisher(channel_uuid, channel_name, layout, 1);
  if (!publisher)
  {
    throw std::runtime_error(fmt::format("Failed to make publisher for {}", channel_name));
  }
  return publisher.value();
}

} // namespace detail

template <typename MemoryResourcesType>
UnitTestCogMemoryResources<MemoryResourcesType>::UnitTestCogMemoryResources(
  jewels::memory::ObjectPtr<MemoryResourcesType> memory_resources)
  : memory_resources_(memory_resources)
{
}

template <typename MemoryResourcesType>
template <size_t index>
[[nodiscard]] bool UnitTestCogMemoryResources<MemoryResourcesType>::is_memory_resource_set() const
{
  return memory_resources_->template is_memory_resource_set<index>();
}

template <typename MemoryResourcesType>
template <size_t index>
[[nodiscard]] const typename std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::
  MemoryResourceType&
  UnitTestCogMemoryResources<MemoryResourcesType>::get_memory_resource()
{
  jewels::FactoryResult<typename MemoryResourcesType::template MemoryResourceRefType<index>> value;
  if (!jewels::ok(memory_resources_->template get_memory_resource<index>(jewels::Out{value})))
  {
    throw std::runtime_error(
      fmt::format(
        "MemoryResource has not been set for {}",
        std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::name));
  }
  return value->get();
}

template <typename MemoryResourcesType>
template <size_t index>
void UnitTestCogMemoryResources<MemoryResourcesType>::set_memory_resource(
  typename std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::MemoryResourceType memory_resource)
{
  if (!jewels::ok(memory_resources_->template set_memory_resource<index>(memory_resource)))
  {
    throw std::runtime_error(
      fmt::format(
        "MemoryResource has already been set for {}",
        std::tuple_element_t<index, typename MemoryResourcesType::PoliciesTuple>::name));
  }
}

template <typename MemoryResourcesType>
void UnitTestCogMemoryResources<MemoryResourcesType>::initialize_memory_resources()
{
  [this]<std::size_t... index>(std::index_sequence<index...>)
  {
    (this->initialize_memory_resource_if_not_set<index>(), ...);
  }(std::make_index_sequence<MemoryResourcesType::policy_count>());
}

template <typename MemoryResourcesType>
template <size_t index>
void UnitTestCogMemoryResources<MemoryResourcesType>::initialize_memory_resource_if_not_set()
{
  if (!is_memory_resource_set<index>())
  {
    set_memory_resource<index>(jewels::memory::MemoryResource{std::pmr::get_default_resource()});
  }
}

template <typename ConfigsType>
UnitTestCogConfigs<ConfigsType>::UnitTestCogConfigs(jewels::memory::ObjectPtr<ConfigsType> configs)
  : configs_(configs)
{
}

template <typename ConfigsType>
template <size_t index>
[[nodiscard]] bool UnitTestCogConfigs<ConfigsType>::is_config_set() const
{
  return configs_->template is_config_set<index>();
}

template <typename ConfigsType>
template <size_t index>
[[nodiscard]] typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType&
UnitTestCogConfigs<ConfigsType>::get_config()
{
  jewels::FactoryResult<typename ConfigsType::template ConfigRefType<index>> value;
  if (!jewels::ok(configs_->template get_config<index>(jewels::Out{value})))
  {
    throw std::runtime_error(
      fmt::format(
        "Config has not been set for {}", std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::name));
  }
  return value->get();
}

template <typename ConfigsType>
template <size_t index>
[[nodiscard]] std::shared_ptr<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType>
UnitTestCogConfigs<ConfigsType>::get_config_handle() const
{
  std::shared_ptr<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType> value;
  if (!jewels::ok(configs_->template get_config_handle<index>(jewels::Out{value})))
  {
    throw std::runtime_error(
      fmt::format(
        "Config has not been set for {}", std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::name));
  }
  return value;
}

template <typename ConfigsType>
template <size_t index>
void UnitTestCogConfigs<ConfigsType>::set_config_handle(
  std::shared_ptr<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType> config_handle)
{
  if (!jewels::ok(configs_->template set_config_handle<index>(config_handle)))
  {
    throw std::runtime_error(
      fmt::format(
        "Config has already been set for {}", std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::name));
  }
}

template <typename ConfigsType>
void UnitTestCogConfigs<ConfigsType>::initialize_configs()
{
  [this]<std::size_t... index>(std::index_sequence<index...>)
  { (this->initialize_config_if_not_set<index>(), ...); }(std::make_index_sequence<ConfigsType::policy_count>());
}

template <typename ConfigsType>
template <size_t index>
void UnitTestCogConfigs<ConfigsType>::initialize_config_if_not_set()
{
  if (!is_config_set<index>())
  {
    set_config_handle<index>(
      std::make_shared<typename std::tuple_element_t<index, typename ConfigsType::PoliciesTuple>::ConfigType>());
  }
}

template <typename StatesType>
UnitTestCogStates<StatesType>::UnitTestCogStates(
  jewels::memory::ObjectPtr<StatesType> states,
  jewels::memory::ObjectPtr<pinion::AbstractChannelFactory> channel_factory,
  std::span<std::shared_ptr<pinion::AbstractPublisher>, StatesType::policy_count> state_publishers)
  : states_(states), channel_factory_(channel_factory), state_publishers_(state_publishers)
{
}

template <typename StatesType>
template <size_t index>
[[nodiscard]] bool UnitTestCogStates<StatesType>::is_state_set() const
{
  return states_->template is_state_set<index>();
}

template <typename StatesType>
template <size_t index>
[[nodiscard]] StatesType::template SchemaType<index>& UnitTestCogStates<StatesType>::get_state()
{
  jewels::FactoryResult<std::reference_wrapper<typename StatesType::template SchemaType<index>>> value;
  if (!jewels::ok(states_->template get_state<index>(jewels::Out{value})))
  {
    throw std::runtime_error(
      fmt::format(
        "State has not been set for {}", std::tuple_element_t<index, typename StatesType::PoliciesTuple>::name));
  }
  return value->get();
}

template <typename StatesType>
template <size_t index>
typename StatesType::template RecordPtrType<index> UnitTestCogStates<StatesType>::get_state_handle() const
{
  typename StatesType::template RecordPtrType<index> value;
  if (!jewels::ok(states_->template get_state_handle<index>(jewels::Out{value})))
  {
    throw std::runtime_error(
      fmt::format(
        "State has not been set for {}", std::tuple_element_t<index, typename StatesType::PoliciesTuple>::name));
  }
  return value;
}

template <typename StatesType>
template <size_t index>
void UnitTestCogStates<StatesType>::set_state_handle(typename StatesType::template RecordPtrType<index> state_handle)
{
  if (!jewels::ok(states_->template set_state_handle<index>(std::move(state_handle))))
  {
    throw std::runtime_error(
      fmt::format(
        "State has already been set for {}", std::tuple_element_t<index, typename StatesType::PoliciesTuple>::name));
  }
}

template <typename StatesType>
void UnitTestCogStates<StatesType>::initialize_states(const jewels::memory::MemoryResource& memory_resource)
{
  [this, memory_resource]<std::size_t... index>(std::index_sequence<index...>)
  {
    (this->initialize_state_if_not_set<index>(memory_resource), ...);
  }(std::make_index_sequence<StatesType::policy_count>());
}

template <typename StatesType>
template <size_t index>
void UnitTestCogStates<StatesType>::initialize_state_if_not_set(
  const jewels::memory::MemoryResource& /*memory_resource*/)
  requires TappyType<typename StatesType::template SchemaType<index>>
{
  if (!is_state_set<index>())
  {
    constexpr auto channel_name = std::tuple_element_t<index, typename StatesType::PoliciesTuple>::name;
    state_publishers_[index] = detail::make_publisher(
      *channel_factory_, channel_name, 1U, sizeof(typename StatesType::template SchemaType<index>));
    auto publisher_handle = state_publishers_[index]->extract_publisher();
    if (!publisher_handle)
    {
      throw std::runtime_error(fmt::format("Failed to extract publisher handle for {}", channel_name));
    }
    auto record_ptr = std::make_shared<CogStateDataImpl<typename StatesType::template SchemaType<index>>>(
      std::move(publisher_handle).value());
    set_state_handle<index>(std::move(record_ptr));
  }
}

template <typename StatesType>
template <size_t index>
void UnitTestCogStates<StatesType>::initialize_state_if_not_set(const jewels::memory::MemoryResource& memory_resource)
  requires(!TappyType<typename StatesType::template SchemaType<index>>)
{
  if (!is_state_set<index>())
  {
    auto record_ptr =
      std::make_shared<CogStateDataImpl<typename StatesType::template SchemaType<index>>>(memory_resource);
    set_state_handle<index>(std::move(record_ptr));
  }
}

template <typename MsgType>
UnitTestCogInputPublishable<MsgType>::UnitTestCogInputPublishable(
  jewels::time::SyncTime publish_time, pinion::PublisherReservation&& reserved_slot)
  : publish_time_(publish_time),
    reserved_slot_(std::move(reserved_slot)),
    publishable_(detail::make_publishable<MsgType>(reserved_slot_))
{
}

template <typename MsgType>
UnitTestCogInputPublishable<MsgType>::~UnitTestCogInputPublishable()
{
  publishable_.mark_for_publish();
  if (!reserved_slot_.process(publish_time_))
  {
    throw std::runtime_error("Failed to publish test message");
  }
}

template <typename MsgType>
[[nodiscard]] MsgType& UnitTestCogInputPublishable<MsgType>::message()
{
  return publishable_.message();
}

template <typename InputType>
UnitTestCogInput<InputType>::UnitTestCogInput(
  jewels::memory::ObjectPtr<bool> is_initialized,
  jewels::memory::ObjectPtr<uint32_t> slot_count,
  jewels::memory::ObjectPtr<std::shared_ptr<pinion::AbstractPublisher>> input_publisher,
  jewels::memory::ObjectPtr<uint64_t> publish_count)
  : is_initialized_(is_initialized),
    slot_count_(slot_count),
    input_publisher_(input_publisher),
    publish_count_(publish_count)
{
}

template <typename InputType>
template <typename OtherInputType>
UnitTestCogInput<InputType>::UnitTestCogInput(const UnitTestCogInput<OtherInputType>& other) noexcept
  : is_initialized_(other.is_initialized_),
    slot_count_(other.slot_count_),
    input_publisher_(other.input_publisher_),
    publish_count_(other.publish_count_)
{
}

template <typename InputType>
[[nodiscard]] uint32_t UnitTestCogInput<InputType>::get_num_slots() const
{
  return *slot_count_;
}

template <typename InputType>
void UnitTestCogInput<InputType>::set_num_slots(uint32_t num_slots)
{
  if (*is_initialized_)
  {
    throw std::runtime_error("Unit test cog has been initialized");
  }
  *slot_count_ = num_slots;
}

template <typename InputType>
[[nodiscard]] UnitTestCogInputPublishable<typename InputType::MsgType>
UnitTestCogInput<InputType>::reserve(jewels::time::SyncTime publish_time)
{
  if (!*is_initialized_)
  {
    throw std::runtime_error("Unit test cog has not been initialized");
  }
  auto reserve_result = (*input_publisher_)->publisher().reserve(true);
  if (!reserve_result)
  {
    throw std::runtime_error(fmt::format("Failed to reserve slot for {}", InputType::name));
  }
  new (reserve_result->slots().front().message().data()) typename InputType::MsgType{};
  return {publish_time, std::move(reserve_result).value()};
}

template <typename InputType>
testing::MessageHandle
UnitTestCogInput<InputType>::publish(const typename InputType::MsgType& message, jewels::time::SyncTime publish_time)
{
  auto publishable = reserve(publish_time);
  publishable.message() = message;
  return testing::MessageHandle{(*publish_count_)++};
}

template <typename InputType>
testing::MessageHandle UnitTestCogInput<InputType>::publish(
  const std::function<void(typename InputType::MsgType&)>& init_fn, jewels::time::SyncTime publish_time)
{
  auto publishable = reserve(publish_time);
  init_fn(publishable.message());
  return testing::MessageHandle{(*publish_count_)++};
}

template <typename InputsType>
UnitTestCogInputs<InputsType>::UnitTestCogInputs(
  jewels::memory::ObjectPtr<bool> is_initialized,
  std::span<uint32_t, InputsType::policy_count> slot_counts,
  std::span<std::shared_ptr<pinion::AbstractPublisher>, InputsType::policy_count> input_publishers,
  std::span<uint64_t, InputsType::policy_count> publish_counts)
  : is_initialized_(is_initialized),
    slot_counts_(slot_counts),
    input_publishers_(input_publishers),
    publish_counts_(publish_counts)
{
}

template <typename InputsType>
template <size_t index>
[[nodiscard]] UnitTestCogInput<typename InputsType::template PolicyType<index>>
UnitTestCogInputs<InputsType>::get_input()
{
  return {
    is_initialized_,
    jewels::memory::make_non_null_from_ref(slot_counts_[index]),
    jewels::memory::make_non_null_from_ref(input_publishers_[index]),
    jewels::memory::make_non_null_from_ref(publish_counts_[index])};
}

template <typename OutputViewPolicy>
UnitTestCogOutput<OutputViewPolicy>::UnitTestCogOutput(
  jewels::memory::ObjectPtr<bool> is_initialized,
  jewels::memory::ObjectPtr<UnitTestCogOutputViewPtrType<OutputViewPolicy>> output_view)
  : is_initialized_(is_initialized), output_view_(output_view)
{
}

template <typename OutputViewPolicy>
[[nodiscard]] auto UnitTestCogOutput<OutputViewPolicy>::get_next_view()
{
  if (!*is_initialized_)
  {
    throw std::runtime_error("Unit test cog has been initialized");
  }
  const auto dial_result = (*output_view_)->make_dial_input(1, {});
  if (!dial_result)
  {
    throw std::runtime_error(fmt::format("Failed to make an input dial for {}", OutputViewPolicy::name));
  }
  // We don't need the last viewed tuple returned by commit
  std::ignore = (*output_view_)->commit(dial_result.value());
  return dial_result->get_new_msgs_view();
}

template <typename OutputViewPolicy>
[[nodiscard]] const typename OutputViewPolicy::MsgType& UnitTestCogOutput<OutputViewPolicy>::get_next_message()
{
  const auto view = get_next_view();
  if (view.empty())
  {
    throw std::runtime_error(fmt::format("No messages published on output {}", OutputViewPolicy::name));
  }
  return view.front();
}

template <typename OutputViewPolicy>
[[nodiscard]] std::optional<std::reference_wrapper<const typename OutputViewPolicy::MsgType>>
UnitTestCogOutput<OutputViewPolicy>::try_get_next_message()
{
  const auto view = get_next_view();
  if (view.empty())
  {
    return std::nullopt;
  }
  return {view.front()};
}

template <typename PublishersType>
UnitTestCogOutputs<PublishersType>::UnitTestCogOutputs(
  jewels::memory::ObjectPtr<bool> is_initialized,
  jewels::memory::ObjectPtr<typename PublishersType::UnitTestCogOutputViewTuple> output_views)
  : is_initialized_(is_initialized), output_views_(output_views)
{
}

template <typename PublishersType>
template <size_t index>
[[nodiscard]] UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<index>>
UnitTestCogOutputs<PublishersType>::get_output()
{
  return UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<index>>{
    is_initialized_, jewels::memory::make_non_null_from_ref(std::get<index>(*output_views_))};
}

template <typename DiagnosticsType>
UnitTestCogDiagnostics<DiagnosticsType>::UnitTestCogDiagnostics(
  jewels::memory::ObjectPtr<bool> is_initialized,
  jewels::memory::ObjectPtr<typename DiagnosticsType::UnitTestCogOutputViewTuple> report_views)
  : is_initialized_(is_initialized), report_views_(report_views)
{
}

template <typename DiagnosticsType>
template <size_t index>
[[nodiscard]] UnitTestCogOutput<typename DiagnosticsType::template UnitTestOutputViewPolicyType<index>>
UnitTestCogDiagnostics<DiagnosticsType>::get_diagnostic()
{
  return UnitTestCogOutput<typename DiagnosticsType::template UnitTestOutputViewPolicyType<index>>{
    is_initialized_, jewels::memory::make_non_null_from_ref(std::get<index>(*report_views_))};
}

template <typename Policy>
UnitTestCog<Policy>::UnitTestCog(
  jewels::memory::MemoryResource resource,
  const jewels::Uuid<common::CogInstanceId>& instance_id,
  jewels::memory::ObjectPtr<AbstractCogQueue> queue)
  : CogBase(queue),
    memory_resource_(std::move(resource)),
    instance_id_(instance_id),
    pinion_shm_root_(temp_dir_.get_path().string()),
    pinion_namespace_(jewels::Uuid<void>::random_uuid().to_string()),
    states_(memory_resource_),
    timers_(memory_resource_),
    inputs_(memory_resource_, queue->is_offline()),
    conditions_(memory_resource_),
    publishers_(memory_resource_),
    diagnostics_(instance_id_),
    infra_diagnostics_(instance_id_)
{
  auto shm_channel_factory_result =
    clockwork::pinion::ShmChannelFactory::make(memory_resource_, pinion_namespace_, pinion_shm_root_);
  if (!shm_channel_factory_result)
  {
    throw std::runtime_error(fmt::format("Failed to make shared memory channel factory"));
  }
  channel_factory_ =
    std::make_shared<clockwork::pinion::ShmChannelFactory>(std::move(shm_channel_factory_result).value());
}

template <typename Policy>
std::string_view UnitTestCog<Policy>::get_name() const
{
  return Policy::name;
}

template <typename Policy>
const jewels::Uuid<common::CogInstanceId>& UnitTestCog<Policy>::get_instance_id() const
{
  return instance_id_;
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, jewels::memory::MemoryResource /*memory_resource*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, std::shared_ptr<CogConfigData> /*config*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, std::shared_ptr<CogStateData> /*state*/, bool /*is_shared*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, std::shared_ptr<AbstractTimer> /*timer*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, std::shared_ptr<pinion::AbstractChannel> /*subscriber*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<void, jewels::MonoError>
UnitTestCog<Policy>::set_subscriber(jewels::Uuid<common::EndpointClassId> /*uuid*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::set_handle(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, pinion::PublisherHandle&& /*handle*/, bool /*connected*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::BinaryOutcome UnitTestCog<Policy>::set_snapshot_config(
  jewels::Uuid<common::EndpointClassId> /*uuid*/, const Tappy<common::SnapshotConfig>& /*snapshot_config*/)
{
  throw std::runtime_error("Not implemented");
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::validate()
{
  if (
    memory_resources_.validate() && configs_.validate() && timers_.validate() && inputs_.validate() &&
    conditions_.validate() && publishers_.validate() && states_.validate() && diagnostics_.validate())
  {
    return {};
  }

  return jewels::unexpected{jewels::MonoError{}};
}

template <typename Policy>
jewels::expected<void, jewels::MonoError> UnitTestCog<Policy>::prime(jewels::time::SyncTime start_time)
{
  return prime(start_time, jewels::memory::MemoryResource{std::pmr::get_default_resource()});
}

template <typename Policy>
jewels::expected<void, jewels::MonoError>
UnitTestCog<Policy>::prime(jewels::time::SyncTime start_time, const jewels::memory::MemoryResource& memory_resource)
{
  if (is_initialized_)
  {
    throw std::runtime_error("Cog has already been initialized");
  }
  is_initialized_ = true;
  get_memory_resources().initialize_memory_resources();
  get_configs().initialize_configs();
  get_states().initialize_states(memory_resource);
  initialize_timers();
  initialize_inputs();
  initialize_outputs();
  initialize_diagnostics();
  initialize_infra_diagnostics();
  if (!validate())
  {
    throw std::runtime_error(fmt::format("Cog validation failed for {}", Policy::name));
  }
  return timers_.prime(start_time);
}

template <typename Policy>
void UnitTestCog<Policy>::notify(jewels::time::SyncTime /*current_time*/)
{
}

template <typename Policy>
CogPrepareOutcome UnitTestCog<Policy>::prepare_for_execution(
  jewels::Out<jewels::time::SyncTime> throttled_until_out, const jewels::time::SyncTime current_time)
{
  if (!is_initialized_)
  {
    throw std::runtime_error("Cog has not been initialized");
  }
  timers_.notify_expired_unit_test_timers(current_time);
  auto timer_conditions = timers_.make_conditions(current_time);
  auto input_conditions = conditions_.make_conditions();
  if (!Policy::is_ready(statistics_, timer_conditions, input_conditions))
  {
    return CogPrepareResult::not_ready;
  }
  auto throttled_until = jewels::time::SyncTime::min();
  typename PublishersType::PublisherThrottleSet throttled_publishers;
  if (jewels::fails(publishers_.update_rate_limiters(
        jewels::Out{throttled_until}, jewels::Out{throttled_publishers}, current_time)))
  {
    *throttled_until_out = throttled_until;
    return CogPrepareResult::publisher_throttled;
  }
  prepared_timer_conditions_ = std::move(timer_conditions);
  prepared_input_conditions_ = std::move(input_conditions);
  return CogPrepareResult::ready;
}

template <typename Policy>
void UnitTestCog<Policy>::notify_expired_timers_for_test(jewels::time::SyncTime current_time)
{
  if (!is_initialized_)
  {
    throw std::runtime_error("Cog has not been initialized");
  }
  timers_.notify_expired_unit_test_timers(current_time);
}

template <typename Policy>
jewels::expected<void, CogExecutionError> UnitTestCog<Policy>::execute(CogExecuteParams params)
{
  return execute(params, std::chrono::microseconds(0));
}

template <typename Policy>
jewels::expected<void, CogExecutionError>
UnitTestCog<Policy>::execute(const CogExecuteParams& params, [[maybe_unused]] std::chrono::microseconds exec_duration)
{
  // Create resources

  auto resources = memory_resources_.make_memory_resources();

  // Create configs

  auto configs = configs_.make_configs();

  // Create states

  auto states = states_.make_states();

  // Create inputs

  if (!prepared_timer_conditions_ || !prepared_input_conditions_)
  {
    return jewels::unexpected(CogExecutionError::not_ready);
  }

  auto inputs = inputs_.template make_dial_inputs<ConditionsType>(*prepared_input_conditions_, params.start_time);

  if (!inputs)
  {
    return jewels::unexpected(CogExecutionError::make_inputs_failed);
  }

  if constexpr (requires { &Policy::resolve_alignment; })
  {
    size_t stale_alignment_index{};
    const auto alignment_result = Policy::resolve_alignment(jewels::Out{stale_alignment_index}, inputs_, *inputs);
    switch (alignment_result.get())
    {
    case clockwork::AlignedLookupResult::resolved:
      break;

    case clockwork::AlignedLookupResult::stale:
    {
      typename Policy::InputsType::LastViewedTuple commit_result;
      if (jewels::fails(inputs_.commit_single(jewels::Out{commit_result}, *inputs, stale_alignment_index)))
      {
        // Invariant violation
        throw std::runtime_error{"Alignment resolution returned an invalid alignment input index."};
      }
      const auto& [endpoint_id, last_viewed] = commit_result;
      conditions_.commit(endpoint_id, last_viewed);
      inputs_.reset_saved_state();
      return jewels::unexpected(CogExecutionError::alignment_stale);
    }

    case clockwork::AlignedLookupResult::pending:
      inputs_.reset_saved_state();
      return jewels::unexpected(CogExecutionError::alignment_pending);
    }
  }

  // Create outputs

  auto slots = publishers_.reserve_slots();
  if (!slots)
  {
    return jewels::unexpected(CogExecutionError::reserve_slots_failed);
  }

  auto publishables = publishers_.make_publishables(*slots);
  if (!publishables)
  {
    return jewels::unexpected(CogExecutionError::make_publishables_failed);
  }

  auto diagnostics = diagnostics_.make_report(params.start_time);

  // Execute!

  auto make_dial_fn = [this, &params, &resources, &configs, &states, &inputs, &publishables, &diagnostics]()
  {
    if constexpr (::clockwork::detail::has_dynamic_timer_v<Policy>)
    {
      auto& dynamic_timer_handler = timers_.template get_handler<Policy::dynamic_timer_index>();
      return Policy::make_dial(
        params,
        resources,
        configs,
        states,
        *inputs,
        *publishables,
        *prepared_timer_conditions_,
        *prepared_input_conditions_,
        diagnostics,
        signals_,
        dynamic_timer_handler);
    }
    else
    {
      return Policy::make_dial(
        params,
        resources,
        configs,
        states,
        *inputs,
        *publishables,
        *prepared_timer_conditions_,
        *prepared_input_conditions_,
        diagnostics,
        signals_);
    }
  };

  auto dial = make_dial_fn();

  if (!timers_.update_last_exec_time(params.start_time, *prepared_timer_conditions_))
  {
    // Timers are documented as not expected to fail; this would be an unexpected kernel failure.
    jewels::log_cerr_error("Timer re-arming failed");
    std::terminate();
  }

  if constexpr (requires { Policy::start_of_execution_signals(signals_, params.start_time); })
  {
    Policy::start_of_execution_signals(signals_, params.start_time);
  }
  Policy::execute(dial);
  if constexpr (requires { Policy::end_of_execution_signals(signals_, params.start_time); })
  {
    Policy::end_of_execution_signals(signals_, params.start_time);
  }

  // Take state snapshots if configured
  if (jewels::fails(states_.publish_snapshots(params.start_time)))
  {
    // Snapshots are best-effort, log and continue
    jewels::log_cerr_error("State snapshot failures occurred in cog '{}'", get_name());
  }

  publishers_.update_throttle_status(*slots);

  // Update stats
  statistics_.on_execute_complete(false);

  diagnostics_.commit(diagnostics, params.start_time);

  if (const auto result = Policy::publish_report_groups(signals_, *publishables); jewels::fails(result))
  {
    throw std::runtime_error(fmt::format("Failed to publish report groups in cog '{}'", get_name()));
  }
  if (const auto result = pinion::process_slots(std::span{*slots}, params.start_time); !result)
  {
    throw std::runtime_error{fmt::format("Failed to process slots: {}", result.error())};
  }

  if constexpr (requires { &Policy::commit_alignment; })
  {
    Policy::commit_alignment(inputs_, *inputs);
  }
  conditions_.commit(inputs_.commit(*inputs));

  return {};
}

template <typename Policy>
UnitTestCogMemoryResources<typename Policy::MemoryResourcesType> UnitTestCog<Policy>::get_memory_resources()
{
  return UnitTestCogMemoryResources<typename Policy::MemoryResourcesType>(
    jewels::memory::make_non_null_from_ref(memory_resources_));
}

template <typename Policy>
UnitTestCogConfigs<typename Policy::ConfigsType> UnitTestCog<Policy>::get_configs()
{
  return UnitTestCogConfigs<typename Policy::ConfigsType>(jewels::memory::make_non_null_from_ref(configs_));
}

template <typename Policy>
UnitTestCogStates<typename Policy::StatesType> UnitTestCog<Policy>::get_states()
{
  return UnitTestCogStates<typename Policy::StatesType>(
    jewels::memory::make_non_null_from_ref(states_),
    jewels::memory::make_non_null_from_ref(*channel_factory_),
    state_publishers_);
}

template <typename Policy>
UnitTestCogInputs<typename Policy::InputsType> UnitTestCog<Policy>::get_inputs()
{
  return UnitTestCogInputs<typename Policy::InputsType>(
    jewels::memory::make_non_null_from_ref(is_initialized_),
    input_slot_counts_,
    input_publishers_,
    input_publish_counts_);
}

template <typename Policy>
UnitTestCogOutputs<typename Policy::PublishersType> UnitTestCog<Policy>::get_outputs()
{
  return UnitTestCogOutputs<typename Policy::PublishersType>(
    jewels::memory::make_non_null_from_ref(is_initialized_), jewels::memory::make_non_null_from_ref(output_views_));
}

template <typename Policy>
UnitTestCogDiagnostics<typename Policy::DiagnosticsType> UnitTestCog<Policy>::get_diagnostics()
{
  return UnitTestCogDiagnostics<typename Policy::DiagnosticsType>(
    jewels::memory::make_non_null_from_ref(is_initialized_), jewels::memory::make_non_null_from_ref(report_views_));
}

template <typename Policy>
[[nodiscard]] UnitTestCogOutput<typename UnitTestCog<Policy>::InfraDiagnosticsType::UnitTestOutputViewPolicyType>
UnitTestCog<Policy>::get_infra_diagnostics()
{
  return UnitTestCogOutput<typename InfraDiagnosticsType::UnitTestOutputViewPolicyType>{
    jewels::memory::make_non_null_from_ref(is_initialized_),
    jewels::memory::make_non_null_from_ref(infra_report_view_)};
}

template <typename Policy>
[[nodiscard]] std::string_view UnitTestCog<Policy>::get_pinion_shm_root() const
{
  return pinion_shm_root_;
}

template <typename Policy>
[[nodiscard]] std::string_view UnitTestCog<Policy>::get_pinion_namespace() const
{
  return pinion_namespace_;
}

template <typename Policy>
[[nodiscard]] pinion::AbstractChannelFactory& UnitTestCog<Policy>::get_channel_factory()
{
  return *channel_factory_;
}

template <typename Policy>
void UnitTestCog<Policy>::initialize_timers()
{
  [this]<std::size_t... index>(std::index_sequence<index...>)
  {
    const auto initialize_timer = [this]<size_t timer_index>()
    {
      using TimerPolicy = std::tuple_element_t<timer_index, typename TimersType::PoliciesTuple>;
      using HandlerType = ::clockwork::detail::handler_type_t<TimerPolicy>;
      auto timer_ptr = std::make_shared<HandlerType>(
        std::make_shared<DummyTimer>(),
        std::make_shared<CogPassthroughObserver<UnitTestCog<Policy>>>(jewels::memory::make_non_null_from_ref(*this)));
      this->timers_.template set_unit_test_timer<timer_index>(std::move(timer_ptr));
    };
    (initialize_timer.template operator()<index>(), ...);
  }(std::make_index_sequence<Policy::TimersType::policy_count>());
}

template <typename Policy>
void UnitTestCog<Policy>::initialize_inputs()
{
  [this]<std::size_t... indices>(std::index_sequence<indices...>)
  {
    const auto initialize_input = [this]<size_t index>()
    {
      constexpr auto channel_name = InputsType::template PolicyType<index>::name;
      input_publishers_[index] = detail::make_publisher(
        *channel_factory_,
        channel_name,
        input_slot_counts_.at(index),
        sizeof(typename InputsType::template PolicyType<index>::MsgType));
      this->inputs_.template set_unit_test_input<index, UnitTestCog<Policy>>(input_publishers_[index]);
      this->initialize_conditions_for_input<InputsType::template PolicyType<index>::endpoint_id>(
        input_publishers_[index]);
    };
    (initialize_input.template operator()<indices>(), ...);
  }(std::make_index_sequence<Policy::InputsType::policy_count>());
}

template <typename Policy>
template <jewels::Uuid<::clockwork::common::EndpointClassId> endpoint_id>
void UnitTestCog<Policy>::initialize_conditions_for_input(const std::shared_ptr<pinion::AbstractPublisher>& channel)
{
  [this, &channel]<std::size_t... indices>(std::index_sequence<indices...>)
  {
    const auto initialize_condition = [this, &channel]<size_t index>()
    {
      if constexpr (ConditionsType::template PolicyType<index>::endpoint_id == endpoint_id)
      {
        this->conditions_.template set_unit_test_condition<index>(channel);
      }
    };
    (initialize_condition.template operator()<indices>(), ...);
  }(std::make_index_sequence<Policy::ConditionsType::policy_count>());
}

template <typename Policy>
void UnitTestCog<Policy>::initialize_outputs()
{
  [this]<std::size_t... indices>(std::index_sequence<indices...>)
  {
    auto initialize_output = [this]<size_t index>()
    {
      using PublisherType = typename PublishersType::template PolicyType<index>;
      this->output_publishers_[index] = detail::make_publisher(
        *channel_factory_,
        PublisherType::name,
        1U,
        sizeof(typename PublishersType::template PolicyType<index>::MsgType));
      auto handle_result = output_publishers_[index]->extract_publisher();
      if (!handle_result)
      {
        throw std::runtime_error(fmt::format("Failed to extract publisher handle for {}", PublisherType::name));
      }
      this->publishers_.template set_unit_test_publisher<index>(std::move(handle_result).value());
      using OutputViewPolicyType = UnitTestCogOutputViewPolicy<typename PublisherType::MsgType, PublisherType>;
      std::get<index>(this->output_views_) =
        std::make_shared<InputView<OutputViewPolicyType>>(this->output_publishers_[index], 0U, memory_resource_, true);
    };
    (initialize_output.template operator()<indices>(), ...);
  }(std::make_index_sequence<Policy::PublishersType::policy_count>());
}

template <typename Policy>
void UnitTestCog<Policy>::initialize_diagnostics()
{
  [this]<std::size_t... indices>(std::index_sequence<indices...>)
  {
    auto initialize_diagnostic = [this]<size_t index>()
    {
      using DiagnosticType = typename DiagnosticsType::template PolicyType<index>;
      this->diagnostics_publishers_[index] =
        detail::make_publisher(*channel_factory_, DiagnosticType::name, 1U, sizeof(Tappy<diagnostics::Report>));
      auto handle_result = diagnostics_publishers_[index]->extract_publisher();
      if (!handle_result)
      {
        throw std::runtime_error(fmt::format("Failed to extract publisher handle for {}", DiagnosticType::name));
      }
      this->diagnostics_.template set_unit_test_diagnostics<index>(std::move(handle_result).value());
      using OutputViewPolicyType = UnitTestCogOutputViewPolicy<Tappy<diagnostics::Report>, DiagnosticType>;
      std::get<index>(this->report_views_) = std::make_shared<InputView<OutputViewPolicyType>>(
        this->diagnostics_publishers_[index], 0U, memory_resource_, true);
    };
    (initialize_diagnostic.template operator()<indices>(), ...);
  }(std::make_index_sequence<Policy::DiagnosticsType::policy_count>());
}

template <typename Policy>
void UnitTestCog<Policy>::initialize_infra_diagnostics()
{
  this->infra_diagnostics_publisher_ = detail::make_publisher(
    *channel_factory_,
    InfraDiagnosticsType::UnitTestOutputViewPolicyType::name,
    1U,
    sizeof(Tappy<diagnostics::Report>));
  auto handle_result = infra_diagnostics_publisher_->extract_publisher();
  if (!handle_result)
  {
    throw std::runtime_error(
      fmt::format(
        "Failed to extract publisher handle for {}", InfraDiagnosticsType::UnitTestOutputViewPolicyType::name));
  }
  this->infra_diagnostics_.set_unit_test_diagnostics_impl(std::move(handle_result).value());
  this->infra_report_view_ = std::make_shared<InputView<typename InfraDiagnosticsType::UnitTestOutputViewPolicyType>>(
    infra_diagnostics_publisher_, 0U, memory_resource_, true);
}

template <typename Policy>
template <size_t index>
[[nodiscard]] std::shared_ptr<pinion::AbstractPublisher>& UnitTestCog<Policy>::get_input_publisher()
{
  return input_publishers_[index];
}

template <typename Policy>
template <size_t index>
[[nodiscard]] std::shared_ptr<pinion::AbstractPublisher>& UnitTestCog<Policy>::get_output_publisher()
{
  return output_publishers_[index];
}

template <typename Policy>
template <size_t index>
void UnitTestCog<Policy>::rewire_input(const std::shared_ptr<pinion::AbstractPublisher>& publisher)
{
  input_publishers_[index] = publisher;
  this->inputs_.template set_unit_test_input<index, UnitTestCog<Policy>>(publisher);
  initialize_conditions_for_input<InputsType::template PolicyType<index>::endpoint_id>(publisher);
}

} // namespace clockwork::testing
