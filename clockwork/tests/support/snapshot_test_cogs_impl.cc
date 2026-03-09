// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/cond_messages_present.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/tests/support/exec_time.hh"
#include "clockwork/tests/support/snapshot_test_cogs_dial.hh"
#include "clockwork/tests/support/snapshot_test_messages.hh"
#include "jewels/container/circular_buffer.hh"

#include <cstdint>
#include <ranges>

namespace clockwork::testing
{
void execute_cog(StatefulCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  const auto& config = dial.get_configs().get_config();

  // Increment counter on each execution
  state.set_exec_count(state.get_exec_count() + 1);

  // Process any update messages
  if (dial.get_conditions().get_new_update().is_active())
  {
    for (const auto& update_msg : dial.get_inputs().get_update_message().get_new_msgs_view())
    {
      // Apply the increment from the message, multiplied by config
      int32_t increment = update_msg.get_increment() * config.get_multiplier();
      state.set_exec_count(state.get_exec_count() + increment);
    }
  }
}
} // namespace clockwork::testing
