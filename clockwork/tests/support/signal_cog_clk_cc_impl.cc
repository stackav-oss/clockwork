// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/signal_cog_clk_cc_dial.hh"
#include "clockwork/tests/support/signal_messages_clk_cc.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdint>
#include <ranges>

namespace clockwork::system_runner
{
void execute_cog(SignalCogDial& dial)
{
  if (!dial.get_conditions().get_new_trigger().is_active())
  {
    return;
  }
  auto& signals = dial.get_signals();

  uint32_t exec_count = 0;
  for (const auto& trigger_msg : dial.get_inputs().get_trigger().get_new_msgs_view())
  {
    const auto value = trigger_msg.get_value();
    const auto secondary = trigger_msg.get_secondary_value();

    // Post-aggregated group: set tracked_value signal (min/max), accumulate accumulated_value (sum)
    signals.set_tracked_value(value);
    signals.accumulate_accumulated_value(secondary);

    // Batched group: accumulate batched_value (min/max pre-agg), accumulate batched_count (sum pre-agg)
    signals.accumulate_batched_value(value);
    signals.accumulate_batched_count(secondary);
    ++exec_count;
  }

  // Publish acknowledgment
  dial.get_outputs().get_ack().message().set_execution_count(exec_count);
  dial.get_outputs().get_ack().mark_for_publish();
}
} // namespace clockwork::system_runner
