# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — join plan computation.

Determines the nesting order and search strategy for each input in
the generated alignment search code using a greedy level-by-level
algorithm that calls :func:`classify_input` at each step.

See docs/join_plan.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.extract_specs import BoundValue, FieldProperty, base_input_name
from clockwork.dsl.aligner.objective_analysis import (
    NearestReference,
    SearchClassification,
    SearchType,
    classify_input,
)

if TYPE_CHECKING:
    from clockwork.dsl.aligner.extract_specs import (
        EqualityConstraint,
        FieldAccessor,
        InputSelector,
        Objective,
    )
    from clockwork.dsl.aligner.stn import StnAnalysis
    from clockwork.dsl.ir import dfl
    from clockwork.dsl.ir.aligner import Aligner


# Ranking for search-type quality (lower = better search, more efficient).
_SEARCH_TYPE_RANK: dict[SearchType, int] = {
    SearchType.EXACT_MATCH: 0,
    SearchType.NEAREST: 1,
    SearchType.FIRST_IN_RANGE: 2,
    SearchType.LAST_IN_RANGE: 2,
    SearchType.ANY_MATCH: 3,
    SearchType.ENUMERATE: 4,
}

# Ranking for equality-filter quality (lower = better).
_EQUALITY_QUALITY_UNIQUE = 0
_EQUALITY_QUALITY_NON_UNIQUE = 1
_EQUALITY_QUALITY_NONE = 2


def _is_sorted(prop: FieldProperty | None) -> bool:
    """Return ``True`` iff *prop* guarantees sorted (non-decreasing) order."""
    return prop is FieldProperty.STRICTLY_INCREASING or prop is FieldProperty.NON_DECREASING


class JoinPlanError(Exception):
    """Raised when join plan computation encounters an invalid configuration."""


@dataclass(frozen=True, slots=True)
class EqualityCheck:
    """An equality-filter check to apply at a join level.

    Produced for non-sorted target fields with equality constraints.
    When the target field is declared sorted (``is_strictly_increasing``
    or ``is_non_decreasing``), the equality becomes ``EXACT_MATCH``
    instead.

    Attributes:
        target_accessor: Field accessor on the target (being searched) input.
        reference_accessor: Field accessor on the reference (bound) input.
        is_unique: Whether the target field has at-most-one-match guarantee
            (from ``is_unique`` assumption; sorted targets do not reach
            this path — they produce ``EXACT_MATCH``).
        source_constraint: The original equality constraint, for diagnostics.
    """

    target_accessor: FieldAccessor
    reference_accessor: FieldAccessor
    is_unique: bool
    source_constraint: EqualityConstraint


@dataclass(frozen=True, slots=True)
class FeasibleWindow:
    """STN-derived feasible window for one input relative to another.

    Represents the constraint: ``lo <= (target - reference) <= hi``.
    ``None`` bounds indicate unconstrained in that direction.

    Attributes:
        reference_accessor: Field accessor on the reference (bound) input.
        target_accessor: Field accessor on the target (being searched) input.
        lo: Lower bound on ``target - reference``, or ``None`` for unbounded.
            Preserves the duration / integer type from the original constraints.
        hi: Upper bound on ``target - reference``, or ``None`` for unbounded.
            Preserves the duration / integer type from the original constraints.
    """

    reference_accessor: FieldAccessor
    target_accessor: FieldAccessor
    lo: BoundValue | None
    hi: BoundValue | None


@dataclass(frozen=True, slots=True)
class JoinLevel:
    """One level in the join plan.

    Each level binds one input using a specific search strategy.

    Attributes:
        input: The input bound at this level.
        search_type: Search strategy for this level's messages.
        objective_term: The winning separable DFL expression term used to
            derive ``search_type``, or ``None`` for ANY_MATCH/ENUMERATE.
        nearest_reference: Reference and target accessors for NEAREST
            searches.  ``None`` for non-NEAREST types.
        depends_on: Input selectors that must be bound
            before this level.
        windows: STN-derived feasible windows constraining this level
            relative to already-bound inputs.
        is_optional: Whether this input is optional.
    """

    input: InputSelector
    search_type: SearchType
    objective_term: dfl.Expr | None
    nearest_reference: NearestReference | None
    depends_on: frozenset[InputSelector]
    windows: tuple[FeasibleWindow, ...]
    equality_checks: tuple[EqualityCheck, ...]
    is_optional: bool


@dataclass(frozen=True, slots=True)
class JoinPlan:
    """The complete join plan for one presence configuration.

    An ordered sequence of join levels built by the greedy algorithm.
    The first level is the outermost iteration.

    Attributes:
        levels: All join levels in execution order.
    """

    levels: tuple[JoinLevel, ...]


