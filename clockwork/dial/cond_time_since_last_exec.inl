// IWYU pragma: private, include "clockwork/dial/cond_time_since_last_exec.hh"
#pragma once

#include "clockwork/dial/cond_time_since_last_exec.hh"

#include "clockwork/dial/exec_condition.hh"

#include <chrono>
#include <cstdint>

namespace clockwork
{

template <uint64_t threshold_ns>
constexpr TimeSinceLastExecCondition<threshold_ns>::TimeSinceLastExecCondition(
  const bool is_active, const std::chrono::nanoseconds time_since_last_exec) noexcept
  : ExecCondition{is_active}, time_since_last_exec_(time_since_last_exec)
{
}

template <uint64_t threshold_ns>
[[nodiscard]] constexpr std::chrono::nanoseconds
TimeSinceLastExecCondition<threshold_ns>::get_time_since_last_exec() const noexcept
{
  return time_since_last_exec_;
}

template <uint64_t threshold_ns>
[[nodiscard]] consteval std::chrono::nanoseconds
TimeSinceLastExecCondition<threshold_ns>::get_threshold() const noexcept
{
  return std::chrono::nanoseconds{threshold_ns};
}

// We can write static unit tests, so they are just here instead of a separate compiled test:
static_assert(!TimeSinceLastExecCondition<0>{}.is_active());
static_assert(TimeSinceLastExecCondition<0>{}.get_time_since_last_exec() == std::chrono::nanoseconds{0});
static_assert(TimeSinceLastExecCondition<1>{}.get_threshold() == std::chrono::nanoseconds{1});
static_assert(TimeSinceLastExecCondition<0>{true, {}}.is_active());
static_assert(
  TimeSinceLastExecCondition<0>{true, std::chrono::nanoseconds{3}}.get_time_since_last_exec() ==
  std::chrono::nanoseconds{3});

} // namespace clockwork
