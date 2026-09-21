# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python representation of Cog components."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

from clockwork.dsl.ir import (
    report_group as report_group_module,
)
from clockwork.dsl.ir import (
    signal as signal_ir,
)
from clockwork.dsl.ir.cog_components import AnyMessagePresent

if TYPE_CHECKING:
    from collections import abc
    from decimal import Decimal
    from uuid import UUID

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.diagnostics import DiagnosticsDef, InfraDiagnosticsDef
    from clockwork.dsl.ir.report_group import ReportGroupDef, ReportGroupEntry

from clockwork.dsl.cpp import context, literal, typereg, types
from clockwork.dsl.ir import (
    aligner,
    clkbuiltins,
    cog,
    cog_components,
    diagnostics,
    expr,
    extern_type,
    primitive,
    schema,
    schema_reg,
    statement,
    typesys,
    units,
    uuid_reg,
)
from clockwork.dsl.ir.cog_components import Condition, DynamicTimer, NewMessagePresent, TimeSinceLastExec
from clockwork.dsl.ir.cog_parameters import CogParameterRef
from clockwork.dsl.ir.diagnostics import COG_INFRA_DIAGS_GROUP_DEF_NAME, COG_INFRA_DIAGS_GROUP_NAME
from clockwork.dsl.ir.module_id import CLK_REPO


@dataclass
class ConditionsStruct:
    """Represents the Conditions sub-struct of a C++ Dial.

    Attributes:
        condition_defs (dict): the cog.ConditionDefs, e.g. Condition IRs, compiled from in the DSL
        conditions (dict): Python object representation of the Condition IRs
    """

    condition_defs: dict[str, cog.ConditionDef]
    conditions: dict[str, ConditionBase]

    @classmethod
    def from_ir(
        cls: type[ConditionsStruct],
        compiler_context: CompilerContext,
        condition_defs: dict[str, cog.ConditionDef],
    ) -> ConditionsStruct:
        """Create ConditionsStruct representation from IR."""
        conditions: dict[str, ConditionBase] = {}
        for condition_def in condition_defs.values():
            name = cls.name_for(condition_def)
            cond = condition_def.condition
            if isinstance(cond, TimeSinceLastExec):
                conditions[name] = TimeSinceLastExecCondition.from_ir(compiler_context, name, condition_def)
            elif isinstance(cond, AnyMessagePresent):
                conditions[name] = AnyMessageCondition.from_ir(compiler_context, name, condition_def)
            elif isinstance(cond, NewMessagePresent):
                conditions[name] = NewMessageCondition.from_ir(compiler_context, name, condition_def)
            elif isinstance(cond, DynamicTimer):
                conditions[name] = DynamicTimerCondition.from_ir(compiler_context, name, condition_def)
            else:
                msg = f"Condition type for {name} is not supported."
                raise NotImplementedError(msg)
        return cls(condition_defs=condition_defs, conditions=conditions)

    @classmethod
    def name_for(cls: type[ConditionsStruct], condition_def: cog.ConditionDef) -> str:
        """Get the name of the Conditions struct member to use for a condition."""
        return condition_def.name


@dataclass
class ConditionBase:
    """Base class for Conditions."""

    identifier: str
    uuid: UUID
    cpp_type: types.CppType | types.CppTemplateType


@dataclass
class TimeSinceLastExecCondition(ConditionBase):
    """Representation of TimeSinceLastExec condition."""

    time_ns: Decimal

    @classmethod
    def from_ir(
        cls: type[TimeSinceLastExecCondition],
        compiler_context: CompilerContext,
        name: str,
        condition_def: cog.ConditionDef,
    ) -> TimeSinceLastExecCondition:
        """Create a class representation of time since last exec condition IR."""
        condition = condition_def.condition
        assert isinstance(condition, cog.TimeSinceLastExec)

        time_ns = condition.duration.as_unit(units.NANOSECONDS).value

        time_ns_value = primitive.DecimalValue(type_info=clkbuiltins.UINT64, value=time_ns)
        time_ns_literal = literal.decimal_value_to_cpp(time_ns_value)

        cpp_type = types.CppTemplateType(
            include=[context.Header(CLK_REPO, "clockwork/dial/cond_time_since_last_exec.hh")],
            template_name="TimeSinceLastExecCondition",
            cpp_namespace="clockwork",
            arguments=[time_ns_literal],
            const=True,
        )

        return cls(
            identifier=name,
            time_ns=time_ns,
            uuid=uuid_reg.lookup_uuid(compiler_context, condition_def),
            cpp_type=cpp_type,
        )


@dataclass
class DynamicTimerCondition(ConditionBase):
    """Representation of DynamicTimer condition (for aligner optional input timeouts)."""

    @classmethod
    def from_ir(
        cls: type[DynamicTimerCondition],
        compiler_context: CompilerContext,
        name: str,
        condition_def: cog.ConditionDef,
    ) -> DynamicTimerCondition:
        """Create a DynamicTimerCondition from the IR."""
        condition = condition_def.condition
        assert isinstance(condition, DynamicTimer)

        cpp_type = types.CppType(
            includes=[context.Header(CLK_REPO, "clockwork/dial/cond_dynamic_timer.hh")],
            type_name="DynamicTimerCondition",
            cpp_namespace="clockwork",
            const=True,
        )

        return cls(
            identifier=name,
            uuid=uuid_reg.lookup_uuid(compiler_context, condition_def),
            cpp_type=cpp_type,
        )


@dataclass
class MessagesPresentCondition(ConditionBase):
    """Representation of MessagesPresent condition."""

    input_name: str
    lower_bound: int
    upper_bound: int

    @classmethod
    def from_ir(
        cls: type[MessagesPresentCondition],
        compiler_context: CompilerContext,
        name: str,
        condition_def: cog.ConditionDef,
    ) -> MessagesPresentCondition:
        """Create a class representation of messages present condition IR."""
        condition = condition_def.condition
        assert isinstance(condition, cog.MessagesPresent)
        input_name = condition.input_name
        assert isinstance(condition.lower_bound, int)
        lower_bound = condition.lower_bound
        if condition.upper_bound is None:
            upper_bound = 2**clkbuiltins.UINT32.bit_width - 1
        else:
            assert isinstance(condition.upper_bound, int)
            upper_bound = condition.upper_bound

        lower_bound_literal = literal.int_to_cpp(lower_bound, clkbuiltins.UINT32)
        upper_bound_literal = literal.int_to_cpp(upper_bound, clkbuiltins.UINT32)

        cpp_type = types.CppTemplateType(
            include=[
                context.Header(CLK_REPO, "clockwork/dial/cond_messages_present.hh"),
                context.SystemHeader("cstdint"),
            ],
            template_name="MessagePresentCondition",
            cpp_namespace="clockwork",
            arguments=[lower_bound_literal, upper_bound_literal],
            const=True,
        )

        return cls(
            identifier=name,
            input_name=input_name,
            lower_bound=lower_bound,
            upper_bound=upper_bound,
            uuid=uuid_reg.lookup_uuid(compiler_context, condition_def),
            cpp_type=cpp_type,
        )

    def is_upper_bound_max(self) -> bool:
        """If upper bound is max value."""
        var: int = 2**clkbuiltins.UINT32.bit_width - 1
        return self.upper_bound == var