def _build_constraint_graph(
    stn: StnAnalysis,
    equalities: tuple[EqualityConstraint, ...] = (),
) -> dict[InputSelector, set[InputSelector]]:
    """Build the constraint dependency graph between inputs.

    Two inputs are connected if any STN edge or equality constraint
    links field accessors belonging to different inputs.

    Returns:
        Mapping from input to the set of other inputs it shares
        constraints with.
    """
    graph: dict[InputSelector, set[InputSelector]] = {}
    for edge in stn.edges:
        src = edge.source.input_name
        dst = edge.destination.input_name
        if src == dst:
            continue
        graph.setdefault(src, set()).add(dst)
        graph.setdefault(dst, set()).add(src)
    for eq in equalities:
        src = eq.left.input_name
        dst = eq.right.input_name
        if src == dst:
            continue
        graph.setdefault(src, set()).add(dst)
        graph.setdefault(dst, set()).add(src)
    return graph


def _compute_windows(
    target: InputSelector,
    bound_inputs: frozenset[InputSelector],
    stn: StnAnalysis,
) -> tuple[FeasibleWindow, ...]:
    """Compute feasible windows for *target* relative to bound inputs.

    For each STN edge connecting *target* to an already-bound input,
    derive the feasible window from the STN distance matrix.  Deduplicates
    windows by (target_accessor, reference_accessor) pair.
    """
    seen_pairs: set[tuple[FieldAccessor, FieldAccessor]] = set()
    windows: list[FeasibleWindow] = []

    for edge in stn.edges:
        src_input = edge.source.input_name
        dst_input = edge.destination.input_name

        if dst_input == target and src_input in bound_inputs:
            target_acc = edge.destination
            ref_acc = edge.source
        elif src_input == target and dst_input in bound_inputs:
            target_acc = edge.source
            ref_acc = edge.destination
        else:
            continue

        pair = (target_acc, ref_acc)
        if pair in seen_pairs:
            continue
        seen_pairs.add(pair)

        lo, hi = stn.feasible_window(target_acc, ref_acc)
        windows.append(
            FeasibleWindow(
                reference_accessor=ref_acc,
                target_accessor=target_acc,
                lo=lo,
                hi=hi,
            )
        )

    # Sort for deterministic output.
    windows.sort(
        key=lambda w: (
            repr(w.target_accessor.input_name),
            repr(w.target_accessor.field_name),
            repr(w.reference_accessor.input_name),
            repr(w.reference_accessor.field_name),
        )
    )
    return tuple(windows)


def _check_exact_match(
    candidate: InputSelector,
    bound_inputs: frozenset[InputSelector],
    stn: StnAnalysis,
    equalities: tuple[EqualityConstraint, ...],
    field_assumptions: dict[FieldAccessor, FieldProperty],
) -> bool:
    """Check if *candidate* qualifies for EXACT_MATCH search.

    Two code paths:

    **Path A — STN equality**: Both fields are declared sorted
    (``is_strictly_increasing`` or ``is_non_decreasing``, required for
    STN membership), and they are constrained to be exactly equal.  Check
    via :meth:`StnAnalysis.are_equal`.

    **Path B — EqualityConstraint with sorted target**: One side belongs
    to the candidate and is declared sorted, the other side belongs to
    a bound input.  The target view is sorted, so binary search narrows
    to the matching window (duplicates are iterated by the forward scan).

    Returns:
        ``True`` if the candidate qualifies for EXACT_MATCH.
    """
    for node in stn.nodes:
        if node.input_name != candidate:
            continue
        for other_node in stn.nodes:
            if other_node.input_name not in bound_inputs:
                continue
            if stn.are_equal(node, other_node):
                return True

    for eq in equalities:
        candidate_fa: FieldAccessor | None = None
        if eq.left.input_name == candidate and eq.right.input_name in bound_inputs:
            candidate_fa = eq.left
        elif eq.right.input_name == candidate and eq.left.input_name in bound_inputs:
            candidate_fa = eq.right
        else:
            continue
        if _is_sorted(field_assumptions.get(candidate_fa)):
            return True

    return False


