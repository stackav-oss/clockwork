// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/test_tools/tests/support/addition_message.hh"
#include "clockwork/test_tools/tests/support/addition_tester_cog_dial.hh"
#include "clockwork/test_tools/tests/support/sum_message.hh"
#include "jewels/container/circular_buffer.hh"

#include <catch2/catch_test_macros.hpp>

#include <ranges>

namespace clockwork::system_runner
{
void execute_cog(AdditionTesterCogDial& dial)
{
  if (dial.get_conditions().get_periodic_10hz().is_active())
  {
    auto& addition_message = dial.get_outputs().get_addition_message().message();
    addition_message.set_addend_one(6);
    addition_message.set_addend_two(5);
    dial.get_outputs().get_addition_message().mark_for_publish();
  }
  if (dial.get_conditions().get_new_sum_message().is_active())
  {
    for (const auto& sum_msg : dial.get_inputs().get_sum_message().get_new_msgs_view())
    {
      CHECK(sum_msg.get_sum() == 11);
    }
  }
}
} // namespace clockwork::system_runner