@dataclass
class AnyMessageCondition(MessagesPresentCondition):
    """A any_message condition."""


@dataclass
class NewMessageCondition(MessagesPresentCondition):
    """A new_message condition."""


@dataclass
class ResourcesStruct:
    """Represents the Resources sub-struct of a C++ Dial.

    Attributes:
        resource_defs (dict): the cog.Resources, e.g. Resource IRs, compiled from in the DSL
        resources (dict): Python object representation of the Resource IRs
    """

    resource_defs: dict[str, cog.ResourceDef]
    resources: dict[str, Resource]

    @classmethod
    def from_ir(
        cls: type[ResourcesStruct],
        compiler_context: CompilerContext,
        resource_defs: dict[str, cog.ResourceDef],
    ) -> ResourcesStruct:
        """Create ResourcesStruct representation from IR."""
        resources: dict[str, Resource] = {}
        for resource_def in resource_defs.values():
            resource = Resource.from_ir(compiler_context, resource_def)
            resources[resource.identifier] = resource
        return cls(resource_defs=resource_defs, resources=resources)


@dataclass
class Resource:
    """Represents Resource structure."""

    identifier: str
    cpp_type: types.CppType | types.CppTemplateType
    uuid: UUID

    @classmethod
    def from_ir(
        cls: type[Resource],
        compiler_context: CompilerContext,
        resource_def: cog.ResourceDef,
    ) -> Resource:
        """Create an Resource."""
        identifier = cls.name_for(resource_def)
        cpp_type = types.MEMORY_RESOURCE
        return cls(identifier=identifier, cpp_type=cpp_type, uuid=uuid_reg.lookup_uuid(compiler_context, resource_def))

    @classmethod
    def name_for(cls: type[Resource], resource_def: cog.ResourceDef) -> str:
        """Produce the member variable name for the given resource."""
        return resource_def.name


@dataclass
class ConfigsStruct:
    """Represents the Configs sub-struct of a C++ Dial.

    Attributes:
        config_defs (dict): the cog.Configs, e.g. Config IRs, compiled from in the DSL
        configs (dict): Python object representation of the Config IRs
    """

    config_defs: dict[str, cog.ConfigDef]
    configs: dict[str, Config]

    @classmethod
    def from_ir(
        cls: type[ConfigsStruct],
        compiler_context: CompilerContext,
        config_defs: dict[str, cog.ConfigDef],
    ) -> ConfigsStruct:
        """Create ConfigsStruct representation from IR."""
        configs: dict[str, Config] = {}
        for config_def in config_defs.values():
            config = Config.from_ir(compiler_context, config_def)
            configs[config.identifier] = config
        return cls(config_defs=config_defs, configs=configs)


@dataclass
class Config:
    """Represents Config structure."""

    identifier: str
    cpp_type: types.CppType | types.CppTemplateType
    uuid: UUID

    @classmethod
    def from_ir(
        cls: type[Config],
        compiler_context: CompilerContext,
        config_def: cog.ConfigDef,
    ) -> Config:
        """Create an Config."""
        msg_type = config_def.message_type
        if isinstance(msg_type, schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation):
            return cls._handle_schema_config(compiler_context, config_def)
        msg = f"Attempt to create an config for non-Interface type {type(msg_type)}: {msg_type}"
        raise NotImplementedError(msg)

    @classmethod
    def _handle_schema_config(
        cls: type[Config], compiler_context: CompilerContext, config_def: cog.ConfigDef
    ) -> Config:
        assert isinstance(config_def.message_type, schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation)
        identifier = cls.name_for(config_def)
        if isinstance(config_def.message_type, schema_reg.InterfaceInfo):
            cpp_type = typereg.get_cpp_type(config_def.module.context, config_def.message_type.interface_ir.typespec)
        else:
            cpp_type = typereg.get_cpp_type(config_def.module.context, config_def.message_type)
        cpp_type.const = True
        return cls(identifier=identifier, cpp_type=cpp_type, uuid=uuid_reg.lookup_uuid(compiler_context, config_def))

    @classmethod
    def name_for(cls: type[Config], config_def: cog.ConfigDef) -> str:
        """Produce the member variable name for the given input."""
        return config_def.name


@dataclass
class StatesStruct:
    """Represents the States sub-struct of a C++ Dial.

    Attributes:
        state_defs (dict): the cog.States, e.g. State IRs, compiled from in the DSL
        states (dict): Python object representation of the State IRs
    """

    state_defs: dict[str, cog.StateDef]
    states: dict[str, State]

    @classmethod
    def from_ir(
        cls: type[StatesStruct],
        compiler_context: CompilerContext,
        state_defs: dict[str, cog.StateDef],
    ) -> StatesStruct:
        """Create StatesStruct representation from IR."""
        states: dict[str, State] = {}
        for state_def in state_defs.values():
            state = State.from_ir(compiler_context, state_def)
            states[state.identifier] = state
        return cls(state_defs=state_defs, states=states)


