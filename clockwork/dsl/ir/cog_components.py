# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR nodes for Cog components."""

from __future__ import annotations

from abc import abstractmethod
from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Any, Final, cast

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    dfl,
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
from clockwork.dsl.ir.message_type import (
    MessageTypeMixin,
    resolve_parameterized_schema_interface,
    resolve_schema_interface,
)
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable
    from typing import Self

    from fltk.fegen.pyrt.span_protocol import SpanProtocol
    from fltk.fegen.pyrt.terminalsrc import TerminalSource


# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogComponent(typesys.NamedAttribute):
    """Base class for cog components."""

    @classmethod
    @abstractmethod
    def from_statement(
        cls: type[Self],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> Self:
        """Construct an instance of this component from a statement."""


@dataclass
class Condition:
    """Base class for different types of execution conditions."""

    @classmethod
    def from_cst(cls: type[Condition], cst_cond: cst.ConditionSpec, module: node.Module) -> Condition:
        """Create the correct Condition IR node from a CST node.

        This is polymorphic and will return a child class of Condition.
        """
        dispatcher = cast(
            "dict[cst.ConditionSpec.Label, Callable[[cst.ConditionSpec, node.Module], Condition]]",
            {
                cst.ConditionSpec.Label.TIME_SINCE_LAST_EXEC: TimeSinceLastExec.from_cst,
                cst.ConditionSpec.Label.ANY_MESSAGE: AnyMessagePresent.from_cst,
                cst.ConditionSpec.Label.NEW_MESSAGE: NewMessagePresent.from_cst,
            },
        )

        # Check to protect against grammar change
        expected_num_children: Final = 2
        if (num_children := len(cst_cond.children)) != expected_num_children:
            msg = f"Expecting {expected_num_children} children, but found {num_children}"
            raise RuntimeError(msg)
        cond_type = cst_cond.children[0][0]
        if cond_type is None or cond_type not in dispatcher:
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
                typesys.unify(clkbuiltins.INT64, value.type_info)
                lower_bound = value
            elif name_str == "max":
                typesys.unify(clkbuiltins.INT64, value.type_info)
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

        # Figure out lower bound.
        default_lower_bound: int = 1
        if self.lower_bound is None:
            lower_bound_int = default_lower_bound
        else:
            lower_bound_int: int = self._decimal_to_int(self.lower_bound)
            # If the user specifies a lower bound, it must not be the default value.
            if lower_bound_int == default_lower_bound:
                msg = self.lower_bound.append_error_line(
                    f"Execution condition has 'min' explicitly set to the default ({default_lower_bound})."
                    + f"\nTo prevent ambiguity, this is not allowed. Please remove 'min={lower_bound_int}' and trust the default."
                )
                raise ValueError(msg)
            if lower_bound_int <= 0:
                msg = self.lower_bound.append_error_line(
                    f"Execution condition has invalid 'min' {lower_bound_int}. It must be at least 1",
                )
                raise ValueError(msg)

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
        return primitive.decimal_to_int(result)


@dataclass
class AnyMessagePresent(MessagesPresent):
    """A any_message condition."""


@dataclass
class NewMessagePresent(MessagesPresent):
    """A new_message condition."""


@dataclass
class DynamicTimer(Condition):
    """A dynamic one-shot timer condition (used by aligner cogs).

    Unlike TimeSinceLastExec which has a fixed periodic threshold,
    a DynamicTimer is armed/disarmed at runtime by the generated aligner code.
    This condition type is not user-declarable; it is only emitted
    programmatically by the aligner cog generator.
    """


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
        # fmt: off
        result = cls(
            module=module,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=get_span(cst_def.child_identifier().child_value(), module.terminals),
            cst_node=cst_def,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=clkbuiltins.COG_CONDITION_TYPE,
            condition=Condition.from_cst(cst_def.child_condition_spec(), module),
        )
        # fmt: on
        scope.define(result.name, result, module.terminals)
        return result


_DEFAULT_VIEW_MAX_MSGS = 1


@dataclass
class ViewParams(node.CstNode[cst.Block]):
    """Parameters for input views."""

    max_msgs: int | expr.Expr
    manual_cursor: bool | expr.Expr
    no_dial: bool | expr.Expr
    skip_threshold: int | expr.Expr | None
    safety_margin: int | expr.Expr | None
    copy_inputs: bool | expr.Expr
    is_optional: bool | expr.Expr = False
    expose_seqno: bool = False
    use_device_ptr: bool | expr.Expr = False
    multi_connect: int | expr.Expr | None = None
    # Names of view params explicitly set by the user (via ``try_handle_param``).
    # Used to distinguish user-provided values from defaults for overlay and
    # validation logic (e.g., per-upstream overrides, aligner ``max_msgs``).
    _user_set: set[str] = field(default_factory=set)

    def is_user_set(self, param_name: str) -> bool:
        """Return True if ``param_name`` was explicitly set by the user."""
        return param_name in self._user_set

    @classmethod
    def make_default(cls: type[ViewParams], module: node.Module, cst_node: cst.Block | None = None) -> ViewParams:
        """Make a ViewParams with all values at defaults."""
        return ViewParams(
            module=module,
            cst_node=cst_node,
            max_msgs=_DEFAULT_VIEW_MAX_MSGS,
            manual_cursor=False,
            no_dial=False,
            skip_threshold=None,
            safety_margin=None,
            copy_inputs=False,
        )

    @classmethod
    def from_cst(cls: type[ViewParams], cst_node: cst.Block, module: node.Module) -> ViewParams:
        """Construct view parameters from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        result = ViewParams.make_default(module, cst_node=cst_node)
        seen_params: set[str] = set()
        for statement_cst in cst_node.children_statement():
            param_cst = statement_cst.child_definition()
            result._handle_param(param_cst, seen_params)  # noqa: SLF001 (result is also a ViewParams)
        return result

    def _validate_max_msgs_param(
        self, user_specified_max_msgs: int, name_span: SpanProtocol, terminals: TerminalSource
    ) -> None:
        """If the user specified max_msgs, validate it.

        It must not be equal to the default (1) to prevent ambiguity.
        It must be positive.
        """
        maybe_error = None
        if user_specified_max_msgs == _DEFAULT_VIEW_MAX_MSGS:
            maybe_error = (
                f"Input view has 'max_msgs' explicitly set to the default ({_DEFAULT_VIEW_MAX_MSGS})."
                + f"\nTo prevent ambiguity, this is not allowed. Please remove 'max_msgs: {_DEFAULT_VIEW_MAX_MSGS}' and trust the default."
            )
        elif user_specified_max_msgs <= 0:
            maybe_error = f"Input view has invalid 'max_msgs' {user_specified_max_msgs}. It must be at least 1."
        if maybe_error is not None:
            msg = maybe_error + format_line_with_error(
                name_span,
                terminals,
                self.module.module_id,
            )
            raise ValueError(msg)

    _KNOWN_PARAMS: Final[frozenset[str]] = frozenset(
        {
            "max_msgs",
            "manual_cursor",
            "no_dial",
            "skip_threshold",
            "safety_margin",
            "copy_inputs",
            "connect_optional",
            "use_device_ptr",
            "multi_connect",
        }
    )

    def try_handle_param(self, cst_node: cst.Definition, seen_params: set[str]) -> bool:  # noqa: PLR0911, C901 # Complexity from a branch for each param type
        """Try to handle a view parameter from a CST node.

        Returns True if the parameter was recognized and handled, False otherwise.
        Duplicate parameter detection only applies to recognized parameters;
        duplicates among unrecognized params are the caller's responsibility.

        Args:
            cst_node: The CST node for the parameter.
            seen_params: Set of already-seen parameter names (updated in-place on success).

        Returns:
            True if the parameter was a known view parameter, False otherwise.
        """
        assert self.module.terminals is not None
        name = get_span(name_span := cst_node.child_name().child_value(), self.module.terminals)

        if name not in self._KNOWN_PARAMS:
            return False

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
            self._validate_max_msgs_param(self._resolve_max_msgs(), name_span, self.module.terminals)
            self._user_set.add("max_msgs")
            return True
        if name == "manual_cursor":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.manual_cursor = value
            self._user_set.add("manual_cursor")
            return True
        if name == "no_dial":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.no_dial = value
            self._user_set.add("no_dial")
            return True
        if name == "skip_threshold":
            typesys.unify(clkbuiltins.UINT64, value.type_info)
            self.skip_threshold = value
            self._user_set.add("skip_threshold")
            return True
        if name == "safety_margin":
            typesys.unify(clkbuiltins.UINT64, value.type_info)
            self.safety_margin = value
            self._user_set.add("safety_margin")
            return True
        if name == "copy_inputs":
            typesys.unify(clkbuiltins.BOOL, value.type_info)

            self.copy_inputs = value
            self._user_set.add("copy_inputs")
            return True
        if name == "connect_optional":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.is_optional = value
            self._user_set.add("connect_optional")
            return True
        if name == "use_device_ptr":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.use_device_ptr = value
            self._user_set.add("use_device_ptr")
            return True
        if name == "multi_connect":
            typesys.unify(clkbuiltins.UINT64, value.type_info)
            self.multi_connect = value
            self._user_set.add("multi_connect")
            return True

        return False

    def _handle_param(self, cst_node: cst.Definition, seen_params: set[str]) -> None:
        """Handle a view parameter, raising on unrecognized params.

        For cog inputs, all params in the block must be view params.
        """
        if not self.try_handle_param(cst_node, seen_params):
            assert self.module.terminals is not None
            name = get_span(cst_node.child_name().child_value(), self.module.terminals)
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
        self._resolve_use_device_ptr()
        self._resolve_multi_connect()
        if self.multi_connect is not None and self.no_dial:
            msg = self.append_error_line("multi_connect and no_dial parameters are mutually exclusive.")
            raise ValueError(msg)

    def _resolve_max_msgs(self) -> int:
        if isinstance(self.max_msgs, expr.Expr):
            result = self.max_msgs.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = self.max_msgs.append_error_line(
                    f"Expected a DecimalValue for parameter max_msgs, but got {type(result)}",
                )
                raise TypeError(msg)
            self.max_msgs = primitive.unsigned_decimal_to_int(result)
        return self.max_msgs

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

    def _resolve_use_device_ptr(self) -> None:
        if isinstance(self.use_device_ptr, expr.Expr):
            result = self.use_device_ptr.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.use_device_ptr.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter use_device_ptr, but got {type(result)}",
                )
                raise TypeError(msg)
            self.use_device_ptr = primitive.value_to_bool(result)

    def _resolve_multi_connect(self) -> None:
        if isinstance(self.multi_connect, expr.Expr):
            result = self.multi_connect.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = self.multi_connect.append_error_line(
                    f"Expected a DecimalValue for parameter multi_connect, but got {type(result)}",
                )
                raise TypeError(msg)
            self.multi_connect = primitive.unsigned_decimal_to_int(result)


class MetricsLogType(Enum):
    """The type of metrics log."""

    non_redundant_telemetry = 0
    event = 1
    none = 2


@dataclass
class OutputDef(CogComponent, node.DocableEntity, node.CstNode[cst.Definition], MessageTypeMixin):
    """A definition of a Cog output."""

    is_generic: bool
    log_type: MetricsLogType = MetricsLogType.none
    is_optional: bool = False
    max_msgs_per_exec: int = 1

    @classmethod
    @override
    def from_statement(
        cls: type[OutputDef],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> OutputDef:
        """Construct an OutputDef IR node from a CST OutputDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without TerminalSource"
            raise ValueError(msg)

        if not isinstance(definition, dfl.Definition):
            msg = f"OutputDef cannot be constructed from {type(definition)}."
            raise TypeError(ctx.format_error(dfl.get_expr_span(definition), msg))

        cst_doc = definition.cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        message_type = definition.value
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)

        params = {}
        if definition.options is not None:
            for statement in definition.options.statements:
                if not isinstance(statement, dfl.Definition):
                    msg = f"OutputDef params cannot be constructed from {type(statement)}."
                    raise TypeError(ctx.format_error(dfl.get_expr_span(statement), msg))

                _handle_output_param(statement.cst_node, params, ctx.scope, module)

        # fmt: off
        result = cls(
            module=module,
            cst_node=definition.cst_node,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=ctx.scope,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=definition.name,
            type_info=clkbuiltins.COG_OUTPUT_TYPE,
            message_type=message_type,
            is_generic=is_generic,
            is_optional=params.get("connect_optional", False),
            max_msgs_per_exec=params.get("max_msgs_per_exec", 1),
        )
        # fmt: on
        ctx.scope.define(definition.name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.is_generic:
            # Resolution is done done when the cog is instantiated
            self.message_type = resolve_parameterized_schema_interface(self.module.context, self.message_type)
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)


