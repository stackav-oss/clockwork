// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/tests/support/legacy_unit_test_cogs_dial.hh"
#include "clockwork/cog/tests/support/unit_test_configs_clk_cc.hh"
#include "clockwork/cog/tests/support/unit_test_messages_clk_cc.hh"
#include "clockwork/cog/tests/support/unit_test_states_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/dial/msg_input.hh"
#include "clockwork/pinion/publishable.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <ranges>

namespace clockwork::cogs::testing
{

void execute_cog(LegacyResourceCogDial& dial)
{
  REQUIRE(dial.get_resources().get_memres1() != dial.get_resources().get_memres2());
}

void execute_cog(LegacyConfigCogDial& dial)
{
  REQUIRE(dial.get_configs().get_config1().get_field1() == 1);
  REQUIRE(dial.get_configs().get_config2().get_field2() == 2);
}

void execute_cog(LegacyStateInitCogDial& dial)
{
  dial.get_states().get_clk_state().set_value(1);
  dial.get_states().get_cxx_state().value = 2;
}

void execute_cog(LegacyStatePeriodicCogDial& dial)
{
  dial.get_states().get_clk_state().set_value(dial.get_states().get_clk_state().get_value() + 1);
  ++dial.get_states().get_cxx_state().value;
}

void execute_cog(LegacyInputCogDial& dial)
{
  for (const auto& message : dial.get_inputs().get_input1().get_new_msgs_view())
  {
    ++dial.get_states().get_state().get_mutable_input1_count();
    dial.get_states().get_state().set_input1_value(message.get_field1());
  }
  for (const auto& message : dial.get_inputs().get_input2().get_new_msgs_view())
  {
    ++dial.get_states().get_state().get_mutable_input2_count();
    dial.get_states().get_state().set_input2_value(message.get_field2());
  }
  for (const auto& message : dial.get_inputs().get_input3().get_new_msgs_view())
  {
    ++dial.get_states().get_state().get_mutable_input3_count();
    dial.get_states().get_state().set_input3_value(message.get_field3());
  }
}

void execute_cog(LegacyOutputCogDial& dial)
{
  if (!dial.get_states().get_state().get_output1_sent())
  {
    dial.get_outputs().get_output1().message().set_field1(1);
    dial.get_outputs().get_output1().mark_for_publish();
    dial.get_states().get_state().set_output1_sent(true);
  }
  else
  {
    dial.get_outputs().get_output2().message().set_field2(2);
    dial.get_outputs().get_output2().mark_for_publish();
  }
}

void execute_cog(LegacyDiagnosticsCogDial& dial)
{
  dial.get_diagnostics().set<diagnostics::SignalId::injected_a>(10.0f);
  dial.get_diagnostics().set<diagnostics::SignalId::injected_b>(20.0f);
};

void execute_cog(LegacyDiagnosticsCog2Dial& dial)
{
  dial.get_diagnostics().get_diag1().set<diagnostics::SignalId::injected_a>(10);
  dial.get_diagnostics().get_diag1().set<diagnostics::SignalId::injected_b>(20);
  dial.get_diagnostics().get_diag2().set<diagnostics::SignalId::injected_a>(30.0f);
  dial.get_diagnostics().get_diag2().set<diagnostics::SignalId::injected_b>(40.0f);
}

} // namespace clockwork::cogs::testing
