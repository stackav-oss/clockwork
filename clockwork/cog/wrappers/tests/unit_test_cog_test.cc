// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/tests/support/unit_test_states_clk_cc.hh"
#include "clockwork/cog/wrappers/tests/support/legacy_parameterized_unit_test_cogs_test.hh"
#include "clockwork/cog/wrappers/tests/support/legacy_unit_test_cogs_test.hh"
#include "clockwork/cog/wrappers/tests/support/parameterized_unit_test_cogs_clk_cc_test.hh"
#include "clockwork/cog/wrappers/tests/support/unit_test_cogs_clk_cc_test.hh"
#include "clockwork/cog/wrappers/tests/support/unit_test_configs_clk_cc.hh"
#include "clockwork/cog/wrappers/tests/support/unit_test_messages_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/utility/string_param.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <tuple>
#include <variant>

namespace clockwork::cogs::testing
{
namespace
{

using ResourceCogTestWrapperTestTypes = std::tuple<
  std::tuple<ResourceCogTestWrapper>,
  std::tuple<legacy_testing::ResourceCogTestWrapper>,
  std::tuple<params_testing::ResourceCogTestWrapper<bool>>,
  std::tuple<legacy_params_testing::ResourceCogTestWrapper<bool>>>;

TEMPLATE_LIST_TEST_CASE("ResourceCogTestWrapper", "", ResourceCogTestWrapperTestTypes)
{
  using ResourceCogTestWrapperType = std::tuple_element_t<0, TestType>;

  const auto time0 = jewels::time::SyncTime{};
  ResourceCogTestWrapperType test_cog;
  const auto memres1 = jewels::memory::MemoryResource{std::pmr::null_memory_resource()};
  const auto memres2 = jewels::memory::MemoryResource{std::pmr::get_default_resource()};
  test_cog.get_resources().set_memres1(memres1);
  test_cog.initialize(time0);
  REQUIRE(test_cog.get_resources().get_memres1() == memres1);
  REQUIRE(test_cog.get_resources().get_memres2() == memres2);
  REQUIRE(test_cog.execute(time0));
  REQUIRE_FALSE(test_cog.execute(time0));
}

using ConfigCogTestWrapperTestTypes = std::tuple<
  std::tuple<ConfigCogTestWrapper>,
  std::tuple<legacy_testing::ConfigCogTestWrapper>,
  std::tuple<params_testing::ConfigCogTestWrapper<Config1, Config2>>,
  std::tuple<legacy_params_testing::ConfigCogTestWrapper<Config1, Config2>>>;

TEMPLATE_LIST_TEST_CASE("ConfigCogTestWrapper", "", ConfigCogTestWrapperTestTypes)
{
  using ConfigCogTestWrapperType = std::tuple_element_t<0, TestType>;

  const auto time0 = jewels::time::SyncTime{};
  ConfigCogTestWrapperType test_cog;
  auto config1 = std::make_shared<Tappy<Config1>>();
  config1->set_field1(2);
  test_cog.get_configs().set_config1_handle(config1);
  REQUIRE(test_cog.get_configs().get_config1().get_field1() == 2);
  config1->set_field1(1);
  REQUIRE(test_cog.get_configs().get_config1().get_field1() == 1);
  REQUIRE(test_cog.get_configs().get_config1_handle()->get_field1() == 1);
  test_cog.initialize(time0);
  test_cog.get_configs().get_config2().set_field2(2);
  REQUIRE(test_cog.get_configs().get_config2().get_field2() == 2);
  REQUIRE(test_cog.execute(time0));
  REQUIRE_FALSE(test_cog.execute(time0));
}

using StateCogTestWrapperTestTypes = std::tuple<
  std::tuple<StateInitCogTestWrapper, StatePeriodicCogTestWrapper>,
  std::tuple<legacy_testing::StateInitCogTestWrapper, legacy_testing::StatePeriodicCogTestWrapper>,
  std::tuple<
    params_testing::StateInitCogTestWrapper<Tappy<ClkState>, CxxState>,
    params_testing::StatePeriodicCogTestWrapper<Tappy<ClkState>, CxxState>>,
  std::tuple<
    legacy_params_testing::StateInitCogTestWrapper<Tappy<ClkState>, CxxState>,
    legacy_params_testing::StatePeriodicCogTestWrapper<Tappy<ClkState>, CxxState>>>;

TEMPLATE_LIST_TEST_CASE("StateCogTestWrapper", "", StateCogTestWrapperTestTypes)
{
  using StateInitCogTestWrapperType = std::tuple_element_t<0, TestType>;
  using StatePeriodicCogTestWrapperType = std::tuple_element_t<1, TestType>;

  const auto time0 = jewels::time::SyncTime{};
  StateInitCogTestWrapperType test_init_cog;
  test_init_cog.initialize(time0);

  REQUIRE(test_init_cog.get_states().get_clk_state().get_value() == 0);
  REQUIRE(test_init_cog.get_states().get_cxx_state().value == 0);

  REQUIRE(test_init_cog.execute(time0));

  REQUIRE(test_init_cog.get_states().get_clk_state().get_value() == 1);
  REQUIRE(test_init_cog.get_states().get_cxx_state().value == 2);

  StatePeriodicCogTestWrapperType test_periodic_cog;
  auto clk_state_handle = test_init_cog.get_states().get_clk_state_handle();
  test_periodic_cog.get_states().set_clk_state_handle(test_init_cog.get_states().get_clk_state_handle());
  test_periodic_cog.get_states().set_cxx_state_handle(test_init_cog.get_states().get_cxx_state_handle());
  test_periodic_cog.initialize(time0);

  REQUIRE(test_periodic_cog.get_states().get_clk_state().get_value() == 1);
  REQUIRE(test_periodic_cog.get_states().get_cxx_state().value == 2);

  const auto time99 = jewels::time::SyncTime{std::chrono::milliseconds(99)};
  REQUIRE_FALSE(test_periodic_cog.execute(time99));
  const auto time100 = jewels::time::SyncTime{std::chrono::milliseconds(100)};
  REQUIRE(test_periodic_cog.execute(time100));

  REQUIRE(test_periodic_cog.get_states().get_clk_state().get_value() == 2);
  REQUIRE(test_periodic_cog.get_states().get_cxx_state().value == 3);

  const auto time199 = jewels::time::SyncTime{std::chrono::milliseconds(199)};
  REQUIRE_FALSE(test_periodic_cog.execute(time199));
  const auto time200 = jewels::time::SyncTime{std::chrono::milliseconds(200)};
  REQUIRE(test_periodic_cog.execute(time200));

  REQUIRE(test_periodic_cog.get_states().get_clk_state().get_value() == 3);
  REQUIRE(test_periodic_cog.get_states().get_cxx_state().value == 4);
}

using InputCogTestWrapperTestTypes = std::tuple<
  std::tuple<InputCogTestWrapper>,
  std::tuple<legacy_testing::InputCogTestWrapper>,
  std::tuple<params_testing::InputCogTestWrapper<Message1, Message2, Message3>>,
  std::tuple<legacy_params_testing::InputCogTestWrapper<Message1, Message2, Message3>>>;

TEMPLATE_LIST_TEST_CASE("InputCogTestWrapper", "", InputCogTestWrapperTestTypes)
{
  using InputCogTestWrapperType = std::tuple_element_t<0, TestType>;

  const auto time0 = jewels::time::SyncTime{};
  InputCogTestWrapperType test_cog;
  REQUIRE(test_cog.get_inputs().get_input1().get_num_slots() == 1U);
  REQUIRE(test_cog.get_inputs().get_input2().get_num_slots() == 2U);
  REQUIRE(test_cog.get_inputs().get_input3().get_num_slots() == 3U);

  test_cog.get_inputs().get_input1().set_num_slots(10U);
  test_cog.get_inputs().get_input2().set_num_slots(10U);
  test_cog.get_inputs().get_input3().set_num_slots(10U);
  test_cog.initialize(time0);
  REQUIRE(test_cog.get_inputs().get_input1().get_num_slots() == 10U);
  REQUIRE(test_cog.get_inputs().get_input2().get_num_slots() == 10U);
  REQUIRE(test_cog.get_inputs().get_input3().get_num_slots() == 10U);

  REQUIRE_FALSE(test_cog.execute(time0));
  REQUIRE(test_cog.get_states().get_state().get_input1_count() == 0U);
  REQUIRE(test_cog.get_states().get_state().get_input2_count() == 0U);
  REQUIRE(test_cog.get_states().get_state().get_input3_count() == 0U);

  SECTION("Publish one message to each channel")
  {
    test_cog.get_inputs().get_input1().publish([](auto& msg) { msg.set_field1(1); }, time0);
    Tappy<Message2> msg2{};
    msg2.set_field2(2);
    test_cog.get_inputs().get_input2().publish(msg2, time0);
    test_cog.get_inputs().get_input3().publish([](auto& msg) { msg.set_field3(3); }, time0);

    REQUIRE(test_cog.execute(time0, std::chrono::microseconds(100)));
    REQUIRE(test_cog.get_states().get_state().get_input1_count() == 1U);
    REQUIRE(test_cog.get_states().get_state().get_input1_value() == 1);
    REQUIRE(test_cog.get_states().get_state().get_input2_count() == 1U);
    REQUIRE(test_cog.get_states().get_state().get_input2_value() == 2);
    REQUIRE(test_cog.get_states().get_state().get_input3_count() == 1U);
    REQUIRE(test_cog.get_states().get_state().get_input3_value() == 3);
  }

  SECTION("Input1 executes for every message")
  {
    Tappy<Message1> msg1{};

    for (auto i = 1U; i <= 10U; ++i)
    {
      msg1.set_field1(static_cast<int32_t>(i));
      test_cog.get_inputs().get_input1().publish(msg1, time0);
    }

    for (auto i = 1U; i <= 10U; ++i)
    {
      CAPTURE(i);
      REQUIRE(test_cog.execute(time0));
      REQUIRE(test_cog.get_states().get_state().get_input1_count() == i);
      REQUIRE(test_cog.get_states().get_state().get_input1_value() == static_cast<int32_t>(i));
    }

    REQUIRE_FALSE(test_cog.execute(time0));
  }

  SECTION("Input2 drops every other message")
  {
    Tappy<Message2> msg2{};

    for (auto i = 1U; i <= 10U; ++i)
    {
      msg2.set_field2(static_cast<int32_t>(i));
      test_cog.get_inputs().get_input2().publish(msg2, time0);
    }

    for (auto i = 1U; i <= 5U; ++i)
    {
      CAPTURE(i);
      REQUIRE(test_cog.execute(time0));
      REQUIRE(test_cog.get_states().get_state().get_input2_count() == i);
      REQUIRE(test_cog.get_states().get_state().get_input2_value() == static_cast<int32_t>(i) * 2);
    }

    REQUIRE_FALSE(test_cog.execute(time0));
  }

  SECTION("Input3 sees all messages in batches of 3")
  {
    Tappy<Message3> msg3{};

    for (auto i = 1U; i <= 10U; ++i)
    {
      msg3.set_field3(static_cast<int32_t>(i));
      test_cog.get_inputs().get_input3().publish(msg3, time0);
    }

    for (auto i = 1U; i <= 3U; ++i)
    {
      CAPTURE(i);
      REQUIRE(test_cog.execute(time0));
      REQUIRE(test_cog.get_states().get_state().get_input3_count() == i * 3U);
      REQUIRE(test_cog.get_states().get_state().get_input3_value() == static_cast<int32_t>(i) * 3);
    }

    REQUIRE(test_cog.execute(time0));
    REQUIRE(test_cog.get_states().get_state().get_input3_count() == 10U);
    REQUIRE(test_cog.get_states().get_state().get_input3_value() == 10);

    REQUIRE_FALSE(test_cog.execute(time0));
  }
}

using OutputCogTestWrapperTestTypes = std::tuple<
  std::tuple<OutputCogTestWrapper>,
  std::tuple<legacy_testing::OutputCogTestWrapper>,
  std::tuple<params_testing::OutputCogTestWrapper<Message1, Message2>>,
  std::tuple<legacy_params_testing::OutputCogTestWrapper<Message1, Message2>>>;

TEMPLATE_LIST_TEST_CASE("OutputCogTestWrapper", "", OutputCogTestWrapperTestTypes)
{
  using OutputCogTestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  OutputCogTestWrapperType test_cog;

  test_cog.initialize(current_time);

  REQUIRE_FALSE(test_cog.get_outputs().get_output1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output1().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field1() == 1);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output2().try_get_next_message());

