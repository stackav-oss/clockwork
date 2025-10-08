# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR nodes for Cog components."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Final

from clockwork.dsl import cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    expr,
    interface,
    node,
    primitive,
    representation,
    schema,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span
from clockwork.dsl.ir.message_type import MessageTypeMixin, resolve_schema_interface
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable


@dataclass
class Condition:
    """Base class for different types of execution conditions."""

    @classmethod
    def from_cst(cls: type[Condition], cst_cond: cst.ConditionSpec, module: node.Module) -> Condition:
        """Create the correct Condition IR node from a CST node.

        This is polymorphic and will return a child class of Condition.
        """
        dispatcher: dict[
            cst.ConditionSpec.Label,
            Callable[[cst.ConditionSpec, node.Module], Condition],
        ] = {
            cst.ConditionSpec.Label.TIME_SINCE_LAST_EXEC: TimeSinceLastExec.from_cst,
            cst.ConditionSpec.Label.ANY_MESSAGE: AnyMessagePresent.from_cst,
            cst.ConditionSpec.Label.NEW_MESSAGE: NewMessagePresent.from_cst,
        }

        # Check to protect against grammar change
        expected_num_children: Final = 2
        if (num_children := len(cst_cond.children)) != expected_num_children:
            msg = f"Expecting {expected_num_children} children, but found {num_children}"
            raise RuntimeError(msg)
        cond_type = cst_cond.children[0][0]
        if cond_type not in dispatcher:
            msg = node.append_error_line(
                cst_cond,
                module,
                f"Invalid condition type or handler not implemented for {cond_type}",
            )
            raise TypeError(msg)
        return dispatcher[cond_type](cst_cond, module)


