// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/scaffolding/tests/support/test_cogs_dial.hh"
#include "clockwork/scaffolding/tests/support/test_msgs.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory_resource>
#include <tuple>
#include <vector>

namespace clockwork::testing
{

void execute_cog(InitCog1Dial& dial)
{
  jewels::log_cerr_info("cog INIT exec");
  dial.get_states().get_state().set_cycle(2);
  dial.get_states().get_state().set_group_id(dial.get_configs().get_config().get_group_id());
  dial.get_states().get_fib().map[0] = 0;
  dial.get_states().get_fib().map[1] = 1;
}

void execute_cog(TestCog1Dial& dial)
{
  std::ignore = std::pmr::vector<int>(0, 1000, dial.get_resources().get_memory1());
  std::ignore = std::pmr::vector<int>(0, 500, dial.get_resources().get_memory2());
  uint64_t counter = dial.get_states().get_state_a().get_cycle();
  jewels::log_cerr_info("cog 111 exec {}", counter);
  dial.get_outputs().get_out_a().message().set_cycle(counter);
  dial.get_outputs().get_out_a().message().set_config(dial.get_configs().get_cfg_a().get_id());
  dial.get_outputs().get_out_a().mark_for_publish();
  dial.get_states().get_state_a().set_cycle(counter + 1);
  dial.get_diagnostics().set<diagnostics::SignalId::injected_a>(1);
  dial.get_diagnostics().set<diagnostics::SignalId::injected_b>(0);
}

void execute_cog(TestCog2Dial& dial)
{
  std::ignore = std::pmr::vector<int>(0, 200, dial.get_resources().get_memory());
  auto first = dial.get_inputs().get_in_a().get_first_new();
  if (first != dial.get_inputs().get_in_a().end())
  {
    const auto cycle = first->get_cycle();
    auto& fib = dial.get_states().get_fib().map;
    auto result = fib[cycle] = fib.at(cycle - 1) + fib.at(cycle - 2);
    jewels::log_cerr_info("cog 222 exec {} -> {}", cycle, result);
    dial.get_outputs().get_out_a().message().set_cycle(cycle);
    dial.get_outputs().get_out_a().message().set_echo_config(first->get_config());
    dial.get_outputs().get_out_a().message().set_group_id(dial.get_states().get_state_a().get_group_id());
    dial.get_outputs().get_out_a().message().set_result(result);
    dial.get_outputs().get_out_a().mark_for_publish();
  }
  dial.get_diagnostics().set<diagnostics::SignalId::injected_a>(.5f);
  dial.get_diagnostics().set<diagnostics::SignalId::injected_b>(0.f);
}

} // namespace clockwork::testing
