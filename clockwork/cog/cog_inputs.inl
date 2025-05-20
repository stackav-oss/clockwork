// IWYU pragma: private, include "clockwork/cog/cog_inputs.hh"
#pragma once

#include "clockwork/cog/cog_inputs.hh"

#include "clockwork/cog/cog_passthrough_observer.hh"
#include "clockwork/cog/input_view.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

template <typename... Policies>
CogInputs<Policies...>::CogInputs(jewels::memory::MemoryResource resource) noexcept
  : resource_(std::move(resource))
{
}

template <typename... Policies>
bool CogInputs<Policies...>::validate() const
{
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
  pinion::SubscriberHandle handle,
  jewels::memory::ObjectPtr<CogType> cog)
{
  std::shared_ptr<pinion::Observer> observer = {};
  auto try_set = [this, &observer, &endpoint_id, &handle, &cog](auto& subscriber) -> bool
  {
    using SubscriberType = typename std::decay_t<decltype(subscriber)>::element_type;
    if (endpoint_id == SubscriberType::endpoint_id)
    {
      subscriber = jewels::memory::make_pmr_shared<SubscriberType>(resource_, std::move(handle));
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
template <typename ConditionsType>
auto CogInputs<Policies...>::make_dial_inputs(const typename ConditionsType::ConditionsTuple& conditions)
  -> jewels::expected<InputDialTuple, pinion::ProgressError>
{
  auto make_input = [&conditions]<typename Policy>(SubscriberType<Policy>& subscriber)
  {
    auto max_new_msgs = ConditionsType::template get_max_new_msgs<Policy>(conditions);
    return subscriber->make_dial_input(max_new_msgs);
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
  return [this, &inputs]<size_t... idx>(std::index_sequence<idx...>)
  {
    return LastViewedArray{std::get<idx>(subscribers_)->commit(std::get<idx>(inputs))...};
  }(std::make_index_sequence<std::tuple_size_v<InputDialTuple>>{});
}

template <typename... Policies>
bool CogInputs<Policies...>::is_overrun() const
{
  return std::apply([](auto&... subscriber) { return (subscriber->is_overrun() || ...); }, subscribers_);
}

} // namespace clockwork