@dataclass
class State:
    """Represents State structure."""

    identifier: str
    cpp_type: types.CppType | types.CppTemplateType
    msg_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation | extern_type.ExternType
    read_only: bool
    uuid: UUID

    @classmethod
    def from_ir(
        cls: type[State],
        compiler_context: CompilerContext,
        state_def: cog.StateDef,
    ) -> State:
        """Create an State."""
        # Sanity check
        if isinstance(state_def.message_type, expr.Expr) or isinstance(state_def.params.mutable, expr.Expr):
            msg = f"Attempt to generate dial state for unresolved schema: {state_def.message_type}"
            raise NotImplementedError(msg)

        msg_type = state_def.message_type
        read_only = not state_def.params.mutable
        identifier = cls.name_for(state_def)
        uuid = uuid_reg.lookup_uuid(compiler_context, state_def)
        if isinstance(msg_type, schema_reg.InterfaceInfo):
            if isinstance(msg_type.interface_ir.typespec, expr.Expr):
                msg = f"Attempt to generate dial state for unresolved schema: {state_def.message_type}"
                raise NotImplementedError(msg)
            cpp_type = typereg.get_cpp_type(state_def.module.context, msg_type.interface_ir.typespec)
        elif isinstance(msg_type, CogParameterRef | typesys.Instantiation):
            cpp_type = typereg.get_cpp_type(state_def.module.context, msg_type)
        else:
            cpp_type = typereg.get_cpp_type(state_def.module.context, msg_type)
        # NOTE: we copy the type here, otherwise const is set for everyone
        cpp_type = replace(cpp_type, const=read_only)
        return cls(
            identifier=identifier, msg_type=state_def.message_type, cpp_type=cpp_type, read_only=read_only, uuid=uuid
        )

    @classmethod
    def name_for(cls: type[State], state_def: cog.StateDef) -> str:
        """Produce the member variable name for the given input."""
        return state_def.name


@dataclass
class MinMessagesEvaluator:
    """Evaluate the minimum number of messages guaranteed to be present in the input view, based on execution conditions."""

    input_identifier: str
    execution_spec: cog.ExecutionSpec

    def get_min_messages(self) -> int:
        """Get the minimum number of messages guaranteed that the input will have when the cog executes."""
        return self._get_min_messages(self.execution_spec.condition, require_new_messages=False)

    def get_min_new_messages(self) -> int:
        """Get the minimum number of *new* messages guaranteed that the input will have when the cog executes."""
        return self._get_min_messages(self.execution_spec.condition, require_new_messages=True)

    def _get_min_messages(
        self,
        execution_condition_expr: cog.ConditionExpr,
        require_new_messages: bool,
    ) -> int:
        """Get the min_msgs value from the conditions if it exists."""
        if isinstance(execution_condition_expr, cog.InitConditionExpr):
            return 0
        if isinstance(execution_condition_expr, cog.LogConditionExpr):
            msg = f"LogConditionExpr not supported in _get_min_msgs: {execution_condition_expr}"
            raise NotImplementedError(msg)
        if isinstance(execution_condition_expr, cog.SimpleConditionExpr):
            if not isinstance(execution_condition_expr.condition, cog.ConditionDef):
                msg = f"{execution_condition_expr} may not have been resolved."
                raise TypeError(msg)
            condition: Condition = execution_condition_expr.condition.condition
            return self._get_simple_condition_min_messages(condition, require_new_messages)

        if isinstance(execution_condition_expr, cog.BinaryConditionExpr):
            return self._get_binary_condition_min_messages(execution_condition_expr, require_new_messages)
        msg = f"'execution_condition_expr' is of type {type(execution_condition_expr)}, which is not supported."
        raise TypeError(msg)

    def _get_simple_condition_min_messages(self, condition: Condition, require_new_messages: bool) -> int:
        """Get the minimum number of [new] messages guaranteed by a resolved simple condition."""
        match condition:
            case cog.TimeSinceLastExec():
                return 0
            case DynamicTimer():
                # A timer firing doesn't guarantee any messages.
                return 0
            case AnyMessagePresent() | NewMessagePresent():
                if require_new_messages and isinstance(condition, AnyMessagePresent):
                    # We're looking for new messages and this condition doesn't guarantee the message will be new
                    return 0

                if condition.input_name != self.input_identifier:
                    # wrong input identifier
                    return 0

                lower_bound = condition.lower_bound
                if lower_bound is None:
                    return 0
                if isinstance(lower_bound, int):
                    return lower_bound
                msg = f"Condition lower_bound type {type(lower_bound)} not supported in _get_min_msgs: {lower_bound}"
                raise TypeError(msg)
            case _:
                msg = f"Condition type {type(condition)} not supported in _get_min_msgs."
                raise NotImplementedError(msg)

    def _get_binary_condition_min_messages(
        self, execution_condition_expr: cog.BinaryConditionExpr, require_new_messages: bool
    ) -> int:
        """Get the minimum number of [new] messages guaranteed by a binary condition."""
        lhs_min: int = self._get_min_messages(execution_condition_expr.lhs, require_new_messages)
        rhs_min: int = self._get_min_messages(execution_condition_expr.rhs, require_new_messages)
        match execution_condition_expr.op:
            case cog.ConditionOp.AND:
                # Both conditions guarantee a minimum, so take the max of the two mins.
                return max(lhs_min, rhs_min)
            case cog.ConditionOp.OR:
                # We could get both or either side, so we have to assume the worst case, which is the minimum.
                return min(lhs_min, rhs_min)


@dataclass
class InputsStruct:
    """Represents the Inputs sub-struct of a C++ Dial.

    Attributes:
        input_defs (dict): the cog.Inputs, e.g. Input IRs, compiled from in the DSL
        inputs (dict): Python object representation of the Input IRs
        multi_connect_inputs (dict): Python object representation of the multi connect input IRs
        aligned_inputs (list): AlignedInput groups from CogAlignedInputDef entries
    """

    input_defs: abc.Mapping[str, cog.InputDef | cog_components.InputDefElement]
    inputs: dict[str, Input]
    multi_connect_inputs: dict[str, Input]
    aligned_inputs: list[AlignedInput]

    @classmethod
    def from_ir(
        cls: type[InputsStruct],
        compiler_context: CompilerContext,
        input_defs: dict[str, cog.InputDef],
        execution_spec: cog.ExecutionSpec,
        expanded_aligned_input_defs: dict[str, cog.InputDef],
        aligned_input_defs: dict[str, cog.CogAlignedInputDef] | None = None,
    ) -> InputsStruct:
        """Create InputsStruct representation from IR."""
        inputs: dict[str, Input] = {}
        multi_connect_inputs: dict[str, Input] = {}
        for input_def in input_defs.values():
            if input_def.elements:
                input_elements = []
                for element in input_def.elements:
                    ipt = Input.from_ir(compiler_context, element, execution_spec, None)
                    input_elements.append(ipt.identifier)
                    inputs[ipt.identifier] = ipt
                ipt = Input.from_ir(compiler_context, input_def, execution_spec, input_elements)
                multi_connect_inputs[ipt.identifier] = ipt
            else:
                ipt = Input.from_ir(compiler_context, input_def, execution_spec, None)
                inputs[ipt.identifier] = ipt

        aligned: list[AlignedInput] = []
        if aligned_input_defs:
            aligned.extend(
                AlignedInput.from_ir(
                    compiler_context,
                    aligned_def,
                    execution_spec,
                    expanded_aligned_input_defs,
                )
                for aligned_def in aligned_input_defs.values()
            )

        return cls(
            input_defs=input_defs, inputs=inputs, multi_connect_inputs=multi_connect_inputs, aligned_inputs=aligned
        )


