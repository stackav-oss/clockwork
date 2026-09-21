// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/cog/wrappers/tests/support/legacy_parameterized_unit_test_cogs_impl.hh"
#pragma once

#include "clockwork/cog/wrappers/tests/support/legacy_parameterized_unit_test_cogs_impl.hh"

#include "clockwork/cog/wrappers/tests/support/legacy_parameterized_unit_test_cogs_dial.hh"
#include "clockwork/diagnostics/report_definitions.hh"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace clockwork::cogs::legacy_params_testing
{

template <ResourceCogDialType DialType>
void execute_cog(DialType& dial)
{
  REQUIRE(dial.get_resources().get_memres1() != dial.get_resources().get_memres2());
}

template <ConfigCogDialType DialType>
void execute_cog(DialType& dial)
{
  REQUIRE(dial.get_configs().get_config1().get_field1() == 1);
  REQUIRE(dial.get_configs().get_config2().get_field2() == 2);
}

template <StateInitCogDialType DialType>
void execute_cog(DialType& dial)
{
  dial.get_states().get_clk_state().set_value(1);
  dial.get_states().get_cxx_state().value = 2;
}

template <StatePeriodicCogDialType DialType>
void execute_cog(DialType& dial)
{
  dial.get_states().get_clk_state().set_value(dial.get_states().get_clk_state().get_value() + 1);
  ++dial.get_states().get_cxx_state().value;
}

template <InputCogDialType DialType>
void execute_cog(DialType& dial)
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

template <OutputCogDialType DialType>
void execute_cog(DialType& dial)
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

template <MultiConnectInputCogDialType DialType>
void execute_cog(DialType& dial)
{
  const auto input1 = dial.get_inputs().get_input1();
  const auto input1_0_view = input1[0].get().get_new_msgs_view();
  if (!input1_0_view.empty())
  {
    dial.get_outputs().get_output1_0().message() = input1_0_view.back();
    dial.get_outputs().get_output1_0().mark_for_publish();
  }
  const auto input1_1_view = input1[1].get().get_new_msgs_view();
  if (!input1_1_view.empty())
  {
    dial.get_outputs().get_output1_1().message() = input1_1_view.back();
    dial.get_outputs().get_output1_1().mark_for_publish();
  }
  const auto input2 = dial.get_inputs().get_input2();
  const auto input2_0_view = input2[0].get().get_new_msgs_view();
  if (!input2_0_view.empty())
  {
    dial.get_outputs().get_output2_0().message() = input2_0_view.back();
    dial.get_outputs().get_output2_0().mark_for_publish();
  }
  const auto input2_1_view = input2[1].get().get_new_msgs_view();
  if (!input2_1_view.empty())
  {
    dial.get_outputs().get_output2_1().message() = input2_1_view.back();
    dial.get_outputs().get_output2_1().mark_for_publish();
  }
  const auto input2_2_view = input2[2].get().get_new_msgs_view();
  if (!input2_2_view.empty())
  {
    dial.get_outputs().get_output2_2().message() = input2_2_view.back();
    dial.get_outputs().get_output2_2().mark_for_publish();
  }
}

template <DiagnosticsCogDialType DialType>
void execute_cog(DialType& dial)
{
  dial.get_diagnostics().template set<diagnostics::SignalId::injected_a>(10.0f);
  dial.get_diagnostics().template set<diagnostics::SignalId::injected_b>(20.0f);
};

template <DiagnosticsCog2DialType DialType>
void execute_cog(DialType& dial)
{
  dial.get_diagnostics().get_diag1().template set<diagnostics::SignalId::injected_a>(10);
  dial.get_diagnostics().get_diag1().template set<diagnostics::SignalId::injected_b>(20);
  dial.get_diagnostics().get_diag2().template set<diagnostics::SignalId::injected_a>(30.0f);
  dial.get_diagnostics().get_diag2().template set<diagnostics::SignalId::injected_b>(40.0f);
}

template <SignalsCogDialType DialType>
void execute_cog(DialType& dial)
{
  auto& exec_count = dial.get_states().get_state().get_mutable_exec_count();
  ++exec_count;
  dial.get_signals().set_value(static_cast<int64_t>(exec_count));
}

template <SignalsCog2DialType DialType>
void execute_cog(DialType& dial)
{
  auto& exec_count = dial.get_states().get_state().get_mutable_exec_count();
  ++exec_count;
  dial.get_signals().set_value1(static_cast<int64_t>(exec_count));
  dial.get_signals().accumulate_value2(exec_count + 1);
  dial.get_signals().accumulate_value2(exec_count + 2);
  dial.get_signals().accumulate_value3(exec_count + 2);
  dial.get_signals().accumulate_value3(exec_count + 3);
  dial.get_signals().accumulate_value4(exec_count + 3);
  dial.get_signals().accumulate_value4(exec_count + 4);
}

} // namespace clockwork::cogs::legacy_params_testing
