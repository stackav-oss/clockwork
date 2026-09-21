// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/publishable.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/tests/support/cog_metrics_driver_cog_clk_cc_dial.hh"
#include "clockwork/tests/support/cog_metrics_test_messages_clk_cc.hh"
#include "jewels/uuid/uuid.hh"

#include <cstdint>

namespace clockwork::cog_metrics_test
{
namespace
{
uint32_t driver_counter = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables) For testing purposes only
} // namespace

void execute_cog(CogMetricsDriverCogDial& dial)
{
  ++driver_counter;
  // Send trigger messages to both test cogs
  dial.get_outputs().get_trigger_a().message().set_counter(driver_counter);
  dial.get_outputs().get_trigger_a().mark_for_publish();

  dial.get_outputs().get_trigger_b().message().set_counter(driver_counter);
  dial.get_outputs().get_trigger_b().mark_for_publish();

  dial.get_outputs().get_trigger_c().message().set_counter(driver_counter);
  dial.get_outputs().get_trigger_c().mark_for_publish();
}
} // namespace clockwork::cog_metrics_test
