# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — objective analysis and search type classification.

Analyzes objective expressions to determine the search strategy for each
input.

See docs/objective_analysis.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.extract_specs import (
    FieldAccessor,
    FirstInBatch,
    LastInBatch,
    ObjectiveSense,
    try_field_accessor,
)
from clockwork.dsl.ir import dfl
from clockwork.dsl.ir.aligner import AlignerLetBinding

if TYPE_CHECKING:
    from clockwork.dsl.aligner.extract_specs import InputSelector, Objective


class SearchType(Enum):
    """Classification of how to search for a message from a particular input."""

    EXACT_MATCH = "exact_match"
    NEAREST = "nearest"
    LAST_IN_RANGE = "last_in_range"
    FIRST_IN_RANGE = "first_in_range"
    ANY_MATCH = "any_match"
    ENUMERATE = "enumerate"


@dataclass(frozen=True, slots=True)
class NearestReference:
    """Reference and target field accessors for a NEAREST search.

    For ``minimize(|A.field - B.field|)`` where B is the candidate:

    - ``reference`` is ``A.field`` (the already-bound input's field).
    - ``target`` is ``B.field`` (the candidate input's field).

    Extracted during objective classification so that downstream codegen
    does not need to re-parse DFL expressions.
    """

    reference: FieldAccessor
    target: FieldAccessor


@dataclass(frozen=True, slots=True)
class SearchClassification:
    """Result of classifying a single input's search type.

    Attributes:
        search_type: How to search for messages from this input.
        objective_term: The DFL sub-expression driving the search type,
            or ``None`` for ANY_MATCH, ENUMERATE, or when no separable
            objective term applies.
        nearest_reference: Reference and target accessors for NEAREST
            searches.  Populated only when ``search_type`` is NEAREST.
    """

    search_type: SearchType
    objective_term: dfl.Expr | None
    nearest_reference: NearestReference | None = None


class ObjectiveAnalysisError(Exception):
    """Raised when objective analysis detects an invalid specification."""


@dataclass(frozen=True, slots=True)
class DecomposedObjective:
    """Result of decomposing and analyzing one objective.

    Attributes:
        sense: MINIMIZE or MAXIMIZE.
        terms: Additive terms after decomposition.
        is_separable: Whether all terms reference at most one input each
            (considering bound members as known/constant).
        input_terms: Mapping from input name to the single additive term
            that references it.  Includes both bound and non-bound inputs.
        non_separable_inputs: Inputs involved in non-separable terms,
            requiring enumeration.
    """

    sense: ObjectiveSense
    terms: tuple[dfl.Expr, ...]
    is_separable: bool
    input_terms: dict[InputSelector, dfl.Expr]
    non_separable_inputs: frozenset[InputSelector]


def resolve_let_bindings(expr: dfl.Expr) -> dfl.Expr:
    """Replace ``Ref`` nodes pointing to ``AlignerLetBinding`` with their values.

    Handles transitive let bindings by recursing until no more let-binding
    references remain.  Uses :func:`dfl.map_expr` for structural recursion.
    """

    def _resolve(e: dfl.Expr) -> dfl.Expr:
        match e:
            case dfl.Ref() as ref:
                entity = ref.lookup()
                if isinstance(entity, AlignerLetBinding):
                    return _resolve(resolve_let_bindings(entity.value))
                return ref
            case _:
                return dfl.map_expr(_resolve, e)

    return _resolve(expr)


def collect_field_accessors(expr: dfl.Expr) -> frozenset[FieldAccessor]:
    """Recursively collect all ``input.field`` accessors in *expr*."""

    def _collect(node: dfl.Expr, child_results: list[frozenset[FieldAccessor]]) -> frozenset[FieldAccessor]:
        result: frozenset[FieldAccessor] = frozenset().union(*child_results) if child_results else frozenset()
        accessor = try_field_accessor(node)
        if accessor is not None:
            result = result | {accessor}
        return result

    return dfl.fold_expr(_collect, expr)


def referenced_inputs(expr: dfl.Expr) -> frozenset[InputSelector]:
    """Return the set of inputs referenced in *expr*."""
    return frozenset(fa.input_name for fa in collect_field_accessors(expr))