  current_time += std::chrono::milliseconds(999);
  REQUIRE_FALSE(test_cog.execute(current_time));
  current_time += std::chrono::milliseconds(1);
  REQUIRE(test_cog.execute(current_time));
  REQUIRE(test_cog.get_outputs().get_output2().get_next_message().get_field2() == 2);

  current_time += std::chrono::seconds(1);
  REQUIRE(test_cog.execute(current_time, std::chrono::microseconds(100)));
  REQUIRE(test_cog.get_outputs().get_output2().get_next_message().get_field2() == 2);
}

using MultiConnectInputCogTestWrapperTestTypes = std::tuple<
  std::tuple<MultiConnectInputCogTestWrapper>,
  std::tuple<legacy_testing::MultiConnectInputCogTestWrapper>,
  std::tuple<params_testing::MultiConnectInputCogTestWrapper<Message1, Message2>>,
  std::tuple<legacy_params_testing::MultiConnectInputCogTestWrapper<Message1, Message2>>>;

TEMPLATE_LIST_TEST_CASE("MultiConnectInputCogTestWrapper", "", MultiConnectInputCogTestWrapperTestTypes)
{
  using MultiConnectInputCogTestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  MultiConnectInputCogTestWrapperType test_cog;

  test_cog.initialize(current_time);

  REQUIRE_FALSE(test_cog.get_outputs().get_output1_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_2().try_get_next_message());

  Tappy<Message1> msg1{};
  msg1.set_field1(1);
  test_cog.get_inputs().get_input1()[0].publish(msg1, current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output1_0().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field1() == 1);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_2().try_get_next_message());

