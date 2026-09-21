// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/cog/time_since_last_exec_handler.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/observer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace clockwork
{

namespace detail
{

/// Trait to determine the handler type for a timer policy.
///
/// If the policy defines a HandlerType alias, that type is used.
/// Otherwise, defaults to TimeSinceLastExecHandler<Policy>.
template <typename Policy, typename = void>
struct HandlerTypeOf
{
  using type = TimeSinceLastExecHandler<Policy>;
};

template <typename Policy>
struct HandlerTypeOf<Policy, std::void_t<typename Policy::HandlerType>>
{
  using type = typename Policy::HandlerType;
};

/// Alias for the handler type associated with a timer policy.
template <typename Policy>
using handler_type_t = typename HandlerTypeOf<Policy>::type;

/// True if the cog policy defines `static constexpr bool has_dynamic_timer = true`.
/// Used by SimpleCog to conditionally pass the dynamic timer handler to make_dial().
template <typename P, typename = void>
inline constexpr bool has_dynamic_timer_v = false;

template <typename P>
inline constexpr bool has_dynamic_timer_v<P, std::void_t<decltype(P::has_dynamic_timer)>> = P::has_dynamic_timer;

} // namespace detail

/// Helper class to handle the set of cog timers. Maintains the necessary bookkeeping and
/// conversion to dial input types based on the templated policies.
///
/// @tparam Policies Timer policy structs. Each must provide:
///   - static constexpr auto endpoint_id: UUID for wiring
///   - static constexpr std::string_view name: for error messages
///   Policies for TimeSinceLastExecHandler additionally provide threshold_ns.
///   Policies for DynamicTimerHandler provide using HandlerType = DynamicTimerHandler<Policy>.
template <typename... Policies>
class CogTimers
{
public:
  static constexpr auto policy_count = sizeof...(Policies);
  template <typename Policy>
  using TimerPtr = std::shared_ptr<detail::handler_type_t<Policy>>;
  using PoliciesTuple = std::tuple<Policies...>;
  using TimersTuple = std::tuple<TimerPtr<Policies>...>;
  using ConditionsTuple = std::tuple<typename detail::handler_type_t<Policies>::ConditionType...>;

  /// Construct from a pinion timer handle.
  explicit CogTimers(jewels::memory::MemoryResource resource) noexcept;

  /// Validate that all internal types are set.
  [[nodiscard]] bool validate() const;

  /// Set the timer handle.
  /// @tparam CogType The cog type setting the handle, it is expected to have a `notify()` call to be invoked on
  /// updates.
  template <typename CogType>
  [[nodiscard]] jewels::expected<std::shared_ptr<pinion::Observer>, jewels::MonoError> set_handle(
    jewels::Uuid<common::EndpointClassId> endpoint_id,
    std::shared_ptr<AbstractTimer> abstract_timer,
    jewels::memory::ObjectPtr<CogType> cog);

  /// Construct the ConditionTypes for the given timer.
  /// @param[in] now The current time.
  /// @return tuple with all timer conditions
  template <std::size_t index>
  [[nodiscard]] auto make_condition(jewels::time::SyncTime now);

  /// Construct the ConditionTypes for the timers.
  /// @param[in] now The current time.
  /// @return tuple with all timer conditions
  [[nodiscard]] ConditionsTuple make_conditions(jewels::time::SyncTime now);

  /// Update last executed time and restart the timers (if needed).
  ///
  /// This resets any timer which was *not* an active trigger for this
  /// execution. We don't reset timers that were active in order to get good
  /// periodic behavior without drift. Timers that weren't active are being used
  /// for non-periodic purposes (e.g. timeout handling) and should reset on each
  /// execution.
  ///
  /// This assumes that the timers are self-rearming so that they don't need to
  /// be reset in order to be periodic.
  ///
  /// @param[in] last_exec_time The time of the start of the last exection.
  /// @param[in] conditions The conditions tuple (from make_conditions) which
  /// triggered this execution @return The duration used or unexpected if
  /// starting the timer fails
  [[nodiscard]] jewels::expected<void, jewels::MonoError>
  update_last_exec_time(jewels::time::SyncTime last_exec_time, const ConditionsTuple& conditions);

  /// Prime the timers. To be called on startup to start the timers for the first time.
  /// @param[in] start_time The start time.
  [[nodiscard]] jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime start_time);

  /// Set the timer handle at the specified index
  ///
  /// Used by unit test cogs to setup the dummy timers
  ///
  /// @tparam index Timer index
  /// @param[in] timer_ptr Timer pointer
  template <size_t index>
  void set_unit_test_timer(TimerPtr<typename std::tuple_element_t<index, PoliciesTuple>> timer_ptr);

  /// Call notify for expired timers
  ///
  /// Used by unit test cogs which run with dummy timers
  ///
  /// @param[in] now Current time
  void notify_expired_unit_test_timers(jewels::time::SyncTime now);

  /// Get a reference to the underlying handler at the given tuple index.
  /// @tparam index The index of the timer in the handlers tuple.
  template <std::size_t index>
  [[nodiscard]] auto& get_handler();

private:
  /// Memory resource
  jewels::memory::MemoryResource resource_;
  /// Policy structs
  PoliciesTuple policies_;
  /// Timers
  TimersTuple timers_;
};

} // namespace clockwork

#include "clockwork/cog/cog_timers.inl"