@dataclass
class TimeSinceLastExec(Condition, node.CstNode[cst.ConditionSpec]):
    """A time_since_last_exec condition."""

    duration: primitive.UnitValue

    @classmethod
    @override
    def from_cst(
        cls: type[TimeSinceLastExec],
        cst_cond: cst.ConditionSpec,
        module: node.Module,
    ) -> TimeSinceLastExec:
        """Create an IR TimeSinceLastExec from CST ConditionSpec."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        # We just call this to get an exception raised if it's an unexpected spec type (not yet implemented)
        cst_cond.child_time_since_last_exec()
        cst_arg = cst_cond.child_arg_list().child_arg()
        if (name := cst_arg.maybe_name()) and get_span(
            name_span := name.child_value(),
            module.terminals,
        ) != "duration":
            msg = (
                f"Invalid argument {get_span(name_span, module.terminals)} passed to time_since_last_exec(duration: Duration)\n"
                + format_line_with_error(name_span, module.terminals, module.module_id)
            )
            raise ValueError(msg)
        expr = cst_arg.child_expr()
        if (arg_identifier := expr.maybe_identifier()) is not None:
            msg = "Parameters not yet implemented\n" + format_line_with_error(
                arg_identifier.span, module.terminals, module.module_id
            )
            raise NotImplementedError(msg)
        literal = expr.child_literal()
        if (unit_literal := literal.maybe_unit_literal()) is None:
            msg = "Unitless literals not allowed in Duration typed context.\n" + format_line_with_error(
                literal.span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)
        duration = primitive.UnitLiteral.from_child_cst(unit_literal=unit_literal, parent_cst=literal, module=module)
        if duration.type_info is not clkbuiltins.DURATION:
            msg = '"duration" parameter to time_since_last_exec must have Duration type.'
            raise TypeError(msg)
        return cls(module=module, cst_node=cst_cond, duration=duration)


@dataclass
class MessagesPresent(Condition, node.CstNode[cst.ConditionSpec]):
    """A message_presence condition."""

    input_name: str
    lower_bound: int | expr.Expr | None
    upper_bound: int | expr.Expr | None

    @classmethod
    @override
    def from_cst(
        cls: type[MessagesPresent],
        cst_cond: cst.ConditionSpec,
        module: node.Module,
    ) -> MessagesPresent:
        """Process ConditionSpec, common for AnyMessage and NewMessage conditions."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        input_name: str | None = None
        lower_bound: expr.Expr | None = None
        upper_bound: expr.Expr | None = None
        seen_params: set[str] = set()
        args = list(cst_cond.child_arg_list().children_arg())
        # The first arg can be nameless, but it is assumed to be the input name
        arg0 = args[0]
        if arg0.maybe_name() is None:
            input_name = get_span(arg0.child_expr().child_identifier().child_value(), module.terminals)
            seen_params.add("input")
            args.pop(0)

        for arg in args:
            # The args should be named and be expressions
            if (name := arg.maybe_name()) is None:
                msg = node.append_error_line(arg, module, "Conditions require named arguments")
                raise ValueError(msg)
            if (name_str := get_span(name.child_value(), module.terminals)) in seen_params:
                msg = node.append_error_line(name, module, f"Parameter '{name_str}' specified more than once")
                raise ValueError(msg)

            seen_params.add(name_str)
            value = expr.Expr.from_cst(arg.child_expr(), module)

            if name_str == "min":
                typesys.unify(clkbuiltins.UINT64, value.type_info)
                lower_bound = value
            elif name_str == "max":
                typesys.unify(clkbuiltins.UINT64, value.type_info)
                upper_bound = value
            elif name_str == "input":
                input_name = get_span(arg.child_expr().child_identifier().child_value(), module.terminals)
            else:
                msg = node.append_error_line(name, module, f"Unrecognized parameter {name_str}")
                raise ValueError(msg)

        if input_name is None:
            msg = node.append_error_line(cst_cond, module, "input is not specified for condition")
            raise ValueError(msg)

        return cls(
            module=module,
            cst_node=cst_cond,
            input_name=input_name,
            lower_bound=lower_bound,
            upper_bound=upper_bound,
        )

    def resolve(self) -> None:
        """Perform finalization."""
        # Check if resolved already; also acts as early check for linting purposes
        # Note that while isinstance(xyz, expr.Expr|None) would simplify the conditional, it can't help pass some lint checks downstream.
        if (self.lower_bound is not None and not isinstance(self.lower_bound, expr.Expr)) or (
            self.upper_bound is not None and not isinstance(self.upper_bound, expr.Expr)
        ):
            return

        lower_bound_int = 1 if self.lower_bound is None else self._decimal_to_int(self.lower_bound)

        upper_bound_int = None
        if self.upper_bound is not None:
            upper_bound_int = self._decimal_to_int(self.upper_bound)

        # Validate bounds
        if upper_bound_int is not None and upper_bound_int < lower_bound_int:
            # upper_bound shouldn't be None at this point, but the linter can't deduce directly
            assert self.upper_bound is not None
            msg = self.upper_bound.append_error_line(
                f"Invalid bounds: max {upper_bound_int} is smaller than min {lower_bound_int}",
            )
            raise ValueError(msg)

        self.lower_bound = lower_bound_int
        self.upper_bound = upper_bound_int

    def _decimal_to_int(self, value: expr.Expr) -> int:
        result = value.evaluate()
        if not isinstance(result, primitive.DecimalValue):
            msg = value.append_error_line(
                f"Expected a DecimalValue for parameter, but got {type(result)}",
            )
            raise TypeError(msg)
        return primitive.unsigned_decimal_to_int(result)


@dataclass
class AnyMessagePresent(MessagesPresent):
    """A any_message condition."""


@dataclass
class NewMessagePresent(MessagesPresent):
    """A new_message condition."""