  msg1.set_field1(2);
  test_cog.get_inputs().get_input1()[1].publish(msg1, current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output1_1().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field1() == 2);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_2().try_get_next_message());

  Tappy<Message2> msg2{};
  msg2.set_field2(3);
  test_cog.get_inputs().get_input2()[0].publish(msg2, current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output2_0().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field2() == 3);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_2().try_get_next_message());

  msg2.set_field2(4);
  test_cog.get_inputs().get_input2()[1].publish(msg2, current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output2_1().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field2() == 4);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_2().try_get_next_message());

  msg2.set_field2(5);
  test_cog.get_inputs().get_input2()[2].publish(msg2, current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
  {
    const auto maybe_msg = test_cog.get_outputs().get_output2_2().try_get_next_message();
    REQUIRE(maybe_msg);
    REQUIRE(maybe_msg->get().get_field2() == 5);
  }
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output1_1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_0().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_outputs().get_output2_1().try_get_next_message());
}

using DiagnosticsCogTestWrapperTestTypes = std::tuple<
  std::tuple<DiagnosticsCogTestWrapper>,
  std::tuple<legacy_testing::DiagnosticsCogTestWrapper>,
  std::tuple<params_testing::DiagnosticsCogTestWrapper<"fault_injector_b", "a">>,
  std::tuple<legacy_params_testing::DiagnosticsCogTestWrapper<"fault_injector_b", "a">>>;

