// IWYU pragma: private, include "clockwork/cog/cog_inputs.hh"
#pragma once

#include "clockwork/cog/cog_inputs.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/input_view.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace clockwork
{

template <typename... Policies>
CogInputs<Policies...>::CogInputs(jewels::memory::MemoryResource resource, bool running_offline) noexcept
  : resource_(std::move(resource)), running_offline_(running_offline)
{
}

template <typename... Policies>
bool CogInputs<Policies...>::validate() const
{
  const std::scoped_lock lock{subscribers_mutex_};
  auto validate = []<typename Policy>(const std::shared_ptr<InputView<Policy>>& subscriber)
  {
    if (!subscriber)
    {
      jewels::log_cerr_error("Subscriber {} unset", Policy::name);
      return false;
    }
    return subscriber->validate();
  };
  return std::apply([&](auto&... subscriber) -> bool { return (validate(subscriber) && ...); }, subscribers_);
}

template <typename... Policies>
template <typename CogType>
jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> CogInputs<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id,
  std::shared_ptr<pinion::AbstractChannel> channel,
  jewels::memory::ObjectPtr<CogType> cog)
{
  const std::scoped_lock lock{subscribers_mutex_};
  std::shared_ptr<pinion::Observer> observer = {};
  auto try_set = [this, &observer, &endpoint_id, &channel, &cog](auto& subscriber) -> bool
  {
    using SubscriberType = typename std::decay_t<decltype(subscriber)>::element_type;
    if (endpoint_id == SubscriberType::endpoint_id)
    {
      subscriber = jewels::memory::make_pmr_shared<SubscriberType>(
        resource_, std::move(channel), CogType::event_metrics_batch_size, resource_, running_offline_);
      observer = jewels::memory::make_pmr_shared<CogPassthroughObserver<CogType>>(resource_, cog);
      return true;
    }
    return false;
  };

  auto is_set =
    std::apply([&try_set](auto&... subscriber) -> bool { return (try_set(subscriber) || ...); }, subscribers_);

  if (is_set)
  {
    return observer;
  }

  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
template <typename CogType>
jewels::expected<void, jewels::MonoError>
CogInputs<Policies...>::set_input(jewels::Uuid<common::EndpointClassId> endpoint_id)
{
  const std::scoped_lock lock{subscribers_mutex_};
  auto try_set = [this, &endpoint_id](auto& subscriber) -> bool
  {
    using SubscriberType = typename std::decay_t<decltype(subscriber)>::element_type;
    if (endpoint_id == SubscriberType::endpoint_id)
    {
      subscriber = jewels::memory::make_pmr_shared<SubscriberType>(
        resource_, CogType::event_metrics_batch_size, resource_, running_offline_);
      return true;
    }
    return false;
  };

  auto is_set =
    std::apply([&try_set](auto&... subscriber) -> bool { return (try_set(subscriber) || ...); }, subscribers_);

  if (is_set)
  {
    return {};
  }

  return jewels::unexpected(jewels::MonoError{});
}
template <typename... Policies>
template <typename ConditionsType>
auto CogInputs<Policies...>::make_dial_inputs(
  const typename ConditionsType::ConditionsTuple& conditions, jewels::time::SyncTime current_time)
  -> jewels::expected<InputDialTuple, pinion::ProgressError>
{
  const std::scoped_lock lock{subscribers_mutex_};
  auto make_input = [&conditions, current_time]<typename Policy>(SubscriberType<Policy>& subscriber)
  {
    auto max_new_msgs = ConditionsType::template get_max_new_msgs<Policy>(conditions);
    return subscriber->make_dial_input(max_new_msgs, current_time);
  };

  auto dial_inputs = std::apply(
    [&make_input](auto&... subscribers) { return std::make_tuple(make_input(subscribers)...); }, subscribers_);

  if (!std::apply([](auto&... dial_input) -> bool { return (static_cast<bool>(dial_input) && ...); }, dial_inputs))
  {
    return jewels::unexpected{pinion::ProgressError{}};
  }

  return std::apply([](auto&... dial_input) { return InputDialTuple(std::move(*dial_input)...); }, dial_inputs);
}

template <typename... Policies>
auto CogInputs<Policies...>::commit(const InputDialTuple& inputs) -> LastViewedArray
{
  const std::scoped_lock lock{subscribers_mutex_};
  return [this, &inputs]<size_t... idx>(std::index_sequence<idx...>)
  {
    return LastViewedArray{std::get<idx>(subscribers_)->commit(std::get<idx>(inputs))...};
  }(std::make_index_sequence<std::tuple_size_v<InputDialTuple>>{});
}

template <typename... Policies>
bool CogInputs<Policies...>::is_overrun() const
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::apply([](auto&... subscriber) { return (subscriber->is_overrun() || ...); }, subscribers_);
}

template <typename... Policies>
bool CogInputs<Policies...>::almost_overrun() const
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::apply([](auto&... subscriber) { return (subscriber->almost_overrun() || ...); }, subscribers_);
}

template <typename... Policies>
bool CogInputs<Policies...>::is_published_once_channel_invalid() const
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::apply(
    [](auto&... subscriber) { return (subscriber->is_published_once_channel_invalid() || ...); }, subscribers_);
}

