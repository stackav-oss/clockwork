// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/stress_cog_dial.hh"

#include <fmt/base.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace clockwork::tests
{

namespace
{

/// Time each cog sleeps to simulate processing
constexpr auto cog_sleep_time = std::chrono::microseconds(50);

/// Stress timer cog implementation
/// @tparam index Cog index
/// @tparam DialType Cog dial type
/// @param[in] dial Cog dial
template <size_t index, typename DialType>
void execute_stress_timer_cog_impl(DialType& dial)
{
  auto& state = dial.get_states().get_state();
  ++state.run_counters.at(index + 1U);
  dial.get_outputs().get_exec_state().message().set_exec_count(state.send_exec_counts.at(index));
  ++state.send_exec_counts.at(index);
  dial.get_outputs().get_exec_state().mark_for_publish();
  std::this_thread::sleep_for(cog_sleep_time);
}

/// Process messages on an input channel
/// @param[in] condition Input condition
/// @param[in] input Input handle
/// @param[in,out] receive_counter Message received counter
/// @param[in,out] recv_exec_count Expected receive count
/// @param[in,out] drop_counter Drop counter
void process_stress_input(
  const auto& condition, auto& input, size_t& recv_counter, int32_t& recv_exec_count, size_t& drop_counter)
{
  if (condition.is_active())
  {
    for (const auto& message : input.get_new_msgs_view())
    {
      const auto send_exec_count = message.get_exec_count();
      drop_counter += static_cast<size_t>(send_exec_count - recv_exec_count);
      recv_exec_count = send_exec_count + 1;
      ++recv_counter;
    }
  }
}

} // namespace

void execute_cog(StressInitCogDial& /*dial*/) {}

void execute_cog(StressReporterCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  ++state.reporter_counter;
  fmt::print("{}: run [", state.reporter_counter);
  bool first = true;
  bool second = false;
  for (auto& counter : state.run_counters)
  {
    fmt::print("{}{}{}", first || second ? "" : ", ", counter, first ? "] [" : "");
    second = first;
    first = false;
    counter = 0U;
  }
  fmt::print("] rcv [");
  first = true;
  for (auto& counter : state.recv_counters)
  {
    fmt::print("{}{}", first ? "" : ", ", counter);
    first = false;
    counter = 0U;
  }
  first = true;
  fmt::print("] drops [");
  for (auto& counter : state.drop_counters)
  {
    fmt::print("{}{}", first ? "" : ", ", counter);
    first = false;
    counter = 0U;
  }
  fmt::println("]");
}

void execute_cog(StressTimerCog2Dial& dial)
{
  execute_stress_timer_cog_impl<0U>(dial);
}

void execute_cog(StressTimerCog3Dial& dial)
{
  execute_stress_timer_cog_impl<1U>(dial);
}

void execute_cog(StressTimerCog5Dial& dial)
{
  execute_stress_timer_cog_impl<2U>(dial);
}

void execute_cog(StressTimerCog7Dial& dial)
{
  execute_stress_timer_cog_impl<3U>(dial);
}

void execute_cog(StressTimerCog11Dial& dial)
{
  execute_stress_timer_cog_impl<4U>(dial);
}

void execute_cog(StressSubscriberCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  ++state.run_counters.at(0U);
  process_stress_input(
    dial.get_conditions().get_new_exec_state2(),
    dial.get_inputs().get_exec_state2(),
    state.recv_counters.at(0U),
    state.recv_exec_counts.at(0U),
    state.drop_counters.at(0U));
  process_stress_input(
    dial.get_conditions().get_new_exec_state3(),
    dial.get_inputs().get_exec_state3(),
    state.recv_counters.at(1U),
    state.recv_exec_counts.at(1U),
    state.drop_counters.at(1U));
  process_stress_input(
    dial.get_conditions().get_new_exec_state5(),
    dial.get_inputs().get_exec_state5(),
    state.recv_counters.at(2U),
    state.recv_exec_counts.at(2U),
    state.drop_counters.at(2U));
  process_stress_input(
    dial.get_conditions().get_new_exec_state7(),
    dial.get_inputs().get_exec_state7(),
    state.recv_counters.at(3U),
    state.recv_exec_counts.at(3U),
    state.drop_counters.at(3U));
  process_stress_input(
    dial.get_conditions().get_new_exec_state11(),
    dial.get_inputs().get_exec_state11(),
    state.recv_counters.at(4U),
    state.recv_exec_counts.at(4U),
    state.drop_counters.at(4U));
}

} // namespace clockwork::tests