TEMPLATE_LIST_TEST_CASE("DiagnosticsCogTestWrapper", "", DiagnosticsCogTestWrapperTestTypes)
{
  using DiagnosticsCogTestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  DiagnosticsCogTestWrapperType test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
}

using DiagnosticsCog2TestWrapperTestTypes = std::tuple<
  std::tuple<DiagnosticsCog2TestWrapper>,
  std::tuple<legacy_testing::DiagnosticsCog2TestWrapper>,
  std::tuple<params_testing::DiagnosticsCog2TestWrapper<"fault_injector_a", "fault_injector_b", "a">>,
  std::tuple<legacy_params_testing::DiagnosticsCog2TestWrapper<"fault_injector_a", "fault_injector_b", "a">>>;

TEMPLATE_LIST_TEST_CASE("DiagnosticsCog2TestWrapper", "", DiagnosticsCog2TestWrapperTestTypes)
{
  using DiagnosticsCog2TestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  DiagnosticsCog2TestWrapperType test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
}

using SignalsCogTestWrapperTestTypes = std::tuple<
  std::tuple<SignalsCogTestWrapper>,
  std::tuple<legacy_testing::SignalsCogTestWrapper>,
  std::tuple<params_testing::SignalsCogTestWrapper<SignalsCogState>>,
  std::tuple<legacy_params_testing::SignalsCogTestWrapper<SignalsCogState>>>;

