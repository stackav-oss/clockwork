// IWYU pragma: private, include "clockwork/cog/cog_conditions.hh"
#pragma once

#include "clockwork/cog/cog_conditions.hh"

#include "clockwork/cog/input_condition.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace clockwork
{

template <typename... Policies>
CogConditions<Policies...>::CogConditions(jewels::memory::MemoryResource resource) noexcept
  : resource_(std::move(resource))
{
}

template <typename... Policies>
bool CogConditions<Policies...>::validate() const
{
  auto validate = []<typename Policy>(const std::unique_ptr<InputCondition<Policy>>& condition)
  {
    if (!condition)
    {
      jewels::log_cerr_error("Condition {} unset", Policy::name);
      return false;
    }
    return condition->validate();
  };
  return std::apply([&](auto&... condition) -> bool { return (validate(condition) && ...); }, conditions_);
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError> CogConditions<Policies...>::set_handle(
  jewels::Uuid<common::EndpointClassId> endpoint_id, std::shared_ptr<pinion::AbstractChannel> channel)
{
  auto try_set = [&endpoint_id, &channel]<typename Policy>(std::unique_ptr<InputCondition<Policy>>& condition) -> bool
  {
    if (endpoint_id == Policy::endpoint_id)
    {
      condition = std::make_unique<InputCondition<Policy>>(channel);
      return true;
    }
    return false;
  };

  auto set_flags =
    std::apply([&try_set](auto&... condition) { return std::tuple(try_set(condition)...); }, conditions_);
  auto is_set = std::apply([](auto&... flag) -> bool { return (flag || ...); }, set_flags);

  if (is_set)
  {
    return {};
  }

  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
jewels::expected<void, jewels::MonoError>
CogConditions<Policies...>::set_condition(jewels::Uuid<common::EndpointClassId> endpoint_id)
{
  auto try_set = [&endpoint_id]<typename Policy>(std::unique_ptr<InputCondition<Policy>>& condition) -> bool
  {
    if (endpoint_id == Policy::endpoint_id)
    {
      condition = std::make_unique<InputCondition<Policy>>();
      return true;
    }
    return false;
  };

  auto set_flags =
    std::apply([&try_set](auto&... condition) { return std::tuple(try_set(condition)...); }, conditions_);
  auto is_set = std::apply([](auto&... flag) -> bool { return (flag || ...); }, set_flags);

  if (is_set)
  {
    return {};
  }

  return jewels::unexpected(jewels::MonoError{});
}

template <typename... Policies>
auto CogConditions<Policies...>::make_conditions() const -> ConditionsTuple
{
  return std::apply([](auto&... condition) { return ConditionsTuple(condition->make_condition()...); }, conditions_);
}

template <typename... Policies>
void CogConditions<Policies...>::commit(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::SlotRef last_consumed)
{
  auto update = [&endpoint_id, &last_consumed]<typename Policy>(std::unique_ptr<InputCondition<Policy>>& condition)
  {
    if (Policy::endpoint_id == endpoint_id)
    {
      condition->commit(last_consumed);
    }
  };
  std::apply([&update](auto&... condition) { (update(condition), ...); }, conditions_);
}

template <typename... Policies>
template <std::size_t input_count>
void CogConditions<Policies...>::commit(const std::array<LastConsumedTuple, input_count>& records)
{
  std::apply([this](auto&... record) { (this->commit(std::get<0>(record), std::get<1>(record)), ...); }, records);
}

template <typename... Policies>
template <typename ViewPolicy>
auto CogConditions<Policies...>::get_max_new_msgs(const ConditionsTuple& conditions) -> PinionDifferenceType
{
  if constexpr (policy_count == 0)
  {
    return std::numeric_limits<PinionDifferenceType>::max();
  }
  else
  {
    auto has_conds = false;
    auto max_new_msgs = std::optional<PinionDifferenceType>();

    auto update_max = [&has_conds, &max_new_msgs](const auto& view_id, const auto& cond_id, const auto& condition)
    {
      if (view_id != cond_id)
      {
        return;
      }

      has_conds = true;

      if (!condition.is_active())
      {
        return;
      }

      max_new_msgs = max_new_msgs ? std::min<PinionDifferenceType>(*max_new_msgs, condition.get_num_messages())
                                  : PinionDifferenceType{condition.get_num_messages()};
    };

    std::apply(
      [&update_max](auto&... condition)
      { (update_max(ViewPolicy::endpoint_id, Policies::endpoint_id, condition), ...); },
      conditions);

    // If there are no conditions associated with this view then return max.

    if (!has_conds)
    {
      return std::numeric_limits<PinionDifferenceType>::max();
    }

    // There are no active conditions return zero.

    if (!max_new_msgs)
    {
      return PinionDifferenceType{0};
    }

    return *max_new_msgs;
  }
}

template <typename... Policies>
template <size_t index>
void CogConditions<Policies...>::set_unit_test_condition(std::shared_ptr<pinion::AbstractChannel> channel)
{
  std::get<index>(conditions_) = std::make_unique<InputCondition<PolicyType<index>>>(channel);
}

} // namespace clockwork
