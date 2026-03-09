// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/cog/include_common.hh"
#include "clockwork/cog/tests/support/legacy_unit_test_cogs_test.hh"
#include "clockwork/cog/tests/support/unit_test_cog.hh"
#include "clockwork/cog/tests/support/unit_test_cogs_clk_cc_test.hh"
#include "clockwork/cog/tests/support/unit_test_configs_clk_cc.hh"
#include "clockwork/cog/tests/support/unit_test_messages_clk_cc.hh"
#include "clockwork/diagnostics/report_clk_cc.hh"
#include "clockwork/diagnostics/report_definitions.hh"
#include "clockwork/memory/start_lifetime_as.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace clockwork::cogs::testing
{
namespace
{

TEMPLATE_TEST_CASE("ResourceCogTestWrapper", "", ResourceCogTestWrapper, LegacyResourceCogTestWrapper)
{
  const auto time0 = jewels::time::SyncTime{};
  TestType test_cog;
  const auto memres1 = jewels::memory::MemoryResource{std::pmr::null_memory_resource()};
  const auto memres2 = jewels::memory::MemoryResource{std::pmr::get_default_resource()};
  test_cog.get_resources().set_memres1(memres1);
  test_cog.initialize(time0);
  REQUIRE(test_cog.get_resources().get_memres1() == memres1);
  REQUIRE(test_cog.get_resources().get_memres2() == memres2);
  REQUIRE(test_cog.execute(time0));
  REQUIRE_FALSE(test_cog.execute(time0));
}

TEMPLATE_TEST_CASE("ConfigCogTestWrapper", "", ConfigCogTestWrapper, LegacyConfigCogTestWrapper)
{
  const auto time0 = jewels::time::SyncTime{};
  TestType test_cog;
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
  std::tuple<LegacyStateInitCogTestWrapper, LegacyStatePeriodicCogTestWrapper>>;

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

TEMPLATE_TEST_CASE("InputCogTestWrapper", "", InputCogTestWrapper, LegacyInputCogTestWrapper)
{
  const auto time0 = jewels::time::SyncTime{};
  TestType test_cog;
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

TEMPLATE_TEST_CASE("OutputCogTestWrapper", "", OutputCogTestWrapper, LegacyOutputCogTestWrapper)
{
  auto current_time = jewels::time::SyncTime{};
  TestType test_cog;

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

TEMPLATE_TEST_CASE("DiagnosticsCogTestWrapper", "", DiagnosticsCogTestWrapper, LegacyDiagnosticsCogTestWrapper)
{
  auto current_time = jewels::time::SyncTime{};
  TestType test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
}

TEMPLATE_TEST_CASE("DiagnosticsCog2TestWrapper", "", DiagnosticsCog2TestWrapper, LegacyDiagnosticsCog2TestWrapper)
{
  auto current_time = jewels::time::SyncTime{};
  DiagnosticsCog2TestWrapper test_cog;
  test_cog.initialize(current_time);

  current_time += std::chrono::milliseconds(100);
  REQUIRE(test_cog.execute(current_time));
}

} // namespace
} // namespace clockwork::cogs::testing