TEMPLATE_LIST_TEST_CASE("SignalsCogTestWrapper", "", SignalsCogTestWrapperTestTypes)
{
  using SignalsCogTestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  SignalsCogTestWrapperType test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  REQUIRE_FALSE(test_cog.get_signals().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  const auto& signals_msg = test_cog.get_signals().get_next_message();
  REQUIRE(signals_msg.get_execution_count() == 2U);
  REQUIRE(signals_msg.get_value_value_min() == 1);
  REQUIRE(signals_msg.get_value_value_max() == 2);
}

using SignalsCog2TestWrapperTestTypes = std::tuple<
  std::tuple<SignalsCog2TestWrapper>,
  std::tuple<legacy_testing::SignalsCog2TestWrapper>,
  std::tuple<params_testing::SignalsCog2TestWrapper<SignalsCogState>>,
  std::tuple<legacy_params_testing::SignalsCog2TestWrapper<SignalsCogState>>>;

TEMPLATE_LIST_TEST_CASE("SignalsCog2TestWrapper", "", SignalsCog2TestWrapperTestTypes)
{
  using SignalsCog2TestWrapperType = std::tuple_element_t<0, TestType>;

  auto current_time = jewels::time::SyncTime{};
  SignalsCog2TestWrapperType test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  REQUIRE_FALSE(test_cog.get_signals().get_group1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_signals().get_group2().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  auto group1_msg = test_cog.get_signals().get_group1().get_next_message();
  REQUIRE(group1_msg.get_execution_count() == 2U);
  REQUIRE(group1_msg.get_value1_value_min() == 1);
  REQUIRE(group1_msg.get_value1_value_max() == 2);
  REQUIRE(group1_msg.get_value2_sum_max() == 7U);
  REQUIRE(group1_msg.get_value2_sum_mean() == 6.0f);
  REQUIRE_FALSE(test_cog.get_signals().get_group2().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  REQUIRE_FALSE(test_cog.get_signals().get_group1().try_get_next_message());
  auto group2_msg = test_cog.get_signals().get_group2().get_next_message();
  REQUIRE(group2_msg.get_signals().size() == 3);
  REQUIRE(group2_msg.get_signals()[0].get_value3_min() == 3);
  REQUIRE(group2_msg.get_signals()[0].get_value3_max() == 4);
  REQUIRE(group2_msg.get_signals()[0].get_value4_sum() == 9);
  REQUIRE(group2_msg.get_signals()[1].get_value3_min() == 4);
  REQUIRE(group2_msg.get_signals()[1].get_value3_max() == 5);
  REQUIRE(group2_msg.get_signals()[1].get_value4_sum() == 11);
  REQUIRE(group2_msg.get_signals()[2].get_value3_min() == 5);
  REQUIRE(group2_msg.get_signals()[2].get_value3_max() == 6);
  REQUIRE(group2_msg.get_signals()[2].get_value4_sum() == 13);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  group1_msg = test_cog.get_signals().get_group1().get_next_message();
  REQUIRE(group1_msg.get_execution_count() == 2U);
  REQUIRE(group1_msg.get_value1_value_min() == 3);
  REQUIRE(group1_msg.get_value1_value_max() == 4);
  REQUIRE(group1_msg.get_value2_sum_max() == 11U);
  REQUIRE(group1_msg.get_value2_sum_mean() == 10.0f);
  REQUIRE_FALSE(test_cog.get_signals().get_group2().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  REQUIRE_FALSE(test_cog.get_signals().get_group1().try_get_next_message());
  REQUIRE_FALSE(test_cog.get_signals().get_group2().try_get_next_message());

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));

  group1_msg = test_cog.get_signals().get_group1().get_next_message();
  REQUIRE(group1_msg.get_execution_count() == 2U);
  REQUIRE(group1_msg.get_value1_value_min() == 5);
  REQUIRE(group1_msg.get_value1_value_max() == 6);
  REQUIRE(group1_msg.get_value2_sum_max() == 15U);
  REQUIRE(group1_msg.get_value2_sum_mean() == 14.0f);
  group2_msg = test_cog.get_signals().get_group2().get_next_message();
  REQUIRE(group2_msg.get_signals().size() == 3);
  REQUIRE(group2_msg.get_signals()[0].get_value3_min() == 6);
  REQUIRE(group2_msg.get_signals()[0].get_value3_max() == 7);
  REQUIRE(group2_msg.get_signals()[0].get_value4_sum() == 15);
  REQUIRE(group2_msg.get_signals()[1].get_value3_min() == 7);
  REQUIRE(group2_msg.get_signals()[1].get_value3_max() == 8);
  REQUIRE(group2_msg.get_signals()[1].get_value4_sum() == 17);
  REQUIRE(group2_msg.get_signals()[2].get_value3_min() == 8);
  REQUIRE(group2_msg.get_signals()[2].get_value3_max() == 9);
  REQUIRE(group2_msg.get_signals()[2].get_value4_sum() == 19);
}

} // namespace
} // namespace clockwork::cogs::testing
