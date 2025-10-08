# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ Dial structs."""

from dataclasses import dataclass

from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    VOID,
    CppMethod,
    CppNamedType,
    CppType,
    Ref,
    ref_qualify,
)
from clockwork.dsl.ir import (
    cog,
    python_cog_dial,
)
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO


def get_python_module_name(py_type: str) -> str:
    """Get the python module name for a python type.

    Arguments:
        py_type: Fully qualified python type.

    Returns:
        Fully qualified python module name.
    """
    module_parts = py_type.split(".")
    minimum_module_parts = 2
    if len(module_parts) < minimum_module_parts:
        return py_type
    return ".".join(module_parts[:-1])


def get_python_class_name(py_type: str) -> str:
    """Get the python class name for a python type.

    Arguments:
        py_type: Fully qualified python type.

    Returns:
        Python class name.
    """
    module_parts = py_type.split(".")
    return module_parts[-1]


@dataclass
class CppPythonCog:
    """Representation of the C++ implementation of a python cog."""

    cog_ir: cog.Cog
    dial_class_name: str
    python_dial_class_name: str
    python_impl_class_name: str
    cpp_namespace: str | None
    dial_header: Header

    def _render_initialize(self) -> CppChunk:
        """Render the C++ code to initialize for calling the python implementation.

        Returns:
            CppChunk containing the initialization code.
        """
        cpp_chunk = CppChunk()
        cpp_chunk.append(
            [
                "::clockwork::python::throw_if_not_initialized();",
                "const ::clockwork::python::GilLockGuard gil_guard;",
                "const auto dial_dict =",
                "    ::clockwork::python::PythonObject::import_module(",
                f'        "{get_python_module_name(self.python_dial_class_name)}");',
                "const auto impl_dict =",
                "    ::clockwork::python::PythonObject::import_module(",
                f'        "{get_python_module_name(self.python_impl_class_name)}");',
            ]
        )
        return cpp_chunk

    def _render_configs(self) -> CppChunk:
        """Render the C++ code to create the dial configuration instances.

        Returns:
            CppChunks for the dial configs.
        """
        cpp_chunk = CppChunk()
        configs_args: list[str] = []
        for config_name in self.cog_ir.configs:
            configs_args.append(f"configs_{config_name}_buffer")
            cpp_chunk.append(
                [
                    f"const auto& configs_{config_name} = dial.get_configs().get_{config_name}();",
                    f"const auto configs_{config_name}_buffer =",
                    "    ::clockwork::python::PythonObject::make_read_only_memory_view(",
                    f"        &configs_{config_name}, sizeof(configs_{config_name}));",
                ]
            )
        configs_class_name = python_cog_dial.get_configs_class_name(self.dial_class_name)
        cpp_chunk.append(
            [
                f'const auto configs_class = dial_dict.get_dictionary_item("{configs_class_name}");',
                f"const auto configs_obj = configs_class.call_object({', '.join(configs_args)});",
            ]
        )
        return cpp_chunk

    def _render_states(self) -> CppChunk:
        """Render the C++ code to create the dial state instances.

        Returns:
            CppChunks for the dial states.
        """
        cpp_chunk = CppChunk()
        states_args: list[str] = []
        for state_name, state_def in self.cog_ir.states.items():
            if state_name == python_cog_dial.PYTHON_STATE_NAME:
                states_args.append(
                    f"dial.get_states().get_{python_cog_dial.PYTHON_STATE_NAME}().get_python_state_dictionary()"
                )
            else:
                states_args.append(f"states_{state_name}_buffer")
                if state_def.params.mutable:
                    cpp_chunk.append(f"auto& states_{state_name} =")
                else:
                    cpp_chunk.append(f"const auto& states_{state_name} =")
                cpp_chunk.append(f"    dial.get_states().get_{state_name}();")
                cpp_chunk.append(f"const auto states_{state_name}_buffer =")
                if state_def.params.mutable:
                    cpp_chunk.append("    ::clockwork::python::PythonObject::make_writable_memory_view(")
                else:
                    cpp_chunk.append("    ::clockwork::python::PythonObject::make_read_only_memory_view(")
                cpp_chunk.append(f"        &states_{state_name}, sizeof(states_{state_name}));")
        states_class_name = python_cog_dial.get_states_class_name(self.dial_class_name)
        cpp_chunk.append(
            [
                f'const auto states_class = dial_dict.get_dictionary_item("{states_class_name}");',
                f"const auto states_obj = states_class.call_object({', '.join(states_args)});",
            ]
        )
        return cpp_chunk

    def _render_inputs(self) -> CppChunk:
        """Render the C++ code to create the dial input instances.

        Returns:
            CppChunks for the dial inputs.
        """
        cpp_chunk = CppChunk()
        inputs_args: list[str] = []
        for input_name in self.cog_ir.inputs:
            input_class_name = python_cog_dial.get_input_class_name(self.dial_class_name, input_name)
            inputs_args.append(f"inputs_{input_name}_obj")
            cpp_chunk.append(
                [
                    f"const auto& inputs_{input_name} = dial.get_inputs().get_{input_name}();",
                    f"const auto& inputs_{input_name}_view = inputs_{input_name}.get_view();",
                    f"std::vector<::clockwork::python::PythonObject> inputs_{input_name}_buffers;",
                    f"inputs_{input_name}_buffers.reserve(inputs_{input_name}_view.size());",
                    f"for (const auto& msg : inputs_{input_name}_view)",
                    "{",
                    f"    inputs_{input_name}_buffers.emplace_back(",
                    "        ::clockwork::python::PythonObject::make_read_only_memory_view(&msg, sizeof(msg)));",
                    "}",
                    f"const auto inputs_{input_name}_buffers_obj =",
                    f"    ::clockwork::python::PythonObject::make_list(inputs_{input_name}_buffers);",
                    f"const auto inputs_{input_name}_new_index =",
                    f"    std::distance(inputs_{input_name}_view.begin(), inputs_{input_name}.get_first_new());",
                    f'const auto inputs_{input_name}_class = dial_dict.get_dictionary_item("{input_class_name}");',
                    f"const auto inputs_{input_name}_obj = inputs_{input_name}_class.call_object(",
                    f"    inputs_{input_name}_buffers_obj,",
                    f"    ::clockwork::python::PythonObject::make_integer(inputs_{input_name}_new_index));",
                ]
            )
        inputs_class_name = python_cog_dial.get_inputs_class_name(self.dial_class_name)
        cpp_chunk.append(
            [
                f'const auto inputs_class = dial_dict.get_dictionary_item("{inputs_class_name}");',
                f"const auto inputs_obj = inputs_class.call_object({', '.join(inputs_args)});",
            ]
        )
        return cpp_chunk

    def _render_outputs(self) -> CppChunk:
        """Render the C++ code to create the dial output instances.

        Returns:
            CppChunks for the dial outputs.
        """
        cpp_chunk = CppChunk()
        outputs_args: list[str] = []
        for output_name in self.cog_ir.outputs:
            output_class_name = python_cog_dial.get_output_class_name(self.dial_class_name, output_name)
            outputs_args.append(f"outputs_{output_name}_obj")
            cpp_chunk.append(
                [
                    f"auto& outputs_{output_name} = dial.get_outputs().get_{output_name}();",
                    f"const auto outputs_{output_name}_buffer =",
                    "    ::clockwork::python::PythonObject::make_writable_memory_view(",
                    f"        &outputs_{output_name}.message(), sizeof(outputs_{output_name}.message()));",
                    f'const auto outputs_{output_name}_class = dial_dict.get_dictionary_item("{output_class_name}");',
                    f"const auto outputs_{output_name}_obj = outputs_{output_name}_class.call_object(",
                    f"    outputs_{output_name}_buffer);",
                ]
            )
        outputs_class_name = python_cog_dial.get_outputs_class_name(self.dial_class_name)
        cpp_chunk.append(
            [
                f'const auto outputs_class = dial_dict.get_dictionary_item("{outputs_class_name}");',
                f"const auto outputs_obj = outputs_class.call_object({', '.join(outputs_args)});",
            ]
        )
        return cpp_chunk

    def _render_execute(self) -> CppChunk:
        """Render the C++ code to call the python dial and handle the outputs.

        Returns:
            CppChunks to execute the python cog.
        """
        cpp_chunk = CppChunk()
        cpp_chunk.append(
            [
                f'const auto dial_class = dial_dict.get_dictionary_item("{get_python_class_name(self.python_dial_class_name)}");',
                "if (!dial_class)",
                "{",
                f'    throw std::runtime_error("Dial class {self.python_dial_class_name} not found");',
                "}",
                "const auto start_time_obj = ::clockwork::python::PythonObject::make_integer(dial.get_start_time().time_since_epoch().count());",
                "const auto dial_obj = dial_class.call_object(start_time_obj, configs_obj, states_obj, inputs_obj, outputs_obj);",
                f'const auto impl_class = impl_dict.get_dictionary_item("{get_python_class_name(self.python_impl_class_name)}");',
                "if (!impl_class)",
                "{",
                f'    throw std::runtime_error("Impl class {self.python_impl_class_name} not found");',
                "}",
                'impl_class.call_method("execute_cog", dial_obj);',
                'states_obj.call_method("finalize");',
            ]
        )
        for output_name in self.cog_ir.outputs:
            cpp_chunk.append(
                [
                    f'if (outputs_{output_name}_obj.get_attribute("is_published").is_true())',
                    "{",
                    f"    outputs_{output_name}.mark_for_publish();",
                    "}",
                ]
            )
        return cpp_chunk

    def render(self) -> CppModuleChunks:
        """Render the C++ python cog implementation that calls the python implementation.

        Returns:
            CppModuleChunks for the generated C++ implementation.
        """
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.context.add_include(self.dial_header)
        cpp_mod.implementation_chunk.context.add_include(self.dial_header)
        cpp_mod.implementation_chunk.context.add_include(Header(JEWELS_REPO, "jewels/log_cerr/log_cerr.hh"))
        cpp_mod.implementation_chunk.context.add_include(Header(CLK_REPO, "clockwork/python/gil_lock_guard.hh"))
        cpp_mod.implementation_chunk.context.add_include(Header(CLK_REPO, "clockwork/python/python_init.hh"))
        cpp_mod.implementation_chunk.context.add_include(Header(CLK_REPO, "clockwork/python/python_object.hh"))
        cpp_mod.implementation_chunk.context.add_include(SystemHeader("iterator"))
        cpp_mod.implementation_chunk.context.add_include(SystemHeader("exception"))
        cpp_mod.implementation_chunk.context.add_include(SystemHeader("vector"))

        dial_type = CppType([], self.dial_class_name, None)
        dial_ref_type = ref_qualify(dial_type, Ref.L)

        execute_cog_body = CppChunk()
        execute_cog_body.append(
            [
                "try",
                "{",
            ]
        )
        execute_cog_body.append(self._render_initialize(), indent=1)
        execute_cog_body.append(self._render_configs(), indent=1)
        execute_cog_body.append(self._render_states(), indent=1)
        execute_cog_body.append(self._render_inputs(), indent=1)
        execute_cog_body.append(self._render_outputs(), indent=1)
        execute_cog_body.append(self._render_execute(), indent=1)
        execute_cog_body.append(
            [
                "}",
                "catch (const std::exception& exc)",
                "{",
                f'    ::jewels::log_cerr_error("Caught exception from {self.dial_class_name} python cog: {{}}", exc.what());',
                "    throw;",
                "}",
            ]
        )

        enclosing_namespace = self.cpp_namespace if self.cpp_namespace else ""

        execute_cog = CppMethod(
            name="execute_cog",
            doc=None,
            return_type=VOID,
            arguments=[CppNamedType(argument_type=dial_ref_type, argument_name="dial")],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=execute_cog_body,
            no_discard=False,
            static=False,
        )
        cpp_mod.append(execute_cog.render(None, enclosing_namespace))

        return cpp_mod