def referenced_base_inputs(expr: dfl.Expr) -> frozenset[str]:
    """Return the set of base input names referenced in *expr*.

    Like :func:`referenced_inputs` but strips ``FirstInBatch`` / ``LastInBatch``
    wrappers, returning plain input name strings.  Used for auto-drop filtering
    against ``absent_optionals`` which is a ``set[str]``.
    """
    return frozenset(fa.base_input_name for fa in collect_field_accessors(expr))


def decompose_additive(expr: dfl.Expr) -> tuple[dfl.Expr, ...]:
    """Flatten *expr* into a tuple of additive terms.

    Recognizes ``Binary(ADD, ...)`` chains and ``sum(ExprTuple(...))``
    patterns from expanded ``spread(...)`` calls.
    """
    match expr:
        case dfl.Binary(op=dfl.BinaryOp.ADD, left=left, right=right):
            return (*decompose_additive(left), *decompose_additive(right))

        case dfl.Call(
            func=dfl.Ref() as func_ref,
            args=(dfl.CallArg(expr=dfl.ExprTuple(elements=elems)),),
        ) if _is_sum_builtin(func_ref):
            result: list[dfl.Expr] = []
            for elem in elems:
                result.extend(decompose_additive(elem))
            return tuple(result)

        case _:
            return (expr,)


def _is_sum_builtin(ref: dfl.Ref) -> bool:
    """Check whether *ref* resolves to the ``sum`` built-in function."""
    entity = ref.lookup()
    return entity is dfl.get_builtin("sum")


def _build_nearest_reference(
    left: dfl.Expr,
    right: dfl.Expr,
    target_input: InputSelector,
) -> NearestReference:
    """Build a ``NearestReference`` from the two sides of a difference expression.

    The target is the accessor whose ``input_name`` matches *target_input*.
    The reference is the other accessor.

    Args:
        left: Left operand of the SUB expression.
        right: Right operand of the SUB expression.
        target_input: The candidate input being classified.
    """
    lhs_fa = try_field_accessor(left)
    rhs_fa = try_field_accessor(right)
    assert lhs_fa is not None, f"Expected field accessor for left operand, got {left}"
    assert rhs_fa is not None, f"Expected field accessor for right operand, got {right}"
    if lhs_fa.input_name == target_input:
        return NearestReference(reference=rhs_fa, target=lhs_fa)
    return NearestReference(reference=lhs_fa, target=rhs_fa)


def _classify_objective_term(
    term: dfl.Expr,
    sense: ObjectiveSense,
    target_input: InputSelector,
    bound_inputs: frozenset[InputSelector],
) -> tuple[SearchType, NearestReference | None]:
    """Classify the search type implied by one objective term.

    Returns:
        A ``(SearchType, NearestReference | None)`` tuple.
        ``NearestReference`` is populated only for NEAREST.
    """
    # Pattern: squared difference  (a - b) * (a - b)
    match term:
        case dfl.Binary(
            op=dfl.BinaryOp.MUL,
            left=dfl.Binary(op=dfl.BinaryOp.SUB, left=l_left, right=l_right),
            right=dfl.Binary(op=dfl.BinaryOp.SUB, left=r_left, right=r_right),
        ):
            if _is_squared_difference_from_bound((l_left, l_right), (r_left, r_right), target_input, bound_inputs):
                nr = _build_nearest_reference(l_left, l_right, target_input)
                return SearchType.NEAREST, nr
        case _:
            pass

    # Pattern: absolute difference  |a - b|
    match term:
        case dfl.Unary(
            op=dfl.UnaryOp.ABS,
            operand=dfl.Binary(op=dfl.BinaryOp.SUB, left=sub_left, right=sub_right),
        ):
            if _is_difference_from_bound(sub_left, sub_right, target_input, bound_inputs):
                nr = _build_nearest_reference(sub_left, sub_right, target_input)
                return SearchType.NEAREST, nr
        case _:
            pass

    # Pattern: simple field accessor (monotone objective)
    accessor = try_field_accessor(term)
    if accessor is not None and accessor.input_name == target_input:
        if sense == ObjectiveSense.MAXIMIZE:
            return SearchType.LAST_IN_RANGE, None
        return SearchType.FIRST_IN_RANGE, None

    # Fallback that works for everything
    return SearchType.ENUMERATE, None


