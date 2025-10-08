# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""PythonCogDial-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import chain
from typing import TYPE_CHECKING, Final

from clockwork.dsl import cst
from clockwork.dsl.bazel.targets import Label, get_bazel_label_for_python_type
from clockwork.dsl.cog.cppcog import to_camel, to_dial_name
from clockwork.dsl.ir import cog, expr, node, primitive
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.python import py_context

if TYPE_CHECKING:
    from collections.abc import Iterable

    from clockwork.dsl import cst

PYTHON_STATE_NAME: Final = "python_state"

_CONFIGS_CLASS_SUFFIX = "Configs"
_STATES_CLASS_SUFFIX = "States"
_INPUTS_CLASS_SUFFIX = "Inputs"
_OUTPUTS_CLASS_SUFFIX = "Outputs"
_SLOT_CLASS_SUFFIX = "Slot"


def get_import_for_type(python_type: str) -> str:
    """Get the import line for a python type.

    Arguments:
        python_type: Fully qualified python type.

    Returns:
        Import statement for the python type.
    """
    module_parts = python_type.split(".")
    if len(module_parts) == 1:
        return ""
    return f"import {'.'.join(module_parts[:-1])}"


def get_configs_class_name(dial_class_name: str) -> str:
    """Get the configs class name for a cog dial.

    Arguments:
        dial_class_name: Cog dial class name.

    Returns:
        Configs class name.
    """
    return dial_class_name + _CONFIGS_CLASS_SUFFIX


def get_states_class_name(dial_class_name: str) -> str:
    """Get the states class name for a cog dial.

    Arguments:
        dial_class_name: Cog dial class name.

    Returns:
        States class name.
    """
    return dial_class_name + _STATES_CLASS_SUFFIX


def get_input_class_name(dial_class_name: str, input_name: str) -> str:
    """Get the input class name for a cog input.

    Arguments:
        dial_class_name: Cog dial class name.
        input_name: Cog input name.

    Returns:
        Input class name.
    """
    return dial_class_name + _INPUTS_CLASS_SUFFIX + to_camel(input_name)


def get_inputs_class_name(dial_class_name: str) -> str:
    """Get the inputs class name for a cog dial.

    Arguments:
        dial_class_name: Cog dial class name.

    Returns:
        Inputs class name.
    """
    return dial_class_name + _INPUTS_CLASS_SUFFIX


def get_output_class_name(dial_class_name: str, output_name: str) -> str:
    """Get the output class name for a cog output.

    Arguments:
        dial_class_name: Cog dial class name.
        output_name: Cog output name.

    Returns:
        Output class name.
    """
    return dial_class_name + _OUTPUTS_CLASS_SUFFIX + to_camel(output_name)


def get_outputs_class_name(dial_class_name: str) -> str:
    """Get the outputs class name for a cog dial.

    Arguments:
        dial_class_name: Cog dial class name.

    Returns:
        Outputs class name.
    """
    return dial_class_name + _OUTPUTS_CLASS_SUFFIX


@dataclass
class PythonDialType:
    """Represents the python type for config, state, input, or output."""

    cst_node: (
        cst.PyPythonCogConfigType | cst.PyPythonCogStateType | cst.PyPythonCogInputType | cst.PyPythonCogOutputType
    )
    python_type: expr.Expr | str

    def get_bazel_label_for_type(self) -> Label | None:
        """Get the bazel label for the python type.

        Returns:
            Bazel label or None if the type is not qualified.
        """
        assert isinstance(self.python_type, str)
        return get_bazel_label_for_python_type(self.python_type)


