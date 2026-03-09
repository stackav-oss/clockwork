// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/cond_time_since_last_exec.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/tests/support/signal_messages_clk_cc.hh"
#include "clockwork/tests/support/signal_tester_cog_clk_cc_dial.hh"
#include "jewels/container/circular_buffer.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <ranges>

namespace clockwork::system_runner
{
namespace
{
uint32_t trigger_counter = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

void execute_cog(SignalTesterCogDial& dial)
{
  if (dial.get_conditions().get_periodic_10hz().is_active())
  {
    auto& trigger_msg = dial.get_outputs().get_trigger().message();
    // Use incrementing values so signal aggregation results are predictable
    trigger_msg.set_value(static_cast<int64_t>(trigger_counter) * 10);
    trigger_msg.set_secondary_value(trigger_counter + 1);
    dial.get_outputs().get_trigger().mark_for_publish();
    ++trigger_counter;
  }
  if (dial.get_conditions().get_new_ack().is_active())
  {
    for (const auto& ack_msg : dial.get_inputs().get_ack().get_new_msgs_view())
    {
      // Verify the signal cog is actually executing and producing acks
      CHECK(ack_msg.get_execution_count() > 0);
    }
  }
}
} // namespace clockwork::system_runner