def _is_squared_difference_from_bound(
    left_pair: tuple[dfl.Expr, dfl.Expr],
    right_pair: tuple[dfl.Expr, dfl.Expr],
    target_input: InputSelector,
    bound_inputs: frozenset[InputSelector],
) -> bool:
    """Check if ``(a - b) * (c - d)`` is a squared difference from a bound input.

    Both SUB expressions must have identical field-accessor pairs, one
    referencing the target input and the other referencing a bound input.
    """
    l_lhs_fa = try_field_accessor(left_pair[0])
    l_rhs_fa = try_field_accessor(left_pair[1])
    r_lhs_fa = try_field_accessor(right_pair[0])
    r_rhs_fa = try_field_accessor(right_pair[1])

    if l_lhs_fa is None or l_rhs_fa is None or r_lhs_fa is None or r_rhs_fa is None:
        return False

    if l_lhs_fa != r_lhs_fa or l_rhs_fa != r_rhs_fa:
        return False

    return _is_difference_from_bound_fa(l_lhs_fa, l_rhs_fa, target_input, bound_inputs)


def _is_difference_from_bound(
    left: dfl.Expr,
    right: dfl.Expr,
    target_input: InputSelector,
    bound_inputs: frozenset[InputSelector],
) -> bool:
    """Check if ``left - right`` is a difference between target and a bound input."""
    lhs_fa = try_field_accessor(left)
    rhs_fa = try_field_accessor(right)
    if lhs_fa is None or rhs_fa is None:
        return False
    return _is_difference_from_bound_fa(lhs_fa, rhs_fa, target_input, bound_inputs)


def _is_difference_from_bound_fa(
    lhs: FieldAccessor,
    rhs: FieldAccessor,
    target_input: InputSelector,
    bound_inputs: frozenset[InputSelector],
) -> bool:
    """Check if field accessors represent ``target - bound`` or ``bound - target``."""
    return (lhs.input_name == target_input and rhs.input_name in bound_inputs) or (
        lhs.input_name in bound_inputs and rhs.input_name == target_input
    )


def _merge_search_classifications(
    classifications: list[tuple[SearchType, dfl.Expr, NearestReference | None]],
    candidate_repr: InputSelector,
) -> SearchClassification:
    """Merge multiple separable-term classifications for one candidate.

    Merge rules:

    - Single type: use it.
    - Multiple of same type: use that type.
    - NEAREST + FIRST_IN_RANGE or NEAREST + LAST_IN_RANGE: NEAREST
      (it subsumes monotone searches).
    - FIRST_IN_RANGE + LAST_IN_RANGE: Error (conflicting objectives).
    - ENUMERATE terms (from unrecognized patterns) are filtered out when
      non-ENUMERATE classifications exist.

    Args:
        classifications: Separable-term ``(search_type, term, nearest_ref)`` triples.
        candidate_repr: Representative input for error messages.

    Returns:
        Merged ``SearchClassification``.

    Raises:
        ObjectiveAnalysisError: On conflicting FIRST + LAST directions.
    """
    if not classifications:
        return SearchClassification(SearchType.ENUMERATE, None)

    # Filter out ENUMERATE — fallback from unrecognized patterns
    # doesn't provide starting-point information.
    non_enum = [(st, term, nr) for st, term, nr in classifications if st != SearchType.ENUMERATE]

    if not non_enum:
        return SearchClassification(SearchType.ENUMERATE, None)

    types = {st for st, _, _ in non_enum}

    if len(types) == 1:
        st = next(iter(types))
        _, term, nr = non_enum[0]
        return SearchClassification(st, term, nr)

    has_nearest = SearchType.NEAREST in types
    has_first = SearchType.FIRST_IN_RANGE in types
    has_last = SearchType.LAST_IN_RANGE in types

    if has_first and has_last:
        msg = (
            f"Conflicting objectives for input '{candidate_repr}': FIRST_IN_RANGE and LAST_IN_RANGE cannot be combined."
        )
        raise ObjectiveAnalysisError(msg)

    if has_nearest:
        # NEAREST subsumes monotone directions.
        _, term, nr = next((s, t, n) for s, t, n in non_enum if s == SearchType.NEAREST)
        return SearchClassification(SearchType.NEAREST, term, nr)

    # Should not reach here with valid combinations of our known types.
    msg = f"Cannot merge search types for input '{candidate_repr}': {types}"
    raise ObjectiveAnalysisError(msg)