@dataclass
class Input:
    """Represents Input structure."""

    identifier: str
    msg_type: types.CppType | types.CppTemplateType
    cpp_type: types.CppType | types.CppTemplateType
    safety_margin: int | None
    min_msgs: int
    min_new_msgs: int
    max_msgs: int
    skip_threshold: int | None
    manual_cursor: bool
    expose_seqno: bool
    use_device_ptr: bool
    no_dial: bool
    copy_inputs: bool
    no_accessor: bool
    input_elements: list[str] | None
    uuid: UUID

    @classmethod
    def from_ir(
        cls: type[Input],
        compiler_context: CompilerContext,
        input_def: cog.InputDef | cog_components.InputDefElement,
        execution_spec: cog.ExecutionSpec,
        input_elements: list[str] | None,
    ) -> Input:
        """Create an Input."""
        msg_type = input_def.message_type
        if isinstance(msg_type, schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation):
            return cls._handle_schema_input(compiler_context, input_def, execution_spec, input_elements)
        msg = f"Attempt to create an input for non-Interface type {type(msg_type)}: {msg_type}"
        raise NotImplementedError(msg)

    @classmethod
    def _handle_schema_input(
        cls: type[Input],
        compiler_context: CompilerContext,
        input_def: cog.InputDef | cog_components.InputDefElement,
        execution_spec: cog.ExecutionSpec,
        input_elements: list[str] | None,
    ) -> Input:
        """Create Input based on schema type."""
        if (
            isinstance(input_def.message_type, expr.Expr)
            or (
                isinstance(input_def.message_type, schema_reg.InterfaceInfo)
                and isinstance(
                    input_def.message_type.interface_ir.typespec,
                    expr.Expr,
                )
            )
            or isinstance(input_def.view_params.max_msgs, expr.Expr)
            or isinstance(input_def.view_params.safety_margin, expr.Expr)
            or isinstance(input_def.view_params.manual_cursor, expr.Expr)
            or isinstance(input_def.view_params.skip_threshold, expr.Expr)
            or isinstance(input_def.view_params.no_dial, expr.Expr)
            or isinstance(input_def.view_params.copy_inputs, expr.Expr)
            or isinstance(input_def.view_params.use_device_ptr, expr.Expr)
            or isinstance(input_def.view_params.multi_connect, expr.Expr)
        ):
            msg = f"Attempt to generate dial input for unresolved schema: {input_def.message_type}"
            raise NotImplementedError(msg)

        assert isinstance(input_def.message_type, schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation)
        name = cls.name_for(input_def)
        if isinstance(input_def.message_type, CogParameterRef | typesys.Instantiation):
            msg_type = typereg.get_cpp_type(input_def.module.context, input_def.message_type)
        else:
            msg_type = typereg.get_cpp_type(input_def.module.context, input_def.message_type.interface_ir.typespec)
        max_msgs = input_def.view_params.max_msgs
        safety_margin = input_def.view_params.safety_margin
        manual_cursor = input_def.view_params.manual_cursor
        expose_seqno = input_def.view_params.expose_seqno
        use_device_ptr = input_def.view_params.use_device_ptr
        no_dial = input_def.view_params.no_dial
        no_accessor = input_def.view_params.multi_connect is not None and input_elements is None
        min_msgs_evaluator = MinMessagesEvaluator(input_def.name, execution_spec)
        min_msgs = min_msgs_evaluator.get_min_messages()
        min_new_msgs = min_msgs_evaluator.get_min_new_messages()

        # sanity check
        if min_new_msgs > min_msgs:
            msg = input_def.append_error_line(
                f"Internal error: Input '{input_def.name}' has execution condition {min_new_msgs=}, which is greater than execution condition {min_msgs=}."
            )
            raise ValueError(msg)

        template_name = "MessageInputDial"

        cpp_type = types.CppTemplateType(
            include=[context.Header(CLK_REPO, "clockwork/dial/msg_input.hh")],
            template_name=template_name,
            cpp_namespace="clockwork",
            arguments=[
                msg_type,
                literal.int_to_cpp(max_msgs, clkbuiltins.UINT64),
                literal.int_to_cpp(min_msgs, clkbuiltins.UINT64),
                literal.int_to_cpp(min_new_msgs, clkbuiltins.UINT64),
                types.CppValue(None, str(manual_cursor).lower()),
                types.CppValue(None, str(expose_seqno).lower()),
                types.CppValue(None, str(use_device_ptr).lower()),
            ],
            const=not manual_cursor,
        )

        return cls(
            identifier=name,
            msg_type=msg_type,
            cpp_type=cpp_type,
            min_msgs=min_msgs,
            min_new_msgs=min_new_msgs,
            max_msgs=max_msgs,
            safety_margin=safety_margin,
            skip_threshold=input_def.view_params.skip_threshold,
            manual_cursor=manual_cursor,
            expose_seqno=expose_seqno,
            use_device_ptr=use_device_ptr,
            no_dial=no_dial,
            copy_inputs=input_def.view_params.copy_inputs,
            no_accessor=no_accessor,
            input_elements=input_elements,
            uuid=uuid_reg.lookup_uuid(compiler_context, input_def),
        )

    @classmethod
    def name_for(cls: type[Input], input_def: cog.InputDef | cog_components.InputDefElement) -> str:
        """Produce the member variable name for the given input."""
        return input_def.name


