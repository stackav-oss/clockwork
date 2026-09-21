// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/exec_time.hh"
#include "clockwork/tests/support/restore_test_cog_dial.hh"
#include "clockwork/tests/support/snapshot_test_messages.hh"
#include "jewels/uuid/uuid.hh"

namespace clockwork::testing
{
void execute_cog(RestoreTestCogDial& dial)
{
  auto& state = dial.get_states().get_state();
  const auto& config = dial.get_configs().get_config();

  dial.get_outputs().get_pub_state().message() = state;
  dial.get_outputs().get_pub_state().mark_for_publish();

  dial.get_outputs().get_pub_config().message() = config;
  dial.get_outputs().get_pub_config().mark_for_publish();
}
} // namespace clockwork::testing