def _apply_batch_boundary_default(
    candidate: InputSelector,
    classification: SearchClassification,
) -> SearchClassification:
    """Promote ANY_MATCH to directional type for batch boundary inputs.

    ``FirstInBatch`` inputs are promoted to FIRST_IN_RANGE,
    ``LastInBatch`` inputs are promoted to LAST_IN_RANGE.
    Non-batch or already-classified inputs are returned unchanged.
    """
    if classification.search_type != SearchType.ANY_MATCH:
        return classification
    match candidate:
        case FirstInBatch():
            return SearchClassification(SearchType.FIRST_IN_RANGE, None)
        case LastInBatch():
            return SearchClassification(SearchType.LAST_IN_RANGE, None)
        case _:
            return classification


def classify_input(
    candidate: InputSelector,
    bound_inputs: frozenset[InputSelector],
    objectives: tuple[Objective, ...],
    active_inputs: frozenset[InputSelector],
) -> SearchClassification:
    """Classify a single input's search type given the current bound set.

    Designed to be called iteratively by the join planner.  At each level
    the planner calls this for each remaining candidate with the growing
    set of already-placed inputs.

    The classification proceeds in three steps:

    1. **Decompose** each objective into additive terms and identify terms
       referencing the candidate.
    2. **Classify** each separable term via :func:`_classify_objective_term`.
    3. **Merge** classifications across objectives.

    Non-separable terms (referencing the candidate *and* unbound inputs)
    are ignored for starting-point selection — they cannot be optimized
    at this level because the other variables are unknown.

    When an objective has multiple additive terms referencing the same
    candidate (e.g., ``minimize(|c1 - L| + |c2 - L|)``), each term
    is classified independently and the results are merged.

    Args:
        candidate: The input to classify.
        bound_inputs: Inputs already placed (treated as bound/known).
        objectives: All objectives for the active presence configuration.
        active_inputs: All inputs active in the current presence config.

    Returns:
        A ``SearchClassification`` with the determined search type and
        the winning objective term (if any).

    Raises:
        ObjectiveAnalysisError: On conflicting objective directions.
    """
    candidate_members = frozenset({candidate})

    separable_classifications: list[tuple[SearchType, dfl.Expr, NearestReference | None]] = []
    any_references_candidate = False

    for objective in objectives:
        resolved = resolve_let_bindings(objective.expr)
        terms = decompose_additive(resolved)

        for term in terms:
            inputs = referenced_inputs(term)
            term_candidate_members = inputs & candidate_members
            if not term_candidate_members:
                continue

            if not inputs <= active_inputs:
                continue

            any_references_candidate = True

            unbound_refs = inputs - bound_inputs - candidate_members
            if unbound_refs:
                # Non-separable: references candidate + unbound inputs.
                continue

            target = min(term_candidate_members, key=repr)
            search_type, nearest_ref = _classify_objective_term(
                term,
                objective.sense,
                target,
                bound_inputs,
            )
            separable_classifications.append((search_type, term, nearest_ref))

    if not any_references_candidate:
        # If we have no explicit objectives, apply defaults for batch boundaries
        return _apply_batch_boundary_default(
            candidate,
            SearchClassification(SearchType.ANY_MATCH, None),
        )

    if not separable_classifications:
        return SearchClassification(SearchType.ENUMERATE, None)

    result = _merge_search_classifications(
        separable_classifications,
        candidate,
    )
    return _apply_batch_boundary_default(candidate, result)
