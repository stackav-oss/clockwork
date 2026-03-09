// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/examples/log_runner/example_cogs_clk_cc_dial.hh"
#include "clockwork/examples/log_runner/test_message_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <chrono>

namespace clockwork::log_runner
{

void execute_cog(ExampleCogPublisherDial& dial)
{
  jewels::log_cerr_warn("Example cog called publisher called at {}", dial.get_start_time());
  dial.get_outputs().get_test_message_output().message().get_underlying_message_string().set_truncate("a message");

  dial.get_outputs().get_test_message_output().mark_for_publish();
}

void execute_cog(ExampleCogSubscriberDial& dial)
{
  jewels::log_cerr_warn("Example cog subscriber called at {}", dial.get_start_time());
}
} // namespace clockwork::log_runner
