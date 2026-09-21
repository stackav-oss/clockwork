# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Constraint direction analysis for aligner join plan levels.

Analyzes constraints between join-plan levels to determine:

- **Constraint direction**: which scan direction relaxes constraints
  against later-level inputs (separate for required and optional).
- **Objective direction**: which scan direction the search type prefers.
- **Cutoff expressions**: compile-time thresholds beyond which scanning
  in a given direction is provably futile.

The results are consumed by codegen to optimize search loops.
"""

from __future__ import annotations

import itertools
from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.objective_analysis import SearchType
from clockwork.dsl.aligner.stn import normalize_bound

if TYPE_CHECKING:
    from clockwork.dsl.aligner.extract_specs import (
        BoundValue,
        DifferenceConstraint,
        EqualityConstraint,
        ExtractedSpecs,
        FieldAccessor,
        InputSelector,
    )
    from clockwork.dsl.aligner.join_plan import JoinPlan


class Direction(Enum):
    """Direction preference for message index selection at a join level.

    ALL_MIN / ALL_MAX indicate that all relevant constraints prefer
    earlier / later messages respectively.  MIXED means constraints
    pull in conflicting directions.  UNCONSTRAINED means no relevant
    constraints exist.
    """

    ALL_MIN = "all_min"
    ALL_MAX = "all_max"
    MIXED = "mixed"
    UNCONSTRAINED = "unconstrained"


class _PreferDirection(Enum):
    """Per-constraint direction preference (internal).

    PREFER_MIN means making the current variable smaller relaxes the
    constraint.  PREFER_MAX means making it larger relaxes it.
    """

    PREFER_MIN = "prefer_min"
    PREFER_MAX = "prefer_max"


class _ConstraintOrigin(Enum):
    """Origin of a constraint for cutoff classification.

    UNCONDITIONAL: Active in all presence configs.
    THEN_BRANCH: Active when a ``has_candidates`` optional is present.
    ELSE_BRANCH: Active when a ``has_candidates`` optional is absent.
    """

    UNCONDITIONAL = "unconditional"
    THEN_BRANCH = "then_branch"
    ELSE_BRANCH = "else_branch"


@dataclass(frozen=True, slots=True)
class CutoffExpr:
    """Compile-time expression for a scan-direction cutoff.

    For right cutoffs, the runtime check is::

        current_field > later_view.last().field + bound

    For left cutoffs, the runtime check is::

        current_field < later_view.first().field - bound

    ``later_accessor`` identifies the field on the later-level input's
    view.  ``current_field`` identifies the field on the scanning
    level's iterator.  ``bound`` preserves the duration / integer type
    from the original constraint, in canonical units.

    Attributes:
        later_accessor: FieldAccessor on the later-level input.
        current_field: Field name on the current level's input.
        bound: The constraint bound as a typed ``BoundValue``.
    """

    later_accessor: FieldAccessor
    current_field: str
    bound: BoundValue


@dataclass(frozen=True, slots=True)
class DirectionAnalysis:
    """Compile-time direction analysis result for one join level.

    Provides separate required and optional constraint directions to
    support codegen's three-state search strategy:

    - **No feasible solution yet:** move in the direction required
      constraints pull (toward feasibility).
    - **Partial solution (missing optionals):** move in the direction
      optional constraints pull (toward fullness).
    - **Complete solution (all optionals present):** fullness cannot
      improve.  Whether to continue searching depends on the objective
      direction — if objectives align with the search direction, the
      first complete solution is optimal; otherwise, the search must
      continue within the feasibility region.

    Attributes:
        required_constraint_direction: Aggregated direction from
            constraints against required later-level inputs.
        optional_constraint_direction: Aggregated direction from
            constraints against optional later-level inputs.
        objective_direction: Direction preference from the level's
            search type / objective.
        right_cutoffs: Cutoff expressions for right (increasing) scan.
            Derived from unconditional required later-level constraints
            only.
        left_cutoffs: Cutoff expressions for left (decreasing) scan.
            Derived from unconditional required later-level constraints
            only.
        optional_right_cutoffs: Cutoff expressions for right scan against
            optional later-level inputs.  Derived from unconditional and
            then-branch constraints.
        optional_left_cutoffs: Cutoff expressions for left scan against
            optional later-level inputs.  Derived from unconditional and
            then-branch constraints.
    """

    required_constraint_direction: Direction
    optional_constraint_direction: Direction
    objective_direction: Direction
    right_cutoffs: tuple[CutoffExpr, ...]
    left_cutoffs: tuple[CutoffExpr, ...]
    optional_right_cutoffs: tuple[CutoffExpr, ...]
    optional_left_cutoffs: tuple[CutoffExpr, ...]


_SEARCH_TYPE_TO_OBJECTIVE_DIRECTION: dict[SearchType, Direction] = {
    SearchType.NEAREST: Direction.MIXED,
    SearchType.FIRST_IN_RANGE: Direction.ALL_MIN,
    SearchType.LAST_IN_RANGE: Direction.ALL_MAX,
    SearchType.ANY_MATCH: Direction.UNCONSTRAINED,
    SearchType.ENUMERATE: Direction.MIXED,
    SearchType.EXACT_MATCH: Direction.UNCONSTRAINED,
}


def _objective_direction(search_type: SearchType) -> Direction:
    """Map a search type to its objective direction."""
    return _SEARCH_TYPE_TO_OBJECTIVE_DIRECTION[search_type]


def _aggregate_directions(directions: list[_PreferDirection]) -> Direction:
    """Aggregate per-constraint direction preferences into a single Direction.

    Returns UNCONSTRAINED if the list is empty.
    """
    if not directions:
        return Direction.UNCONSTRAINED
    has_min = _PreferDirection.PREFER_MIN in directions
    has_max = _PreferDirection.PREFER_MAX in directions
    if has_min and has_max:
        return Direction.MIXED
    if has_min:
        return Direction.ALL_MIN
    return Direction.ALL_MAX


def _cutoff_sort_key(cutoff: CutoffExpr) -> tuple[str, str]:
    """Deterministic sort key for cutoff expressions."""
    return (repr(cutoff.later_accessor.input_name), repr(cutoff.later_accessor.field_name))


def _collect_tagged_constraints(
    specs: ExtractedSpecs,
) -> list[tuple[DifferenceConstraint, _ConstraintOrigin]]:
    """Collect all constraints with their origin classification.

    The origin distinguishes unconditional constraints (always active),
    then-branch constraints (active when an optional is present), and
    else-branch constraints (active when an optional is absent).
    """
    tagged: list[tuple[DifferenceConstraint, _ConstraintOrigin]] = [
        (c, _ConstraintOrigin.UNCONDITIONAL) for c in specs.unconditional_constraints
    ]
    for constraint_list in specs.conditional_constraints.values():
        tagged.extend((c, _ConstraintOrigin.THEN_BRANCH) for c in constraint_list)
    for constraint_list in specs.else_constraints.values():
        tagged.extend((c, _ConstraintOrigin.ELSE_BRANCH) for c in constraint_list)
    return tagged


def _collect_equalities(specs: ExtractedSpecs) -> itertools.chain[EqualityConstraint]:
    """Collect all equality constraints from all branches.

    Origin classification is unnecessary because equalities affect only
    direction aggregation, not cutoff generation.
    """
    return itertools.chain(
        specs.unconditional_equalities,
        *specs.conditional_equalities.values(),
        *specs.else_equalities.values(),
    )


@dataclass
class _LevelAccumulator:
    """Mutable accumulator for per-level direction and cutoff data."""

    required_directions: list[_PreferDirection]
    optional_directions: list[_PreferDirection]
    right_cutoffs: list[CutoffExpr]
    left_cutoffs: list[CutoffExpr]
    optional_right_cutoffs: list[CutoffExpr]
    optional_left_cutoffs: list[CutoffExpr]

    @classmethod
    def empty(cls) -> _LevelAccumulator:
        """Create an empty accumulator."""
        return cls(
            required_directions=[],
            optional_directions=[],
            right_cutoffs=[],
            left_cutoffs=[],
            optional_right_cutoffs=[],
            optional_left_cutoffs=[],
        )

    def add_constraint(  # noqa: PLR0913 # Too many args mitigated by kwonly args
        self,
        *,
        direction: _PreferDirection,
        later_idx: int,
        later_accessor: FieldAccessor,
        current_field: str,
        origin: _ConstraintOrigin,
        optional_level_indices: frozenset[int],
        bound: BoundValue,
    ) -> None:
        """Classify and record a later-level constraint."""
        is_optional_later = later_idx in optional_level_indices
        if is_optional_later:
            self.optional_directions.append(direction)
        else:
            self.required_directions.append(direction)

        # Required cutoffs: unconditional constraints against required later levels.
        if origin == _ConstraintOrigin.UNCONDITIONAL and not is_optional_later:
            bound_value = normalize_bound(bound)
            cutoff = CutoffExpr(later_accessor=later_accessor, current_field=current_field, bound=bound_value)
            if direction == _PreferDirection.PREFER_MIN:
                self.right_cutoffs.append(cutoff)
            else:
                self.left_cutoffs.append(cutoff)

        # Optional cutoffs: unconditional and then-branch constraints against
        # optional later levels.  Else-branch constraints are excluded because
        # they apply when the optional is absent.
        if origin != _ConstraintOrigin.ELSE_BRANCH and is_optional_later:
            bound_value = normalize_bound(bound)
            cutoff = CutoffExpr(later_accessor=later_accessor, current_field=current_field, bound=bound_value)
            if direction == _PreferDirection.PREFER_MIN:
                self.optional_right_cutoffs.append(cutoff)
            else:
                self.optional_left_cutoffs.append(cutoff)

    def to_direction_analysis(self, search_type: SearchType) -> DirectionAnalysis:
        """Build the final ``DirectionAnalysis`` from accumulated data."""
        return DirectionAnalysis(
            required_constraint_direction=_aggregate_directions(self.required_directions),
            optional_constraint_direction=_aggregate_directions(self.optional_directions),
            objective_direction=_objective_direction(search_type),
            right_cutoffs=tuple(sorted(self.right_cutoffs, key=_cutoff_sort_key)),
            left_cutoffs=tuple(sorted(self.left_cutoffs, key=_cutoff_sort_key)),
            optional_right_cutoffs=tuple(sorted(self.optional_right_cutoffs, key=_cutoff_sort_key)),
            optional_left_cutoffs=tuple(sorted(self.optional_left_cutoffs, key=_cutoff_sort_key)),
        )


def compute_direction_analysis(
    join_plan: JoinPlan,
    specs: ExtractedSpecs,
    member_to_level: dict[InputSelector, int],
) -> tuple[DirectionAnalysis, ...]:
    """Compute direction analysis for each level of a join plan.

    Returns a tuple parallel to ``join_plan.levels``, where element ``i``
    is the ``DirectionAnalysis`` for ``levels[i]``.

    Args:
        join_plan: The greedy join plan with levels in nesting order.
        specs: The original (pre-merge) extracted specs, including all
            constraint variants (unconditional, conditional, else-branch).
        member_to_level: Mapping from InputSelector to level index,
            as built by :func:`~clockwork.dsl.aligner.pipeline.build_member_to_level`.

    Returns:
        Tuple of ``DirectionAnalysis``, one per join level.
    """
    levels = join_plan.levels

    optional_level_indices: frozenset[int] = frozenset(idx for idx, level in enumerate(levels) if level.is_optional)
    tagged_constraints = _collect_tagged_constraints(specs)

    results: list[DirectionAnalysis] = []
    for current_idx, current_level in enumerate(levels):
        acc = _LevelAccumulator.empty()

        for constraint, origin in tagged_constraints:
            # Skip IndexInView constraints (batch iterator position
            # relationships).  They encode ordering and count bounds
            # between iterators of the same view, not field-value
            # relationships suitable for scan cutoffs.
            if not isinstance(constraint.minuend.field_name, str) or not isinstance(
                constraint.subtrahend.field_name, str
            ):
                continue

            minuend_level = member_to_level[constraint.minuend.input_name]
            subtrahend_level = member_to_level[constraint.subtrahend.input_name]

            if minuend_level == current_idx and subtrahend_level > current_idx:
                acc.add_constraint(
                    direction=_PreferDirection.PREFER_MIN,
                    later_idx=subtrahend_level,
                    later_accessor=constraint.subtrahend,
                    current_field=constraint.minuend.field_name,
                    origin=origin,
                    optional_level_indices=optional_level_indices,
                    bound=constraint.bound,
                )
            elif subtrahend_level == current_idx and minuend_level > current_idx:
                assert isinstance(constraint.subtrahend.field_name, str)
                acc.add_constraint(
                    direction=_PreferDirection.PREFER_MAX,
                    later_idx=minuend_level,
                    later_accessor=constraint.minuend,
                    current_field=constraint.subtrahend.field_name,
                    origin=origin,
                    optional_level_indices=optional_level_indices,
                    bound=constraint.bound,
                )

        # Equality constraints (non-strictly-increasing fields) select a
        # *different* downstream candidate at each scan position, breaking
        # the monotonic tightening assumption, so we force MIXED by producing
        # both directions.
        for eq in _collect_equalities(specs):
            left_level = member_to_level[eq.left.input_name]
            right_level = member_to_level[eq.right.input_name]

            if left_level == current_idx and right_level > current_idx:
                later_idx = right_level
            elif right_level == current_idx and left_level > current_idx:
                later_idx = left_level
            else:
                continue

            is_optional_later = later_idx in optional_level_indices
            dirs = acc.optional_directions if is_optional_later else acc.required_directions
            dirs.append(_PreferDirection.PREFER_MIN)
            dirs.append(_PreferDirection.PREFER_MAX)

        results.append(acc.to_direction_analysis(current_level.search_type))

    return tuple(results)