@dataclass
class AlignedInput:
    """Represents a group of aligned inputs from a CogAlignedInputDef.

    The alignment message input is registered as a regular Input with no_dial=True
    (it has a policy struct and endpoint but no dial accessor).
    The upstream inputs are regular Input objects grouped here for dial nesting.
    """

    group_name: str
    alignment_msg_input: Input
    upstream_inputs: list[Input]
    resolved_aligner_inputs: dict[str, aligner.ResolvedAlignerInput]

    @classmethod
    def from_ir(
        cls: type[AlignedInput],
        compiler_context: CompilerContext,
        aligned_def: cog.CogAlignedInputDef,
        execution_spec: cog.ExecutionSpec,
        expanded_aligned_input_defs: dict[str, cog.InputDef],
    ) -> AlignedInput:
        """Create an AlignedInput from a CogAlignedInputDef."""
        aligner_type = aligned_def.aligned_type
        if not isinstance(aligner_type, aligner.Aligner):
            msg = f"Expected Aligner, got {type(aligner_type).__name__}"
            raise TypeError(msg)

        if aligner_type.alignment_iface is None:
            msg = f"Aligner '{aligner_type.name}' has no alignment interface (not fully resolved)"
            raise RuntimeError(msg)

        resolved = aligner_type.get_resolved()

        alignment_iface_info = schema_reg.InterfaceInfo.make(aligner_type.alignment_iface)
        alignment_msg_input = _make_alignment_msg_input(
            compiler_context, aligned_def, alignment_iface_info, execution_spec
        )

        upstream_inputs: list[Input] = []
        for aligner_input in aligner_type.inputs.values():
            input_name = f"{aligned_def.name}.{aligner_input.name}"
            consumer_input_def = expanded_aligned_input_defs[input_name]
            upstream_input = _make_upstream_input(
                aligned_def,
                aligner_input,
                consumer_input_def,
                execution_spec,
            )
            upstream_inputs.append(upstream_input)

        return cls(
            group_name=aligned_def.name,
            alignment_msg_input=alignment_msg_input,
            upstream_inputs=upstream_inputs,
            resolved_aligner_inputs=resolved.inputs,
        )


def _make_alignment_msg_input(
    compiler_context: CompilerContext,
    aligned_def: cog.CogAlignedInputDef,
    alignment_iface_info: schema_reg.InterfaceInfo,
    execution_spec: cog.ExecutionSpec,
) -> Input:
    """Create an Input for the alignment message subscription."""
    name = aligned_def.name

    msg_type = typereg.get_cpp_type(aligned_def.module.context, alignment_iface_info.interface_ir.typespec)

    min_msgs_evaluator = MinMessagesEvaluator(name, execution_spec)
    min_msgs = min_msgs_evaluator.get_min_messages()
    min_new_msgs = min_msgs_evaluator.get_min_new_messages()

    alignment_view_params = aligned_def.view_params
    if isinstance(alignment_view_params.max_msgs, expr.Expr):
        msg = f"Unresolved view params on aligned input '{aligned_def.name}'"
        raise TypeError(msg)
    max_msgs = alignment_view_params.max_msgs
    manual_cursor = (
        alignment_view_params.manual_cursor if isinstance(alignment_view_params.manual_cursor, bool) else False
    )

    cpp_type = types.CppTemplateType(
        include=[context.Header(CLK_REPO, "clockwork/dial/msg_input.hh")],
        template_name="MessageInputDial",
        cpp_namespace="clockwork",
        arguments=[
            msg_type,
            literal.int_to_cpp(max_msgs, clkbuiltins.UINT64),
            literal.int_to_cpp(min_msgs, clkbuiltins.UINT64),
            literal.int_to_cpp(min_new_msgs, clkbuiltins.UINT64),
            types.CppValue(None, "true" if manual_cursor else "false"),
            types.CppValue(None, "false"),
            types.CppValue(None, "false"),  # use_device_ptr
        ],
        const=True,
    )

    return Input(
        identifier=name,
        msg_type=msg_type,
        cpp_type=cpp_type,
        min_msgs=min_msgs,
        min_new_msgs=min_new_msgs,
        max_msgs=max_msgs,
        safety_margin=alignment_view_params.safety_margin
        if not isinstance(alignment_view_params.safety_margin, expr.Expr)
        else None,
        skip_threshold=alignment_view_params.skip_threshold
        if not isinstance(alignment_view_params.skip_threshold, expr.Expr)
        else None,
        manual_cursor=manual_cursor,
        expose_seqno=False,
        use_device_ptr=False,
        no_dial=True,
        copy_inputs=False,
        no_accessor=False,
        input_elements=None,
        uuid=uuid_reg.lookup_uuid(compiler_context, aligned_def),
    )


def _make_upstream_input(
    aligned_def: cog.CogAlignedInputDef,
    aligner_input: aligner.AlignerInputDef,
    consumer_input_def: cog.InputDef,
    execution_spec: cog.ExecutionSpec,
) -> Input:
    """Create an Input for an upstream channel subscription within an aligned group."""
    input_name = aligner_input.name
    iface_info = aligner_input.get_interface_info()
    msg_type = typereg.get_cpp_type(aligner_input.module.context, iface_info.interface_ir.typespec)

    view_params = consumer_input_def.view_params
    max_msgs = view_params.max_msgs
    safety_margin = view_params.safety_margin
    manual_cursor = view_params.manual_cursor
    expose_seqno = view_params.expose_seqno
    use_device_ptr = view_params.use_device_ptr

    if (
        isinstance(max_msgs, expr.Expr)
        or isinstance(safety_margin, expr.Expr)
        or isinstance(manual_cursor, expr.Expr)
        or isinstance(view_params.skip_threshold, expr.Expr)
        or isinstance(view_params.copy_inputs, expr.Expr)
        or isinstance(use_device_ptr, expr.Expr)
    ):
        msg = f"Unresolved view params on aligner input '{aligner_input.name}'"
        raise TypeError(msg)

    skip_threshold = view_params.skip_threshold
    copy_inputs = view_params.copy_inputs

    min_msgs_evaluator = MinMessagesEvaluator(input_name, execution_spec)
    min_msgs = min_msgs_evaluator.get_min_messages()
    min_new_msgs = min_msgs_evaluator.get_min_new_messages()

    cpp_type = types.CppTemplateType(
        include=[context.Header(CLK_REPO, "clockwork/dial/msg_input.hh")],
        template_name="MessageInputDial",
        cpp_namespace="clockwork",
        arguments=[
            msg_type,
            literal.int_to_cpp(max_msgs, clkbuiltins.UINT64),
            literal.int_to_cpp(min_msgs, clkbuiltins.UINT64),
            literal.int_to_cpp(min_new_msgs, clkbuiltins.UINT64),
            types.CppValue(None, str(manual_cursor).lower()),
            types.CppValue(None, str(expose_seqno).lower()),
            types.CppValue(None, str(use_device_ptr).lower()),
        ],
        const=not manual_cursor,
    )

    upstream_uuid = uuid_reg.uuid_from_name(f"{aligned_def.value_key()}.{aligner_input.name}")

    return Input(
        identifier=input_name,
        msg_type=msg_type,
        cpp_type=cpp_type,
        min_msgs=min_msgs,
        min_new_msgs=min_new_msgs,
        max_msgs=max_msgs,
        safety_margin=safety_margin,
        skip_threshold=skip_threshold,
        manual_cursor=manual_cursor,
        expose_seqno=expose_seqno,
        use_device_ptr=use_device_ptr,
        no_dial=False,
        copy_inputs=copy_inputs,
        no_accessor=False,
        input_elements=None,
        uuid=upstream_uuid,
    )


