# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner cog code generation."""

from __future__ import annotations

from decimal import Decimal
from functools import reduce

from clockwork.dsl.aligner.gen.alignment_schema import generate_alignment_schema
from clockwork.dsl.ir import aligner, cog, node, schema, schema_reg
from clockwork.dsl.ir.aligner_metrics_report_groups import generate_aligner_metrics_report_groups
from clockwork.dsl.ir.clkbuiltins import (
    COG_CONDITION_TYPE,
    COG_CONFIG_TYPE,
    COG_INPUT_TYPE,
    COG_OUTPUT_TYPE,
    TYPE_TYPE,
)
from clockwork.dsl.ir.cog import (
    BinaryConditionExpr,
    ConditionOp,
    ExecutionSpec,
    MetricsOptions,
    ResolvedStateDef,
    ResolvedStateParams,
    SimpleConditionExpr,
    StateDef,
    StateParams,
)
from clockwork.dsl.ir.cog_components import ConditionDef, DynamicTimer, InputDef, NewMessagePresent, OutputDef
from clockwork.dsl.ir.diagnostics import InfraDiagnosticsDef
from clockwork.dsl.ir.interface import InterfaceInstantiation
from clockwork.dsl.ir.representation import ResolvedReprInstantiation


def _make_input_defs(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
    inner_scope: node.Scope,
) -> dict[str, InputDef]:
    """Create ``InputDef`` IR nodes for each aligner input."""
    inputs: dict[str, InputDef] = {}
    for resolved_input in aligner_ir.inputs.values():
        # fmt: off
        input_def = InputDef(
            module=module,
            cst_node=None,
            doc=resolved_input.doc,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=resolved_input.name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=inner_scope,
            message_type=resolved_input.interface_info,
            type_info=COG_INPUT_TYPE,
            view_params=resolved_input.view_params,
            is_generic=False,
            elements=None,
        )
        # fmt: on
        inner_scope.define(resolved_input.name, input_def, None)
        inputs[resolved_input.name] = input_def
    return inputs


def _has_optional_with_nonzero_timeout(aligner_ir: aligner.ResolvedAligner) -> bool:
    """Return True if the aligner has at least one optional input with a non-zero timeout."""
    return any(
        inp.optional and inp.timeout is not None and inp.timeout.value > Decimal(0)
        for inp in aligner_ir.inputs.values()
    )


def _make_execution_spec(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
    inner_scope: node.Scope,
) -> tuple[dict[str, ConditionDef], ExecutionSpec]:
    """Build the aligner execution condition.

    Creates one ``ConditionDef`` per input (new_message) combined with OR.
    If the aligner has optional inputs with non-zero timeouts, a
    ``DynamicTimer`` condition is also added to the OR chain.

    Returns:
        A tuple of (conditions dict, execution spec).
    """
    conditions: dict[str, ConditionDef] = {}
    condition_exprs: list[SimpleConditionExpr] = []

    for input_name in aligner_ir.inputs:
        cond_name = f"new_{input_name}"
        new_msg = NewMessagePresent(
            module=module,
            cst_node=None,
            input_name=input_name,
            lower_bound=1,
            upper_bound=None,
        )
        # fmt: off
        cond_def = ConditionDef(
            module=module,
            cst_node=None,
            doc=None,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=cond_name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=inner_scope,
            type_info=COG_CONDITION_TYPE,
            condition=new_msg,
        )
        # fmt: on
        inner_scope.define(cond_name, cond_def, None)
        conditions[cond_name] = cond_def

        condition_exprs.append(
            SimpleConditionExpr(
                module=module,
                cst_node=None,
                condition=cond_def,
            )
        )

    if _has_optional_with_nonzero_timeout(aligner_ir):
        timer_cond_name = "optional_timer"
        timer_cond = DynamicTimer()
        # fmt: off
        timer_cond_def = ConditionDef(
            module=module,
            cst_node=None,
            doc=None,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=timer_cond_name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=inner_scope,
            type_info=COG_CONDITION_TYPE,
            condition=timer_cond,
        )
        # fmt: on
        inner_scope.define(timer_cond_name, timer_cond_def, None)
        conditions[timer_cond_name] = timer_cond_def

        condition_exprs.append(
            SimpleConditionExpr(
                module=module,
                cst_node=None,
                condition=timer_cond_def,
            )
        )

    combined: cog.ConditionExpr = reduce(
        lambda lhs, rhs: BinaryConditionExpr(
            module=module,
            cst_node=None,
            lhs=lhs,
            rhs=rhs,
            op=ConditionOp.OR,
        ),
        condition_exprs,
    )

    exec_spec = ExecutionSpec(
        module=module,
        cst_node=None,
        doc=None,
        condition=combined,
    )
    return conditions, exec_spec


