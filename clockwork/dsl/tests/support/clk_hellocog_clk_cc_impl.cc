// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/clk_hellocog_clk_cc_dial.hh"

namespace clockwork::testing::cogs
{

void execute_cog(HelloCogDial& /*dial*/)
{
}

void execute_cog(HelloCogWithMetricsDial& /*dial*/) {}
void execute_cog(HelloInitDial& /*dial*/) {}
void execute_cog(HelloInit2Dial& /*dial*/) {}
} // namespace clockwork::testing::cogs