@dataclass
class PythonCogDial:
    """Instantiates a python cog dial inside a py_target."""

    cst_node: cst.PyPythonCogDial
    module: node.Module
    cog_ir: cog.Cog | expr.Expr
    dial_class_name: str | None
    config_types: dict[str, PythonDialType]
    state_types: dict[str, PythonDialType]
    input_types: dict[str, PythonDialType]
    output_types: dict[str, PythonDialType]
    mutable_states: set[str]

    @classmethod
    def _dial_types_from_cst(
        cls: type[PythonCogDial],
        cst_node_children: Iterable[
            cst.PyPythonCogConfigType | cst.PyPythonCogStateType | cst.PyPythonCogInputType | cst.PyPythonCogOutputType
        ],
        module: node.Module,
        member_type: str,
    ) -> dict[str, PythonDialType]:
        dial_types: dict[str, PythonDialType] = {}
        for dial_type_cst in cst_node_children:
            assert module.terminals
            member_name = get_span(dial_type_cst.child_name().child_value(), module.terminals)
            if member_name in dial_types:
                msg = node.append_error_line(dial_type_cst, module, f"Duplicate {member_type} name '{member_name}'")
                raise ValueError(msg)
            type_value = expr.Expr.from_cst(dial_type_cst.child_python_type(), module)
            dial_types[member_name] = PythonDialType(cst_node=dial_type_cst, python_type=type_value)
        return dial_types

    @classmethod
    def from_cst(cls: type[PythonCogDial], cst_node: cst.PyPythonCogDial, module: node.Module) -> PythonCogDial:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        dial_block_cst = cst_node.maybe_py_python_cog_dial_block()
        assert dial_block_cst
        config_types = cls._dial_types_from_cst(dial_block_cst.children_py_python_cog_config_type(), module, "config")
        state_types = cls._dial_types_from_cst(dial_block_cst.children_py_python_cog_state_type(), module, "state")
        input_types = cls._dial_types_from_cst(dial_block_cst.children_py_python_cog_input_type(), module, "input")
        output_types = cls._dial_types_from_cst(dial_block_cst.children_py_python_cog_output_type(), module, "output")

        return cls(
            cst_node=cst_node,
            module=module,
            cog_ir=typespec,
            dial_class_name=None,
            config_types=config_types,
            state_types=state_types,
            input_types=input_types,
            output_types=output_types,
            mutable_states=set(),
        )

    def _resolve_dial_types(
        self, member_type: str, dial_types: dict[str, PythonDialType], cog_members: set[str]
    ) -> None:
        dial_keys = set(dial_types.keys())
        if not cog_members:
            if dial_keys:
                msg = node.append_error_line(
                    self.cst_node, self.module, f"Mismatch in type definitions for {member_type}: {dial_keys}"
                )
                raise ValueError(msg)
            return

        if not dial_keys:
            if cog_members:
                msg = node.append_error_line(
                    self.cst_node, self.module, f"Mismatch in type definitions for {member_type}: {cog_members}"
                )
                raise ValueError(msg)
            return

        if dial_keys != cog_members:
            msg = node.append_error_line(
                self.cst_node, self.module, f"Mismatch in type definitions for {member_type}: {dial_keys ^ cog_members}"
            )
            raise ValueError(msg)

        for py_type in dial_types.values():
            if isinstance(py_type.python_type, expr.Expr):
                type_val = py_type.python_type.evaluate()
                if not isinstance(type_val, primitive.StringValue):
                    msg = node.append_error_line(py_type.cst_node, self.module, "Invalid python type definition")
                    raise TypeError(msg)
                py_type.python_type = type_val.value

    def resolve(self) -> None:
        """Perform IR finalization."""
        if not isinstance(self.cog_ir, expr.Expr):
            msg = f"Attempt to resolve twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.cog_ir.evaluate()
        if not isinstance(typespec, cog.Cog):
            msg = self.cog_ir.append_error_line(f"Expected Cog, got {type(typespec)}")
            raise TypeError(msg)
        self.cog_ir = typespec
        self.dial_class_name = to_dial_name(self.cog_ir.name)
        self.cog_ir = typespec
        self._resolve_dial_types("configs", self.config_types, set(self.cog_ir.configs.keys()))
        if PYTHON_STATE_NAME not in self.cog_ir.states:
            msg = self.cog_ir.append_error_line(f"Missing mandatory {PYTHON_STATE_NAME} in cog states")
            raise ValueError(msg)
        for state_key, state_def in self.cog_ir.states.items():
            assert state_def.resolved
            if state_key == PYTHON_STATE_NAME:
                if not state_def.resolved.params.mutable:
                    msg = self.cog_ir.append_error_line(f"{PYTHON_STATE_NAME} is not mutable")
                    raise ValueError(msg)
            elif state_def.resolved.params.mutable:
                self.mutable_states.add(state_key)
        state_keys = set(self.cog_ir.states.keys())
        state_keys.discard(PYTHON_STATE_NAME)
        self._resolve_dial_types("states", self.state_types, state_keys)
        self._resolve_dial_types("inputs", self.input_types, set(self.cog_ir.inputs.keys()))
        self._resolve_dial_types("outputs", self.output_types, set(self.cog_ir.outputs.keys()))

    def _render_configs(self) -> py_context.PythonChunks:
        """Render the python dial configs definition.

        Returns:
            Python chunks with the configs definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("from dataclasses import dataclass")
        assert self.dial_class_name
        python_chunks.impl.append(f"""
