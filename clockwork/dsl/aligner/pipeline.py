# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — unified analysis and codegen plan pipeline.

Chains analysis stages (spec extraction, STN analysis, join plan)
into a single entry point.  Builds the all-present join plan used by
C++ code generation and computes maximally permissive windows across
``has_candidates`` branch variants.

See docs/README.md for the pipeline overview.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.direction_analysis import DirectionAnalysis, compute_direction_analysis
from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    EqualityConstraint,
    ExtractedSpecs,
    FirstInBatch,
    LastInBatch,
    Objective,
    base_input_name,
    extract_specs,
    try_field_accessor,
)
from clockwork.dsl.aligner.gen.codegen_plan import (
    CodegenLevel,
    CodegenPlan,
    ConditionalBranch,
    Constraint,
)
from clockwork.dsl.aligner.join_plan import JoinLevel, JoinPlan, compute_join_plan
from clockwork.dsl.aligner.objective_analysis import (
    SearchType,
    decompose_additive,
    referenced_inputs,
)
from clockwork.dsl.aligner.partition_plan import compute_partition_plan
from clockwork.dsl.aligner.presence import (
    MAX_OPTIONAL_INPUTS,
    PresenceConfig,
    compute_maximally_permissive_windows,
    merge_specs_for_config,
)
from clockwork.dsl.aligner.stn import StnAnalysis, analyze_stn
from clockwork.dsl.ir import clkbuiltins, dfl
from clockwork.dsl.ir import units as ir_units
from clockwork.dsl.ir.schema import InstantiatedSchema

if TYPE_CHECKING:
    from clockwork.dsl.aligner.extract_specs import FieldAccessor, InputSelector
    from clockwork.dsl.aligner.join_plan import FeasibleWindow
    from clockwork.dsl.ir.aligner import Aligner, ResolvedAligner


class PipelineError(Exception):
    """Raised when the analysis pipeline encounters an invalid configuration."""


@dataclass(frozen=True, slots=True)
class ConfigAnalysis:
    """Analysis results for one presence configuration.

    Each optional-input presence configuration produces a distinct STN
    and join plan.

    Attributes:
        config: The presence configuration this analysis is for.
        stn: STN analysis result (Floyd-Warshall distance matrix, equality classes).
        join_plan: Ordered join levels with search strategies and feasible windows.
    """

    config: PresenceConfig
    stn: StnAnalysis
    join_plan: JoinPlan


@dataclass(frozen=True, slots=True)
class AlignerAnalysis:
    """Complete analysis results for an aligner across all presence configurations.

    Attributes:
        aligner_name: Name of the analyzed aligner.
        specs: Extracted specs (shared across configs, partitioned into
            unconditional and conditional by ``extract_specs``).
        config_analyses: Analysis results keyed by presence configuration.
            Contains the all-present configuration used by code generation.
        optional_inputs: Names of optional inputs, for reference.
        member_to_level: Mapping from InputSelector to level index in the
            all-present join plan.  Computed once and shared between
            direction analysis and codegen plan.
        direction_analyses: Per-level constraint/objective direction analysis
            for the all-present join plan.
    """

    aligner_name: str
    specs: ExtractedSpecs
    config_analyses: dict[PresenceConfig, ConfigAnalysis]
    optional_inputs: frozenset[str]
    maximally_permissive_windows: dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow]
    member_to_level: dict[InputSelector, int]
    direction_analyses: tuple[DirectionAnalysis, ...]


def build_member_to_level(
    levels: tuple[JoinLevel, ...],
) -> dict[InputSelector, int]:
    """Map each InputSelector to its level index in a join plan.

    Built once for the all-present join plan and stored on
    :class:`AlignerAnalysis` for reuse by direction analysis and
    codegen plan computation.
    """
    return {level.input: idx for idx, level in enumerate(levels)}