@dataclass
class ConditionDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.ConditionDef]):
    """A named execution condition."""

    condition: Condition

    @classmethod
    def from_cst(
        cls: type[ConditionDef],
        scope: node.Scope,
        cst_def: cst.ConditionDef,
        module: node.Module,
    ) -> ConditionDef:
        """Create an IR ConditionDef from a CST ConditionDef."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_def.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        result = cls(
            module=module,
            name=get_span(cst_def.child_identifier().child_value(), module.terminals),
            cst_node=cst_def,
            doc=doc,
            scope=scope,
            type_info=clkbuiltins.COG_CONDITION_TYPE,
            condition=Condition.from_cst(cst_def.child_condition_spec(), module),
        )
        scope.define(result.name, result, module.terminals)
        return result


@dataclass
class ViewParams(node.CstNode[cst.InputBlock]):
    """Parameters for input views."""

    max_msgs: int | expr.Expr
    manual_cursor: bool | expr.Expr
    no_dial: bool | expr.Expr
    skip_threshold: int | expr.Expr | None
    safety_margin: int | expr.Expr | None
    copy_inputs: bool | expr.Expr
    is_optional: bool | expr.Expr = False

    @classmethod
    def make_default(cls: type[ViewParams], module: node.Module, cst_node: cst.InputBlock | None = None) -> ViewParams:
        """Make a ViewParams with all values at defaults."""
        return ViewParams(
            module=module,
            cst_node=cst_node,
            max_msgs=1,
            manual_cursor=False,
            no_dial=False,
            skip_threshold=None,
            safety_margin=None,
            copy_inputs=False,
        )

    @classmethod
    def from_cst(cls: type[ViewParams], cst_node: cst.InputBlock, module: node.Module) -> ViewParams:
        """Construct view parameters from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        result = ViewParams.make_default(module, cst_node=cst_node)
        seen_params: set[str] = set()
        for param_cst in cst_node.children_input_block_param():
            result._handle_param(param_cst, seen_params)  # noqa: SLF001 (result is also a ViewParams)
        return result

    def _handle_param(self, cst_node: cst.InputBlockParam, seen_params: set[str]) -> None:  # noqa: PLR0911 A large
        # number of returns is reasonable because this is a factory type function.
        assert self.module.terminals is not None
        name = get_span(name_span := cst_node.child_param().child_value(), self.module.terminals)
        if name in seen_params:
            msg = f"Parameter '{name}' specified more than once:\n" + format_line_with_error(
                name_span,
                self.module.terminals,
                self.module.module_id,
            )
            raise ValueError(msg)
        seen_params.add(name)
        value = expr.Expr.from_cst(cst_node.child_value(), self.module)
        if name == "max_msgs":
            typesys.unify(clkbuiltins.UINT32, value.type_info)
            self.max_msgs = value
            return
        if name == "manual_cursor":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.manual_cursor = value
            return
        if name == "no_dial":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.no_dial = value
            return
        if name == "skip_threshold":
            typesys.unify(clkbuiltins.UINT64, value.type_info)
            self.skip_threshold = value
            return
        if name == "safety_margin":
            typesys.unify(clkbuiltins.UINT64, value.type_info)
            self.safety_margin = value
            return
        if name == "copy_inputs":
            typesys.unify(clkbuiltins.BOOL, value.type_info)

            self.copy_inputs = value
            return
        if name == "connect_optional":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.is_optional = value
            return

        msg = f"Unsupported view parameter '{name}'"
        raise NotImplementedError(msg)

    def resolve(self) -> None:
        """Perform finalization of the view params IR."""
        self._resolve_max_msgs()
        self._resolve_manual_cursor()
        self._resolve_no_dial()
        self._resolve_skip_threshold()
        self._resolve_safety_margin()
        self._resolve_copy_inputs()
        self._resolve_is_optional()

    def _resolve_max_msgs(self) -> None:
        if isinstance(self.max_msgs, expr.Expr):
            result = self.max_msgs.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = self.max_msgs.append_error_line(
                    f"Expected a DecimalValue for parameter max_msgs, but got {type(result)}",
                )
                raise TypeError(msg)
            self.max_msgs = primitive.unsigned_decimal_to_int(result)

    def _resolve_manual_cursor(self) -> None:
        if isinstance(self.manual_cursor, expr.Expr):
            result = self.manual_cursor.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.manual_cursor.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter manual_cursor, but got {type(result)}",
                )
                raise TypeError(msg)
            self.manual_cursor = primitive.value_to_bool(result)

    def _resolve_no_dial(self) -> None:
        if isinstance(self.no_dial, expr.Expr):
            result = self.no_dial.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.no_dial.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter no_dial, but got {type(result)}",
                )
                raise TypeError(msg)
            self.no_dial = primitive.value_to_bool(result)

    def _resolve_skip_threshold(self) -> None:
        if isinstance(self.skip_threshold, expr.Expr):
            result = self.skip_threshold.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = self.skip_threshold.append_error_line(
                    f"Expected a DecimalValue for parameter skip_threshold, but got {type(result)}",
                )
                raise TypeError(msg)
            self.skip_threshold = primitive.unsigned_decimal_to_int(result)

    def _resolve_safety_margin(self) -> None:
        if isinstance(self.safety_margin, expr.Expr):
            result = self.safety_margin.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = self.safety_margin.append_error_line(
                    f"Expected a DecimalValue for parameter safety_margin, but got {type(result)}",
                )
                raise TypeError(msg)
            self.safety_margin = primitive.unsigned_decimal_to_int(result)

    def _resolve_copy_inputs(self) -> None:
        if isinstance(self.copy_inputs, expr.Expr):
            result = self.copy_inputs.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.copy_inputs.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter copy_inputs, but got {type(result)}",
                )
                raise TypeError(msg)
            self.copy_inputs = primitive.value_to_bool(result)

    def _resolve_is_optional(self) -> None:
        if isinstance(self.is_optional, expr.Expr):
            result = self.is_optional.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.is_optional.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter optional, but got {type(result)}",
                )
                raise TypeError(msg)
            self.is_optional = primitive.value_to_bool(result)


