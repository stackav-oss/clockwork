// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/gil_lock_guard.hh"
#include "clockwork/python/python_init.hh"
#include "clockwork/python/python_object.hh"
#include "clockwork/python/python_state.hh"
#include "clockwork/python/tests/support/test_config_clk_cc.hh"
#include "clockwork/python/tests/support/test_input_message_clk_cc.hh"
#include "clockwork/python/tests/support/test_output_message_clk_cc.hh"
#include "clockwork/python/tests/support/test_state_clk_cc.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace clockwork::python::tests
{
namespace
{

TEST_CASE("Execute test cog through a generated dial")
{
  const jewels::memory::MemoryResource memory_resource(std::pmr::new_delete_resource());

  REQUIRE_THROWS(throw_if_not_initialized());
  python_init(InitializationMode::unit_test);
  REQUIRE_THROWS(python_init(InitializationMode::production));
  REQUIRE_NOTHROW(python_init(InitializationMode::unit_test));
  REQUIRE_NOTHROW(throw_if_not_initialized());

  const GilLockGuard gil_guard;

  PythonState python_state{memory_resource};

  const auto dial_dict = PythonObject::import_module("clockwork.python.tests.support.clk_python_cog_clk_py_dial");
  const auto impl_dict = PythonObject::import_module("clockwork.python.tests.support.clk_python_cog_clk_py_impl");

  Tappy<TestConfig> config{};
  config.get_underlying_config_string().set_truncate("Config");
  const auto config_buffer = PythonObject::make_read_only_memory_view(&config, sizeof(config));
  const auto cog_configs_class = dial_dict.get_dictionary_item("PythonCogDialConfigs");
  const auto cog_configs = cog_configs_class.call_object(config_buffer);

  python_state.get_python_state_dictionary().set_dictionary_item(
    "python_state", PythonObject::make_string("PythonState"));
  Tappy<TestState> state{};
  state.get_underlying_state_string().set_truncate("State");
  const auto state_buffer = PythonObject::make_writable_memory_view(&state, sizeof(state));
  const auto cog_states_class = dial_dict.get_dictionary_item("PythonCogDialStates");
  const auto cog_states = cog_states_class.call_object(python_state.get_python_state_dictionary(), state_buffer);

  Tappy<TestInputMessage> input1_1{};
  input1_1.get_underlying_message_string().set_truncate("Test string 1_1");
  Tappy<TestInputMessage> input1_2{};
  input1_2.get_underlying_message_string().set_truncate("Test string 1_2");
  const auto input1_list = PythonObject::make_list(
    std::array{
      PythonObject::make_read_only_memory_view(&input1_1, sizeof(input1_1)),
      PythonObject::make_read_only_memory_view(&input1_2, sizeof(input1_2))});
  const auto cog_input1_class = dial_dict.get_dictionary_item("PythonCogDialInputsInput1");
  const auto cog_input1 = cog_input1_class.call_object(input1_list, PythonObject::make_integer(1));

  Tappy<TestInputMessage> input2_1{};
  input2_1.get_underlying_message_string().set_truncate("Test string 2_1");
  Tappy<TestInputMessage> input2_2{};
  input2_2.get_underlying_message_string().set_truncate("Test string 2_2");
  const auto input2_list = PythonObject::make_list(
    std::array{
      PythonObject::make_read_only_memory_view(&input2_1, sizeof(input2_1)),
      PythonObject::make_read_only_memory_view(&input2_2, sizeof(input2_2))});
  const auto cog_input2_class = dial_dict.get_dictionary_item("PythonCogDialInputsInput2");
  const auto cog_input2 = cog_input2_class.call_object(input2_list, PythonObject::make_integer(2));

  const auto cog_inputs_class = dial_dict.get_dictionary_item("PythonCogDialInputs");
  const auto cog_inputs = cog_inputs_class.call_object(cog_input1, cog_input2);

  Tappy<TestOutputMessage> output1{};
  const auto output1_buffer = PythonObject::make_writable_memory_view(&output1, sizeof(output1));
  const auto cog_output1_class = dial_dict.get_dictionary_item("PythonCogDialOutputsOutput1");
  const auto cog_output1 = cog_output1_class.call_object(output1_buffer);

  Tappy<TestOutputMessage> output2{};
  const auto output2_buffer = PythonObject::make_writable_memory_view(&output2, sizeof(output2));
  const auto cog_output2_class = dial_dict.get_dictionary_item("PythonCogDialOutputsOutput2");
  const auto cog_output2 = cog_output2_class.call_object(output2_buffer);

  const auto cog_outputs_class = dial_dict.get_dictionary_item("PythonCogDialOutputs");
  const auto cog_outputs = cog_outputs_class.call_object(cog_output1, cog_output2);

  const auto start_time_obj = PythonObject::make_integer(42'000'000'000);

  const auto cog_dial_class = dial_dict.get_dictionary_item("PythonCogDial");
  const auto cog_dial = cog_dial_class.call_object(start_time_obj, cog_configs, cog_states, cog_inputs, cog_outputs);

  const auto cog_impl_class = impl_dict.get_dictionary_item("PythonCogImpl");
  REQUIRE(cog_impl_class.call_method("execute_cog", cog_dial));
  REQUIRE(cog_states.call_method("finalize"));

  REQUIRE(state.get_state_string() == "PythonState");
  REQUIRE(python_state.get_python_state_dictionary().get_dictionary_item("python_state").get_string() == "Config");

  REQUIRE(cog_output1.get_attribute("is_published").is_true());
  REQUIRE(cog_output2.get_attribute("is_published").is_true());

  REQUIRE(output1.get_message_strings().size() == 4U);
  REQUIRE(output1.get_time_of_validity().time_since_epoch().count() == 42'000'000'000);
  REQUIRE(output1.get_message_strings()[0U] == std::string_view{"Test string 1_1"});
  REQUIRE(output1.get_message_strings()[1U] == std::string_view{"Test string 1_2"});
  REQUIRE(output1.get_message_strings()[2U] == std::string_view{"Test string 2_1"});
  REQUIRE(output1.get_message_strings()[3U] == std::string_view{"Test string 2_2"});

  REQUIRE(output2.get_message_strings().size() == 1U);
  REQUIRE(output2.get_time_of_validity().time_since_epoch().count() == 42'000'000'001);
  REQUIRE(output2.get_message_strings()[0U] == std::string_view{"Test string 1_2"});
}

} // namespace
} // namespace clockwork::python::tests
