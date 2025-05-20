# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Cog-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING, Final, Generic, TypeVar

from clockwork.dsl import cst
from clockwork.dsl.ir import (
    clkbuiltins,
    expr,
    extern_type,
    node,
    primitive,
    schema_reg,
    typesys,
    units,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span, int_from_cst
from clockwork.dsl.ir.diagnostics import DiagnosticsDef
from clockwork.dsl.ir.message_type import MessageTypeMixin, resolve_schema_interface

if TYPE_CHECKING:
    from collections.abc import Callable


@dataclass
class Cog(typesys.TypeDef, node.CstNode[cst.Cog], typesys.InstantiatableEntity):
    """IR Node representing a Cog."""

    doc: node.Doc
    inner_scope: node.Scope
    resources: dict[str, ResourceDef]
    configs: dict[str, ConfigDef]
    states: dict[str, StateDef]
    diagnostics: dict[str, DiagnosticsDef]
    inputs: dict[str, InputDef]
    outputs: dict[str, OutputDef]
    conditions: dict[str, ConditionDef]
    rate_limits: dict[str, RateLimitSpec]
    execution_spec: ExecutionSpec
    simulation_options: SimulationOptions | None
    python_options: PythonOptions | None

    def is_init(self) -> bool:
        """Determine if this is an init cog."""
        return isinstance(self.execution_spec.condition, InitConditionExpr)

    def is_log(self) -> bool:
        """Determine if this is a log reading cog."""
        return isinstance(self.execution_spec.condition, LogConditionExpr)

    # We must disable C901 and PLR0912 here (function complexity, branches) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    @classmethod
    def from_cst(cls: type[Cog], parent_scope: node.Scope, cst_cog: cst.Cog, module: node.Module) -> Cog:  # noqa: PLR0912, C901 (see above)
        """Create an IR Cog from a CST Cog."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name = get_span(cst_cog.child_identifier().child_value(), module.terminals)
        scope = parent_scope.make_child_scope(name)

        if resources_block := cst_cog.maybe_resources_block():
            resources = {
                resource_def.name: resource_def
                for resource_def in (
                    ResourceDef.from_cst(resource_cst, module, scope)
                    for resource_cst in resources_block.children_resource_def()
                )
            }
        else:
            resources = {}

        if configs_block := cst_cog.maybe_configs_block():
            configs = {
                config_def.name: config_def
                for config_def in (
                    ConfigDef.from_cst(config_cst, module, scope) for config_cst in configs_block.children_config_def()
                )
            }
        else:
            configs = {}

        if states_block := cst_cog.maybe_states_block():
            states = {
                state_def.name: state_def
                for state_def in (
                    StateDef.from_cst(state_cst, module, scope) for state_cst in states_block.children_state_def()
                )
            }
        else:
            states = {}

        if diagnostics_block := cst_cog.maybe_diagnostics_block():
            if diagnostics_defs := list(diagnostics_block.children_diagnostics_def()):
                diagnostics_objs = [DiagnosticsDef.from_cst(i, module, scope) for i in diagnostics_defs]
            else:
                diagnostics_objs = [DiagnosticsDef.from_cst(diagnostics_block, module, scope)]
            diagnostics = {i.name: i for i in diagnostics_objs}
        else:
            diagnostics = {}

        if inputs_block := cst_cog.maybe_inputs_block():
            inputs = {
                input_def.name: input_def
                for input_def in (
                    InputDef.from_cst(input_cst, module, scope) for input_cst in inputs_block.children_input_def()
                )
            }
        else:
            inputs = {}

        if outputs_block := cst_cog.maybe_outputs_block():
            outputs = {
                output_def.name: output_def
                for output_def in (
                    OutputDef.from_cst(output_cst, module, scope) for output_cst in outputs_block.children_output_def()
                )
            }
        else:
            outputs = {}

        if simulation_options_block := cst_cog.maybe_simulation_options_block():
            simulation_options = SimulationOptions.from_cst(simulation_options_block, module)
        else:
            simulation_options = None

        if python_options_block := cst_cog.maybe_python_options_block():
            python_options = PythonOptions.from_cst(python_options_block, module)
        else:
            python_options = None

        exec_block = cst_cog.child_execution_block()
        exec_spec = exec_block.child_execute_when()
        name = get_span(cst_cog.child_identifier().child_value(), module.terminals)
        condition_defs, rate_limits = _unpack_execution_statements(exec_block, scope, module)

        return cls(
            type_info=clkbuiltins.TYPE_TYPE,
            cst_node=cst_cog,
            module=module,
            name=name,
            scope=parent_scope,
            doc=node.Doc.from_cst(cst_cog.child_doc(), module),
            inner_scope=scope,
            resources=resources,
            configs=configs,
            states=states,
            diagnostics=diagnostics,
            inputs=inputs,
            outputs=outputs,
            conditions={cond.name: cond for cond in condition_defs},
            rate_limits=rate_limits,
            execution_spec=ExecutionSpec.from_cst(exec_spec, module),
            simulation_options=simulation_options,
            python_options=python_options,
        )

    def _resolve_condition_expr(self, root: ConditionExpr) -> None:
        """Recursively resolve execute_expr, by resolving ConditionExpr within."""
        if isinstance(root, InitConditionExpr):
            return
        if isinstance(root, LogConditionExpr):
            return
        if isinstance(root, SimpleConditionExpr):
            if not isinstance(root.condition, ConditionDef):
                root.condition.resolve(self.scope)
            return
        if isinstance(root, BinaryConditionExpr):
            self._resolve_condition_expr(root.lhs)
            self._resolve_condition_expr(root.rhs)
            return

        msg = f"Type {type(root)} is not supported."
        raise TypeError(msg)

    # We must disable C901 here (function complexity) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    def resolve(self) -> None:  # noqa: C901 (see above)
        """Perform finalization of the IR."""
        for resource_def in self.resources.values():
            resource_def.resolve()
        for config_def in self.configs.values():
            config_def.resolve()
        for state_def in self.states.values():
            state_def.resolve()
        for input_def in self.inputs.values():
            input_def.resolve()
        for output_def in self.outputs.values():
            output_def.resolve()
        for condition_def in self.conditions.values():
            if isinstance(condition_def.condition, MessagesPresent):
                condition_def.condition.resolve()
        for limit_spec in self.rate_limits.values():
            limit_spec.resolve()
        if self.python_options:
            self.python_options.resolve()
        self._resolve_condition_expr(self.execution_spec.condition)
        if self.is_init():
            self._check_init_requirements()
        for diagnostics_def in self.diagnostics.values():
            diagnostics_def.resolve()
        self._check_safety_margins()

    def _check_safety_margins(self) -> None:
        new_max_inputs: set[str] = set()
        for condition in self.conditions.values():
            if isinstance(condition.condition, NewMessagePresent) and condition.condition.upper_bound is not None:
                new_max_inputs.add(condition.condition.input_name)

        for input_name, input_def in self.inputs.items():
            view_params = input_def.view_params
            if view_params.safety_margin is not None and input_name not in new_max_inputs:
                msg = input_def.append_error_line(
                    "Inputs may only specify a safety margin when associated with a new_message condition that uses the 'max' parameter"
                )
                raise ValueError(msg)
            if view_params.safety_margin is None and input_name in new_max_inputs:
                view_params.safety_margin = view_params.max_msgs

    def _check_init_requirements(self) -> None:
        base_msg = "Init cogs (as determined by 'execute when' expression)"
        if self.inputs:
            msg = node.append_error_line(
                self.cst_node.child_inputs_block() if self.cst_node else None,
                self.module,
                f"{base_msg} may not have message inputs.",
            )
            raise ValueError(msg)

    def make_instance(
        self, *, cst_node: cst.NewStmt | None, module: node.Module, scope: node.Scope, name: str, doc: node.Doc | None
    ) -> CogInstance:
        """Create an instance of the entity."""
        return CogInstance.make(cog_class=self, cst_node=cst_node, module=module, scope=scope, name=name, doc=doc)


def _unpack_execution_statements(
    exec_block: cst.ExecutionBlock, scope: node.Scope, module: node.Module
) -> tuple[list[ConditionDef], dict[str, RateLimitSpec]]:
    condition_defs = []
    rate_limits = {}
    for stmt in exec_block.children_execution_statement():
        if cond_def := stmt.maybe_condition_def():
            condition_defs.append(ConditionDef.from_cst(scope=scope, cst_def=cond_def, module=module))
        if rl_spec_cst := stmt.maybe_rate_limit_spec():
            spec = RateLimitSpec.from_cst(cst_spec=rl_spec_cst, module=module)
            if spec.output.identifier in rate_limits:
                msg = spec.append_error_line(
                    f"Outputs can only have one rate limit, but got multiple for {spec.output.identifier}"
                )
                raise ValueError(msg)
            rate_limits[spec.output.identifier] = spec

    return condition_defs, rate_limits


@dataclass
class ResourceDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.ResourceDef]):
    """A definition of a Cog resource."""

    @classmethod
    def from_cst(
        cls: type[ResourceDef],
        cst_node: cst.ResourceDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> ResourceDef:
        """Construct an ResourceDef IR node from a CST ResourceDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        name = get_span(
            cst_node.child_resource_memory_def().child_identifier().child_value(), terminals=module.terminals
        )
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_RESOURCE_TYPE,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""


@dataclass
class ResolvedConfigDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.ConfigDef]):
    """A definition of a Cog config."""

    message_type: schema_reg.InterfaceInfo
    source: ConfigDef