template <typename... Policies>
CogInputs<Policies...>::SubscribersTuple& CogInputs<Policies...>::subscribers()
{
  return subscribers_;
}

template <typename... Policies>
template <typename Report, typename Enum, Enum... missing_ids, Enum... safety_skip_ids>
void CogInputs<Policies...>::set_infra_diagnostics(
  Report& report,
  jewels::time::SyncTime start_time,
  std::integer_sequence<Enum, missing_ids...> /*missing*/,
  std::integer_sequence<Enum, safety_skip_ids...> /*safety_skip*/) const
{

  ((report.template set<missing_ids>(std::chrono::duration_cast<std::chrono::microseconds>(
     start_time - std::get<SubscriberType<Policies>>(subscribers_)->latest_message_time()))),
   ...);
  ((report.template set<safety_skip_ids>(std::get<SubscriberType<Policies>>(subscribers_)->did_safety_skip())), ...);
}

template <typename... Policies>
template <size_t index>
auto CogInputs<Policies...>::prepare_aligned_input(
  jewels::Out<typename std::tuple_element_t<index, typename CogInputs<Policies...>::SubscribersTuple>::element_type::
                InputDialType> dial_out,
  uint64_t target_seqno)
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::get<index>(subscribers_)->prepare_aligned_view(jewels::Out{*dial_out}, target_seqno);
}

template <typename... Policies>
template <size_t index>
auto CogInputs<Policies...>::prepare_aligned_input_range(
  jewels::Out<typename std::tuple_element_t<index, typename CogInputs<Policies...>::SubscribersTuple>::element_type::
                InputDialType> dial_out,
  uint64_t begin_seq,
  uint64_t end_seq)
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::get<index>(subscribers_)->prepare_aligned_range(jewels::Out{*dial_out}, begin_seq, end_seq);
}

template <typename... Policies>
template <size_t index>
auto CogInputs<Policies...>::prepare_empty_aligned_input()
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::get<index>(subscribers_)->prepare_empty_aligned_view();
}

template <typename... Policies>
template <size_t index>
auto CogInputs<Policies...>::commit_single(const InputDialTuple& inputs)
{
  const std::scoped_lock lock{subscribers_mutex_};
  return std::get<index>(subscribers_)->commit(std::get<index>(inputs));
}

template <typename... Policies>
jewels::BinaryOutcome CogInputs<Policies...>::commit_single(
  jewels::Out<LastViewedTuple> commit_result_out, const InputDialTuple& inputs, size_t index)
{
  if (index >= policy_count)
  {
    return jewels::failure;
  }

  [&]<size_t... indices>(std::index_sequence<indices...>)
  {
    ((index == indices ? (*commit_result_out = this->template commit_single<indices>(inputs), void()) : void()), ...);
  }(std::index_sequence_for<Policies...>{});
  return jewels::success;
}

template <typename... Policies>
void CogInputs<Policies...>::reset_saved_state()
{
  const std::scoped_lock lock{subscribers_mutex_};
  std::apply([](auto&... subs) { (subs->reset_saved_state(), ...); }, subscribers_);
}

template <typename... Policies>
template <size_t index>
void CogInputs<Policies...>::advance_aligned_cursor(uint64_t seqno)
{
  const std::scoped_lock lock{subscribers_mutex_};
  std::get<index>(subscribers_)->advance_aligned_cursor(seqno);
}

template <typename... Policies>
template <size_t index, typename CogType>
void CogInputs<Policies...>::set_unit_test_input(std::shared_ptr<pinion::AbstractChannel> channel)
{
  std::get<index>(subscribers_) = std::make_shared<InputView<PolicyType<index>>>(
    std::move(channel), CogType::event_metrics_batch_size, resource_, running_offline_);
}

template <typename... Policies>
template <typename ConditionsType>
[[nodiscard]] constexpr std::array<uint32_t, CogInputs<Policies...>::policy_count>
CogInputs<Policies...>::get_default_unit_test_slot_counts()
{
  return []<std::size_t... input_index>(std::index_sequence<input_index...>)
  {
    constexpr auto get_default_unit_test_slots_from_conditions = []<typename PolicyType>()
    {
      return []<size_t... cond_index>(std::index_sequence<cond_index...>)
      {
        constexpr auto get_default_unit_test_slots_from_condition = []<typename ConditionType>()
        {
          return (ConditionType::endpoint_id == PolicyType::endpoint_id &&
                  ConditionType::bounds_max != std::numeric_limits<uint32_t>::max())
                   ? ConditionType::bounds_max
                   : 1U;
        };
        return std::max(
          {1U,
           (get_default_unit_test_slots_from_condition
              // .template operator()<std::tuple_element_t<cond_index, typename ConditionsType::PoliciesTuple>>())...});
              .template operator()<typename ConditionsType::template PolicyType<cond_index>>())...});
      }(std::make_index_sequence<ConditionsType::policy_count>{});
    };
    return std::array<uint32_t, policy_count>{std::max(
      PolicyType<input_index>::max_view_size,
      get_default_unit_test_slots_from_conditions.template operator()<PolicyType<input_index>>())...};
  }(std::make_index_sequence<policy_count>{});
}

} // namespace clockwork