def _analyze_config(
    config: PresenceConfig,
    config_specs: ExtractedSpecs,
    aligner_node: Aligner,
    active_inputs: frozenset[InputSelector],
) -> ConfigAnalysis:
    """Run STN → join plan for one presence configuration.

    Args:
        config: The presence configuration.
        config_specs: Config-specific merged specs.
        aligner_node: The aligner IR node.
        active_inputs: Input names active in this configuration.

    Returns:
        Analysis results for this configuration.
    """
    stn = analyze_stn(config_specs.unconditional_constraints)

    join_plan = compute_join_plan(
        inputs=tuple(sorted(active_inputs, key=str)),
        objectives=config_specs.unconditional_objectives,
        active_inputs=active_inputs,
        stn=stn,
        aligner_node=aligner_node,
        equalities=config_specs.unconditional_equalities,
        field_assumptions=config_specs.field_assumptions,
    )
    return ConfigAnalysis(
        config=config,
        stn=stn,
        join_plan=join_plan,
    )


def _collect_objective_referenced_selectors(specs: ExtractedSpecs) -> frozenset[InputSelector]:
    """Return every ``InputSelector`` that appears in any objective expression.

    Includes ``FirstInBatch`` / ``LastInBatch`` so that batch endpoints pinned
    by an objective can be detected individually.
    """
    selectors: set[InputSelector] = set()
    for obj in specs.unconditional_objectives:
        selectors |= referenced_inputs(obj.expr)
    for objs in specs.conditional_objectives.values():
        for obj in objs:
            selectors |= referenced_inputs(obj.expr)
    for objs in specs.else_objectives.values():
        for obj in objs:
            selectors |= referenced_inputs(obj.expr)
    return frozenset(selectors)


def _active_selectors_for_names(
    resolved: ResolvedAligner,
    active_names: frozenset[str],
) -> frozenset[InputSelector]:
    """Expand active input names to join-plan selectors.

    Batch inputs are represented by two selector levels, one for the
    first message and one for the last message in the selected batch.
    """
    active_inputs: set[InputSelector] = set()
    for name in active_names:
        inp = resolved.inputs[name]
        if inp.batch_size is not None:
            active_inputs |= {FirstInBatch(name), LastInBatch(name)}
        else:
            active_inputs.add(name)
    return frozenset(active_inputs)


def _collect_branching_optionals(specs: ExtractedSpecs) -> frozenset[str]:
    """Return optionals whose ``has_candidates`` constraints need branch variants."""
    return frozenset(set(specs.conditional_constraints) | set(specs.else_constraints))


def _validate_arbitrary_selection(
    resolved: ResolvedAligner,
    specs: ExtractedSpecs,
    all_present_plan: JoinPlan,
) -> None:
    """Require each aligner input to either constrain selection or opt into ``arbitrary_selection``.

    Rule A (non-batched inputs): if the input's join-plan level is
    ``ANY_MATCH`` and no objective references the input, the solver would
    silently pick an arbitrary candidate. Require either an objective or
    an explicit ``arbitrary_selection: true`` opt-in.

    Rule B (batched inputs): both the ``FirstInBatch`` and ``LastInBatch``
    endpoints must be referenced by at least one objective, otherwise one
    end of the batch slides freely. ``arbitrary_selection: true`` opts out.
    """
    objective_refs = _collect_objective_referenced_selectors(specs)
    level_by_selector = {level.input: level for level in all_present_plan.levels}

    for name, inp in resolved.inputs.items():
        if inp.arbitrary_selection:
            continue
        if inp.batch_size is None:
            level = level_by_selector.get(name)
            assert level is not None, (
                f"Aligner '{resolved.name}' input '{name}' missing from all-present join plan; "
                "this indicates a programming error in join planning."
            )
            if level.search_type is not SearchType.ANY_MATCH:
                continue
            if name in objective_refs:
                continue
            msg = (
                f"Aligner '{resolved.name}' input '{name}' would be selected arbitrarily: "
                "the join plan has no constraint that pins it and no objective references it. "
                "Add a minimize(...) or maximize(...) that references this input, or set "
                "'arbitrary_selection: true' on the input to acknowledge arbitrary selection."
            )
            raise PipelineError(msg)
        first = FirstInBatch(name)
        last = LastInBatch(name)
        missing: list[str] = []
        if first not in objective_refs:
            missing.append("first-in-batch")
        if last not in objective_refs:
            missing.append("last-in-batch")
        if not missing:
            continue
        msg = (
            f"Aligner '{resolved.name}' batched input '{name}' has unconstrained "
            f"{' and '.join(missing)} endpoint(s): the solver may slide the batch boundary "
            "arbitrarily within the feasible window. Add a minimize/maximize referencing "
            f"min({name}.<field>) and/or max({name}.<field>) as appropriate, or set "
            "'arbitrary_selection: true' on the input."
        )
        raise PipelineError(msg)