def _handle_output_param(param: cst.Definition, params: dict[str, Any], scope: node.Scope, module: node.Module) -> None:
    assert module.terminals is not None
    param_name = get_span(name_span := param.child_name().child_value(), module.terminals)

    if param_name in params:
        msg = f"Parameter '{param_name}' specified more than once:\n" + format_line_with_error(
            name_span,
            module.terminals,
            module.module_id,
        )
        raise ValueError(msg)

    value = expr.Expr.from_cst(param.child_value(), module)
    if param_name == "connect_optional":
        typesys.unify(clkbuiltins.BOOL, value.type_info)
        value = node.resolve_names(value, scope)
        result = value.evaluate()
        if not isinstance(result, typesys.NamedValue):
            msg = (
                f"Expected a NamedValue (true or false) for parameter connect_optional, but got {type(result)}: "
                + format_line_with_error(
                    param.span,
                    module.terminals,
                    module.module_id,
                )
            )
            raise TypeError(msg)

        params[param_name] = primitive.value_to_bool(result)
    elif param_name == "max_msgs_per_exec":
        typesys.unify(clkbuiltins.UINT64, value.type_info)
        result = value.evaluate()
        if not isinstance(result, primitive.DecimalValue):
            msg = (
                f"Expected an integer value for parameter max_msgs_per_exec, but got {type(result)}: "
                + format_line_with_error(
                    param.span,
                    module.terminals,
                    module.module_id,
                )
            )
            raise TypeError(msg)

        max_msgs_value = primitive.unsigned_decimal_to_int(result)
        if max_msgs_value <= 1:
            msg = (
                "max_msgs_per_exec must be greater than 1. Omit max_msgs_per_exec entirely for single-message outputs.\n"
                + format_line_with_error(param.span, module.terminals, module.module_id)
            )
            raise ValueError(msg)

        params[param_name] = max_msgs_value
    else:
        msg = f"Unsupported cog output parameter '{param_name}'" + format_line_with_error(
            param.span,
            module.terminals,
            module.module_id,
        )
        raise NotImplementedError(msg)


