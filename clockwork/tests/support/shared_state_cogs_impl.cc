// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/exec_time.hh"
#include "clockwork/tests/support/shared_state_cogs_dial.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <chrono>
#include <cstdint>
#include <ranges>
#include <thread>

namespace clockwork::testing
{

namespace
{
template <typename T>
void exe(T& dial)
{
  dial.get_outputs().get_exec_time().message().set_exec_time(dial.get_start_time());
  dial.get_outputs().get_exec_time().mark_for_publish();
}
} // namespace

void execute_cog(NoSleepCogDial& dial)
{
  exe(dial);
}

void execute_cog(LongSleepCogDial& dial)
{
  std::this_thread::sleep_for(std::chrono::seconds(1));
  exe(dial);
}

void execute_cog(SharedStateTesterCogDial& dial)
{
  auto& inputs = dial.get_inputs();

  auto& state_no_sleep = dial.get_states().get_state_no_sleep();
  auto view_no_sleep = inputs.get_input_no_sleep().get_new_msgs_view();
  state_no_sleep.set_exec_count(state_no_sleep.get_exec_count() + static_cast<int32_t>(view_no_sleep.size()));

  auto& state_long_sleep = dial.get_states().get_state_long_sleep();
  auto view_long_sleep = inputs.get_input_long_sleep().get_new_msgs_view();
  state_long_sleep.set_exec_count(state_long_sleep.get_exec_count() + static_cast<int32_t>(view_long_sleep.size()));

  jewels::log_cerr_info(
    "no sleeps: {} :: long sleeps: {}", state_no_sleep.get_exec_count(), state_long_sleep.get_exec_count());
}

} // namespace clockwork::testing