def analyze_aligner(
    aligner_node: Aligner,
) -> AlignerAnalysis:
    """Run the full analysis pipeline on an aligner.

    Chains: extract_specs → all-present STN/join plan.

    The aligner must already be resolved and type-checked before calling
    this function.

    Args:
        aligner_node: The Aligner IR node (must have ``resolved`` set and
            body statements must have been type-checked).

    Returns:
        Complete analysis results for code generation.

    Raises:
        PipelineError: If the aligner has too many ``has_candidates``
            optionals to analyze branch variants.
        Any errors from individual stages propagate directly
        (``InconsistentConstraintsError``, ``SpecExtractionError``,
        ``JoinPlanError``, etc.).
    """
    resolved = aligner_node.resolved
    if resolved is None:
        msg = f"Aligner '{aligner_node.name}' must be resolved before analysis"
        raise PipelineError(msg)

    specs = extract_specs(aligner_node)

    optional_inputs = frozenset(name for name, inp in resolved.inputs.items() if inp.optional)
    required_inputs = frozenset(name for name in resolved.inputs if name not in optional_inputs)

    branching_optionals = _collect_branching_optionals(specs)
    if len(branching_optionals) > MAX_OPTIONAL_INPUTS:
        msg = (
            f"Aligner '{resolved.name}' has {len(branching_optionals)} optionals with has_candidates constraints, "
            f"exceeding the maximum of {MAX_OPTIONAL_INPUTS}. "
            f"Branch variant enumeration would be intractable."
        )
        raise PipelineError(msg)

    mp_windows = compute_maximally_permissive_windows(specs, optional_inputs)

    all_present_config = PresenceConfig(frozenset(optional_inputs))
    all_present_specs = merge_specs_for_config(specs, all_present_config, all_optional_inputs=optional_inputs)
    all_active_inputs = _active_selectors_for_names(resolved, required_inputs | optional_inputs)
    all_present_analysis = _analyze_config(
        config=all_present_config,
        config_specs=all_present_specs,
        aligner_node=aligner_node,
        active_inputs=all_active_inputs,
    )
    config_analyses: dict[PresenceConfig, ConfigAnalysis] = {all_present_config: all_present_analysis}
    all_present_plan = config_analyses[all_present_config].join_plan
    member_to_level = build_member_to_level(all_present_plan.levels)
    direction_analyses = compute_direction_analysis(all_present_plan, specs, member_to_level)

    _validate_arbitrary_selection(resolved, specs, all_present_plan)

    return AlignerAnalysis(
        aligner_name=resolved.name,
        specs=specs,
        config_analyses=config_analyses,
        optional_inputs=optional_inputs,
        maximally_permissive_windows=mp_windows,
        member_to_level=member_to_level,
        direction_analyses=direction_analyses,
    )


def _get_constraint_endpoints(
    constraint: Constraint,
) -> tuple[FieldAccessor, FieldAccessor]:
    """Extract the two endpoints from a constraint.

    Returns ``(a, b)`` where ``a`` and ``b`` are field accessors.
    For difference constraints: ``(minuend, subtrahend)``.
    For equality constraints: ``(left, right)``.
    """
    match constraint:
        case DifferenceConstraint(minuend=a, subtrahend=b):
            return (a, b)
        case EqualityConstraint(left=a, right=b):
            return (a, b)