@dataclass
class OutputsStruct:
    """Represents the Outputs sub-struct of a C++ Dial."""

    output_defs: abc.Mapping[str, cog.OutputDef | cog.MetricsOutputDef | cog.ReportGroupDef]
    outputs: dict[str, Output]

    @classmethod
    def from_ir(
        cls: type[OutputsStruct],
        compiler_context: CompilerContext,
        output_defs: abc.Mapping[str, cog.OutputDef | cog.MetricsOutputDef | cog.ReportGroupDef],
        rate_limit_specs: dict[str, cog.RateLimitSpec],
    ) -> OutputsStruct:
        """Create Outputs representation from IR."""
        outputs: dict[str, Output] = {}
        for output_key, output_def in output_defs.items():
            if isinstance(output_def, cog.OutputDef):
                output = Output.from_ir(compiler_context, output_def, rate_limit_specs.get(output_key))
            elif isinstance(output_def, cog.ReportGroupDef):
                output = Output.from_report_group(compiler_context, output_def, rate_limit_specs.get(output_key))
            else:
                output = Output.from_metrics_output_def(compiler_context, output_def, rate_limit_specs.get(output_key))
            outputs[output.identifier] = output
        return cls(output_defs=output_defs, outputs=outputs)


@dataclass
class Output:
    """Represents output."""

    identifier: str
    msg_type: types.CppType | types.CppTemplateType
    cpp_type: types.CppType | types.CppTemplateType
    rate_limit: cog.ResolvedRateLimitSpec | None
    uuid: UUID
    metrics_log_type: cog.MetricsLogType
    max_msgs_per_exec: int = 1
    cog_metrics_output: bool = False  # True for MetricsOutputDef, False for OutputDef and ReportGroupDef
    is_report_group: bool = False  # True for ReportGroupDef, False for OutputDef and MetricsOutputDef

    @classmethod
    def from_metrics_output_def(
        cls: type[Output],
        compiler_context: CompilerContext,
        output_def: cog.MetricsOutputDef,
        rate_limit: cog.RateLimitSpec | None,
    ) -> Output:
        """Create an Output from a MetricsOutputDef."""
        output = cls._handle_schema_output(compiler_context, output_def.get_interface_info(), output_def, rate_limit)
        output.cog_metrics_output = True
        return output

    @classmethod
    def from_report_group(
        cls: type[Output],
        compiler_context: CompilerContext,
        report_group_def: cog.ReportGroupDef,
        rate_limit: cog.RateLimitSpec | None,
    ) -> Output:
        """Create an Output from a ReportGroupDef."""
        output = cls._handle_schema_output(
            compiler_context, report_group_def.get_interface_info(), report_group_def, rate_limit
        )
        output.is_report_group = True
        return output

    @classmethod
    def from_ir(
        cls: type[Output],
        compiler_context: CompilerContext,
        output_def: cog.OutputDef,
        rate_limit: cog.RateLimitSpec | None,
    ) -> Output:
        """Create an Output."""
        msg_type = output_def.message_type
        if isinstance(msg_type, schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation):
            return cls._handle_schema_output(compiler_context, msg_type, output_def, rate_limit)
        msg = f"Attempt to create an output for non-Interface type {type(msg_type)}: {msg_type}"
        raise NotImplementedError(msg)

    @classmethod
    def _handle_schema_output(
        cls: type[Output],
        compiler_context: CompilerContext,
        message_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation,
        output_def: cog.OutputDef | cog.MetricsOutputDef | cog.ReportGroupDef,
        rate_limit: cog.RateLimitSpec | None,
    ) -> Output:
        identifier = cls.name_for(output_def)
        if isinstance(message_type, CogParameterRef | typesys.Instantiation):
            msg_type = typereg.get_cpp_type(output_def.module.context, message_type)
        else:
            if isinstance(message_type.interface_ir.typespec, expr.Expr):
                msg = f"Attempt to generate dial output for unresolved schema: {message_type}"
                raise NotImplementedError(msg)
            msg_type = typereg.get_cpp_type(output_def.module.context, message_type.interface_ir.typespec)

        # Build template arguments: Message type is always first
        template_args: list[types.CppTypeExpr | types.CppValueExpr] = [msg_type]

        # Add capacity as second template argument if output_def has max_msgs_per_exec > 1
        if isinstance(output_def, cog.OutputDef) and output_def.max_msgs_per_exec > 1:
            template_args.append(types.CppValue(value_type=None, value=str(output_def.max_msgs_per_exec)))

        cpp_type = types.CppTemplateType(
            include=[context.Header(CLK_REPO, "clockwork/pinion/publishable.hh")],
            template_name="Publishable",
            cpp_namespace="clockwork::pinion",
            arguments=template_args,
            const=False,
        )

        return cls(
            identifier=identifier,
            msg_type=msg_type,
            cpp_type=cpp_type,
            rate_limit=rate_limit.get_resolved() if rate_limit else None,
            uuid=uuid_reg.lookup_uuid(compiler_context, output_def),
            metrics_log_type=output_def.log_type,
            max_msgs_per_exec=output_def.max_msgs_per_exec if isinstance(output_def, cog.OutputDef) else 1,
        )

    @classmethod
    def name_for(cls: type[Output], output_def: cog.OutputDef | cog.MetricsOutputDef | cog.ReportGroupDef) -> str:
        """Produce the member variable name for the given input."""
        return output_def.name


