# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Cog-related IR nodes."""

from __future__ import annotations

from copy import copy
from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Final, Generic, Mapping, TypeAlias, TypeVar, cast

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    dfl,
    dfl_analysis,
    dfl_types,
    expr,
    extern_type,
    fmt_string,
    interface,
    node,
    primitive,
    representation,
    schema,
    schema_reg,
    statement,
    typesys,
    units,
)
from clockwork.dsl.ir.cog_components import (
    AnyMessagePresent,
    CogAlignedInputDef,
    CogComponent,
    ConditionDef,
    DynamicTimer,
    InputDef,
    MessagesPresent,
    MetricsLogType,
    MetricsOutputDef,
    NewMessagePresent,
    OutputDef,
    TimeSinceLastExec,
)
from clockwork.dsl.ir.cog_metrics_report_groups import generate_cog_metrics_report_groups
from clockwork.dsl.ir.cog_metrics_schema_generation import (
    DEFAULT_METRICS_BATCH_SIZE,
    generate_event_metrics_schema,
    generate_telemetry_metrics_schema,
)
from clockwork.dsl.ir.cog_parameters import (
    CogParameter,
    CogParameterRef,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span, int_from_cst
from clockwork.dsl.ir.diagnostics import DiagnosticsDef, InfraDiagnosticsDef
from clockwork.dsl.ir.message_type import resolve_parameterized_schema_interface, resolve_schema_interface
from clockwork.dsl.ir.report_group import ReportGroupDef, ReportGroupInstance
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable, Iterator, Mapping, Sequence


def _parse_parameters_block(
    parameters_block: cst.CogParametersBlock | None, scope: node.Scope, module: node.Module
) -> dict[str, CogParameter]:
    if module.terminals is None:
        msg = "Cannot construct IR nodes from CST without a TerminalSource"
        raise ValueError(msg)

    parameters: dict[str, CogParameter] = {}

    if not parameters_block:
        return parameters

    for cst_param in parameters_block.children_cog_parameter():
        param_name = get_span(cst_param.child_name().child_value(), module.terminals)
        if param_name in parameters:
            msg = node.append_error_line(cst_param, module, f"Duplicate parameter name '{param_name}'")
            raise ValueError(msg)
        param_doc = node.Doc.maybe_from_cst(cst_param.maybe_doc(), module)
        param_type = expr.TypeExpression.make(expr.Expr.from_cst(cst_param.child_typespec(), module))
        param = CogParameter(
            module=module,
            cst_node=cst_param,
            doc=param_doc,
            param_name=param_name,
            type_info=param_type,
        )
        parameters[param_name] = param
        # Define parameter in inner scope for use by configs, states, etc.
        param_ref = CogParameterRef(
            name=param_name,
            scope=scope,
            type_info=typesys.InferenceVar.make(context=module, cst_node=cst_param),
            parameter_def=param,
        )
        typesys.unify(param_ref.type_info, param_type.inference_var)
        scope.define(param_name, param_ref, module.terminals)

    if not parameters:
        msg = node.append_error_line(parameters_block, module, "A cog parameter block cannot be empty.")
        raise ValueError(msg)

    return parameters


@dataclass
class Cog(
    typesys.TypeDef,
    node.CstNode[cst.Cog | cst.PythonCog],
    typesys.InstantiatableEntity,
    typesys.MembershipEntity,
    statement.InstantiationFactory,
    statement.InstantiatableEntityFactory,
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
    aligned_inputs: dict[str, CogAlignedInputDef] = field(repr=False)
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
    cog_metrics_report_groups: dict[str, ReportGroupDef] = field(repr=False, default_factory=dict)
    expanded_aligned_input_defs: dict[str, InputDef] = field(repr=False, default_factory=dict)
    extra_metrics_input_names: list[str] = field(repr=False, default_factory=list)
    extra_metrics_input_view_sizes: dict[str, int] = field(repr=False, default_factory=dict)
    extra_diagnostics_input_names: list[str] = field(repr=False, default_factory=list)
    parameters: dict[str, CogParameter] = field(repr=False, default_factory=dict)
    guarded_components: list[GuardedComponents[CogComponent]] = field(repr=False, default_factory=list)
    resolved: bool = field(repr=False, default=False)

    def is_init(self) -> bool:
        """Determine if this is an init cog."""
        return isinstance(self.execution_spec.condition, InitConditionExpr)

    def is_log(self) -> bool:
        """Determine if this is a log reading cog."""
        return isinstance(self.execution_spec.condition, LogConditionExpr)

    def is_generic(self) -> bool:
        """Return True if this cog has parameters (is generic)."""
        return bool(self.parameters)

    def _input_view_sizes_for_metrics(self) -> dict[str, int]:
        """Build the input name to view size map used by cog metrics metadata.

        This includes aligner inputs that are generated.
        """
        result = {
            name: inp.view_params.max_msgs
            for name, inp in self.inputs.items()
            if isinstance(inp.view_params.max_msgs, int)
        }
        result.update(self.extra_metrics_input_view_sizes)
        for name, aligned_input in self.aligned_inputs.items():
            if isinstance(aligned_input.view_params.max_msgs, int):
                result[name] = aligned_input.view_params.max_msgs
        for expanded_name, input_def in self.expanded_aligned_input_defs.items():
            if isinstance(input_def.view_params.max_msgs, int):
                result[expanded_name.rsplit(".", 1)[-1]] = input_def.view_params.max_msgs
        return result

    @override
    def concrete_type_info(self) -> typesys.TypeVal | typesys.InferenceVar:
        """Return COG_TYPE regardless of whether this cog is generic."""
        return clkbuiltins.COG_TYPE

    @override
    def generic_parameters(self) -> Sequence[typesys.Parameter] | None:
        """Get the generic parameters for the cog.

        Returns:
            The generic parameters, or None if the cog is not generic.
        """
        if not self.parameters:
            return None

        params = []
        for _, param in sorted(self.parameters.items()):
            type_info = param.type_info
            if isinstance(type_info, expr.TypeExpression):
                type_info = type_info.evaluate()
            assert isinstance(type_info, typesys.TypeVal)
            optional = isinstance(type_info, typesys.Instantiation) and type_info.instantiates is clkbuiltins.OPTIONAL
            params.append(
                typesys.Parameter(
                    name=param.param_name,
                    type_bound=type_info,
                    default=None,
                    is_optional=optional,
                )
            )
        return params

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
        parameters = _parse_parameters_block(cst_cog.child_cog_blocks().maybe_cog_parameters_block(), scope, module)
        is_python_cog = cst_cog.kind == cst.PythonCog.kind
        guarded_components: list[GuardedComponents[CogComponent]] = []

        ctx = dfl.Context(scope=scope, terminals=module.terminals, module_id=module.module_id)
        traits = dfl_types.get_trait_registry(module)

        if is_python_cog and module.generates and node.GenerateTarget.py_cog not in module.generates:
            msg = node.append_error_line(cst_cog, module, "Module must generate py_cpg to define python cogs.")
            raise ValueError(msg)
        if cst_cog.kind == cst.Cog.kind and module.generates and node.GenerateTarget.cpp_cog not in module.generates:
            msg = node.append_error_line(cst_cog, module, "Module must generate cpp_cog to define cogs.")
            raise ValueError(msg)

        if resources_block := cst_cog.child_cog_blocks().maybe_resources_block():
            resources, guarded_resources = _unpack_components(
                block=resources_block.child_block(),
                component_type=ResourceDef,
                have_params=bool(parameters),
                ctx=ctx,
                module=module,
                traits=traits,
            )
            guarded_components.extend(guarded_resources)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            resources = {}

        if configs_block := cst_cog.child_cog_blocks().maybe_configs_block():
            configs, guarded_configs = _unpack_components(
                block=configs_block.child_block(),
                component_type=ConfigDef,
                have_params=bool(parameters),
                ctx=ctx,
                module=module,
                traits=traits,
            )
            guarded_components.extend(guarded_configs)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            configs = {}

        if states_block := cst_cog.child_cog_blocks().maybe_states_block():
            states, guarded_states = _unpack_components(
                block=states_block.child_block(),
                component_type=StateDef,
                have_params=bool(parameters),
                ctx=ctx,
                module=module,
                traits=traits,
            )
            guarded_components.extend(guarded_states)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            states = {}

        if report_groups_cst := cst_cog.child_cog_blocks().maybe_report_groups():
            if is_python_cog:
                msg = node.append_error_line(
                    report_groups_cst, module, "Report groups are not suppported in python cogs"
                )
                raise ValueError(msg)
            report_groups = _unpack_report_groups(report_groups_cst, scope, module)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            report_groups = {}

        if diagnostics_block := cst_cog.child_cog_blocks().maybe_diagnostics_block():
            if is_python_cog:
                msg = node.append_error_line(diagnostics_block, module, "Diagnostics are not suppported in python cogs")
                raise ValueError(msg)
            diagnostics = _unpack_diagnostics(diagnostics_block, scope, module)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            diagnostics = {}

        if inputs_block := cst_cog.child_cog_blocks().maybe_inputs_block():
            inputs, guarded_inputs = _unpack_components(
                block=inputs_block.child_block(),
                component_type=InputDef,
                have_params=bool(parameters),
                ctx=ctx,
                module=module,
                traits=traits,
            )
            guarded_components.extend(guarded_inputs)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            inputs = {}

        if aligned_inputs_block := cst_cog.child_cog_blocks().maybe_cog_aligned_inputs_block():
            if is_python_cog:
                msg = node.append_error_line(
                    aligned_inputs_block, module, "aligned_inputs are not supported in python cogs"
                )
                raise ValueError(msg)
            aligned_inputs = _unpack_aligned_inputs(aligned_inputs_block, scope, module)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            aligned_inputs = {}

        if outputs_block := cst_cog.child_cog_blocks().maybe_outputs_block():
            outputs, guarded_outputs = _unpack_components(
                block=outputs_block.child_block(),
                component_type=OutputDef,
                have_params=bool(parameters),
                ctx=ctx,
                module=module,
                traits=traits,
            )
            guarded_components.extend(guarded_outputs)
        else:
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
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
            if is_python_cog:
                msg = node.append_error_line(
                    python_options_block, module, "Python options are not suppported in python cogs"
                )
                raise ValueError(msg)
            python_options = PythonOptions.from_cst(python_options_block, module, name)
        elif is_python_cog:
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
        exec_spec = _find_exec_when(exec_block, module)
        name = get_span(cst_cog.child_identifier().child_value(), module.terminals)
        condition_defs, rate_limits = _unpack_execution_statements(exec_block, scope, module)

        if metrics_options_block := cst_cog.child_cog_blocks().maybe_metrics_options_block():
            metrics_options = MetricsOptions.from_cst(metrics_options_block, module)
        else:
            metrics_options = MetricsOptions(
                metrics_enabled=False, batch_size=DEFAULT_METRICS_BATCH_SIZE, module=module, cst_node=None
            )
        # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        metrics_outputs = {}
        if metrics_options.metrics_enabled:
            metrics_outputs = define_metrics_outputs(scope, module)

        return cls(
            type_info=clkbuiltins.TYPE_TYPE if parameters else clkbuiltins.COG_TYPE,
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
            cog_metrics_report_groups={},
            diagnostics=diagnostics,
            infra_diagnostics=infra_diagnostics,
            inputs=inputs,
            aligned_inputs=aligned_inputs,
            outputs=outputs,
            metrics_outputs=metrics_outputs,
            conditions={cond.name: cond for cond in condition_defs},
            rate_limits=rate_limits,
            execution_spec=ExecutionSpec.from_cst(exec_spec, module),
            simulation_options=simulation_options,
            python_options=python_options,
            metrics_options=metrics_options,
            attributes=attributes,
            parameters=parameters,
            guarded_components=guarded_components,
        )

    def _resolve_condition_expr(self, root: ConditionExpr) -> ConditionExpr:
        """Recursively resolve execute condition expression, by resolving ConditionExpr within."""
        if isinstance(root, InitConditionExpr):
            return root
        if isinstance(root, LogConditionExpr):
            return root
        if isinstance(root, SimpleConditionExpr):
            if not isinstance(root.condition, ConditionDef):
                root.condition.resolve(self.scope)
            assert isinstance(root.condition, ConditionDef)
            if isinstance(root.condition.condition, MessagesPresent):
                input_name = root.condition.condition.input_name
                if input_name in self.inputs:
                    input_def = self.inputs[input_name]
                    if input_def.elements:
                        # Multi-connect input conditions are an 'OR' of the condition for each element in the input
                        new_root = copy(root)
                        new_root.condition = copy(root.condition)
                        new_root.condition.condition = copy(root.condition.condition)
                        new_root.condition.name += "__0"
                        new_root.condition.condition.input_name = input_def.elements[0].name
                        for index, element in enumerate(input_def.elements[1:]):
                            rhs = copy(root)
                            rhs.condition = copy(root.condition)
                            rhs.condition.condition = copy(root.condition.condition)
                            rhs.condition.name += f"__{index + 1}"
                            rhs.condition.condition.input_name = element.name
                            new_root = BinaryConditionExpr(
                                cst_node=root.cst_node,
                                lhs=new_root,
                                rhs=rhs,
                                op=ConditionOp.OR,
                                module=root.module,
                            )
                        return new_root
            return root
        if isinstance(root, BinaryConditionExpr):
            root.lhs = self._resolve_condition_expr(root.lhs)
            root.rhs = self._resolve_condition_expr(root.rhs)
            return root

        msg = f"Type {type(root)} is not supported."
        raise TypeError(msg)

    def _expand_multi_channel_input_conditions(self) -> None:
        """Expand conditions for multi-channel inputs into one condition for each element of the input."""
        prev_conditions = self.conditions
        self.conditions = {}
        for cond in prev_conditions.values():
            if isinstance(cond.condition, MessagesPresent):
                input_name = cond.condition.input_name
                if input_name in self.inputs:
                    input_def = self.inputs[input_name]
                    if input_def.elements:
                        for index, element in enumerate(input_def.elements):
                            element_cond = copy(cond)
                            element_cond.condition = copy(cond.condition)
                            element_cond.name += f"__{index}"
                            element_cond.condition.input_name = element.name
                            self.conditions[element_cond.name] = element_cond
                        continue
            self.conditions[cond.name] = cond

    # We must disable C901 here (function complexity) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    def resolve(self) -> None:  # noqa: C901, PLR0912 (see above)
        """Perform finalization of the IR."""
        if self.resolved:
            return
        self.resolved = True
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
        all_input_names = []
        for input_def in self.inputs.values():
            if input_def.elements:
                all_input_names.extend([element.name for element in input_def.elements])
            else:
                all_input_names.append(input_def.name)
        all_input_names.extend(self.extra_metrics_input_names)
        state_with_memory_resource_names = [
            name
            for name, state in self.states.items()
            if isinstance(state.message_type, extern_type.ExternType) and state.params.mutable
        ]
        for condition_def in self.conditions.values():
            if isinstance(condition_def.condition, MessagesPresent):
                condition_def.condition.resolve()
        self._expand_multi_channel_input_conditions()

        self.cog_metrics_report_groups = generate_cog_metrics_report_groups(
            is_init=self.is_init(),
            module=self.module,
            parent_scope=self.inner_scope,
            cog=self,
            input_names=all_input_names,
            output_names=list(self.outputs.keys()),
            condition_names=list(self.conditions.keys()),
            memory_resource_names=list(self.resources.keys()) + state_with_memory_resource_names,
            input_view_sizes=self._input_view_sizes_for_metrics(),
        )
        for cog_metrics_rg in self.cog_metrics_report_groups.values():
            cog_metrics_rg.resolve(self.inner_scope, self.name)
        if self.metrics_outputs:
            self._resolve_metrics_outputs(all_input_names)
        for limit_spec in self.rate_limits.values():
            limit_spec.resolve()
        if self.python_options:
            self.python_options.resolve()
        self.execution_spec.condition = self._resolve_condition_expr(self.execution_spec.condition)
        if self.is_init():
            self._check_init_requirements()
        for diagnostics_def in self.diagnostics.values():
            diagnostics_def.resolve()
        self.infra_diagnostics.extra_input_names = self.extra_diagnostics_input_names
        self.infra_diagnostics.resolve()
        self._check_safety_margins()

    def _resolve_metrics_outputs(self, all_input_names: list[str]) -> None:
        """Resolve the metrics outputs."""
        telemetry_metrics_schema = generate_telemetry_metrics_schema(
            self.name, all_input_names, self.outputs, set(self.rate_limits), self.conditions, self.module
        )
        event_metrics_schema, metrics_schema_deps, generated_enums = generate_event_metrics_schema(
            self.name,
            self.metrics_options.batch_size,
            all_input_names,
            self.outputs,
            set(self.rate_limits),
            self.conditions,
            self.module,
        )
        for metrics_output_def in self.metrics_outputs.values():
            if metrics_output_def.log_type == MetricsLogType.non_redundant_telemetry:
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

        def check_input(input_name: str, input_def: InputDef | CogAlignedInputDef) -> None:
            view_params = input_def.view_params
            if view_params.safety_margin is not None and input_name not in new_max_inputs:
                msg = input_def.append_error_line(
                    "Inputs may only specify a safety margin when associated with a new_message condition that uses the 'max' parameter"
                )
                raise ValueError(msg)
            if view_params.safety_margin is None and input_name in new_max_inputs:
                # Ask for the default when we get connected to a channel
                view_params.safety_margin = -1

        for input_name, input_def in self.inputs.items():
            check_input(input_name, input_def)
        for input_name, input_def in self.aligned_inputs.items():
            check_input(input_name, input_def)

    def _check_init_requirements(self) -> None:
        base_msg = "Init cogs (as determined by 'execute when' expression)"
        if self.inputs or self.aligned_inputs:
            cst_location = None
            if self.cst_node is not None:
                cst_cog_blocks = self.cst_node.child_cog_blocks()
                cst_location = (
                    cst_cog_blocks.maybe_inputs_block()
                    if self.inputs
                    else cst_cog_blocks.maybe_cog_aligned_inputs_block()
                )
            msg = node.append_error_line(
                cst_location,
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

    @override
    def make_from_new_stmt(
        self,
        *,
        new_stmt: statement.NewStmt,
    ) -> typesys.InstantiatableEntity:
        """Make an InstantiatabeEntity from a NewStmt."""
        return InstantiatedCogFactory(
            cst_node=new_stmt.cst_node,
            module=new_stmt.module,
            new_stmt=new_stmt,
        )

    @override
    def make_from_instantiate_stmt(self, *, instantiate_stmt: statement.InstantiateStmt) -> InstantiatedCog:
        """Instantiate an entity from an InstantiateStmt."""
        return InstantiatedCog.from_instantiate_stmt(instantiate_stmt)

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
        for report_group in (*self.report_groups.values(), *self.cog_metrics_report_groups.values()):
            representations.extend(report_group.generated_representations)
        return representations

    def generated_report_group_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Get all generated interfaces from report groups."""
        interfaces = []
        for report_group in (*self.report_groups.values(), *self.cog_metrics_report_groups.values()):
            interfaces.extend(report_group.generated_interfaces)
        return interfaces

    def generated_report_group_schemas(self) -> Iterable[schema.Schema]:
        """Get all generated schemas from report groups."""
        generated_schemas = []
        for report_group in (*self.report_groups.values(), *self.cog_metrics_report_groups.values()):
            generated_schemas.extend(report_group.generated_schemas)
        return generated_schemas

    def generated_report_group_metadata_schemas(self) -> Iterable[schema.Schema]:
        """Get bespoke metadata schemas from report groups.

        These are programmatically-generated schemas (e.g. per-input
        sequence-number metadata) referenced as complete value types by the
        generated signal API.  They must be emitted into the cog's ``_types``
        translation unit so their definitions precede the dial that uses them.
        """
        generated_schemas = []
        for report_group in (*self.report_groups.values(), *self.cog_metrics_report_groups.values()):
            generated_schemas.extend(report_group.generated_metadata_schemas)
        return generated_schemas


def _unpack_report_groups(
    report_groups_cst: cst.ReportGroups, scope: node.Scope, module: node.Module
) -> dict[str, ReportGroupDef]:
    return {
        report_group_def.name: report_group_def
        for report_group_def in (
            ReportGroupDef.from_cst(report_groups_cst, module, scope)
            for report_groups_cst in report_groups_cst.children_report_group()
        )
    }


def _unpack_diagnostics(
    diagnostics_block: cst.DiagnosticsBlock, scope: node.Scope, module: node.Module
) -> dict[str, DiagnosticsDef]:
    if diagnostics_defs := list(diagnostics_block.children_diagnostics_def()):
        diagnostics_objs = [DiagnosticsDef.from_cst(i, module, scope) for i in diagnostics_defs]
    else:
        diagnostics_objs = [DiagnosticsDef.from_cst(diagnostics_block, module, scope)]
    return {i.name: i for i in diagnostics_objs}


def _unpack_aligned_inputs(
    aligned_inputs_block: cst.CogAlignedInputsBlock, scope: node.Scope, module: node.Module
) -> dict[str, CogAlignedInputDef]:
    return {
        ai.name: ai
        for ai in (
            CogAlignedInputDef.from_cst(ai_cst, module, scope)
            for ai_cst in _get_definitions_from_block(aligned_inputs_block.child_block())
        )
    }


D_co = TypeVar("D_co", bound="CogComponent", covariant=True)


@dataclass(frozen=True)
class GuardedComponents(Generic[D_co]):
    """Cog definitions that are guarded by or contain a conditional statement that depends on a generic parameter."""

    guard: cst.Statement
    component_type: type[D_co]

    def evaluate(self, ctx: dfl.Context, module: node.Module, traits: dfl_types.TraitRegistry) -> list[D_co]:
        """Evaluated the guarded and use the statements within to construct instances of D."""
        components: list[D_co] = []
        guard = dfl.statement_from_cst(self.guard, ctx, module)
        result = dfl.const_fold(guard, traits)
        if isinstance(result, dfl.Block | dfl.Definition):
            result = dfl_analysis.flatten_blocks(result)

        match result:
            case dfl.Definition() | dfl.CstPassthrough():
                components.append(self.component_type.from_statement(result, ctx, module))
            case dfl.Block(statements=statements):
                for stmt in statements:
                    assert isinstance(stmt, dfl.Definition | dfl.CstPassthrough[cst.Statement])
                    components.append(self.component_type.from_statement(stmt, ctx, module))
            case _:
                msg = f"Cog definition evaluated to unexpected type: {result}"
                raise TypeError(ctx.format_error(self.guard.span, msg))

        return components


def _unpack_components(  # noqa: PLR0913 # mitigated with kwonly args
    *,
    block: cst.Block,
    component_type: type[D_co],
    have_params: bool,
    ctx: dfl.Context,
    module: node.Module,
    traits: dfl_types.TraitRegistry,
) -> tuple[dict[str, D_co], list[GuardedComponents[D_co]]]:
    components: dict[str, D_co] = {}

    ready, partial = _gather_statements(
        block.children_statement(),
        ctx,
        module,
        traits,
    )
    for stmt in ready:
        component = component_type.from_statement(stmt, ctx, module, is_generic=have_params)
        components[component.name] = component

    guarded: list[GuardedComponents[D_co]] = [
        GuardedComponents(guard=stmt, component_type=component_type) for stmt in partial
    ]

    return components, guarded


def _is_fully_resolved(root: dfl.Expr) -> bool:
    """Returns true if the given expression contains no un-evaluated conditional statements."""
    return len(dfl_analysis.find_with_pred(root, lambda e: isinstance(e, dfl.IfElse | dfl.CondExpr | dfl.Match))) == 0


def _gather_statements(
    statements: Iterator[cst.Statement], ctx: dfl.Context, module: node.Module, traits: dfl_types.TraitRegistry
) -> tuple[list[dfl.Definition | dfl.CstPassthrough[cst.Statement]], list[cst.Statement]]:
    """Attempt to evaluate each statement.

    Returns two lists. One contains every statement that was fully evaluated.
    The other contains all partially evaluated statements (i.e. those that
    contain references to cog parameters whose values aren't available yet).
    """
    ready: list[dfl.Definition | dfl.CstPassthrough[cst.Statement]] = []
    partial: list[cst.Statement] = []
    for statement_cst in statements:
        output_expr = dfl.statement_from_cst(statement_cst, ctx, module)
        result = dfl.const_fold(output_expr, traits)
        if isinstance(result, dfl.Block):
            result = dfl_analysis.flatten_blocks(result)

        match result:
            case dfl.Block(statements=block_statements):
                # only add blocks if all of their children are fully resolved
                if all(_is_fully_resolved(block_stmt) for block_stmt in block_statements):
                    ready.extend(cast("list[dfl.Definition | dfl.CstPassthrough[cst.Statement]]", block_statements))
                else:
                    partial.append(statement_cst)
            case dfl.Definition():
                if _is_fully_resolved(result):
                    ready.append(result)
                else:
                    partial.append(statement_cst)
            case dfl.CstPassthrough():
                ready.append(result)
            case dfl.IfElse() | dfl.CondExpr() | dfl.Match():
                partial.append(statement_cst)

    return ready, partial


def _find_exec_when(exec_block: cst.ExecutionBlock, module: node.Module) -> cst.ExecuteWhen:
    assert module.terminals is not None

    exec_when = None
    for stmt in exec_block.child_block().children_statement():
        if when_cst := stmt.maybe_execute_when():
            if exec_when is not None:
                msg = "Cogs must have exactly one 'execute when' statement.\n" + format_line_with_error(
                    exec_block.span,
                    module.terminals,
                    module.module_id,
                )
                raise ValueError(msg)
            exec_when = when_cst

    if exec_when is None:
        msg = "Cog is missing required 'execute when' statement.\n" + format_line_with_error(
            exec_block.span,
            module.terminals,
            module.module_id,
        )
        raise ValueError(msg)

    return exec_when


def _unpack_execution_statements(
    exec_block: cst.ExecutionBlock, scope: node.Scope, module: node.Module
) -> tuple[list[ConditionDef], dict[str, RateLimitSpec]]:
    condition_defs = []
    rate_limits = {}
    for stmt in exec_block.child_block().children_statement():
        exec_stmt = stmt.maybe_execution_statement()
        if exec_stmt is None:
            continue

        if cond_def := exec_stmt.maybe_condition_def():
            condition_defs.append(ConditionDef.from_cst(scope=scope, cst_def=cond_def, module=module))
        if rl_spec_cst := exec_stmt.maybe_rate_limit_spec():
            spec = RateLimitSpec.from_cst(cst_spec=rl_spec_cst, module=module)
            if spec.output.identifier in rate_limits:
                msg = spec.append_error_line(
                    f"Outputs can only have one rate limit, but got multiple for {spec.output.identifier}"
                )
                raise ValueError(msg)
            rate_limits[spec.output.identifier] = spec

    return condition_defs, rate_limits


def _get_options_from_block(block: cst.Block, module: node.Module) -> dict[str, expr.Expr]:
    assert module.terminals is not None
    return {
        get_span(statement.child_definition().child_name().child_value(), module.terminals): expr.Expr.from_cst(
            statement.child_definition().child_value(), module
        )
        for statement in block.children_statement()
        if statement.maybe_definition()
    }


def _get_definitions_from_block(block: cst.Block) -> list[cst.Definition]:
    return [statement.child_definition() for statement in block.children_statement() if statement.maybe_definition()]


@dataclass
class ResourceDef(CogComponent, node.DocableEntity, node.CstNode[cst.Definition]):
    """A definition of a Cog resource."""

    @classmethod
    @override
    def from_statement(
        cls: type[ResourceDef],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> ResourceDef:
        """Construct an ResourceDef IR node from a CST ResourceDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if not isinstance(definition, dfl.Definition):
            msg = f"ResourceDef cannot be constructed from {type(definition)}."
            raise TypeError(ctx.format_error(dfl.get_expr_span(definition), msg))

        cst_doc = definition.cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        value = definition.cst_node.child_value().maybe_identifier()
        if value is None or get_span(value.child_value(), terminals=module.terminals) != "persistent":
            msg = (
                f"Cog resource '{definition.name}' has unsupported type (expected 'persistent').\n"
                + format_line_with_error(
                    definition.cst_node.child_value().span,
                    module.terminals,
                    module.module_id,
                )
            )
            raise NotImplementedError(msg)
        # fmt: off
        result = cls(
            module=module,
            cst_node=definition.cst_node,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=ctx.scope,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=definition.name,
            type_info=clkbuiltins.COG_RESOURCE_TYPE,
        )
        # fmt: on
        ctx.scope.define(definition.name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""


@dataclass
class ResolvedConfigDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.Definition]):
    """A definition of a Cog config."""

    message_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation
    source: ConfigDef


@dataclass
class ConfigDef(CogComponent, node.DocableEntity, node.CstNode[cst.Definition]):
    """A definition of a Cog config."""

    message_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation | expr.Expr
    is_generic: bool
    resolved: ResolvedConfigDef | None = None

    @classmethod
    @override
    def from_statement(
        cls: type[ConfigDef],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> ConfigDef:
        """Construct an ConfigDef IR node from a CST ConfigDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if not isinstance(definition, dfl.Definition):
            msg = f"ConfigDef cannot be constructed from {type(definition)}."
            raise TypeError(ctx.format_error(dfl.get_expr_span(definition), msg))

        cst_doc = definition.cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        message_type = definition.value
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        # fmt: off
        result = cls(
            module=module,
            cst_node=definition.cst_node,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=ctx.scope,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=definition.name,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            message_type=message_type,
            is_generic=is_generic,
        )
        # fmt: on
        ctx.scope.define(definition.name, result, module.terminals)
        return result

    def resolve(self) -> ResolvedConfigDef:
        """Perform finalization of the IR."""
        if self.resolved:
            return self.resolved
        if self.is_generic:
            # Resolution is done done when the cog is instantiated
            self.message_type = resolve_parameterized_schema_interface(self.module.context, self.message_type)
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        # fmt: off
        self.resolved = ResolvedConfigDef(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=self.name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            message_type=self.message_type,
            source=self,
        )
        # fmt: on
        return self.resolved

    def get_resolved(self) -> ResolvedConfigDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ResolvedStateDef(typesys.NamedAttribute, node.DocableEntity, node.CstNode[cst.Definition]):
    """A definition of a Cog state."""

    message_type: schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation | extern_type.ExternType
    params: ResolvedStateParams
    source: StateDef


@dataclass
class StateDef(CogComponent, node.DocableEntity, node.CstNode[cst.Definition]):
    """A definition of a Cog state."""

    message_type: (
        schema_reg.InterfaceInfo | CogParameterRef | typesys.Instantiation | extern_type.ExternType | expr.Expr
    )
    params: StateParams
    is_generic: bool
    resolved: ResolvedStateDef | None = None

    @classmethod
    @override
    def from_statement(
        cls: type[StateDef],
        definition: dfl.Definition | dfl.CstPassthrough[cst.Statement],
        ctx: dfl.Context,
        module: node.Module,
        is_generic: bool = False,
    ) -> StateDef:
        """Construct an StateDef IR node from a CST StateDef node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        if not isinstance(definition, dfl.Definition):
            msg = f"StateDef cannot be constructed from {type(definition)}."
            raise TypeError(ctx.format_error(dfl.get_expr_span(definition), msg))

        cst_doc = definition.cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        # TODO(OI-4058): refactor StateParams to use dfl.Block/dfl.Definition
        params_cst = definition.cst_node.maybe_block()
        params = StateParams.from_cst(params_cst, module) if params_cst else StateParams.make_default(module)
        message_type = definition.value
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)
        # fmt: off
        result = cls(
            module=module,
            cst_node=definition.cst_node,
            doc=doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=ctx.scope,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=definition.name,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            message_type=message_type,
            params=params,
            is_generic=is_generic,
        )
        # fmt: on
        ctx.scope.define(definition.name, result, module.terminals)
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
        elif self.is_generic:
            # Resolution is done done when the cog is instantiated
            self.message_type = resolve_parameterized_schema_interface(self.module.context, self.message_type)
        else:
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        resolved_params = self.params.resolve()
        # fmt: off
        self.resolved = ResolvedStateDef(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=self.name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            message_type=self.message_type,
            params=resolved_params,
            source=self,
        )
        # fmt: on
        return self.resolved

    def get_resolved(self) -> ResolvedStateDef:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved


@dataclass
class ResolvedStateParams(node.CstNode[cst.Block]):
    """Parameters for state views."""

    mutable: bool
    source: StateParams | None


@dataclass
class StateParams(node.CstNode[cst.Block]):
    """Parameters for state views."""

    mutable: bool | expr.Expr
    resolved: ResolvedStateParams | None = None

    @classmethod
    def make_default(cls: type[StateParams], module: node.Module, cst_node: cst.Block | None = None) -> StateParams:
        """Make a StateParams with all values at defaults."""
        return StateParams(module=module, cst_node=cst_node, mutable=False)

    @classmethod
    def from_cst(cls: type[StateParams], cst_node: cst.Block, module: node.Module) -> StateParams:
        """Construct state parameters from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        result = StateParams.make_default(module, cst_node=cst_node)
        seen_params: set[str] = set()
        for statement_cst in cst_node.children_statement():
            param_cst = statement_cst.child_definition()
            result._handle_param(param_cst, seen_params)  # noqa: SLF001 (result is StateParams)
        return result

    def _handle_param(self, cst_node: cst.Definition, seen_params: set[str]) -> None:
        assert self.module.terminals is not None
        name = get_span(name_span := cst_node.child_name().child_value(), self.module.terminals)
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
    # fmt: off
    telemetry_metrics = MetricsOutputDef(
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        "cog_telemetry_metrics",
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        parent_scope,
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        clkbuiltins.COG_METRICS_OUTPUT_TYPE,
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        module,
        # pyrefly: ignore[bad-keyword-argument] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        log_type=MetricsLogType.non_redundant_telemetry,
        # pyrefly: ignore[bad-keyword-argument] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        message_type=None,
        message_interface_type=None,
    )
    # fmt: on
    result[telemetry_metrics.name] = telemetry_metrics
    # fmt: off
    event_metrics = MetricsOutputDef(
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        "cog_event_metrics",
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        parent_scope,
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        clkbuiltins.COG_METRICS_OUTPUT_TYPE,
        # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        module,
        # pyrefly: ignore[bad-keyword-argument] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        log_type=MetricsLogType.event,
        # pyrefly: ignore[bad-keyword-argument] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        message_type=None,
        message_interface_type=None,
    )
    # fmt: on
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
            period_kind = type(cst_spec.child_period()).__name__.lower()
            msg = node.append_error_line(
                cst_spec.child_period(),
                module,
                f"Expected time literal for rate limit period, but got {period_kind}",
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

        if self.limit <= 0:
            msg = self.append_error_line(f"Rate limit must be positive, but got {self.limit}")
            raise ValueError(msg)

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

        options = _get_options_from_block(cst_node.child_block(), module)

        if enabled_expr := options.get("enabled"):
            assert enabled_expr.cst_node is not None  # we always construct with one above
            typesys.unify(clkbuiltins.BOOL, enabled_expr.type_info)
            enabled_expr = node.resolve_names(enabled_expr, clkbuiltins.BUILTINS_SCOPE)
            result = enabled_expr.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = (
                    f"Expected a NamedValue (true or false) for parameter 'enabled', but got {type(result)}: "
                    + format_line_with_error(
                        enabled_expr.cst_node.span,
                        module.terminals,
                        module.module_id,
                    )
                )
                raise TypeError(msg)

            metrics_enabled = primitive.value_to_bool(result)

        if batch_size_expr := options.get("batch_size"):
            assert batch_size_expr.cst_node is not None  # we always construct with one above
            typesys.unify(clkbuiltins.UINT64, batch_size_expr.type_info)
            result = batch_size_expr.evaluate()
            if not isinstance(result, primitive.DecimalValue):
                msg = (
                    f"Expected an integer value for parameter 'batch_size', but got {type(result)}: "
                    + format_line_with_error(
                        batch_size_expr.cst_node.span,
                        module.terminals,
                        module.module_id,
                    )
                )
                raise TypeError(msg)

            batch_size_value = primitive.unsigned_decimal_to_int(result)

        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
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

        options = _get_options_from_block(cst_node.child_block(), module)

        duration_expr = options.get("execution_duration")
        if duration_expr is None:
            msg = "Missing required simluation option 'execution_duration'.\n" + format_line_with_error(
                cst_node.span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)

        typesys.unify(clkbuiltins.DURATION, duration_expr.type_info)
        execution_duration = duration_expr.evaluate()
        if not isinstance(execution_duration, primitive.UnitLiteral):
            assert duration_expr.cst_node is not None  # we always construct with one above
            msg = "Expected duration literal for sime execution duration.\n" + format_line_with_error(
                duration_expr.cst_node.span,
                module.terminals,
                module.module_id,
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

        options = _get_options_from_block(cst_node.child_block(), module)

        if "dial" not in options:
            msg = "Missing required python option 'dial': " + format_line_with_error(
                cst_node.span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)

        if "impl" not in options:
            msg = "Missing required python option 'impl': " + format_line_with_error(
                cst_node.span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)

        return cls(
            module=module,
            cst_node=cst_node,
            python_dial_class_name=options["dial"],
            python_impl_class_name=options["impl"],
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

    cog_class: Cog | InstantiatedCog
    inner_scope: node.Scope
    members: list[
        CogInstanceMember[
            ResourceDef
            | ConfigDef
            | StateDef
            | InputDef
            | CogAlignedInputDef
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
        cog_class: Cog | InstantiatedCog,
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
        # fmt: off
        result = cls(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            inner_scope=inner_scope,
            type_info=clkbuiltins.COG_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            cog_class=cog_class,
            members=[],
        )
        # fmt: on
        for report_group in (*cog_class.report_groups.values(), *cog_class.cog_metrics_report_groups.values()):
            # Special handling for report groups. These instances are distinct from other members because they by
            # necessity only can know their contained signal instances at instantiation time, therefore
            # CogInstanceMember[T] does not suffice here.
            rg_instance = report_group.make_instance(result.fqn)
            result.report_group_instances.append(rg_instance)

        for entity in cog_class.inner_scope.names.values():
            result._make_member(entity)
        result._make_member(cog_class.infra_diagnostics)
        return result

    def _make_member(  # noqa: C901, PLR0912 This is effectively a factory with a switch statement
        self, entity: node.NamedEntity
    ) -> None:
        result: (
            CogInstanceMember[
                ResourceDef
                | ConfigDef
                | StateDef
                | InputDef
                | CogAlignedInputDef
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
            if entity.elements:
                result.elements = []
                for input_element in entity.elements:
                    element = CogInstanceMemberElement.make(
                        self, input_element, clkbuiltins.COG_INPUT_INSTANCE_TYPE, input_element.index
                    )
                    self.inner_scope.define(element.name, element, self.module.terminals)
                    # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                    result.elements.append(element)
        elif isinstance(entity, CogAlignedInputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_ALIGNED_INPUT_INSTANCE_TYPE)
        elif isinstance(entity, OutputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_OUTPUT_INSTANCE_TYPE)
        elif isinstance(entity, MetricsOutputDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_METRICS_OUTPUT_INSTANCE_TYPE)
        elif isinstance(entity, ConditionDef):
            match entity.condition:
                case TimeSinceLastExec() | DynamicTimer():
                    result = CogInstanceMember.make(self, entity, clkbuiltins.COG_CONDITION_INSTANCE_TYPE)
                case NewMessagePresent() | AnyMessagePresent():
                    pass  # Message-presence conditions do not generate connectable endpoints
                case _:
                    msg = self.append_error_line(f"Unrecognized condition type: {type(entity.condition).__name__}")
                    raise NotImplementedError(msg)
        elif isinstance(entity, DiagnosticsDef | InfraDiagnosticsDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_DIAGNOSTICS_INSTANCE_TYPE)
        elif isinstance(entity, ReportGroupDef):
            result = CogInstanceMember.make(self, entity, clkbuiltins.COG_REPORT_GROUP_INSTANCE_TYPE)
        elif isinstance(self.cog_class, InstantiatedCog) and entity.name not in self.cog_class.cog_ir.parameters:
            msg = self.append_error_line(f"Unrecognized instance member {entity}")
            raise NotImplementedError(msg)
        if result:
            self.inner_scope.define(entity.name, result, self.module.terminals)
            self.members.append(result)

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
    elements: list[CogInstanceMemberElement[T]] | None = field(repr=False)

    @classmethod
    def make(
        cls: type[CogInstanceMember[T]], cog_instance: CogInstance, member: T, type_info: typesys.TypeVal
    ) -> CogInstanceMember[T]:
        """Create a cog instance member."""
        # pyrefly: ignore[bad-argument-count, bad-argument-type, bad-return, bad-specialization] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return cls(member.name, cog_instance.inner_scope, type_info, cog_instance, member, None)


@dataclass
class CogInstanceMemberElement(typesys.NamedAttribute, Generic[T]):
    """Represents an element of member (e.g., multi_connect input) of a Cog instance."""

    cog_instance: CogInstance
    member: T = field(repr=False)
    index: int

    @classmethod
    def make(
        cls: type[CogInstanceMemberElement[T]],
        cog_instance: CogInstance,
        member: T,
        type_info: typesys.TypeVal,
        index: int,
    ) -> CogInstanceMemberElement[T]:
        """Create an element of a cog instance member."""
        # pyrefly: ignore[bad-argument-count, bad-argument-type, bad-return, bad-specialization] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return cls(member.name, cog_instance.inner_scope, type_info, cog_instance, member, index)


@dataclass
class InstantiatedCog(typesys.TypeDef, typesys.MembershipEntity, typesys.InstantiatableEntity):
    """IR Node representing an instantiated Cog."""

    module: node.Module
    inner_scope: node.Scope
    instantiation: typesys.Instantiation = field(repr=False)
    cog_ir: Cog = field(repr=False)
    resources: dict[str, ResourceDef] = field(repr=False)
    configs: dict[str, ConfigDef] = field(repr=False)
    states: dict[str, StateDef] = field(repr=False)
    diagnostics: dict[str, DiagnosticsDef] = field(repr=False)
    infra_diagnostics: InfraDiagnosticsDef = field(repr=False)
    inputs: dict[str, InputDef] = field(repr=False)
    aligned_inputs: dict[str, CogAlignedInputDef] = field(repr=False)
    outputs: dict[str, OutputDef] = field(repr=False)
    metrics_outputs: dict[str, MetricsOutputDef] = field(repr=False)
    conditions: dict[str, ConditionDef] = field(repr=False)
    rate_limits: dict[str, RateLimitSpec] = field(repr=False)
    execution_spec: ExecutionSpec = field(repr=False)
    simulation_options: SimulationOptions | None = field(repr=False)
    python_options: PythonOptions | None = field(repr=False)
    metrics_options: MetricsOptions = field(repr=False)
    attributes: node.ClkAttributes | None = field(repr=False)
    arguments: Mapping[str, typesys.Value]
    report_groups: dict[str, ReportGroupDef] = field(repr=False, default_factory=dict)
    cog_metrics_report_groups: dict[str, ReportGroupDef] = field(repr=False, default_factory=dict)
    expanded_aligned_input_defs: dict[str, InputDef] = field(repr=False, default_factory=dict)

    def is_init(self) -> bool:
        """Determine if this is an init cog."""
        return self.cog_ir.is_init()

    def is_log(self) -> bool:
        """Determine if this is a log reading cog."""
        return self.cog_ir.is_log()

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

    @classmethod
    def from_instantiate_stmt(
        cls: type[InstantiatedCog],
        instantiate_ir: statement.InstantiateStmt,
    ) -> InstantiatedCog:
        """Create an InstantiatedCog from a cog instantiation.

        Args:
            instantiate_ir: An instantiate statement that instantiates a cog.

        Returns:
            A fully resolved InstantiatedCog.

        Raises:
            TypeError: If the instantiation is not for a Cog.
            ValueError: If required parameters are missing or invalid.
        """
        instantiation = instantiate_ir.typespec
        if not isinstance(instantiation, typesys.Instantiation):
            msg = instantiate_ir.append_error_line("Attempt to instantiate unresolved statement")
            raise TypeError(msg)
        cog_ir = instantiation.instantiates
        if not isinstance(cog_ir, Cog):
            msg = instantiate_ir.append_error_line(
                f"Expected Cog instantiation, got {type(instantiation.instantiates)}"
            )
            raise TypeError(msg)
        if not cog_ir.is_generic():
            msg = instantiate_ir.append_error_line("Cannot instantiate a non-generic cog")
            raise ValueError(msg)

        args = instantiation.arguments
        assert cog_ir.inner_scope.parent is not None
        scope = cog_ir.inner_scope.parent.make_anon_child_scope(cog_ir.name)

        # TODO(OI-4270): Implement parameterized aligners
        if cog_ir.aligned_inputs or cog_ir.expanded_aligned_input_defs:
            msg = instantiate_ir.append_error_line("Parameterized aligners are not implemented.")
            raise ValueError(msg)

        resources = copy(cog_ir.resources)
        configs = _resolve_generic_configs(instantiate_ir, cog_ir.configs, args, scope)
        states = _resolve_generic_states(instantiate_ir, cog_ir.states, args, scope)
        diagnostics = _resolve_generic_diagnostics(instantiate_ir, cog_ir.diagnostics, args, scope)
        inputs = _resolve_generic_inputs(instantiate_ir, cog_ir.inputs, args, scope)
        outputs = _resolve_generic_outputs(instantiate_ir, cog_ir.outputs, args, scope)
        rate_limits = _resolve_generic_rate_limits(cog_ir.rate_limits, outputs)

        _resolve_guarded_components(
            guarded_components=cog_ir.guarded_components,
            arguments=args,
            resources=resources,
            configs=configs,
            states=states,
            inputs=inputs,
            outputs=outputs,
            scope=scope,
            module=cog_ir.module,
        )

        # Pull in any attributes from the generic cog's inner scope that were not defined
        # when we resolved the arguments.
        for attr_name, attr_value in cog_ir.inner_scope.names.items():
            if attr_name not in scope.names:
                scope.define(attr_name, attr_value, cog_ir.module.terminals)

        cog_params = cog_ir.generic_parameters()
        assert cog_params

        return cls(
            type_info=clkbuiltins.COG_TYPE,
            module=cog_ir.module,
            name=cog_ir.name
            + "_"
            + "_".join(f"{param.name}_{instantiation.arguments[param.name].value_key()}" for param in cog_params),
            scope=cog_ir.module.inner_scope,
            instantiation=instantiation,
            cog_ir=cog_ir,
            inner_scope=scope,
            resources=resources,
            configs=configs,
            states=states,
            diagnostics=diagnostics,
            infra_diagnostics=cog_ir.infra_diagnostics,
            inputs=inputs,
            aligned_inputs=cog_ir.aligned_inputs,
            outputs=outputs,
            metrics_outputs=cog_ir.metrics_outputs,
            conditions=cog_ir.conditions,
            rate_limits=rate_limits,
            execution_spec=cog_ir.execution_spec,
            simulation_options=cog_ir.simulation_options,
            python_options=cog_ir.python_options,
            metrics_options=cog_ir.metrics_options,
            attributes=cog_ir.attributes,
            arguments=args,
            report_groups=cog_ir.report_groups,
            cog_metrics_report_groups=cog_ir.cog_metrics_report_groups,
            expanded_aligned_input_defs=cog_ir.expanded_aligned_input_defs,
        )

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

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return self.instantiation.value_key()

    def generated_repr(self) -> Iterable[representation.ReprInstantiation]:
        """Get all generated representations from the Cog (metrics outputs only)."""
        return self.cog_ir.generated_repr()

    def generated_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Get all generated interfaces from the Cog (metrics outputs only)."""
        return self.cog_ir.generated_interfaces()

    def generated_schemas(self) -> Iterable[schema.Schema]:
        """Get all generated schemas from the Cog (metrics outputs only)."""
        return self.cog_ir.generated_schemas()

    def generated_enums(self) -> Iterable[clkenum.ClkEnum]:
        """Get all generated enums from the Cog (metrics outputs only)."""
        return self.cog_ir.generated_enums()

    def generated_report_group_repr(self) -> Iterable[representation.ReprInstantiation]:
        """Get all generated representations from report groups."""
        return self.cog_ir.generated_report_group_repr()

    def generated_report_group_interfaces(self) -> Iterable[interface.InterfaceInstantiation]:
        """Get all generated interfaces from report groups."""
        return self.cog_ir.generated_report_group_interfaces()

    def generated_report_group_schemas(self) -> Iterable[schema.Schema]:
        """Get all generated schemas from report groups."""
        return self.cog_ir.generated_report_group_schemas()

    def generated_report_group_metadata_schemas(self) -> Iterable[schema.Schema]:
        """Get bespoke metadata schemas from report groups."""
        return self.cog_ir.generated_report_group_metadata_schemas()


def _resolve_string_from_args(
    instantiate_ir: statement.InstantiateStmt, str_param: CogParameterRef, args: Mapping[str, typesys.Value]
) -> str:
    """Resolve a string parameter.

    Args:
        instantiate_ir: Instantiate statement for error messages.
        str_param: Generic string to resolve
        args: Cog parameters.

    Returns:
        The resolved string as a StringValue.
    """
    # channel_name is an expr.Expr that needs to be evaluated
    param_name = str_param.parameter_def.param_name
    if param_name not in args:
        msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' not found in instantiation arguments")
        raise ValueError(msg)
    result = args[param_name]
    if isinstance(result, fmt_string.UnevaluatedFmtString):
        result = result.evaluate_from_scope(instantiate_ir.module.inner_scope)
    if not isinstance(result, primitive.StringValue):
        msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' must be a string value, got {type(result)}")
        raise TypeError(msg)
    return result.value


def _resolve_message_type_from_args(
    instantiate_ir: statement.InstantiateStmt,
    message_type: CogParameterRef | typesys.Instantiation,
    args: Mapping[str, typesys.Value],
) -> schema_reg.InterfaceInfo:
    """Resolve the message type, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate statement for error messages.
        message_type: The type to resolve.
        args: The instantiation arguments.

    Returns:
        The resolved message type.
    """
    if isinstance(message_type, CogParameterRef):
        param_name = message_type.parameter_def.param_name
        if param_name not in args:
            msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' not found in instantiation arguments")
            raise ValueError(msg)
        result = args[param_name]
        if isinstance(result, typesys.Instantiation):
            interface_ref = interface.InterfaceReference.from_typespec(result)
            if isinstance(interface_ref, str):
                msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' is not an instantiated schema")
                raise TypeError(msg)
            result = schema_reg.lookup_interface(instantiate_ir.module.context, interface_ref)
            if not result:
                msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' is not an instantiated schema")
                raise ValueError(msg)
        if not isinstance(result, schema_reg.InterfaceInfo):
            msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' must be an interface, got {type(result)}")
            raise TypeError(msg)
        return result

    substituted = _substitute_cog_params(instantiate_ir, message_type, args)
    interface_ref = interface.InterfaceReference.from_typespec(substituted)
    if isinstance(interface_ref, str):
        msg = instantiate_ir.append_error_line("Message type is not an instantiated schema")
        raise TypeError(msg)
    result = schema_reg.lookup_interface(instantiate_ir.module.context, interface_ref)
    if not result:
        msg = instantiate_ir.append_error_line("Message type is not an instantiated schema")
        raise ValueError(msg)

    return result


def _resolve_state_type_from_args(
    instantiate_ir: statement.InstantiateStmt,
    message_type: CogParameterRef | typesys.Instantiation,
    args: Mapping[str, typesys.Value],
) -> schema_reg.InterfaceInfo | extern_type.ExternType:
    """Resolve the state type, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate statement for error messages.
        message_type: The type to resolve.
        args: The instantiation arguments.

    Returns:
        The resolved state type.
    """
    if isinstance(message_type, CogParameterRef):
        param_name = message_type.parameter_def.param_name
        if param_name not in args:
            msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' not found in instantiation arguments")
            raise ValueError(msg)
        arg = args[param_name]
        if isinstance(arg, extern_type.ExternType):
            return arg

    return _resolve_message_type_from_args(instantiate_ir, message_type, args)


def _substitute_cog_params(
    instantiate_ir: statement.InstantiateStmt,
    instantiation: typesys.Instantiation,
    args: Mapping[str, typesys.Value],
) -> typesys.Instantiation:
    """Substitute CogParameterRef values in types.

    Similar to schema.substitute_parameter_refs, but handles CogParameterRef
    instead of schema.ParameterRef.

    Args:
        instantiate_ir: Instantiate statement used to error messages.
        instantiation: Instantiation possibly containing CogParameterRef values.
        args: Parameter names and values from the cog instantiation.

    Returns:
        An instantiation with all parameters substituted.
    """
    # Recursively substitute in arguments
    result_args: dict[str, typesys.Value] = {}
    for arg_name, arg_val in instantiation.arguments.items():
        if isinstance(arg_val, CogParameterRef):
            param_name = arg_val.parameter_def.param_name
            if param_name not in args:
                msg = instantiate_ir.append_error_line(f"Parameter '{param_name}' not found in instantiation arguments")
                raise ValueError(msg)
            result = args[param_name]
            result_args[arg_name] = result
        elif isinstance(arg_val, typesys.Instantiation):
            result_args[arg_name] = _substitute_cog_params(instantiate_ir, arg_val, args)
        else:
            result_args[arg_name] = arg_val

    return typesys.Instantiation(
        type_info=instantiation.type_info,
        instantiates=instantiation.instantiates,
        arguments=result_args,
    )


def _resolve_generic_configs(
    instantiate_ir: statement.InstantiateStmt,
    cog_configs: dict[str, ConfigDef],
    args: Mapping[str, typesys.Value],
    scope: node.Scope,
) -> dict[str, ConfigDef]:
    """Resolve the configs for a generic cog, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate cog statement for error messages
        cog_configs: Generic cog configs
        args: The instantiate arguments
        scope: Scope to hold the resolved configs

    Returns:
        The resolved cog configs
    """
    result: dict[str, ConfigDef] = {}

    for config in cog_configs.values():
        resolved_config = config.get_resolved()
        if isinstance(resolved_config.message_type, schema_reg.InterfaceInfo):
            result[resolved_config.name] = resolved_config.source
            continue
        message_type = _resolve_message_type_from_args(instantiate_ir, resolved_config.message_type, args)
        resolved_config_copy = copy(resolved_config)
        resolved_config_copy.message_type = message_type
        resolved_config_copy.source = copy(resolved_config.source)
        resolved_config_copy.source.message_type = message_type
        resolved_config_copy.source.resolved = resolved_config_copy
        scope.define(resolved_config_copy.name, resolved_config_copy.source, resolved_config_copy.module.terminals)
        result[resolved_config_copy.name] = resolved_config_copy.source

    return result


def _resolve_generic_states(
    instantiate_ir: statement.InstantiateStmt,
    cog_states: dict[str, StateDef],
    args: Mapping[str, typesys.Value],
    scope: node.Scope,
) -> dict[str, StateDef]:
    """Resolve the states for a generic cog, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate cog statement for error messages
        cog_states: Generic cog states
        args: The instantiate arguments
        scope: Scope to hold the resolved states

    Returns:
        The resolved cog states
    """
    result: dict[str, StateDef] = {}

    for state in cog_states.values():
        resolved_state = state.get_resolved()
        if isinstance(resolved_state.message_type, schema_reg.InterfaceInfo | extern_type.ExternType):
            result[resolved_state.name] = resolved_state.source
            continue
        message_type = _resolve_state_type_from_args(instantiate_ir, resolved_state.message_type, args)
        resolved_state_copy = copy(resolved_state)
        resolved_state_copy.message_type = message_type
        resolved_state_copy.source = copy(resolved_state.source)
        resolved_state_copy.source.resolved = resolved_state_copy
        resolved_state_copy.source.message_type = message_type
        scope.define(resolved_state_copy.name, resolved_state_copy.source, resolved_state_copy.module.terminals)
        result[resolved_state_copy.name] = resolved_state_copy.source

    return result


def _resolve_generic_diagnostics(
    instantiate_ir: statement.InstantiateStmt,
    cog_diagnostics: dict[str, DiagnosticsDef],
    args: Mapping[str, typesys.Value],
    scope: node.Scope,
) -> dict[str, DiagnosticsDef]:
    """Resolve the diagnostics for a generic cog, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate statement for error messages
        cog_diagnostics: Generic cog diagnostics
        args: The instantiate arguments
        scope: Scope to hold the resolved diagnostics

    Returns:
        The resolved cog diagnostics
    """
    result: dict[str, DiagnosticsDef] = {}

    for diagnostic in cog_diagnostics.values():
        if not isinstance(diagnostic.group_id, CogParameterRef) and not isinstance(
            diagnostic.instance_id, CogParameterRef
        ):
            result[diagnostic.name] = diagnostic
            continue
        diagnostic_copy = copy(diagnostic)
        if isinstance(diagnostic_copy.group_id, CogParameterRef):
            diagnostic_copy.group_id = _resolve_string_from_args(instantiate_ir, diagnostic_copy.group_id, args)
        if isinstance(diagnostic_copy.instance_id, CogParameterRef):
            diagnostic_copy.instance_id = _resolve_string_from_args(instantiate_ir, diagnostic_copy.instance_id, args)
        scope.define(diagnostic_copy.name, diagnostic_copy, diagnostic_copy.module.terminals)
        result[diagnostic_copy.name] = diagnostic_copy

    return result


def _resolve_generic_inputs(
    instantiate_ir: statement.InstantiateStmt,
    cog_inputs: dict[str, InputDef],
    args: Mapping[str, typesys.Value],
    scope: node.Scope,
) -> dict[str, InputDef]:
    """Resolve the inputs for a generic cog, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate cog statement for error messages
        cog_inputs: Generic cog inputs
        args: The instantiate arguments
        scope: Scope to hold the resolved inputs

    Returns:
        The resolved cog inputs
    """
    result: dict[str, InputDef] = {}

    for cog_input in cog_inputs.values():
        if not isinstance(cog_input.message_type, CogParameterRef | typesys.Instantiation):
            result[cog_input.name] = cog_input
            continue
        input_copy = copy(cog_input)
        input_copy.message_type = _resolve_message_type_from_args(instantiate_ir, cog_input.message_type, args)
        scope.define(input_copy.name, input_copy, input_copy.module.terminals)
        result[input_copy.name] = input_copy

    return result


def _resolve_generic_outputs(
    instantiate_ir: statement.InstantiateStmt,
    cog_outputs: dict[str, OutputDef],
    args: Mapping[str, typesys.Value],
    scope: node.Scope,
) -> dict[str, OutputDef]:
    """Resolve the outputs for a generic cog, substituting parameters as needed.

    Args:
        instantiate_ir: Instantiate cog statement for error messages
        cog_outputs: Generic cog outputs
        args: The instantiate arguments
        scope: Scope to hold the resolved outputs

    Returns:
        The resolved cog outputs
    """
    result: dict[str, OutputDef] = {}

    for cog_output in cog_outputs.values():
        if not isinstance(cog_output.message_type, CogParameterRef | typesys.Instantiation):
            result[cog_output.name] = cog_output
            continue
        output_copy = copy(cog_output)
        output_copy.message_type = _resolve_message_type_from_args(instantiate_ir, cog_output.message_type, args)
        scope.define(output_copy.name, output_copy, output_copy.module.terminals)
        result[output_copy.name] = output_copy

    return result


def _resolve_generic_rate_limits(
    rate_limits: dict[str, RateLimitSpec],
    cog_outputs: dict[str, OutputDef],
) -> dict[str, RateLimitSpec]:
    """Resolve the rate limits for a generic cog, substituting parameters as needed.

    Args:
        rate_limits: Generic cog rate limits
        cog_outputs: Resolved cog outputs

    Returns:
        The resolved cog rate limits
    """
    result: dict[str, RateLimitSpec] = {}

    for rate_limit_name, rate_limit in rate_limits.items():
        resolved_rate_limit = rate_limit.get_resolved()
        if not isinstance(resolved_rate_limit.output.message_type, CogParameterRef | typesys.Instantiation):
            result[rate_limit_name] = rate_limit
            continue
        resolved_output = cog_outputs[resolved_rate_limit.output.name]
        resolved_rate_limit_copy = copy(resolved_rate_limit)
        resolved_rate_limit_copy.output = resolved_output
        rate_limit_copy = copy(rate_limit)
        rate_limit_copy.resolved = resolved_rate_limit_copy
        result[rate_limit_name] = rate_limit_copy

    return result


def _resolve_guarded_components(  # noqa: PLR0913, C901 # mitigated with kwonly args, many cases with simple handling
    *,
    guarded_components: list[GuardedComponents[CogComponent]],
    arguments: Mapping[str, typesys.Value],
    resources: dict[str, ResourceDef],
    configs: dict[str, ConfigDef],
    states: dict[str, StateDef],
    inputs: dict[str, InputDef],
    outputs: dict[str, OutputDef],
    scope: node.Scope,
    module: node.Module,
) -> None:
    assert scope.parent is not None
    assert module.terminals is not None

    # DFL doesn't understand how to deal with CogParameterRefs, so we clone the
    # cog instance's scope and replace each of the cog's paremeters with their
    # actual values.
    dfl_scope = scope.parent.make_anon_child_scope("dfl_eval")
    for k, v in arguments.items():
        dfl_scope.define(k, node.NamedBindingRef(name=k, value=v, scope=dfl_scope), module.terminals)

    ctx = dfl.Context(scope=dfl_scope, terminals=module.terminals, module_id=module.module_id)
    traits = dfl_types.get_trait_registry(module)
    for guard in guarded_components:
        for result in guard.evaluate(ctx, module, traits):
            match result:
                case ResourceDef(name=name):
                    resources[name] = result
                case ConfigDef(name=name):
                    configs[name] = result
                case StateDef(name=name):
                    states[name] = result
                case InputDef(name=name):
                    inputs[name] = result
                case OutputDef(name=name):
                    outputs[name] = result
                case _:
                    msg = f"Conditional statements not implemented for {type(result)}"
                    raise NotImplementedError(ctx.format_error(guard.guard.span, msg))

    # The constructors for any components released from their guards will have
    # added new entires to the temporary scope. Copy them to the cog instance's
    # scope so they get included in the resolution step later.
    for k, v in dfl_scope.names.items():
        if k not in arguments:
            scope.define(k, v, module.terminals)


@dataclass
class InstantiatedCogFactory(node.CstNode[cst.NewStmt], typesys.InstantiatableEntity):
    """Factory to instantiate CogInstance entities from a NewStmt."""

    new_stmt: statement.NewStmt

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
        assert self.new_stmt.instantiation
        evaluated_instantiation = fmt_string.evaluate_instantiation_strings(self.new_stmt.instantiation, scope)
        instantiated_cog = lookup_instantiated_cog(module.context, evaluated_instantiation)
        if not instantiated_cog:
            msg = f"Cog has not been instantiated: ({evaluated_instantiation.value_key()})."
            if cst_node and source_module:
                msg = node.append_error_line(cst_node, source_module, msg)
            raise ValueError(msg)
        return instantiated_cog.make_instance(
            cst_node=cst_node, module=module, source_module=source_module, scope=scope, name=name, doc=doc
        )


INSTANTIATED_COG_KEY_TYPE: TypeAlias = str


@dataclass(frozen=True, eq=True, slots=True)
class InstantiatedCogInfo:
    """Represents an instantiated cog in the registry.

    Attributes:
        instantiated_cog: The instantiated cog
        type_key: The computed unique key for this instantiated cog
    """

    instantiated_cog: InstantiatedCog = field(compare=False)
    type_key: INSTANTIATED_COG_KEY_TYPE

    @classmethod
    def make(
        cls: type[InstantiatedCogInfo],
        instantiated_cog: InstantiatedCog,
    ) -> InstantiatedCogInfo:
        """Construct a InstantiatedCogInfo with computed key.

        Args:
            instantiated_cog: The instantiated cog to be registered

        Returns:
            An object for referencing this instantiated cog uniquely in the registry.
        """
        type_key = cls.key_for(instantiated_cog)
        return cls(instantiated_cog=instantiated_cog, type_key=type_key)

    @staticmethod
    def key_for(
        instantiated_cog: InstantiatedCog | typesys.Instantiation,
    ) -> INSTANTIATED_COG_KEY_TYPE:
        """Compute the registry key for the given instantiated cog."""
        return instantiated_cog.value_key()


class InstantiatedCogRegistry(Context):
    """Registry for instantiated cogs."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty instantiated cog registry."""
        self.name = name
        self.registry: dict[INSTANTIATED_COG_KEY_TYPE, InstantiatedCogInfo] = {}

    @override
    def import_from(self, other: InstantiatedCogRegistry) -> None:
        """Combine this registry with items from another.

        Arguments:
            other: Registry to combine.

        Raises:
            RuntimeError: If a type already exists with different instantiated cog info.
        """
        for key, item_info in other.registry.items():
            if key in self.registry and self.registry[key] != item_info:
                msg = f"Type {key} has conflicting item info"
                raise RuntimeError(msg)
            self.registry[key] = item_info


def register_instantiated_cog(compiler_context: CompilerContext, instantiated_cog: InstantiatedCog) -> None:
    """Registers an instantiated cog in the context.

    Args:
        compiler_context: Compiler context containing the registry.
        instantiated_cog: Instantiated cog to register.

    Raises:
        ValueError: If the instantiated cog is already registered.
    """
    item_info = InstantiatedCogInfo.make(instantiated_cog)
    registry = compiler_context[INSTANTIATED_COG_REGISTRY_KEY]
    try:
        existing_type = registry.registry[item_info.type_key]
        msg = f"Type {item_info} already registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    registry.registry[item_info.type_key] = item_info


def lookup_instantiated_cog(
    compiler_context: CompilerContext, instantiation: typesys.Instantiation
) -> InstantiatedCog | None:
    """Look up a instantiated cog in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        instantiation: An instantiation for a generic Cog.

    Returns:
        The registry entry if found, else None.
    """
    registry = compiler_context[INSTANTIATED_COG_REGISTRY_KEY]
    try:
        return registry.registry[InstantiatedCogInfo.key_for(instantiation)].instantiated_cog
    except KeyError:
        return None


class InstantiatedCogRegistryKey(ContextKey[InstantiatedCogRegistry]):
    """Compiler context key for cog registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> InstantiatedCogRegistry:
        """Create a default instance of the registry."""
        return InstantiatedCogRegistry(compiler_context.name)


INSTANTIATED_COG_REGISTRY_KEY: Final = InstantiatedCogRegistryKey("InstantiatedCogRegistry")