@dataclass
class _LevelBuckets:
    """Mutable accumulator for per-level constraint assignment."""

    unconditional: list[list[Constraint]]
    when_present: list[dict[str, list[Constraint]]]
    when_absent: list[dict[str, list[Constraint]]]

    @classmethod
    def empty(cls, n_levels: int) -> _LevelBuckets:
        """Create empty bucket lists for ``n_levels``."""
        return cls(
            unconditional=[[] for _ in range(n_levels)],
            when_present=[{} for _ in range(n_levels)],
            when_absent=[{} for _ in range(n_levels)],
        )


def _classify_unconditional(
    constraint: Constraint,
    member_to_level: dict[InputSelector, int],
    optional_inputs: frozenset[str],
    buckets: _LevelBuckets,
) -> None:
    """Assign an unconditional constraint to its level bucket.

    Same-level constraints are always unconditional.  For cross-level
    constraints, the earlier endpoint determines classification:
    required earlier → unconditional, optional earlier → when_present
    (auto-drop).
    """
    a, b = _get_constraint_endpoints(constraint)
    level_a = member_to_level[a.input_name]
    level_b = member_to_level[b.input_name]
    target_level = max(level_a, level_b)

    if level_a == level_b:
        buckets.unconditional[target_level].append(constraint)
        return

    earlier_input = b.input_name if target_level == level_a else a.input_name
    if base_input_name(earlier_input) in optional_inputs:
        name = base_input_name(earlier_input)
        buckets.when_present[target_level].setdefault(name, []).append(constraint)
    else:
        buckets.unconditional[target_level].append(constraint)


def _classify_conditional(
    constraint: Constraint,
    opt_name: str,
    member_to_level: dict[InputSelector, int],
    target: list[dict[str, list[Constraint]]],
) -> None:
    """Assign a has_candidates constraint to its level bucket."""
    a, b = _get_constraint_endpoints(constraint)
    level_a = member_to_level[a.input_name]
    level_b = member_to_level[b.input_name]
    target_level = max(level_a, level_b)
    target[target_level].setdefault(opt_name, []).append(constraint)


def _assign_constraints_to_levels(
    n_levels: int,
    specs: ExtractedSpecs,
    member_to_level: dict[InputSelector, int],
    optional_inputs: frozenset[str],
) -> _LevelBuckets:
    """Assign all constraints to levels and classify them.

    Returns a :class:`_LevelBuckets` with three parallel lists of
    length ``n_levels``.
    """
    buckets = _LevelBuckets.empty(n_levels)

    for c in (*specs.unconditional_constraints, *specs.unconditional_equalities):
        _classify_unconditional(c, member_to_level, optional_inputs, buckets)

    for opt, constraints in specs.conditional_constraints.items():
        for c in constraints:
            _classify_conditional(c, opt, member_to_level, buckets.when_present)
    for opt, equalities in specs.conditional_equalities.items():
        for eq in equalities:
            _classify_conditional(eq, opt, member_to_level, buckets.when_present)

    for opt, constraints in specs.else_constraints.items():
        for c in constraints:
            _classify_conditional(c, opt, member_to_level, buckets.when_absent)
    for opt, equalities in specs.else_equalities.items():
        for eq in equalities:
            _classify_conditional(eq, opt, member_to_level, buckets.when_absent)

    return buckets


def _compute_initial_windows(
    levels: tuple[JoinLevel, ...],
    maximally_permissive_windows: dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow],
    member_to_level: dict[InputSelector, int],
    optional_inputs: frozenset[str],
) -> list[tuple[FeasibleWindow, ...]]:
    """Filter maximally permissive windows to each level.

    For level at index i, include windows where the target accessor
    belongs to this level's input and the reference accessor belongs
    to an earlier (already-bound) level.

    Windows with an optional-input reference are excluded because the
    reference iterator may be ``std::nullopt`` at runtime, making
    unconditional dereference unsafe.
    """
    result: list[tuple[FeasibleWindow, ...]] = []
    for idx, level in enumerate(levels):
        windows: list[FeasibleWindow] = []
        for (target_acc, ref_acc), window in maximally_permissive_windows.items():
            if target_acc.input_name != level.input:
                continue
            ref_level = member_to_level.get(ref_acc.input_name)
            if ref_level is None or ref_level >= idx:
                continue
            # Skip windows whose reference is an optional input —
            # the reference iterator may be std::nullopt at runtime.
            if ref_acc.base_input_name in optional_inputs:
                continue
            windows.append(window)
        windows.sort(
            key=lambda w: (
                str(w.target_accessor.input_name),
                str(w.target_accessor.field_name),
                str(w.reference_accessor.input_name),
                str(w.reference_accessor.field_name),
            ),
        )
        result.append(tuple(windows))
    return result


