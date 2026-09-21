// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/cog_metrics_custom_cog_clk_cc_dial.hh"
#include "clockwork/tests/support/cog_metrics_test_messages_clk_cc.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdint>

namespace clockwork::cog_metrics_test
{
namespace
{
uint32_t custom_exec_count = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
} // namespace

void execute_cog(CogMetricsCustomCogDial& dial)
{
  ++custom_exec_count;
  dial.get_outputs().get_ack().message().set_execution_count(custom_exec_count);
  dial.get_outputs().get_ack().mark_for_publish();
}
} // namespace clockwork::cog_metrics_test