class MetricsLogType(Enum):
    """The type of metrics log."""

    telemetry = 0
    event = 1
    none = 2


@dataclass
class OutputDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.OutputDef], MessageTypeMixin):
    """A definition of a Cog output."""

    log_type: MetricsLogType = MetricsLogType.none
    is_optional: bool = False

    @classmethod
    def from_cst(
        cls: type[OutputDef],
        cst_node: cst.OutputDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> OutputDef:
        """Construct an OutputDef IR node from a CST OutputDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        message_type = expr.Expr.from_cst(cst_node.child_output_type(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)

        is_optional = False
        if (maybe_block := cst_node.maybe_output_block()) is not None:
            for param in maybe_block.children_output_block_param():
                if (maybe_optional := param.maybe_connect_optional()) is not None:
                    is_optional = maybe_optional.child_boolean().maybe_true() is not None

        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_OUTPUT_TYPE,
            message_type=message_type,
            is_optional=is_optional,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        self.message_type = resolve_schema_interface(self.module.context, self.message_type)


@dataclass
class InputDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.InputDef], MessageTypeMixin):
    """A definition of a Cog input."""

    view_params: ViewParams

    @classmethod
    def from_cst(
        cls: type[InputDef],
        cst_node: cst.InputDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> InputDef:
        """Construct an InputDef IR node from a CST InputDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        view_params_cst = cst_node.maybe_input_block()
        view_params = (
            ViewParams.from_cst(view_params_cst, module) if view_params_cst else ViewParams.make_default(module)
        )
        message_type = expr.Expr.from_cst(cst_node.child_input_type(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            name=name,
            scope=parent_scope,
            message_type=message_type,
            type_info=clkbuiltins.COG_INPUT_TYPE,
            view_params=view_params,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        self.view_params.resolve()


@dataclass
class MetricsOutputDef(typesys.NamedAttribute):
    """A definition of a Cog metrics output."""

    module: node.Module
    log_type: MetricsLogType
    message_type: representation.ReprInstantiation | None
    message_interface_type: schema_reg.InterfaceInfo | None
    schemas: list[schema.InstantiatedSchema] = field(default_factory=list)
    enums: list[clkenum.ClkEnum] = field(default_factory=list)
    dependent_interfaces: list[schema_reg.InterfaceInfo] = field(default_factory=list)
    dependent_representations: list[representation.ReprInstantiation] = field(default_factory=list)

    def resolve(
        self,
        metrics_schema: schema.InstantiatedSchema,
        metrics_schema_deps: Iterable[schema.InstantiatedSchema],
        generated_enums: Iterable[clkenum.ClkEnum],
    ) -> None:
        """Resolve the metrics output definition by collecting schemas, enums, and dependent representations.

        Args:
            metrics_schema: The main schema for this metrics output.
            metrics_schema_deps: Additional dependent schemas required by this output.
            generated_enums: Enums generated for this metrics output.
        """
        self.schemas.extend(metrics_schema_deps)
        self.schemas.append(metrics_schema)
        self.enums.extend(generated_enums)
        for schema_dep in metrics_schema_deps:
            message_repr, message_interface = self._get_repr_and_interface_from_schema(schema_dep)
            self.dependent_interfaces.append(message_interface)
            self.dependent_representations.append(message_repr)

        message_repr, message_interface = self._get_repr_and_interface_from_schema(metrics_schema)

        self.message_type = message_repr
        self.message_interface_type = message_interface
        self.dependent_interfaces.append(message_interface)
        self.dependent_representations.append(message_repr)

    def _get_repr_and_interface_from_schema(
        self, message_schema: schema.InstantiatedSchema
    ) -> tuple[representation.ReprInstantiation, schema_reg.InterfaceInfo]:
        schema_arg = message_schema.schema.source
        if schema_arg is None:
            msg = f"{message_schema.schema_name} must have an underlying schema"
            raise ValueError(msg)
        repr_typespec = typesys.Instantiation(
            type_info=clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.TACHYON, arguments={"schema": schema_arg}
        )
        resolved_msg_type = representation.ResolvedReprInstantiation(
            module=self.module,
            cst_node=None,
            name="",
            scope=self.module.inner_scope,
            schema_ir=message_schema,
            is_generic=False,
            typespec=repr_typespec,
        )
        repr_instantiation = representation.ReprInstantiation(
            module=self.module,
            cst_node=None,
            name="",
            scope=self.module.inner_scope,
            schema_ir=message_schema,
            is_generic=False,
            typespec=repr_typespec,
            resolved=resolved_msg_type,
        )

        representation_reference = representation.RepresentationReference(message_schema, repr_typespec)
        interface_typesec = typesys.Instantiation(
            clkbuiltins.TYPE_TYPE, instantiates=clkbuiltins.TAP, arguments={"representation": repr_typespec}
        )
        interface_info = schema_reg.InterfaceInfo.make(
            interface.InterfaceInstantiation(
                module=self.module,
                cst_node=None,
                name="",
                scope=self.module.inner_scope,
                representation=representation_reference,
                is_generic=False,
                typespec=interface_typesec,
            )
        )
        return repr_instantiation, interface_info

    def get_interface_info(self) -> schema_reg.InterfaceInfo:
        """Retrieve the underlying interface info.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        if not isinstance(self.message_interface_type, schema_reg.InterfaceInfo):
            msg = f"Attempt to get interface for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return self.message_interface_type

    def get_interface_instantiation(self) -> interface.InterfaceInstantiation:
        """Retrieve the underlying interface instantiation.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        return self.get_interface_info().interface_ir

    def get_generated_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Retrieve the underlying interface instantiation."""
        return [message_interface.interface_ir for message_interface in self.dependent_interfaces]

    def get_resolved_representation_instantiation(self) -> representation.ResolvedReprInstantiation:
        """Retrieve the underlying representation type.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        if self.message_type is None:
            msg = "Attempt to get representation for unresolved entity"
            raise RuntimeError(msg)
        representation_inst = self.message_type.resolved
        if not isinstance(representation_inst, representation.ResolvedReprInstantiation):
            msg = f"Attempt to get representation for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return representation_inst

    def get_generated_repr_instantiations(self) -> Iterable[representation.ReprInstantiation]:
        """Retrieve the underlying representation type.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        representation_list = []
        for message_repr in self.dependent_representations:
            if self.message_type is None:
                msg = "Attempt to get representation for unresolved entity"
                raise RuntimeError(msg)
            representation_list.append(message_repr)
        return representation_list

    def get_generated_schemas(self) -> Iterable[schema.Schema]:
        """Retrieve the underlying schema.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        schema_list = []
        for generated_schema in self.schemas:
            if generated_schema.schema.source is None:
                msg = "Attempt to get schema for unresolved entity"
                raise RuntimeError(msg)
            schema_list.append(generated_schema.schema.source)
        return schema_list