@dataclass
class {get_configs_class_name(self.dial_class_name)}:
""")
        assert isinstance(self.cog_ir, cog.Cog)
        for config_name in self.cog_ir.configs:
            dial_type = self.config_types[config_name]
            assert isinstance(dial_type.python_type, str)
            python_chunks.imports.add(get_import_for_type(dial_type.python_type))
            python_chunks.impl.append(f"    {config_name}: {dial_type.python_type}")
        python_chunks.impl.append("""
    def __init__(
        self,""")
        for config_name in self.config_types:
            python_chunks.impl.append(f"        {config_name}: memoryview,")
        python_chunks.impl.append("""    ) -> None:""")
        if not self.config_types:
            python_chunks.impl.append("        pass")
        for config_name in self.cog_ir.configs:
            dial_type = self.config_types[config_name]
            assert isinstance(dial_type.python_type, str)
            python_chunks.impl.append(
                f"        self.{config_name} = {dial_type.python_type}.deserialize_tachyon({config_name})"
            )
        return python_chunks

    def _render_states_members(self) -> py_context.PythonChunks:
        """Render the python dial states member definitions.

        Returns:
            Python chunks with the states member definitions.
        """
        python_chunks = py_context.PythonChunks()
        assert self.dial_class_name
        python_chunks.impl.append(f"""
@dataclass
class {get_states_class_name(self.dial_class_name)}:
""")
        assert isinstance(self.cog_ir, cog.Cog)
        for state_name in self.cog_ir.states:
            if state_name == PYTHON_STATE_NAME:
                python_chunks.impl.append(f"    {PYTHON_STATE_NAME}: dict[str, typing.Any]")
            else:
                dial_type = self.state_types[state_name]
                assert isinstance(dial_type.python_type, str)
                python_chunks.imports.add(get_import_for_type(dial_type.python_type))
                python_chunks.impl.append(f"    {state_name}: {dial_type.python_type}")
                if state_name in self.mutable_states:
                    python_chunks.impl.append(f"    _{state_name}_buffer: memoryview")
        return python_chunks

    def _render_states_methods(self) -> py_context.PythonChunks:
        """Render the python dial states method definitions.

        Returns:
            Python chunks with the states method definitions.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.impl.append("""
    def __init__(
        self,""")
        assert isinstance(self.cog_ir, cog.Cog)
        for state_name in self.cog_ir.states:
            if state_name == PYTHON_STATE_NAME:
                python_chunks.impl.append(f"        {PYTHON_STATE_NAME}: dict[str, typing.Any],")
            else:
                dial_type = self.state_types[state_name]
                assert isinstance(dial_type.python_type, str)
                python_chunks.impl.append(f"        {state_name}: memoryview,")
        python_chunks.impl.append("    ) -> None:")
        for state_name in self.cog_ir.states:
            if state_name == PYTHON_STATE_NAME:
                python_chunks.impl.append(f"        self.{PYTHON_STATE_NAME} = {PYTHON_STATE_NAME}")
            else:
                dial_type = self.state_types[state_name]
                assert isinstance(dial_type.python_type, str)
                python_chunks.impl.append(
                    f"        self.{state_name} = {dial_type.python_type}.deserialize_tachyon({state_name})"
                )
                if state_name in self.mutable_states:
                    python_chunks.impl.append(f"        self._{state_name}_buffer = {state_name}")
        python_chunks.impl.append("""
    def finalize(self) -> None:""")
        if not self.mutable_states:
            python_chunks.impl.append("        pass")
        else:
            for state_name, dial_type in self.state_types.items():
                if state_name in self.mutable_states:
                    assert isinstance(dial_type.python_type, str)
                    python_chunks.impl.append(f"        self.{state_name}.serialize_tachyon(self._{state_name}_buffer)")
        return python_chunks

    def _render_states(self) -> py_context.PythonChunks:
        """Render the python dial states definition.

        Returns:
            Python chunks with the states definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("import typing")
        python_chunks.system_imports.add("from dataclasses import dataclass")
        python_chunks.append(self._render_states_members())
        python_chunks.append(self._render_states_methods())
        return python_chunks

    def _get_input_slot_class_name(self, input_name: str) -> str:
        """Get the input slot class name for a cog input.

        Arguments:
            input_name: Cog input name.

        Returns:
            Input slot class name.
        """
        assert self.dial_class_name
        return self.dial_class_name + _INPUTS_CLASS_SUFFIX + to_camel(input_name) + _SLOT_CLASS_SUFFIX

    def _render_input_slot_class(self, input_name: str, input_type: str) -> py_context.PythonChunks:
        """Render the definition for an input slot class.

        Arguments:
            input_name: Input name.
            input_type: Input python type.

        Returns:
            Python chunks with the input slot class definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("import dataclasses")
        python_chunks.system_imports.add("import functools")
        python_chunks.imports.add(get_import_for_type(input_type))
        slot_class_name = self._get_input_slot_class_name(input_name)
        python_chunks.impl.append(f"""@dataclass
class {slot_class_name}:

    buffer: memoryview

    @functools.cached_property
    def message(self) -> {input_type}:
        return {input_type}.deserialize_tachyon(self.buffer)""")
        return python_chunks

    def _render_input(self, input_name: str, input_type: str) -> py_context.PythonChunks:
        """Render the definition of an input class.

        Arguments:
            input_name: Input name.
            input_type: Input python type.

        Returns:
            Python chunks with the input class definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.append(self._render_input_slot_class(input_name, input_type))
        input_slot_class_name = self._get_input_slot_class_name(input_name)
        assert self.dial_class_name
        input_class_name = get_input_class_name(self.dial_class_name, input_name)
        python_chunks.impl.append(f"""