_ALIGNER_STATE_SCHEMA_NAME = "AlignerState"
_ALIGNER_STATE_MODULE_FQN = "@clockwork::std::aligners::state"


def _find_aligner_state_schema(
    module: node.Module,
) -> schema.Schema | None:
    """Find the AlignerState schema in the module's imports.

    Searches by FQN so the result is independent of local import names
    or ``as`` aliases.  Handles both module-level imports
    (``use std::aligners::state;``) and entity-level imports
    (``use std::aligners::state::{AlignerState};``).
    """
    externs_scope = module.inner_scope.parent
    assert externs_scope is not None

    for entity in externs_scope.names.values():
        if isinstance(entity, node.NamespacedModule):
            if entity.extern_module.module_id.get_fqn() == _ALIGNER_STATE_MODULE_FQN:
                result = entity.lookup(_ALIGNER_STATE_SCHEMA_NAME)
                if isinstance(result, schema.Schema):
                    return result
        elif (
            isinstance(entity, schema.Schema)
            and entity.name == _ALIGNER_STATE_SCHEMA_NAME
            and entity.module.module_id.get_fqn() == _ALIGNER_STATE_MODULE_FQN
        ):
            return entity

    return None


def _make_state_defs(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
    inner_scope: node.Scope,
) -> dict[str, StateDef]:
    """Create state endpoint for the aligner timeout protocol.

    When the aligner has optional inputs with non-zero timeouts, this creates a
    mutable state endpoint referencing the ``AlignerState`` schema from
    ``@clockwork::std::aligners::state``. The user's module must import that schema module.

    Returns:
        A dict with the ``"aligner_state"`` entry, or empty if no timeouts.

    Raises:
        ValueError: If the ``@clockwork::std::aligners::state`` module is not imported.
    """
    if not _has_optional_with_nonzero_timeout(aligner_ir):
        return {}

    aligner_state_schema = _find_aligner_state_schema(module)
    if aligner_state_schema is None:
        msg = (
            f"Aligner '{aligner_ir.name}' has optional inputs with nonzero timeouts "
            "but the AlignerState schema is not imported. "
            "Add 'use @clockwork::std::aligners::state;' "
            "to your module."
        )
        raise ValueError(msg)
    state_iface_inst = InterfaceInstantiation.from_schema(aligner_state_schema, module)
    state_iface_info = schema_reg.InterfaceInfo.make(state_iface_inst)

    state_params = StateParams(module=module, cst_node=None, mutable=True)
    resolved_params = ResolvedStateParams(
        module=module,
        cst_node=None,
        mutable=True,
        source=state_params,
    )
    state_params.resolved = resolved_params

    state_doc = node.Doc(
        module=module,
        cst_node=None,
        value="Aligner timeout state for tracking partial alignment start time",
    )

    # fmt: off
    state_def = StateDef(
        module=module,
        cst_node=None,
        doc=state_doc,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        name="aligner_state",
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        scope=inner_scope,
        type_info=COG_CONFIG_TYPE,
        message_type=state_iface_info,
        params=state_params,
        is_generic=False,
    )
    # fmt: on
    # fmt: off
    state_def.resolved = ResolvedStateDef(
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        name="aligner_state",
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        scope=inner_scope,
        module=module,
        cst_node=None,
        doc=state_doc,
        type_info=COG_CONFIG_TYPE,
        message_type=state_iface_info,
        params=resolved_params,
        source=state_def,
    )
    # fmt: on
    inner_scope.define("aligner_state", state_def, None)

    return {"aligner_state": state_def}