@dataclass
class DiagnosticsStruct:
    """Represents the Diagnostics sub-struct of a C++ Dial.

    Attributes:
        diagnostics_defs (dict): the DiagnosticsDef, e.g. Diagnostics IRs, compiled from in the DSL
        diagnostics (dict): Python object representation of the Diagnostics IRs
    """

    diagnostics_defs: dict[str, DiagnosticsDef]
    diagnostics: dict[str, Diagnostics]

    @classmethod
    def from_ir(
        cls: type[DiagnosticsStruct],
        compiler_context: CompilerContext,
        diagnostics_defs: dict[str, DiagnosticsDef],
    ) -> DiagnosticsStruct:
        """Create DiagnosticsStruct representation from IR."""
        diagnostics: dict[str, Diagnostics] = {}
        for diagnostics_def in diagnostics_defs.values():
            item = Diagnostics.from_ir(compiler_context, diagnostics_def)
            diagnostics[item.identifier] = item
        return cls(diagnostics_defs=diagnostics_defs, diagnostics=diagnostics)


@dataclass
class Diagnostics:
    """Represents the Diagnostics reporter of a C++ Dial."""

    identifier: str
    group_id: str | CogParameterRef
    instance_id: str | CogParameterRef | None
    cpp_type: types.CppTemplateType  # For the dial common utils which expect a member of this name
    manager_type: types.CppTemplateType
    reporter_type: types.CppTemplateType
    uuid: UUID

    @classmethod
    def from_ir(
        cls: type[Diagnostics],
        compiler_context: CompilerContext,
        diagnostics_def: DiagnosticsDef,
    ) -> Diagnostics:
        """Create Diagnostics representation from IR."""
        if diagnostics_def is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            return None
        if isinstance(diagnostics_def.group_id, expr.Expr) or isinstance(diagnostics_def.instance_id, expr.Expr):
            msg = "Attempt to generate dial state for unresolved diagnostics block"
            raise NotImplementedError(msg)
        if isinstance(diagnostics_def.group_id, CogParameterRef):
            assert diagnostics_def.group_id.parameter_def.get_typeval() == clkbuiltins.STRING
            group_id_value = types.CppScopedValue(
                scope=types.CppTemplateType(
                    include=[diagnostics.REPORT_DEFS_HEADER, diagnostics.IMPL_HEADER],
                    template_name="SignalGroupIdParam",
                    arguments=[types.CppValue(None, f"{diagnostics_def.group_id.parameter_def.param_name}")],
                    cpp_namespace="clockwork::diagnostics",
                ),
                header=[],
                name="value",
            )
        else:
            group_id_value = types.CppScopedValue(
                scope=types.CppType(
                    includes=[diagnostics.REPORT_DEFS_HEADER, diagnostics.IMPL_HEADER],
                    type_name="SignalGroupId",
                    cpp_namespace="clockwork::diagnostics",
                ),
                header=[],
                name=diagnostics_def.group_id,
            )
        return cls.from_params(
            identifier=cls.name_for(diagnostics_def),
            group_type=group_id_value,
            group_id=diagnostics_def.group_id,
            instance_id=diagnostics_def.instance_id,
            uuid=uuid_reg.lookup_uuid(compiler_context, diagnostics_def),
        )

    @classmethod
    def from_cog_infra(
        cls: type[Diagnostics],
        compiler_context: CompilerContext,
        diagnostics_def: InfraDiagnosticsDef,
    ) -> Diagnostics:
        """Create Diagnostics representation for the given cog's infrastructure checks."""
        return cls.from_params(
            identifier=diagnostics_def.name,
            group_type=types.CppType([], COG_INFRA_DIAGS_GROUP_DEF_NAME, None),
            group_id=COG_INFRA_DIAGS_GROUP_NAME,
            instance_id=None,
            uuid=uuid_reg.lookup_uuid(compiler_context, diagnostics_def),
        )

    @classmethod
    def from_params(
        cls: type[Diagnostics],
        identifier: str,
        group_type: types.CppTypeExpr | types.CppValueExpr,
        group_id: str | CogParameterRef,
        instance_id: str | CogParameterRef | None,
        uuid: UUID,
    ) -> Diagnostics:
        """Create Diagnostics representation from parameters, to support classic and cog-infra diagnostics."""
        suffix = "" if isinstance(group_type, types.CppValueExpr) else "Struct"
        # TODO(OI-3528): Note that here includes is left empty, and the group_id_value will have the IMPL_HEADER.
        # This is to work around the fact that we don't have fancy support for include_common.hh being able to provide
        # a header. As a result, if IMPL_HEADER where included here, it would get included by every cog in the system.
        manager_type = types.CppTemplateType(
            include=[],
            template_name="ClockworkManager" + suffix,
            cpp_namespace="clockwork::diagnostics",
            arguments=[group_type],
        )
        reporter_type = types.CppTemplateType(
            include=[diagnostics.IMPL_HEADER],
            template_name="ClockworkReporter" + suffix,
            cpp_namespace="clockwork::diagnostics",
            arguments=[group_type],
        )
        return cls(
            identifier=identifier,
            group_id=group_id,
            instance_id=instance_id,
            cpp_type=reporter_type,
            manager_type=manager_type,
            reporter_type=reporter_type,
            uuid=uuid,
        )

    @classmethod
    def name_for(cls: type[Diagnostics], diagnostics_def: DiagnosticsDef) -> str:
        """Produce the member variable name for the given item."""
        return diagnostics_def.name


