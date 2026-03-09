# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Cog-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Generic, TypeVar

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    expr,
    extern_type,
    interface,
    node,
    primitive,
    representation,
    schema,
    schema_reg,
    typesys,
    units,
)
from clockwork.dsl.ir.cog_components import (
    ConditionDef,
    InputDef,
    MessagesPresent,
    MetricsLogType,
    MetricsOutputDef,
    NewMessagePresent,
    OutputDef,
    TimeSinceLastExec,
)
from clockwork.dsl.ir.cog_metrics_schema_generation import (
    DEFAULT_METRICS_BATCH_SIZE,
    generate_event_metrics_schema,
    generate_telemetry_metrics_schema,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span, int_from_cst
from clockwork.dsl.ir.diagnostics import DiagnosticsDef, InfraDiagnosticsDef
from clockwork.dsl.ir.message_type import resolve_schema_interface
from clockwork.dsl.ir.report_group import ReportGroupDef, ReportGroupInstance
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable


@dataclass
class Cog(
    typesys.TypeDef, node.CstNode[cst.Cog | cst.PythonCog], typesys.InstantiatableEntity, typesys.MembershipEntity
):
    """IR Node representing a Cog."""

    doc: node.Doc
    inner_scope: node.Scope
    resources: dict[str, ResourceDef] = field(repr=False)
    configs: dict[str, ConfigDef] = field(repr=False)
    states: dict[str, StateDef] = field(repr=False)
    diagnostics: dict[str, DiagnosticsDef] = field(repr=False)
    infra_diagnostics: InfraDiagnosticsDef = field(repr=False)
    inputs: dict[str, InputDef] = field(repr=False)
    outputs: dict[str, OutputDef] = field(repr=False)
    metrics_outputs: dict[str, MetricsOutputDef] = field(repr=False)
    conditions: dict[str, ConditionDef] = field(repr=False)
    rate_limits: dict[str, RateLimitSpec] = field(repr=False)
    execution_spec: ExecutionSpec = field(repr=False)
    simulation_options: SimulationOptions | None = field(repr=False)
    python_options: PythonOptions | None = field(repr=False)
    metrics_options: MetricsOptions = field(repr=False)
    attributes: node.ClkAttributes | None = field(repr=False)
    report_groups: dict[str, ReportGroupDef] = field(repr=False, default_factory=dict)

    def is_init(self) -> bool:
        """Determine if this is an init cog."""
        return isinstance(self.execution_spec.condition, InitConditionExpr)

    def is_log(self) -> bool:
        """Determine if this is a log reading cog."""
        return isinstance(self.execution_spec.condition, LogConditionExpr)

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a member by name.

        Returns:
            The entity with that name or None if not found.
        """
        member = self.inner_scope.lookup(name, recursive=False)
        if member is not None:
            assert isinstance(member, typesys.Value), f"Expected member to be a Value, got {member.name}"
            return member
        return None

    @override
    def get_module(self) -> node.Module:
        return self.module

    # We must disable C901, PLR0912 and PLR0915 here (function complexity, branches, function size)
    # because we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    @classmethod
    def from_cst(  # noqa: C901, PLR0912, PLR0915 (see above)
        cls: type[Cog], parent_scope: node.Scope, cst_cog: cst.Cog | cst.PythonCog, module: node.Module
    ) -> Cog:
        """Create an IR Cog from a CST Cog."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name = get_span(cst_cog.child_identifier().child_value(), module.terminals)
        scope = parent_scope.make_child_scope(name)
        attributes = module.handle_outer_attrs(cst_cog.maybe_clk_outer_attrs())

        if (
            isinstance(cst_cog, cst.PythonCog)
            and module.generates
            and node.GenerateTarget.py_cog not in module.generates
        ):
            msg = node.append_error_line(cst_cog, module, "Module must generate py_cpg to define python cogs.")
            raise ValueError(msg)
        if isinstance(cst_cog, cst.Cog) and module.generates and node.GenerateTarget.cpp_cog not in module.generates:
            msg = node.append_error_line(cst_cog, module, "Module must generate cpp_cpg to define cogs.")
            raise ValueError(msg)

        if resources_block := cst_cog.child_cog_blocks().maybe_resources_block():
            resources = {
                resource_def.name: resource_def
                for resource_def in (
                    ResourceDef.from_cst(resource_cst, module, scope)
                    for resource_cst in resources_block.children_resource_def()
                )
            }
        else:
            resources = {}

        if configs_block := cst_cog.child_cog_blocks().maybe_configs_block():
            configs = {
                config_def.name: config_def
                for config_def in (
                    ConfigDef.from_cst(config_cst, module, scope) for config_cst in configs_block.children_config_def()
                )
            }
        else:
            configs = {}

        if states_block := cst_cog.child_cog_blocks().maybe_states_block():
            states = {
                state_def.name: state_def
                for state_def in (
                    StateDef.from_cst(state_cst, module, scope) for state_cst in states_block.children_state_def()
                )
            }
        else:
            states = {}

        if report_groups_cst := cst_cog.child_cog_blocks().maybe_report_groups():
            if isinstance(cst_cog, cst.PythonCog):
                msg = node.append_error_line(
                    report_groups_cst, module, "Report groups are not suppported in python cogs"
                )
                raise ValueError(msg)
            report_groups = {
                report_group_def.name: report_group_def
                for report_group_def in (
                    ReportGroupDef.from_cst(report_groups_cst, module, scope)
                    for report_groups_cst in report_groups_cst.children_report_group()
                )
            }
        else:
            report_groups = {}

        if diagnostics_block := cst_cog.child_cog_blocks().maybe_diagnostics_block():
            if isinstance(cst_cog, cst.PythonCog):
                msg = node.append_error_line(diagnostics_block, module, "Diagnostics are not suppported in python cogs")
                raise ValueError(msg)
            if diagnostics_defs := list(diagnostics_block.children_diagnostics_def()):
                diagnostics_objs = [DiagnosticsDef.from_cst(i, module, scope) for i in diagnostics_defs]
            else:
                diagnostics_objs = [DiagnosticsDef.from_cst(diagnostics_block, module, scope)]
            diagnostics = {i.name: i for i in diagnostics_objs}
        else:
            diagnostics = {}

        if inputs_block := cst_cog.child_cog_blocks().maybe_inputs_block():
            inputs = {
                input_def.name: input_def
                for input_def in (
                    InputDef.from_cst(input_cst, module, scope) for input_cst in inputs_block.children_input_def()
                )
            }
        else:
            inputs = {}

        if outputs_block := cst_cog.child_cog_blocks().maybe_outputs_block():
            outputs = {
                output_def.name: output_def
                for output_def in (
                    OutputDef.from_cst(output_cst, module, scope) for output_cst in outputs_block.children_output_def()
                )
            }
        else:
            outputs = {}

        infra_diagnostics = InfraDiagnosticsDef.make(module, scope, inputs.values(), outputs.values())

        if simulation_options_block := cst_cog.child_cog_blocks().maybe_simulation_options_block():
            simulation_options = SimulationOptions.from_cst(simulation_options_block, module)
        else:
            simulation_options = None

        if python_options_block := cst_cog.child_cog_blocks().maybe_python_options_block():
            if attributes is not None:
                msg = node.append_error_line(
                    python_options_block,
                    module,
                    "Python options are not supported when the generates attribute is set, use `python_cog` instead",
                )
                raise ValueError(msg)
            if isinstance(cst_cog, cst.PythonCog):
                msg = node.append_error_line(
                    python_options_block, module, "Python options are not suppported in python cogs"
                )
                raise ValueError(msg)
            python_options = PythonOptions.from_cst(python_options_block, module, name)
        elif isinstance(cst_cog, cst.PythonCog):
            python_options = PythonOptions(
                module=module,
                cst_node=None,
                python_dial_class_name=None,
                python_impl_class_name=None,
                cog_name=name,
            )
        else:
            python_options = None

        exec_block = cst_cog.child_cog_blocks().child_execution_block()
        exec_spec = exec_block.child_execute_when()
        name = get_span(cst_cog.child_identifier().child_value(), module.terminals)
        condition_defs, rate_limits = _unpack_execution_statements(exec_block, scope, module)

        if metrics_options_block := cst_cog.child_cog_blocks().maybe_metrics_options_block():
            metrics_options = MetricsOptions.from_cst(metrics_options_block, module)
        else:
            metrics_options = MetricsOptions(
                metrics_enabled=True, batch_size=DEFAULT_METRICS_BATCH_SIZE, module=module, cst_node=None
            )
        metrics_outputs = {}
        if metrics_options.metrics_enabled:
            metrics_outputs = define_metrics_outputs(scope, module)

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
            report_groups=report_groups,
            diagnostics=diagnostics,
            infra_diagnostics=infra_diagnostics,
            inputs=inputs,
            outputs=outputs,
            metrics_outputs=metrics_outputs,
            conditions={cond.name: cond for cond in condition_defs},
            rate_limits=rate_limits,
            execution_spec=ExecutionSpec.from_cst(exec_spec, module),
            simulation_options=simulation_options,
            python_options=python_options,
            metrics_options=metrics_options,
            attributes=attributes,
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
    def resolve(self) -> None:  # noqa: C901, PLR0912 (see above)
        """Perform finalization of the IR."""
        for resource_def in self.resources.values():
            resource_def.resolve()
        for config_def in self.configs.values():
            config_def.resolve()
        for state_def in self.states.values():
            state_def.resolve()
        for report_group in self.report_groups.values():
            report_group.resolve(self.inner_scope, self.name)
        for input_def in self.inputs.values():
            input_def.resolve()
        for output_def in self.outputs.values():
            output_def.resolve()
        if self.metrics_outputs:
            self._resolve_metrics_outputs()
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
        self.infra_diagnostics.resolve()
        self._check_safety_margins()

    def _resolve_metrics_outputs(self) -> None:
        """Resolve the metrics outputs."""
        telemetry_metrics_schema = generate_telemetry_metrics_schema(
            self.name, self.inputs, self.outputs, self.conditions, self.module
        )
        event_metrics_schema, metrics_schema_deps, generated_enums = generate_event_metrics_schema(
            self.name, self.metrics_options.batch_size, self.inputs, self.outputs, self.conditions, self.module
        )
        for metrics_output_def in self.metrics_outputs.values():
            if metrics_output_def.log_type == MetricsLogType.telemetry:
                metrics_output_def.resolve(telemetry_metrics_schema, [], [])
            elif metrics_output_def.log_type == MetricsLogType.event:
                metrics_output_def.resolve(event_metrics_schema, metrics_schema_deps, generated_enums)
            else:
                msg = self.append_error_line(
                    f"Attempted to resolve metrics output with an invalid log type: {metrics_output_def.log_type}"
                )
                raise ValueError(msg)

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
                # Ask for the default when we get connected to a channel
                view_params.safety_margin = -1

    def _check_init_requirements(self) -> None:
        base_msg = "Init cogs (as determined by 'execute when' expression)"
        if self.inputs:
            msg = node.append_error_line(
                self.cst_node.child_cog_blocks().child_inputs_block() if self.cst_node else None,
                self.module,
                f"{base_msg} may not have message inputs.",
            )
            raise ValueError(msg)

    @override
    def make_instance(
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> CogInstance:
        """Create an instance of the entity."""
        return CogInstance.make(cog_class=self, cst_node=cst_node, module=module, scope=scope, name=name, doc=doc)

    def generated_repr(self) -> Iterable[representation.ReprInstantiation]:
        """Get all generated representations from the Cog (metrics outputs only)."""
        representations = []
        for metrics_output in self.metrics_outputs.values():
            representations.extend(metrics_output.get_generated_repr_instantiations())
        return representations

    def generated_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Get all generated interfaces from the Cog (metrics outputs only)."""
        interfaces = []
        for metrics_output in self.metrics_outputs.values():
            interfaces.extend(metrics_output.get_generated_interfaces())
        return interfaces

    def generated_schemas(self) -> Iterable[schema.Schema]:
        """Get all generated schemas from the Cog (metrics outputs only)."""
        generated_schemas = []
        for metrics_output in self.metrics_outputs.values():
            generated_schemas.extend(metrics_output.get_generated_schemas())
        return generated_schemas

    def generated_enums(self) -> Iterable[clkenum.ClkEnum]:
        """Get all generated enums from the Cog (metrics outputs only)."""
        generated_enums = []
        for metrics_output in self.metrics_outputs.values():
            generated_enums.extend(metrics_output.enums)
        return generated_enums

    def generated_report_group_repr(self) -> Iterable[representation.ReprInstantiation]:
        """Get all generated representations from report groups."""
        representations = []
        for report_group in self.report_groups.values():
            representations.extend(report_group.generated_representations)
        return representations

    def generated_report_group_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Get all generated interfaces from report groups."""
        interfaces = []
        for report_group in self.report_groups.values():
            interfaces.extend(report_group.generated_interfaces)
        return interfaces

    def generated_report_group_schemas(self) -> Iterable[schema.Schema]:
        """Get all generated schemas from report groups."""
        generated_schemas = []
        for report_group in self.report_groups.values():
            generated_schemas.extend(report_group.generated_schemas)
        return generated_schemas


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
        assert isinstance(self.message_type, expr.Expr)
        message_t = self.message_type.evaluate()
        if isinstance(message_t, extern_type.ExternType):
            self.message_type = message_t
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        resolved_params = self.params.resolve()
        assert isinstance(self.message_type, schema_reg.InterfaceInfo | extern_type.ExternType)
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


def define_metrics_outputs(parent_scope: node.Scope, module: node.Module) -> dict[str, MetricsOutputDef]:
    """Define standard metrics outputs for a Cog."""
    result = {}
    telemetry_metrics = MetricsOutputDef(
        "cog_telemetry_metrics",
        parent_scope,
        clkbuiltins.COG_METRICS_OUTPUT_TYPE,
        module,
        log_type=MetricsLogType.telemetry,
        message_type=None,
        message_interface_type=None,
    )
    result[telemetry_metrics.name] = telemetry_metrics
    event_metrics = MetricsOutputDef(
        "cog_event_metrics",
        parent_scope,
        clkbuiltins.COG_METRICS_OUTPUT_TYPE,
        module,
        log_type=MetricsLogType.event,
        message_type=None,
        message_interface_type=None,
    )
    result[event_metrics.name] = event_metrics
    parent_scope.define(event_metrics.name, event_metrics, module.terminals)
    parent_scope.define(telemetry_metrics.name, telemetry_metrics, module.terminals)
    return result


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
    @override
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
    @override
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
    def from_cst(  # pyright: ignore[reportImplicitOverride, reportIncompatibleMethodOverride]
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
    @override
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
class MetricsOptions(node.CstNode[cst.MetricsOptionsBlock]):
    """A specification of a cog's metrics options."""

    metrics_enabled: bool
    batch_size: int

    @classmethod
    def from_cst(
        cls: type[MetricsOptions],
        cst_node: cst.MetricsOptionsBlock,
        module: node.Module,
    ) -> MetricsOptions:
        """Construct an ConfigDef IR node from a CST ConfigDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        metrics_enabled = True
        batch_size_value = DEFAULT_METRICS_BATCH_SIZE
        metrics_option = cst_node.maybe_metrics_enabled_option()
        if metrics_option:
            metrics_value = metrics_option.child_boolean()
            metrics_enabled = metrics_value.maybe_true() is not None

        batch_size_option = cst_node.maybe_metrics_batch_size_option()
        if batch_size_option:
            decimal_literal = batch_size_option.child_value().maybe_number()
            if decimal_literal is None:
                msg = "Batch size must be a decimal literal.\n" + format_line_with_error(
                    batch_size_option.child_value().span, module.terminals, module.module_id
                )
                raise TypeError(msg)
            batch_size_value = primitive.DecimalLiteral.from_child_cst(
                decimal_literal, batch_size_option.child_value(), module
            ).value

        return cls(module=module, cst_node=cst_node, metrics_enabled=metrics_enabled, batch_size=int(batch_size_value))


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

    cog_name: str
    python_dial_class_name: expr.Expr | str | None
    python_impl_class_name: expr.Expr | str | None

    @classmethod
    def from_cst(
        cls: type[PythonOptions],
        cst_node: cst.PythonOptionsBlock,
        module: node.Module,
        cog_name: str,
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
            cog_name=cog_name,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if isinstance(self.python_dial_class_name, str):
            msg = f"Attempt to resolve CppPythonCog twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        if isinstance(self.python_dial_class_name, expr.Expr):
            dial_val = self.python_dial_class_name.evaluate()
            if not isinstance(dial_val, primitive.StringValue):
                assert self.cst_node is not None
                msg = node.append_error_line(self.cst_node, self.module, "Invalid python dial class definition")
                raise TypeError(msg)
            self.python_dial_class_name = dial_val.value
        else:
            self.python_dial_class_name = (
                self.module.module_id.name.replace("::", ".") + f"_clk_py_dial.{self.cog_name}Dial"
            )
        if isinstance(self.python_impl_class_name, expr.Expr):
            impl_val = self.python_impl_class_name.evaluate()
            if not isinstance(impl_val, primitive.StringValue):
                assert self.cst_node is not None
                msg = node.append_error_line(self.cst_node, self.module, "Invalid python dial class definition")
                raise TypeError(msg)
            self.python_impl_class_name = impl_val.value
        else:
            self.python_impl_class_name = (
                self.module.module_id.name.replace("::", ".") + f"_clk_py_impl.{self.cog_name}Impl"
            )


@dataclass
class CogInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity):
    """An IR Value representing an instance of a Cog."""

    cog_class: Cog
    inner_scope: node.Scope
    members: list[
        CogInstanceMember[
            ResourceDef
            | ConfigDef
            | StateDef
            | InputDef
            | OutputDef
            | ConditionDef
            | DiagnosticsDef
            | InfraDiagnosticsDef
            | MetricsOutputDef
            | ReportGroupDef
        ]
    ] = field(repr=False)
    report_group_instances: list[ReportGroupInstance] = field(default_factory=list, repr=False)

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
        for report_group in cog_class.report_groups.values():
            # Special handling for report groups. These instances are distinct from other members because they by
            # necessity only can know their contained signal instances at instantiation time, therefore
            # CogInstanceMember[T] does not suffice here.
            rg_instance = report_group.make_instance(result.fqn)
            result.report_group_instances.append(rg_instance)

        for entity in cog_class.inner_scope.names.values():
            result._make_member(entity)
        result._make_member(cog_class.infra_diagnostics)
        return result

    def _make_member(  # noqa: C901 This is effectively a factory with a switch statement
        self, entity: node.NamedEntity
    ) -> (
        CogInstanceMember[
            ResourceDef
            | ConfigDef
            | StateDef
            | InputDef
            | OutputDef
            | ConditionDef
            | DiagnosticsDef
            | InfraDiagnosticsDef
            | MetricsOutputDef
            | ReportGroupDef
        ]
        | None
    ):
        result: (
            CogInstanceMember[
                ResourceDef
                | ConfigDef
                | StateDef
                | InputDef
                | OutputDef
                | ConditionDef
                | DiagnosticsDef
                | InfraDiagnosticsDef
                | MetricsOutputDef
                | ReportGroupDef
            ]
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
        elif isinstance(entity, MetricsOutputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_METRICS_OUTPUT_INSTANCE_TYPE)
        elif isinstance(entity, ConditionDef):
            if isinstance(entity.condition, TimeSinceLastExec):
                result = CogInstanceMember.make(self, entity, clkbuiltins.COG_CONDITION_INSTANCE_TYPE)
            # Other condition types do not generate connectable endpoints
        elif isinstance(entity, DiagnosticsDef | InfraDiagnosticsDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_DIAGNOSTICS_INSTANCE_TYPE)
        elif isinstance(entity, ReportGroupDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_REPORT_GROUP_INSTANCE_TYPE)
        else:
            msg = self.append_error_line(f"Unrecognized instance member {entity}")
            raise NotImplementedError(msg)
        if result:
            self.inner_scope.define(entity.name, result, self.module.terminals)
            self.members.append(result)
        return result

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a definition in the membership entity.

        Returns:
            The entity with that name, or None if not found.
        """
        member = self.inner_scope.lookup(name, recursive=False)
        if member is not None:
            assert isinstance(member, typesys.Value)
            return member
        return None


T = TypeVar("T", bound=node.NamedEntity)


@dataclass
class CogInstanceMember(typesys.NamedAttribute, Generic[T]):
    """Represents a member (e.g., input/output/condition) of a Cog instance."""

    cog_instance: CogInstance
    member: T = field(repr=False)

    @classmethod
    def make(
        cls: type[CogInstanceMember[T]], cog_instance: CogInstance, member: T, type_info: typesys.TypeVal
    ) -> CogInstanceMember[T]:
        """Create an input instance."""
        return cls(member.name, cog_instance.inner_scope, type_info, cog_instance, member)