@dataclass
class ConfigDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.ConfigDef]):
    """A definition of a Cog config."""

    message_type: schema_reg.InterfaceInfo | expr.Expr
    resolved: ResolvedConfigDef | None = None

    @classmethod
    def from_cst(
        cls: type[ConfigDef],
        cst_node: cst.ConfigDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> ConfigDef:
        """Construct an ConfigDef IR node from a CST ConfigDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        message_type = expr.Expr.from_cst(cst_node.child_config_type(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            message_type=message_type,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> ResolvedConfigDef:
        """Perform finalization of the IR."""
        if self.resolved:
            return self.resolved
        self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        self.resolved = ResolvedConfigDef(
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            message_type=self.message_type,
            source=self,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedConfigDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ResolvedStateDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.StateDef]):
    """A definition of a Cog state."""

    message_type: schema_reg.InterfaceInfo | extern_type.ExternType
    params: ResolvedStateParams
    source: StateDef | None


@dataclass
class StateDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.StateDef]):
    """A definition of a Cog state."""

    message_type: schema_reg.InterfaceInfo | extern_type.ExternType | expr.Expr
    params: StateParams
    resolved: ResolvedStateDef | None = None

    @classmethod
    def from_cst(
        cls: type[StateDef],
        cst_node: cst.StateDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> StateDef:
        """Construct an StateDef IR node from a CST StateDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        params_cst = cst_node.maybe_state_block()
        params = StateParams.from_cst(params_cst, module) if params_cst else StateParams.make_default(module)
        message_type = expr.Expr.from_cst(cst_node.child_state_type(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            message_type=message_type,
            params=params,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> ResolvedStateDef:
        """Perform finalization of the IR."""
        if self.resolved:
            msg = "Attempting to resolve again"
            raise TypeError(msg)
        assert isinstance(self.message_type, expr.Expr)  # noqa: S101  (invariant)
        message_t = self.message_type.evaluate()
        if isinstance(message_t, extern_type.ExternType):
            self.message_type = message_t
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        resolved_params = self.params.resolve()
        assert isinstance(self.message_type, schema_reg.InterfaceInfo | extern_type.ExternType)  # noqa: S101 (invariant)
        self.resolved = ResolvedStateDef(
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            message_type=self.message_type,
            params=resolved_params,
            source=self,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedStateDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ResolvedStateParams(node.CstNode[cst.StateBlock]):
    """Parameters for state views."""

    mutable: bool
    source: StateParams | None


@dataclass
class StateParams(node.CstNode[cst.StateBlock]):
    """Parameters for state views."""

    mutable: bool | expr.Expr
    resolved: ResolvedStateParams | None = None

    @classmethod
    def make_default(
        cls: type[StateParams], module: node.Module, cst_node: cst.StateBlock | None = None
    ) -> StateParams:
        """Make a StateParams with all values at defaults."""
        return StateParams(module=module, cst_node=cst_node, mutable=False)

    @classmethod
    def from_cst(cls: type[StateParams], cst_node: cst.StateBlock, module: node.Module) -> StateParams:
        """Construct state parameters from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        result = StateParams.make_default(module, cst_node=cst_node)
        seen_params: set[str] = set()
        for param_cst in cst_node.children_state_block_param():
            result._handle_param(param_cst, seen_params)  # noqa: SLF001 (result is StateParams)
        return result

    def _handle_param(self, cst_node: cst.StateBlockParam, seen_params: set[str]) -> None:
        assert self.module.terminals is not None  # noqa: S101  (for mypy)
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
        if name == "mutable":
            typesys.unify(clkbuiltins.BOOL, value.type_info)
            self.mutable = value
            return
        msg = f"Unsupported view parameter '{name}'"
        raise NotImplementedError(msg)

    def resolve(self) -> ResolvedStateParams:
        """Perform finalization of the view params IR."""
        if self.resolved:
            return self.resolved
        if isinstance(self.mutable, expr.Expr):
            result = self.mutable.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.mutable.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter mutable, but got {type(result)}",
                )
                raise TypeError(msg)
            self.mutable = primitive.value_to_bool(result)
        self.resolved = ResolvedStateParams(
            module=self.module, cst_node=self.cst_node, mutable=self.mutable, source=self
        )
        return self.resolved


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
class ViewParams(node.CstNode[cst.InputBlock]):
    """Parameters for input views."""

    max_msgs: int | expr.Expr
    manual_cursor: bool | expr.Expr
    no_dial: bool | expr.Expr
    skip_threshold: int | expr.Expr | None
    safety_margin: int | expr.Expr | None
    copy_inputs: bool | expr.Expr

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

    def _handle_param(self, cst_node: cst.InputBlockParam, seen_params: set[str]) -> None:
        assert self.module.terminals is not None  # noqa: S101  (for mypy)
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


@dataclass
class OutputDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.OutputDef], MessageTypeMixin):
    """A definition of a Cog output."""

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
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_OUTPUT_TYPE,
            message_type=message_type,
        )
        parent_scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        self.message_type = resolve_schema_interface(self.module.context, self.message_type)


@dataclass
class ResolvedRateLimitSpec(node.CstNode[cst.RateLimitSpec]):
    """A rate limit applied to a cog's output."""

    output: OutputDef
    limit: int
    period_s: float


@dataclass
class RateLimitSpec(node.CstNode[cst.RateLimitSpec]):
    """A rate limit applied to a cog's output."""

    output: node.DeferredLookup[OutputDef]
    limit: int
    period: primitive.UnitLiteral
    resolved: ResolvedRateLimitSpec | None = None

    @classmethod
    def from_cst(cls: type[RateLimitSpec], cst_spec: cst.RateLimitSpec, module: node.Module) -> RateLimitSpec:
        """Construct a RateLimitSpec from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise RuntimeError(msg)

        if period_cst := cst_spec.child_period().maybe_unit_literal():
            period = primitive.UnitLiteral.from_child_cst(
                unit_literal=period_cst,
                parent_cst=cst_spec.child_period(),
                module=module,
            )
        else:
            msg = node.append_error_line(
                cst_spec.child_period(),
                module,
                f"Expected time literal for rate limit period, but got {type(cst_spec.child_period())}",
            )
            raise TypeError(msg)

        return cls(
            module=module,
            cst_node=cst_spec,
            output=node.DeferredLookup.make(
                cst_identifier=cst_spec.child_output(),
                terminals=module.terminals,
                expected_type=OutputDef,
            ),
            limit=int_from_cst(cst_spec.child_limit(), module.terminals),
            period=period,
        )

    def resolve(self) -> ResolvedRateLimitSpec:
        """Perform finalization."""
        if self.resolved is not None:
            msg = f"Attempt to resolve RateLimitSpec twice: {self}"
            raise RuntimeError(msg)

        if not isinstance(self.output, OutputDef):
            msg = self.append_error_line(f"Invalid rate limit output: {type(self.output)}")
            raise TypeError(msg)

        if not isinstance(self.period.unit, units.TimeUnit):
            msg = self.append_error_line(
                f"Rate limit period must have time units, but got {self.period.unit.base_unit()}"
            )
            raise TypeError(msg)

        self.resolved = ResolvedRateLimitSpec(
            module=self.module,
            cst_node=self.cst_node,
            output=self.output,
            limit=self.limit,
            period_s=float(self.period.value) * 10**self.period.unit.scale,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedRateLimitSpec:
        """Get the resolved spec."""
        if self.resolved is None:
            msg = "Attempt to access unresolved RateLimitSpec"
            raise RuntimeError(msg)

        return self.resolved


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
            assert self.upper_bound is not None  # noqa: S101  (for mypy)
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
class ConditionExpr(node.CstNode[cst.ConditionExpr]):
    """A condition expression.

    A condition expression is one or more Conditions combined with and/or operators.  This is represented as a tree of
    ConditionExprs.
    """

    @classmethod
    def from_cst(cls: type[ConditionExpr], cst_expr: cst.ConditionExpr, module: node.Module) -> ConditionExpr:
        """Construct the appropriate ConditionExpr subclass from a CST ConditionExpr."""
        if cst_expr.maybe_init() is not None:
            return InitConditionExpr.from_cst(cst_expr, module)
        if cst_expr.maybe_log() is not None:
            return LogConditionExpr.from_cst(cst_expr, module)
        if (identifier := cst_expr.maybe_identifier()) is not None:
            return SimpleConditionExpr.from_cst(identifier, cst_expr, module)
        if cst_expr.maybe_lhs() is not None:
            return BinaryConditionExpr.from_cst(cst_expr, module)
        if (sub_expr := cst_expr.maybe_subexpr()) is not None:
            return ConditionExpr.from_cst(sub_expr, module)
        raise NotImplementedError


@dataclass
class InitConditionExpr(ConditionExpr):
    """A simple condition expression which reference a single condition."""

    @classmethod
    def from_cst(
        cls: type[InitConditionExpr],
        cst_expr: cst.ConditionExpr,
        module: node.Module,
    ) -> InitConditionExpr:
        """Construct from a CST node."""
        return cls(
            module=module,
            cst_node=cst_expr,
        )


@dataclass
class LogConditionExpr(ConditionExpr):
    """A simple condition expression which reference a single condition."""

    @classmethod
    def from_cst(
        cls: type[LogConditionExpr],
        cst_expr: cst.ConditionExpr,
        module: node.Module,
    ) -> LogConditionExpr:
        """Construct from a CST node."""
        return cls(
            module=module,
            cst_node=cst_expr,
        )


@dataclass
class SimpleConditionExpr(ConditionExpr):
    """A simple condition expression which reference a single condition."""

    condition: node.Deferrable[ConditionDef]

    @classmethod
    def from_cst(  # type: ignore[override]
        cls: type[SimpleConditionExpr],
        cst_identifier: cst.Identifier,
        parent_expr: cst.ConditionExpr,
        module: node.Module,
    ) -> SimpleConditionExpr:
        """Construct from a CST node."""
        return cls(
            module=module,
            cst_node=parent_expr,
            condition=node.DeferredLookup.make(
                cst_identifier=cst_identifier,
                terminals=module.terminals,
                expected_type=ConditionDef,
            ),
        )


class ConditionOp(Enum):
    """Operators used in Condition expressions."""

    AND = "and"
    OR = "or"


@dataclass
class BinaryConditionExpr(ConditionExpr):
    """A condition expression with two sub-expressions joined by AND or OR."""

    lhs: ConditionExpr
    rhs: ConditionExpr
    op: ConditionOp

    @classmethod
    def from_cst(
        cls: type[BinaryConditionExpr],
        cst_expr: cst.ConditionExpr,
        module: node.Module,
    ) -> BinaryConditionExpr:
        """Construct from a CST node."""
        if cst_expr.maybe_op_and() is not None:
            op = ConditionOp.AND
        elif cst_expr.maybe_op_or() is not None:
            op = ConditionOp.OR
        else:
            msg = f"Unexpected operator in ConditionExpr: {cst_expr}"
            raise ValueError(msg)
        return cls(
            cst_node=cst_expr,
            lhs=ConditionExpr.from_cst(cst_expr.child_lhs(), module),
            rhs=ConditionExpr.from_cst(cst_expr.child_rhs(), module),
            op=op,
            module=module,
        )


@dataclass
class ExecutionSpec(node.DocableEntity, node.CstNode[cst.ExecuteWhen]):
    """An execution condition specification for a Cog."""

    condition: ConditionExpr

    @classmethod
    def from_cst(cls: type[ExecutionSpec], cst_spec: cst.ExecuteWhen, module: node.Module) -> ExecutionSpec:
        """Create an ExecutionSpec from a CST ExecuteWhen node."""
        cst_doc = cst_spec.maybe_doc()
        cst_expr = cst_spec.child_condition_expr()
        return cls(
            module=module,
            cst_node=cst_spec,
            doc=(node.Doc.from_cst(cst_doc, module) if cst_doc else None),
            condition=ConditionExpr.from_cst(cst_expr, module),
        )


@dataclass
class SimulationOptions(node.CstNode[cst.SimulationOptionsBlock]):
    """A specification of a cog's simulation options."""

    execution_duration: primitive.UnitValue

    @classmethod
    def from_cst(
        cls: type[SimulationOptions],
        cst_node: cst.SimulationOptionsBlock,
        module: node.Module,
    ) -> SimulationOptions:
        """Construct an ConfigDef IR node from a CST ConfigDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        execution = cst_node.child_sim_options().child_execution_duration()
        if (unit_literal := execution.child_value().maybe_unit_literal()) is None:
            msg = "Unitless literals not allowed in Duration typed context.\n" + format_line_with_error(
                execution.child_value().span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)

        execution_duration = primitive.UnitLiteral.from_child_cst(unit_literal, execution.child_value(), module)
        if execution_duration.type_info is not clkbuiltins.DURATION:
            msg = '"execution_duration" parameter must have Duration type.\n"' + format_line_with_error(
                unit_literal.span, module.terminals, module.module_id
            )
            raise TypeError(msg)
        return cls(module=module, cst_node=cst_node, execution_duration=execution_duration)


@dataclass
class PythonOptions(node.CstNode[cst.PythonOptionsBlock]):
    """A specification of a cog's python options."""

    python_dial_class_name: expr.Expr | str
    python_impl_class_name: expr.Expr | str

    @classmethod
    def from_cst(
        cls: type[PythonOptions],
        cst_node: cst.PythonOptionsBlock,
        module: node.Module,
    ) -> PythonOptions:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        python_dial_class_name = expr.Expr.from_cst(cst_node.child_python_dial_option().child_value(), module)
        python_impl_class_name = expr.Expr.from_cst(cst_node.child_python_impl_option().child_value(), module)
        return cls(
            module=module,
            cst_node=cst_node,
            python_dial_class_name=python_dial_class_name,
            python_impl_class_name=python_impl_class_name,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.python_dial_class_name, expr.Expr):
            msg = f"Attempt to resolve CppPythonCog twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        dial_val = self.python_dial_class_name.evaluate()
        assert isinstance(self.python_dial_class_name, expr.Expr)  # noqa: S101 (for mypy)
        if not isinstance(dial_val, primitive.StringValue):
            msg = node.append_error_line(self.cst_node, self.module, "Invalid python dial class definition")
            raise TypeError(msg)
        assert isinstance(self.python_impl_class_name, expr.Expr)  # noqa: S101 (for mypy)
        impl_val = self.python_impl_class_name.evaluate()
        if not isinstance(impl_val, primitive.StringValue):
            msg = node.append_error_line(self.cst_node, self.module, "Invalid python dial class definition")
            raise TypeError(msg)
        self.python_dial_class_name = dial_val.value
        self.python_impl_class_name = impl_val.value


@dataclass
class CogInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity):
    """An IR Value representing an instance of a Cog."""

    cog_class: Cog
    inner_scope: node.Scope
    members: list[
        CogInstanceMember[ResourceDef | ConfigDef | StateDef | InputDef | OutputDef | ConditionDef | DiagnosticsDef]
    ]

    # We have to suppress PLR0913 (too many args) because this is already an extremely simple function that can't be split but still needs all these args. The args are all different types so mypy will catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
    @classmethod
    def make(  # noqa: PLR0913 (see above)
        cls: type[CogInstance],
        *,
        cog_class: Cog,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> CogInstance:
        """Factory function for CogInstance."""
        if cog_class.inner_scope.parent is None:
            msg = node.append_error_line(
                cst_node=cst_node, module=module, msg="Attempt to instantiate Cog without parent scope"
            )
            raise RuntimeError(msg)
        inner_scope = cog_class.inner_scope.parent.make_dynamic_scope(path_parent=scope, name=name)
        result = cls(
            name=name,
            scope=scope,
            inner_scope=inner_scope,
            type_info=clkbuiltins.COG_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            cog_class=cog_class,
            members=[],
        )
        for entity in cog_class.inner_scope.names.values():
            result._make_member(entity)
        return result

    def _make_member(
        self, entity: node.NamedEntity
    ) -> (
        CogInstanceMember[ResourceDef | ConfigDef | StateDef | InputDef | OutputDef | ConditionDef | DiagnosticsDef]
        | None
    ):
        result: (
            CogInstanceMember[ResourceDef | ConfigDef | StateDef | InputDef | OutputDef | ConditionDef | DiagnosticsDef]
            | None
        ) = None
        if isinstance(entity, ResourceDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_RESOURCE_INSTANCE_TYPE)
        elif isinstance(entity, ConfigDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_CONFIG_INSTANCE_TYPE)
        elif isinstance(entity, StateDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_STATE_INSTANCE_TYPE)
        elif isinstance(entity, InputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_INPUT_INSTANCE_TYPE)
        elif isinstance(entity, OutputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_OUTPUT_INSTANCE_TYPE)
        elif isinstance(entity, ConditionDef):
            if isinstance(entity.condition, TimeSinceLastExec):
                result = CogInstanceMember.make(self, entity, clkbuiltins.COG_CONDITION_INSTANCE_TYPE)
            # Other condition types do not generate connectable endpoints
        elif isinstance(entity, DiagnosticsDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_DIAGNOSTICS_INSTANCE_TYPE)
        else:
            msg = self.append_error_line(f"Unrecognized instance member {entity}")
            raise NotImplementedError(msg)
        if result:
            self.inner_scope.define(entity.name, result, self.module.terminals)
            self.members.append(result)
        return result

    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a definition in the membership entity.

        Returns:
            The entity with that name, or None if not found.
        """
        member = self.inner_scope.lookup(name)
        if member is not None:
            assert isinstance(member, typesys.Value)  # noqa: S101  (invariant)
            return member
        return None


T = TypeVar("T", bound=node.NamedEntity)


@dataclass
class CogInstanceMember(typesys.NamedAttribute, Generic[T]):
    """Represents a member (e.g., input/output/condition) of a Cog instance."""

    cog_instance: CogInstance
    member: T

    @classmethod
    def make(
        cls: type[CogInstanceMember[T]], cog_instance: CogInstance, member: T, type_info: typesys.TypeVal
    ) -> CogInstanceMember[T]:
        """Create an input instance."""
        return cls(member.name, cog_instance.inner_scope, type_info, cog_instance, member)
