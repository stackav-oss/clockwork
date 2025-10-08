# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp_python_cog."""

from pathlib import Path
from typing import Final

from clockwork.dsl.cog import cpp_python_cog
from clockwork.dsl.cpp import context
from clockwork.dsl.ir import cog, compiler, importer
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_python_cog_render() -> None:
    target_header_str: Final = """
#include "clockwork/python/tests/support/python_cog_dial.hh"
void execute_cog(PythonCogDial& dial);
"""

    target_source_str: Final = """
#include "clockwork/python/gil_lock_guard.hh"
#include "clockwork/python/python_init.hh"
#include "clockwork/python/python_object.hh"
#include "clockwork/python/tests/support/python_cog_dial.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include <exception>
#include <iterator>
#include <vector>
auto execute_cog(PythonCogDial& dial) -> void
{
    try
    {
        ::clockwork::python::throw_if_not_initialized();
        const ::clockwork::python::GilLockGuard gil_guard;
        const auto dial_dict =
            ::clockwork::python::PythonObject::import_module(
                "clockwork.python.tests.support.py_python_cog_dial");
        const auto impl_dict =
            ::clockwork::python::PythonObject::import_module(
                "clockwork.python.tests.support.py_python_cog_impl");
        const auto& configs_config = dial.get_configs().get_config();
        const auto configs_config_buffer =
            ::clockwork::python::PythonObject::make_read_only_memory_view(
                &configs_config, sizeof(configs_config));
        const auto configs_class = dial_dict.get_dictionary_item("PythonCogDialConfigs");
        const auto configs_obj = configs_class.call_object(configs_config_buffer);
        auto& states_state =
            dial.get_states().get_state();
        const auto states_state_buffer =
            ::clockwork::python::PythonObject::make_writable_memory_view(
                &states_state, sizeof(states_state));
        const auto states_class = dial_dict.get_dictionary_item("PythonCogDialStates");
        const auto states_obj = states_class.call_object(dial.get_states().get_python_state().get_python_state_dictionary(), states_state_buffer);
        const auto& inputs_input1 = dial.get_inputs().get_input1();
        const auto& inputs_input1_view = inputs_input1.get_view();
        std::vector<::clockwork::python::PythonObject> inputs_input1_buffers;
        inputs_input1_buffers.reserve(inputs_input1_view.size());
        for (const auto& msg : inputs_input1_view)
        {
            inputs_input1_buffers.emplace_back(
                ::clockwork::python::PythonObject::make_read_only_memory_view(&msg, sizeof(msg)));
        }
        const auto inputs_input1_buffers_obj =
            ::clockwork::python::PythonObject::make_list(inputs_input1_buffers);
        const auto inputs_input1_new_index =
            std::distance(inputs_input1_view.begin(), inputs_input1.get_first_new());
        const auto inputs_input1_class = dial_dict.get_dictionary_item("PythonCogDialInputsInput1");
        const auto inputs_input1_obj = inputs_input1_class.call_object(
            inputs_input1_buffers_obj,
            ::clockwork::python::PythonObject::make_integer(inputs_input1_new_index));
        const auto& inputs_input2 = dial.get_inputs().get_input2();
        const auto& inputs_input2_view = inputs_input2.get_view();
        std::vector<::clockwork::python::PythonObject> inputs_input2_buffers;
        inputs_input2_buffers.reserve(inputs_input2_view.size());
        for (const auto& msg : inputs_input2_view)
        {
            inputs_input2_buffers.emplace_back(
                ::clockwork::python::PythonObject::make_read_only_memory_view(&msg, sizeof(msg)));
        }
        const auto inputs_input2_buffers_obj =
            ::clockwork::python::PythonObject::make_list(inputs_input2_buffers);
        const auto inputs_input2_new_index =
            std::distance(inputs_input2_view.begin(), inputs_input2.get_first_new());
        const auto inputs_input2_class = dial_dict.get_dictionary_item("PythonCogDialInputsInput2");
        const auto inputs_input2_obj = inputs_input2_class.call_object(
            inputs_input2_buffers_obj,
            ::clockwork::python::PythonObject::make_integer(inputs_input2_new_index));
        const auto inputs_class = dial_dict.get_dictionary_item("PythonCogDialInputs");
        const auto inputs_obj = inputs_class.call_object(inputs_input1_obj, inputs_input2_obj);
        auto& outputs_output1 = dial.get_outputs().get_output1();
        const auto outputs_output1_buffer =
            ::clockwork::python::PythonObject::make_writable_memory_view(
                &outputs_output1.message(), sizeof(outputs_output1.message()));
        const auto outputs_output1_class = dial_dict.get_dictionary_item("PythonCogDialOutputsOutput1");
        const auto outputs_output1_obj = outputs_output1_class.call_object(
            outputs_output1_buffer);
        auto& outputs_output2 = dial.get_outputs().get_output2();
        const auto outputs_output2_buffer =
            ::clockwork::python::PythonObject::make_writable_memory_view(
                &outputs_output2.message(), sizeof(outputs_output2.message()));
        const auto outputs_output2_class = dial_dict.get_dictionary_item("PythonCogDialOutputsOutput2");
        const auto outputs_output2_obj = outputs_output2_class.call_object(
            outputs_output2_buffer);
        const auto outputs_class = dial_dict.get_dictionary_item("PythonCogDialOutputs");
        const auto outputs_obj = outputs_class.call_object(outputs_output1_obj, outputs_output2_obj);
        const auto dial_class = dial_dict.get_dictionary_item("PythonCogDial");
        if (!dial_class)
        {
            throw std::runtime_error("Dial class clockwork.python.tests.support.py_python_cog_dial.PythonCogDial not found");
        }
        const auto start_time_obj = ::clockwork::python::PythonObject::make_integer(dial.get_start_time().time_since_epoch().count());
        const auto dial_obj = dial_class.call_object(start_time_obj, configs_obj, states_obj, inputs_obj, outputs_obj);
        const auto impl_class = impl_dict.get_dictionary_item("PythonCogImpl");
        if (!impl_class)
        {
            throw std::runtime_error("Impl class clockwork.python.tests.support.py_python_cog_impl.PythonCogImpl not found");
        }
        impl_class.call_method("execute_cog", dial_obj);
        states_obj.call_method("finalize");
        if (outputs_output1_obj.get_attribute("is_published").is_true())
        {
            outputs_output1.mark_for_publish();
        }
        if (outputs_output2_obj.get_attribute("is_published").is_true())
        {
            outputs_output2.mark_for_publish();
        }
    }
    catch (const std::exception& exc)
    {
        ::jewels::log_cerr_error("Caught exception from PythonCogDial python cog: {}", exc.what());
        throw;
    }
}
"""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/python/tests/support/python_cog.clk")),
        importer=importer.FilesystemImporter(compile_fn=compiler.compile_source_file),
    )
    cog_ir = module.inner_scope.lookup("PythonCog")
    assert cog_ir is not None
    assert isinstance(cog_ir, cog.Cog)
    dial_header = context.Header(CLK_REPO, "clockwork/python/tests/support/python_cog_dial.hh")
    py_cog = cpp_python_cog.CppPythonCog(
        cog_ir=cog_ir,
        dial_class_name="PythonCogDial",
        python_dial_class_name="clockwork.python.tests.support.py_python_cog_dial.PythonCogDial",
        python_impl_class_name="clockwork.python.tests.support.py_python_cog_impl.PythonCogImpl",
        cpp_namespace="clockwork::python::tests",
        dial_header=dial_header,
    )
    cpp_mod = py_cog.render()
    assert cpp_mod.header_chunk.render_str(render_includes=True).strip() == target_header_str.strip()
    assert cpp_mod.implementation_chunk.render_str(render_includes=True).strip() == target_source_str.strip()