@dataclass
class SignalEntry:
    """Represents a single signal entry in the SignalApi for a C++ Dial.

    This class captures the resolved signal information needed to generate the C++ signal API methods.
    """

    identifier: str
    signal_name: str
    signal_type: types.CppType | types.CppTemplateType
    metadata_type: types.CppType | types.CppTemplateType | None
    pre_aggregation: set[signal_ir.AggregationType]
    report_group_name: str
    is_batched: bool
    post_aggregation: set[signal_ir.AggregationType]
    max_observations: int | None
    presence_bit_index: int | None

    @classmethod
    def from_ir(
        cls: type[SignalEntry],
        compiler_context: CompilerContext,
        entry: ReportGroupEntry,
        report_group: ReportGroupDef,
    ) -> SignalEntry:
        """Create a SignalEntry from a ReportGroupEntry IR node."""
        resolved_signal = entry.resolved_signal()
        identifier = entry.name
        signal_name = resolved_signal.signal_name
        signal_type = typereg.get_cpp_type(compiler_context, resolved_signal.signal_type)

        metadata_type: types.CppType | types.CppTemplateType | None = None
        effective_meta = entry.effective_metadata()
        if effective_meta is not None:
            metadata_type = _get_cpp_type_for_metadata(compiler_context, effective_meta)

        is_batched = (
            report_group.report_group_config is not None
            and report_group.report_group_config.reporting_strategy == report_group_module.ReportingStrategy.BATCHED
        )

        max_observations: int | None = None
        if report_group.report_group_config is not None:
            max_observations = report_group.report_group_config.max_observations

        post_aggregation = entry.effective_post_aggregation()
        validity = report_group.get_signal_validity(entry.name)
        presence_bit_index = (
            validity.index if validity.source is report_group_module.SignalValiditySource.PRESENCE_BIT else None
        )

        return cls(
            identifier=identifier,
            signal_name=signal_name,
            signal_type=signal_type,
            metadata_type=metadata_type,
            pre_aggregation=resolved_signal.pre_aggregation,
            report_group_name=report_group.name,
            is_batched=is_batched,
            post_aggregation=post_aggregation,
            max_observations=max_observations,
            presence_bit_index=presence_bit_index,
        )

    def has_pre_aggregation(self) -> bool:
        """Return True if this signal has pre-aggregation (i.e., uses accumulate instead of set)."""
        # "value" means no aggregation, so if we have anything other than just {VALUE}, we have aggregation
        return self.pre_aggregation != {signal_ir.AggregationType.VALUE}

    def has_single_aggregation(self) -> bool:
        """Return True if this signal has exactly one pre-aggregation type (or just value)."""
        return len(self.pre_aggregation) == 1

    def get_tap_field_name(self, pre_agg: signal_ir.AggregationType | None = None) -> str:
        """Get the Tap API field name for this signal.

        For batched report groups: {identifier}_{pre_agg.value}
        For post-aggregated: depends on aggregation types

        Args:
            pre_agg: The pre-aggregation type for the field. Uses first if not specified.

        Returns:
            The field name as used in the Tap API (e.g., 'set_signal_value', 'get_signal_min').
        """
        if pre_agg is None:
            # Use the first/only pre-aggregation type
            pre_agg = next(iter(self.pre_aggregation))
        return f"{self.identifier}_{pre_agg.value}"

    def get_tap_field_name_with_post_agg(
        self,
        pre_agg: signal_ir.AggregationType,
        post_agg: signal_ir.AggregationType,
    ) -> str:
        """Get the Tap API field name for post-aggregated signals.

        For post-aggregated report groups: {identifier}_{pre_agg.value}_{post_agg.value}

        Args:
            pre_agg: The pre-aggregation type.
            post_agg: The post-aggregation type.

        Returns:
            The field name for post-aggregated signals.
        """
        return f"{self.identifier}_{pre_agg.value}_{post_agg.value}"


def _is_schema_type(type_info: typesys.TypeVal) -> bool:
    """Check if a type is a schema (not a primitive or strong type).

    Schema types need to be wrapped in Tap<Tachyon<>> for use as signal metadata.
    """
    match type_info:
        case statement.InstantiateStmt():
            assert isinstance(type_info.typespec, typesys.Instantiation)
            return isinstance(type_info.typespec.instantiates, schema.Schema | schema.ResolvedSchema)
        case schema.Schema() | schema.InstantiatedSchema() | schema.ResolvedSchema():
            return True
        case typesys.Instantiation():
            return isinstance(type_info.instantiates, schema.Schema | schema.ResolvedSchema)
        case _:
            return False


def _schema_to_tap_tachyon(schema_type: typesys.TypeVal) -> typesys.Instantiation:
    """Convert a schema type to a Tap<Tachyon<Schema>> instantiation."""
    # Normalize to ResolvedSchema or Instantiation
    match schema_type:
        case statement.InstantiateStmt():
            assert isinstance(schema_type.typespec, typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(
                schema_type.typespec
            ).as_instantiation_or_resolved_schema()
        case schema.InstantiatedSchema():
            schema_ir = schema_type.as_instantiation_or_resolved_schema()
        case schema.Schema():
            schema_ir = schema_type.get_resolved()
        case schema.ResolvedSchema() | typesys.Instantiation():
            # Already in the expected form
            schema_ir = schema_type
        case _:
            msg = f"Unexpected schema type in _schema_to_tap_tachyon: {schema_type.value_key()}"
            raise TypeError(msg)

    tachyon = typesys.Instantiation(
        instantiates=clkbuiltins.TACHYON,
        arguments={"schema": schema_ir},
        type_info=clkbuiltins.TYPE_TYPE,
    )
    return typesys.Instantiation(
        instantiates=clkbuiltins.TAP,
        arguments={"representation": tachyon},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def _get_cpp_type_for_metadata(
    compiler_context: CompilerContext, metadata_type: typesys.TypeVal
) -> types.CppType | types.CppTemplateType:
    """Get the C++ type for signal metadata, wrapping schemas in Tap<Tachyon<>>.

    Args:
        compiler_context: The compiler context.
        metadata_type: The CLK metadata type.

    Returns:
        The corresponding C++ type. For schemas, this will be Tap<Tachyon<Schema>>.
    """
    if _is_schema_type(metadata_type):
        wrapped_type = _schema_to_tap_tachyon(metadata_type)
        return typereg.get_cpp_type(compiler_context, wrapped_type)
    return typereg.get_cpp_type(compiler_context, metadata_type)


@dataclass
class SignalsStruct:
    """Represents the Signals API sub-struct of a C++ Dial.

    This collects all signals from all report groups defined in the cog
    and provides a flat API for setting/accumulating signal values.

    Attributes:
        report_groups (dict): the ReportGroupDef IRs from the cog
        signals (dict): Python object representation of the signal entries
    """

    report_groups: dict[str, ReportGroupDef]
    signals: dict[str, SignalEntry]

    @classmethod
    def from_ir(
        cls: type[SignalsStruct],
        compiler_context: CompilerContext,
        report_groups: dict[str, ReportGroupDef],
    ) -> SignalsStruct:
        """Create SignalsStruct representation from IR.

        Collects all signals from all report groups into a flat structure.
        """
        signals: dict[str, SignalEntry] = {}
        for rg_name, report_group in report_groups.items():
            for entry_name, entry in report_group.entries.items():
                # Use a compound key to avoid collisions between signals with the same name in different groups
                signal_key = f"{rg_name}_{entry_name}"
                signals[signal_key] = SignalEntry.from_ir(compiler_context, entry, report_group)
        return cls(report_groups=report_groups, signals=signals)
