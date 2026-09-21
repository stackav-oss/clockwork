// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/cog/tests/support/cog_metrics_test_cog_clk_cc_dial.hh"
#include "clockwork/pinion/publishable.hh"

namespace clockwork::testing::cogs
{
void execute_cog(CogMetricsTestCogDial& dial)
{
  dial.get_outputs().get_result().mark_for_publish(1U);
}
} // namespace clockwork::testing::cogs