def _compute_equality_checks(
    target: InputSelector,
    bound_inputs: frozenset[InputSelector],
    equalities: tuple[EqualityConstraint, ...],
    field_assumptions: dict[FieldAccessor, FieldProperty],
) -> tuple[EqualityCheck, ...]:
    """Compute equality-filter checks for *target* relative to bound inputs.

    Produces an :class:`EqualityCheck` for each ``EqualityConstraint``
    connecting *target* to an already-bound input, **excluding** constraints
    where the target's field is declared sorted (``is_strictly_increasing``
    or ``is_non_decreasing`` — those become ``EXACT_MATCH`` instead of
    filters).

    Handles directionality: either side of the constraint may be the target.
    Deduplicates by ``(target_accessor, reference_accessor)`` pair.
    """
    seen_pairs: set[tuple[FieldAccessor, FieldAccessor]] = set()
    checks: list[EqualityCheck] = []

    for eq in equalities:
        if eq.left.input_name == target and eq.right.input_name in bound_inputs:
            target_acc = eq.left
            ref_acc = eq.right
        elif eq.right.input_name == target and eq.left.input_name in bound_inputs:
            target_acc = eq.right
            ref_acc = eq.left
        else:
            continue

        target_prop = field_assumptions.get(target_acc)
        if _is_sorted(target_prop):
            continue

        pair = (target_acc, ref_acc)
        if pair in seen_pairs:
            continue
        seen_pairs.add(pair)

        is_unique = target_prop is FieldProperty.UNIQUE

        checks.append(
            EqualityCheck(
                target_accessor=target_acc,
                reference_accessor=ref_acc,
                is_unique=is_unique,
                source_constraint=eq,
            )
        )

    checks.sort(
        key=lambda c: (
            repr(c.target_accessor.input_name),
            repr(c.target_accessor.field_name),
            repr(c.reference_accessor.input_name),
            repr(c.reference_accessor.field_name),
        )
    )
    return tuple(checks)


def _equality_quality(
    candidate: InputSelector,
    bound_inputs: frozenset[InputSelector],
    equalities: tuple[EqualityConstraint, ...],
    field_assumptions: dict[FieldAccessor, FieldProperty],
) -> int:
    """Score the equality-filter quality for a candidate (lower is better).

    Returns:
        - _EQUALITY_QUALITY_UNIQUE if the candidate has a unique equality check with a bound input.
        - _EQUALITY_QUALITY_NON_UNIQUE if it has a non-unique equality check.
        - _EQUALITY_QUALITY_NONE if it has no equality relationship with any bound input.
    """
    best = _EQUALITY_QUALITY_NONE
    for eq in equalities:
        if eq.left.input_name == candidate and eq.right.input_name in bound_inputs:
            target_acc = eq.left
        elif eq.right.input_name == candidate and eq.left.input_name in bound_inputs:
            target_acc = eq.right
        else:
            continue

        # Skip sorted targets — those are handled as EXACT_MATCH.
        target_prop = field_assumptions.get(target_acc)
        if _is_sorted(target_prop):
            continue

        if target_prop is FieldProperty.UNIQUE:
            return _EQUALITY_QUALITY_UNIQUE  # Can't do better than this.
        best = min(best, _EQUALITY_QUALITY_NON_UNIQUE)
    return best


def _resolve_max_msgs(inp: InputSelector, aligner_node: Aligner) -> int:
    """Return the ``max_msgs`` for an input.

    This is the literal maximum number of messages that can be in the
    input view at any time, and therefore the worst-case candidate count
    for enumeration.
    """
    resolved = aligner_node.resolve()
    resolved_input = resolved.inputs[base_input_name(inp)]
    max_msgs = resolved_input.view_params.max_msgs
    assert isinstance(max_msgs, int)  # Already resolved
    return max_msgs


def _connectivity_to_unbound(
    candidate: InputSelector,
    bound_inputs: frozenset[InputSelector],
    all_inputs: frozenset[InputSelector],
    constraint_graph: dict[InputSelector, set[InputSelector]],
) -> int:
    """Count how many *unbound* inputs share STN constraints with candidate."""
    neighbors = constraint_graph.get(candidate, set())
    unbound = all_inputs - bound_inputs - {candidate}
    return len(neighbors & unbound)