@dataclass
class InputDef(CogComponent, node.DocableEntity, node.CstNode[cst.Definition], MessageTypeMixin):
    """A definition of a Cog input."""

    view_params: ViewParams
    is_generic: bool
    elements: list[InputDefElement] | None

    @classmethod
    @override
    def from_statement(
        cls: type[InputDef],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> InputDef:
        """Construct an InputDef IR node from a CST InputDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if not isinstance(definition, dfl.Definition):
            msg = f"OutputDef cannot be constructed from {type(definition)}."
            raise TypeError(ctx.format_error(dfl.get_expr_span(definition), msg))

        cst_doc = definition.cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        view_params_cst = definition.cst_node.maybe_block()
        view_params = (
            ViewParams.from_cst(view_params_cst, module) if view_params_cst else ViewParams.make_default(module)
        )
        message_type = definition.value
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        # fmt: off
        result = cls(
            module=module,
            cst_node=definition.cst_node,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=definition.name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=ctx.scope,
            message_type=message_type,
            type_info=clkbuiltins.COG_INPUT_TYPE,
            view_params=view_params,
            is_generic=is_generic,
            elements=None,
        )
        # fmt: on
        ctx.scope.define(definition.name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.is_generic:
            # Resolution is done done when the cog is instantiated
            self.message_type = resolve_parameterized_schema_interface(self.module.context, self.message_type)
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        self.view_params.resolve()
        if isinstance(self.view_params.multi_connect, int):
            self.elements = [
                InputDefElement.from_input_def(self, index) for index in range(self.view_params.multi_connect)
            ]


@dataclass
class InputDefElement(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.Definition], MessageTypeMixin):
    """A definition of a Cog multi_connect input element."""

    view_params: ViewParams
    is_generic: bool
    base_name: str
    index: int

    @classmethod
    def from_input_def(
        cls: type[InputDefElement],
        input_def: InputDef,
        index: int,
    ) -> InputDefElement:
        """Construct an element from an InputDef and index."""
        # fmt: off
        return cls(
            module=input_def.module,
            cst_node=input_def.cst_node,
            doc=input_def.doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=input_def.name + f"__{index}",
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=input_def.scope,
            message_type=input_def.message_type,
            type_info=input_def.type_info,
            view_params=input_def.view_params,
            is_generic=input_def.is_generic,
            base_name=input_def.name,
            index=index,
        )
        # fmt: on


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


@dataclass
class CogAlignedInputDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.Definition]):
    """A cog input that subscribes to an aligner's alignment output and upstream channels.

    Unlike a regular InputDef, this represents N+1 subscriptions: one for the alignment
    message and one for each of the aligner's upstream inputs. The expansion into actual
    endpoints happens at composition time.
    """

    aligned_type: expr.Expr | typesys.TypeDef
    view_params: ViewParams
    # Per-upstream consumer-view overrides, keyed by the aligner's upstream input name.
    # Missing entries are auto-sized by the compiler from the aligner-side view.
    upstream_view_overrides: dict[str, ViewParams] = field(default_factory=dict)

    @classmethod
    def from_cst(
        cls: type[CogAlignedInputDef],
        cst_node: cst.Definition,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> CogAlignedInputDef:
        """Construct a CogAlignedInputDef IR node from a CST CogAlignedInputDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        name = get_span(cst_node.child_name().child_value(), terminals=module.terminals)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        aligned_type = expr.Expr.from_cst(cst_node.child_value(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, aligned_type.type_info)

        view_params = ViewParams.make_default(module)
        upstream_view_overrides: dict[str, ViewParams] = {}
        block_cst = cst_node.maybe_block()
        if block_cst is not None:
            seen_params: set[str] = set()
            for statement_cst in block_cst.children_statement():
                if param_cst := statement_cst.maybe_definition():
                    view_params._handle_param(param_cst, seen_params)  # noqa: SLF001 Part of the same library.

            for statement_cst in block_cst.children_statement():
                override_cst = statement_cst.maybe_block()
                if override_cst is None:
                    continue

                upstream_name = get_span(
                    override_cst.child_name().child_value(),
                    terminals=module.terminals,
                )
                if upstream_name in upstream_view_overrides:
                    msg = (
                        f"Upstream override for '{upstream_name}' specified more than once:\n"
                        + format_line_with_error(
                            override_cst.child_name().child_value(),
                            module.terminals,
                            module.module_id,
                        )
                    )
                    raise ValueError(msg)
                override_params = ViewParams.from_cst(override_cst, module)
                if override_params.multi_connect is not None:
                    msg = "multi_connect option is not supported in aligned inputs." + format_line_with_error(
                        override_cst.child_name().child_value(),
                        module.terminals,
                        module.module_id,
                    )
                    raise ValueError(msg)
                upstream_view_overrides[upstream_name] = override_params
        # fmt: off
        result = cls(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=parent_scope,
            doc=doc,
            type_info=clkbuiltins.COG_ALIGNED_INPUT_TYPE,
            aligned_type=aligned_type,
            view_params=view_params,
            upstream_view_overrides=upstream_view_overrides,
            module=module,
            cst_node=cst_node,
        )
        # fmt: on
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Evaluate the aligned type expression and resolve view params.

        Does NOT validate that the expression evaluates to an Aligner —
        that validation is performed by the compiler (which can import aligner.py).
        """
        if isinstance(self.aligned_type, expr.Expr):
            evaluated = self.aligned_type.evaluate()
            if not isinstance(evaluated, typesys.TypeDef):
                msg = self.append_error_line("aligned_inputs type must be a type")
                raise TypeError(msg)
            self.aligned_type = evaluated
        self.view_params.resolve()
        for upstream_override in self.upstream_view_overrides.values():
            upstream_override.resolve()