def make_aligner_cog(
    aligner_ir: aligner.ResolvedAligner,
    module: node.Module,
) -> cog.Cog:
    """Create a synthetic ``cog.Cog`` IR for an aligner.

    Args:
        aligner_ir: The resolved aligner definition.
        module: The module containing the aligner.

    Returns:
        An IR node for the aligner cog.
    """
    cog_name = aligner_ir.name
    inner_scope = module.inner_scope.make_child_scope(cog_name)

    alignment_schema = generate_alignment_schema(aligner_ir, module)
    schema_source = alignment_schema.schema.source
    assert schema_source is not None

    iface_inst = InterfaceInstantiation.from_schema(schema_source, module)
    iface_info = schema_reg.InterfaceInfo.make(iface_inst)
    repr_inst = ResolvedReprInstantiation.from_schema(schema_source, module)

    aligner_ir.source.alignment_schema = alignment_schema
    aligner_ir.source.alignment_repr = repr_inst
    aligner_ir.source.alignment_iface = iface_inst

    # fmt: off
    output_def = OutputDef(
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        name="alignment",
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        scope=inner_scope,
        type_info=COG_OUTPUT_TYPE,
        doc=node.Doc(
            module=module,
            cst_node=None,
            value=f"Alignment result output for {aligner_ir.source.fqn}",
        ),
        module=module,
        cst_node=None,
        message_type=iface_info,
        is_generic=False,
    )
    # fmt: on
    inner_scope.define("alignment", output_def, None)

    inputs = _make_input_defs(aligner_ir, module, inner_scope)
    conditions, execution_spec = _make_execution_spec(aligner_ir, module, inner_scope)
    states = _make_state_defs(aligner_ir, module, inner_scope)

    input_defs_list = list(inputs.values())
    infra_diags = InfraDiagnosticsDef.make(module, inner_scope, input_defs_list, [output_def])
    infra_diags.resolve()

    synthetic_cog = cog.Cog(
        type_info=TYPE_TYPE,
        cst_node=None,
        module=module,
        name=cog_name,
        scope=module.inner_scope,
        doc=node.Doc(
            module=module,
            cst_node=None,
            value=f"Generated aligner cog for {aligner_ir.source.fqn}",
        ),
        inner_scope=inner_scope,
        resources={},
        configs={},
        states=states,
        diagnostics={},
        infra_diagnostics=infra_diags,
        inputs=inputs,
        aligned_inputs={},
        outputs={"alignment": output_def},
        metrics_outputs={},
        conditions=conditions,
        rate_limits={},
        execution_spec=execution_spec,
        simulation_options=None,
        python_options=None,
        metrics_options=MetricsOptions(
            metrics_enabled=False,
            batch_size=10,
            module=module,
            cst_node=None,
        ),
        attributes=None,
        report_groups={},
    )

    aligner_metrics_rgs = generate_aligner_metrics_report_groups(
        module=module,
        parent_scope=inner_scope,
        aligner=aligner_ir.source,
        input_names=list(inputs.keys()),
        output_names=["alignment"],
        condition_names=list(conditions.keys()),
    )
    synthetic_cog.cog_metrics_report_groups = aligner_metrics_rgs
    for rg in aligner_metrics_rgs.values():
        rg.resolve(inner_scope, cog_name)

    return synthetic_cog