def _score_candidate(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    candidate: InputSelector,
    *,
    classification: SearchClassification,
    is_required: bool,
    bound_inputs: frozenset[InputSelector],
    all_inputs: frozenset[InputSelector],
    constraint_graph: dict[InputSelector, set[InputSelector]],
    aligner_node: Aligner,
    equalities: tuple[EqualityConstraint, ...],
    field_assumptions: dict[FieldAccessor, FieldProperty],
) -> tuple[int, int, int, int, int, str]:
    """Compute a sort key for greedy candidate selection.

    Lower is better (Python ascending sort).  The tuple components are:

    1. Required (0) vs optional (1).
    2. Search type rank (EXACT_MATCH=0, NEAREST=1, …, ENUMERATE=4).
    3. Equality-filter quality (unique=0, non-unique=1, none=2).
    4. Negated connectivity to unbound inputs (higher is better).
    5. Buffer size (``max_msgs``, smaller is better for ENUMERATE).
    6. Alphabetical tiebreak.
    """
    rank = _SEARCH_TYPE_RANK.get(classification.search_type)
    if rank is None:
        msg = f"Unexpected search type {classification.search_type} for {candidate}"
        raise JoinPlanError(msg)
    eq_quality = _equality_quality(candidate, bound_inputs, equalities, field_assumptions)
    connectivity = _connectivity_to_unbound(
        candidate,
        bound_inputs,
        all_inputs,
        constraint_graph,
    )
    max_msgs = _resolve_max_msgs(candidate, aligner_node)
    return (
        0 if is_required else 1,
        rank,
        eq_quality,
        -connectivity,
        max_msgs,
        repr(candidate),
    )


def compute_join_plan(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    *,
    inputs: tuple[InputSelector, ...],
    objectives: tuple[Objective, ...],
    active_inputs: frozenset[InputSelector],
    stn: StnAnalysis,
    aligner_node: Aligner,
    equalities: tuple[EqualityConstraint, ...] = (),
    field_assumptions: dict[FieldAccessor, FieldProperty] | None = None,
) -> JoinPlan:
    """Compute the join plan using a greedy level-by-level algorithm.

    At each step, every remaining input is evaluated as a candidate:

    - :func:`classify_input` determines its objective-derived search type
      given the current bound set.
    - :func:`_check_exact_match` checks for constraint-derived EXACT_MATCH
      (overrides objective-derived type when an equality on a sorted
      field is detected with a bound input).
    - :func:`_score_candidate` ranks candidates by desirability.
    - The best candidate is placed as the next join level.
    - The placed input joins the bound set for subsequent levels.

    Args:
        inputs: All inputs to include in the join plan.
        objectives: Active objectives for this presence configuration.
        active_inputs: Set of input selectors active in this configuration.
        stn: STN analysis result (provides feasible windows and equality classes).
        aligner_node: The parsed aligner (provides per-input view parameters).
        equalities: Non-STN equality constraints for this presence configuration.
        field_assumptions: Declared field properties (from ``assume()`` calls).

    Returns:
        A :class:`JoinPlan` describing the execution order.
    """
    if not inputs:
        msg = "Cannot build join plan with no inputs."
        raise JoinPlanError(msg)

    fa = field_assumptions if field_assumptions is not None else {}
    all_inputs_set = frozenset(inputs)
    constraint_graph = _build_constraint_graph(stn, equalities)
    resolved = aligner_node.resolve()

    bound_inputs: set[InputSelector] = set()
    remaining = list(inputs)
    levels: list[JoinLevel] = []

    while remaining:
        bound_set = frozenset(bound_inputs)

        scored: list[tuple[tuple[int, int, int, int, int, str], InputSelector, SearchClassification]] = []
        for candidate in remaining:
            classification = classify_input(
                candidate=candidate,
                bound_inputs=bound_set,
                objectives=objectives,
                active_inputs=active_inputs,
            )

            # Override with EXACT_MATCH if a constraint-derived equality exists.
            if classification.search_type is not SearchType.EXACT_MATCH and _check_exact_match(
                candidate, bound_set, stn, equalities, fa
            ):
                classification = SearchClassification(SearchType.EXACT_MATCH, classification.objective_term)

            is_required = not resolved.inputs[base_input_name(candidate)].optional
            score = _score_candidate(
                candidate,
                classification=classification,
                is_required=is_required,
                bound_inputs=bound_set,
                all_inputs=all_inputs_set,
                constraint_graph=constraint_graph,
                aligner_node=aligner_node,
                equalities=equalities,
                field_assumptions=fa,
            )
            scored.append((score, candidate, classification))

        scored.sort(key=lambda t: t[0])
        _, best, best_classification = scored[0]

        deps = constraint_graph.get(best, set()) & bound_set
        windows = _compute_windows(best, bound_set, stn)
        eq_checks = _compute_equality_checks(best, bound_set, equalities, fa)

        is_required = not resolved.inputs[base_input_name(best)].optional
        level = JoinLevel(
            input=best,
            search_type=best_classification.search_type,
            objective_term=best_classification.objective_term,
            nearest_reference=best_classification.nearest_reference,
            depends_on=frozenset(deps),
            windows=windows,
            equality_checks=eq_checks,
            is_optional=not is_required,
        )
        levels.append(level)
        bound_inputs.add(best)
        remaining.remove(best)

    return JoinPlan(
        levels=tuple(levels),
    )