def _collect_time_fields(
    schema_ir: InstantiatedSchema,
    input_name: str,
    prefix: str,
    time_types: tuple[object, ...],
    out: set[tuple[str, str]],
) -> None:
    """Recursively collect time-typed fields from a schema, including nested sub-schemas."""
    for field_def in schema_ir.fields.values():
        field_path = f"{prefix}.{field_def.cur_name}" if prefix else field_def.cur_name
        if field_def.type_info in time_types:
            out.add((input_name, field_path))
        elif isinstance(field_def.type_info, InstantiatedSchema):
            _collect_time_fields(field_def.type_info, input_name, field_path, time_types, out)


def _extract_optional_timeouts(
    aligner_ir: ResolvedAligner,
) -> dict[str, int]:
    """Extract per-optional timeout durations in nanoseconds.

    Only includes optional inputs with nonzero timeouts.
    """
    timeouts: dict[str, int] = {}
    for name, inp in aligner_ir.inputs.items():
        if inp.optional and inp.timeout is not None:
            ns = inp.timeout.as_unit(ir_units.NANOSECONDS).value
            ns_int = int(ns)
            if ns_int > 0:
                timeouts[name] = ns_int
    return timeouts


def _is_recognized_objective_term(term: dfl.Expr) -> bool:
    """Check if a single objective term matches a recognized rendering pattern.

    Recognized atomic patterns (same set as ``_classify_objective_term`` in
    ``objective_analysis.py``):

    - Absolute difference: ``|a.field - b.field|``
    - Squared difference: ``(a.field - b.field) * (a.field - b.field)``
    - Simple field accessor: ``input.field``
    """
    # Pattern: squared difference (a - b) * (a - b)
    match term:
        case dfl.Binary(
            op=dfl.BinaryOp.MUL,
            left=dfl.Binary(op=dfl.BinaryOp.SUB, left=ll, right=lr),
            right=dfl.Binary(op=dfl.BinaryOp.SUB, left=rl, right=rr),
        ) if ll == rl and lr == rr:
            return True
        case _:
            pass

    # Pattern: absolute difference |a - b|
    match term:
        case dfl.Unary(
            op=dfl.UnaryOp.ABS,
            operand=dfl.Binary(op=dfl.BinaryOp.SUB),
        ):
            return True
        case _:
            pass

    # Pattern: simple field accessor
    return try_field_accessor(term) is not None


def _is_recognized_objective_pattern(expr: dfl.Expr) -> bool:
    """Check if an objective expression matches recognized rendering patterns.

    Decomposes additive expressions (sums) into terms and checks each.
    """
    terms = decompose_additive(expr)
    return all(_is_recognized_objective_term(term) for term in terms)


def _collect_objectives_for_codegen(
    specs: ExtractedSpecs,
) -> tuple[Objective, ...]:
    """Collect and validate objectives for codegen tie-breaking."""
    objectives: list[Objective] = list(specs.unconditional_objectives)
    for opt in specs.conditional_objectives:
        objectives.extend(specs.conditional_objectives[opt])
    for opt in specs.else_objectives:
        objectives.extend(specs.else_objectives[opt])
    # Sort to preserve DSL declaration order.
    objectives.sort(key=lambda o: o.source_index)

    for obj in objectives:
        if not _is_recognized_objective_pattern(obj.expr):
            msg = (
                f"Objective `{obj.source_expr}` cannot be rendered for candidate comparison. "
                "Only `minimize(|a - b|)`, `minimize((a-b)*(a-b))`, `minimize(a.field)`, "
                "and `maximize(a.field)` are supported."
            )
            raise PipelineError(msg)

    return tuple(objectives)