class {input_class_name}:

    def __init__(
        self,
        buffers: list[memoryview],
        new_msg_index: int,
    ) -> None:
        self._message_slots = [{input_slot_class_name}(buffer) for buffer in buffers]
        self._new_msg_index = new_msg_index

    @property
    def view(self) -> list[{input_slot_class_name}]:
        return self._message_slots

    @property
    def new_msgs_view(self) -> list[{input_slot_class_name}]:
        return self._message_slots[self._new_msg_index:] if self._new_msg_index < len(self._message_slots) else []""")
        return python_chunks

    def _render_inputs(self) -> py_context.PythonChunks:
        """Render the python dial inputs definition.

        Returns:
            Python chunks with the inputs definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("from dataclasses import dataclass")
        assert isinstance(self.cog_ir, cog.Cog)
        for input_name in self.cog_ir.inputs:
            dial_type = self.input_types[input_name]
            assert isinstance(dial_type.python_type, str)
            python_chunks.append(self._render_input(input_name, dial_type.python_type))
        assert self.dial_class_name
        python_chunks.impl.append(f"""
@dataclass
class {get_inputs_class_name(self.dial_class_name)}:
""")
        if not self.input_types:
            python_chunks.impl.append("    pass")
        for input_name in self.cog_ir.inputs:
            assert self.dial_class_name
            python_chunks.impl.append(f"    {input_name}: {get_input_class_name(self.dial_class_name, input_name)}")

        return python_chunks

    def _render_output(self, output_name: str, output_type: str) -> py_context.PythonChunks:
        """Render the definition of an output class.

        Arguments:
            output_name: Input name.
            output_type: Input python type.

        Returns:
            Python chunks with the output class definition.
        """
        python_chunks = py_context.PythonChunks()
        assert self.dial_class_name
        output_class_name = get_output_class_name(self.dial_class_name, output_name)
        python_chunks.imports.add(get_import_for_type(output_type))
        python_chunks.impl.append(f"""class {output_class_name}:

    def __init__(
        self,
        buffer: memoryview,
    ) -> None:
        self._buffer = buffer
        self._is_published = False

    def publish(self, msg: {output_type}) -> None:
        msg.serialize_tachyon(self._buffer)
        self._is_published = True

    @property
    def is_published(self) -> bool:
        return self._is_published""")
        return python_chunks

    def _render_outputs(self) -> py_context.PythonChunks:
        """Render the python dial outputs definition.

        Returns:
            Python chunks with the outputs definition.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("from dataclasses import dataclass")
        assert isinstance(self.cog_ir, cog.Cog)
        for output_name in self.cog_ir.outputs:
            dial_type = self.output_types[output_name]
            assert isinstance(dial_type.python_type, str)
            python_chunks.append(self._render_output(output_name, dial_type.python_type))
        assert self.dial_class_name
        python_chunks.impl.append(f"""
@dataclass
class {get_outputs_class_name(self.dial_class_name)}:
""")
        if not self.output_types:
            python_chunks.impl.append("    pass")
        for output_name in self.cog_ir.outputs:
            assert self.dial_class_name
            python_chunks.impl.append(f"    {output_name}: {get_output_class_name(self.dial_class_name, output_name)}")

        return python_chunks

    def render(self) -> py_context.PythonChunks:
        """Render the python dial to python source.

        Returns:
            Python chunks with the dial implementation.
        """
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("from dataclasses import dataclass")
        python_chunks.append(self._render_configs())
        python_chunks.append(self._render_states())
        python_chunks.append(self._render_inputs())
        python_chunks.append(self._render_outputs())
        assert self.dial_class_name
        python_chunks.impl.append(f"""
@dataclass
class {self.dial_class_name}:

    start_time: int
    configs: {get_configs_class_name(self.dial_class_name)}
    states: {get_states_class_name(self.dial_class_name)}
    inputs: {get_inputs_class_name(self.dial_class_name)}
    outputs: {get_outputs_class_name(self.dial_class_name)}""")

        return python_chunks

    def get_bazel_targets(self) -> list[Label]:
        """Get the bazel labels needed for the dependencies from this dial.

        Returns:
            List of dependency labels.
        """
        bazel_targets: list[Label] = []
        for py_type in chain(
            self.config_types.values(), self.state_types.values(), self.input_types.values(), self.output_types.values()
        ):
            label = py_type.get_bazel_label_for_type()
            if label:
                bazel_targets.append(label)
        return bazel_targets