def compute_codegen_plan(
    analysis: AlignerAnalysis,
    aligner_ir: ResolvedAligner,
) -> CodegenPlan:
    """Assemble analysis results into the codegen input structure.

    This is a thin packaging step: the heavy analysis work (join plan,
    direction analysis, maximally permissive windows) is already done
    by :func:`analyze_aligner`.  This function assigns constraints from
    :class:`~clockwork.dsl.aligner.extract_specs.ExtractedSpecs` to
    each level and packages the results.

    Args:
        analysis: Complete analysis results from the pipeline.
        aligner_ir: The resolved aligner IR (for reuse and timeout info).

    Returns:
        A :class:`~clockwork.dsl.aligner.gen.codegen_plan.CodegenPlan`
        ready for C++ code generation.
    """
    all_present_config = PresenceConfig(frozenset(analysis.optional_inputs))
    all_present_plan = analysis.config_analyses[all_present_config].join_plan
    levels = all_present_plan.levels
    member_to_level = analysis.member_to_level
    specs = analysis.specs

    n = len(levels)

    buckets = _assign_constraints_to_levels(
        n,
        specs,
        member_to_level,
        analysis.optional_inputs,
    )

    initial_windows = _compute_initial_windows(
        levels,
        analysis.maximally_permissive_windows,
        member_to_level,
        analysis.optional_inputs,
    )

    codegen_levels: list[CodegenLevel] = []
    optional_level_indices = frozenset(
        idx for idx, level in enumerate(levels) if base_input_name(level.input) in analysis.optional_inputs
    )
    for idx, level in enumerate(levels):
        base_name = base_input_name(level.input)

        all_opt_names = set(buckets.when_present[idx]) | set(buckets.when_absent[idx])
        cond: dict[str, ConditionalBranch] = {}
        for opt in sorted(all_opt_names):
            cond[opt] = ConditionalBranch(
                when_present=tuple(buckets.when_present[idx].get(opt, [])),
                when_absent=tuple(buckets.when_absent[idx].get(opt, [])),
            )

        codegen_levels.append(
            CodegenLevel(
                level_index=idx,
                join_level=level,
                reuse=aligner_ir.inputs[base_name].reuse,
                direction=analysis.direction_analyses[idx],
                unconditional_checks=tuple(buckets.unconditional[idx]),
                conditional_checks=cond,
                initial_windows=initial_windows[idx],
                has_downstream_optionals=any(j > idx for j in optional_level_indices),
            ),
        )

    objectives = _collect_objectives_for_codegen(specs)

    time_fields: set[tuple[str, str]] = set()
    time_types = (clkbuiltins.SYNC_TIME, clkbuiltins.DURATION)
    for name, inp_def in aligner_ir.inputs.items():
        rep = inp_def.interface_info.interface_ir.representation
        if rep is None:
            continue
        _collect_time_fields(rep.schema_ir, name, "", time_types, time_fields)

    codegen_plan = CodegenPlan(
        aligner_name=analysis.aligner_name,
        levels=tuple(codegen_levels),
        input_names=tuple(aligner_ir.inputs),
        optional_input_names=analysis.optional_inputs,
        batch_input_names=frozenset(name for name, inp in aligner_ir.inputs.items() if inp.batch_size is not None),
        optional_timeouts=_extract_optional_timeouts(aligner_ir),
        objectives=objectives,
        time_fields=frozenset(time_fields),
        specs=specs,
    )
    return CodegenPlan(
        aligner_name=codegen_plan.aligner_name,
        levels=codegen_plan.levels,
        input_names=codegen_plan.input_names,
        optional_input_names=codegen_plan.optional_input_names,
        batch_input_names=codegen_plan.batch_input_names,
        optional_timeouts=codegen_plan.optional_timeouts,
        objectives=codegen_plan.objectives,
        time_fields=codegen_plan.time_fields,
        specs=codegen_plan.specs,
        partition_plan=compute_partition_plan(codegen_plan),
    )
