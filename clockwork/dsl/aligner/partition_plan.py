# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Residual join partitioning for generated aligner search code.

This module computes conservative residual connected components over the
existing flat codegen join plan.  It does not choose a new join order.  Instead,
it identifies suffixes whose remaining levels can be solved as independent
islands under the current prefix.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    EqualityConstraint,
    InputSelector,
    base_input_name,
)
from clockwork.dsl.aligner.objective_analysis import referenced_inputs
from clockwork.dsl.aligner.partition_types import PartitionComponent, PartitionPlan, PartitionPoint

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable

    from clockwork.dsl.aligner.extract_specs import Objective
    from clockwork.dsl.aligner.gen.codegen_plan import CodegenPlan, Constraint

_minimum_connected_levels = 2


@dataclass(slots=True)
class _DisjointSet:
    """Tiny disjoint-set helper for level-index components."""

    parent: dict[int, int]

    @classmethod
    def from_items(cls, items: Iterable[int]) -> _DisjointSet:
        """Create a disjoint set with each item in its own component."""
        return cls(parent={item: item for item in items})

    def find(self, item: int) -> int:
        """Return the canonical representative for ``item``."""
        parent = self.parent[item]
        if parent != item:
            parent = self.find(parent)
            self.parent[item] = parent
        return parent

    def union(self, first: int, second: int) -> None:
        """Merge the two components containing ``first`` and ``second``."""
        first_root = self.find(first)
        second_root = self.find(second)
        if first_root != second_root:
            self.parent[second_root] = first_root


def compute_partition_plan(plan: CodegenPlan) -> PartitionPlan:
    """Compute conservative residual partitions for ``plan``.

    A partition is emitted only when the remaining suffix splits into more than
    one connected component.  The flat join order inside each component is
    preserved.
    """
    selector_to_level = {level.join_level.input: level.level_index for level in plan.levels}
    partitions: list[PartitionPoint] = []
    visited_scopes: set[tuple[int, ...]] = set()

    def visit_scope(scope_level_indices: tuple[int, ...]) -> None:
        if len(scope_level_indices) < _minimum_connected_levels or scope_level_indices in visited_scopes:
            return
        visited_scopes.add(scope_level_indices)

        components = _residual_components(plan, selector_to_level, scope_level_indices)
        if len(components) <= 1:
            visit_scope(scope_level_indices[1:])
            return

        partitions.append(
            PartitionPoint(
                partition_id=len(partitions),
                start_level_index=scope_level_indices[0],
                scope_level_indices=scope_level_indices,
                components=tuple(
                    PartitionComponent(component_id=index, level_indices=component)
                    for index, component in enumerate(components)
                ),
            )
        )
        for component in components:
            visit_scope(component)

    visit_scope(tuple(level.level_index for level in plan.levels))

    return PartitionPlan(partitions=tuple(partitions))


def _residual_components(
    plan: CodegenPlan,
    selector_to_level: dict[InputSelector, int],
    scope_level_indices: tuple[int, ...],
) -> tuple[tuple[int, ...], ...]:
    """Return residual connected components for the suffix starting at a level."""
    remaining_indices = frozenset(scope_level_indices)
    dsu = _DisjointSet.from_items(remaining_indices)

    def connect_selectors(selectors: Iterable[InputSelector]) -> None:
        levels = sorted(
            {
                level
                for selector in selectors
                if (level := selector_to_level.get(selector)) is not None and level in remaining_indices
            }
        )
        if len(levels) < _minimum_connected_levels:
            return
        first = levels[0]
        for level in levels[1:]:
            dsu.union(first, level)

    _connect_same_base_remaining_levels(plan, remaining_indices, connect_selectors)

    for constraint in (*plan.specs.unconditional_constraints, *plan.specs.unconditional_equalities):
        connect_selectors(_constraint_selectors(constraint))

    for objective in plan.specs.unconditional_objectives:
        connect_selectors(referenced_inputs(objective.expr))

    _connect_branch_specs(plan, remaining_indices, selector_to_level, connect_selectors)

    grouped: dict[int, list[int]] = {}
    for level_index in sorted(remaining_indices):
        grouped.setdefault(dsu.find(level_index), []).append(level_index)

    components = tuple(tuple(levels) for levels in grouped.values())
    return tuple(sorted(components, key=lambda levels: levels[0]))


def _connect_same_base_remaining_levels(
    plan: CodegenPlan,
    remaining_indices: frozenset[int],
    connect_selectors: Callable[[Iterable[InputSelector]], None],
) -> None:
    """Keep all unbound selectors for one base input in the same component."""
    selectors_by_base: dict[str, list[InputSelector]] = {}
    for level in plan.levels:
        if level.level_index not in remaining_indices:
            continue
        selectors_by_base.setdefault(base_input_name(level.join_level.input), []).append(level.join_level.input)
    for selectors in selectors_by_base.values():
        connect_selectors(selectors)


def _constraint_selectors(constraint: Constraint) -> tuple[InputSelector, InputSelector]:
    """Return the selectors referenced by a hard constraint."""
    match constraint:
        case DifferenceConstraint(minuend=a, subtrahend=b):
            return (a.input_name, b.input_name)
        case EqualityConstraint(left=a, right=b):
            return (a.input_name, b.input_name)


def _connect_branch_specs(
    plan: CodegenPlan,
    remaining_indices: frozenset[int],
    selector_to_level: dict[InputSelector, int],
    connect_selectors: Callable[[Iterable[InputSelector]], None],
) -> None:
    """Connect conservative dependencies from ``has_candidates`` branches."""

    def branch_selectors(
        optional_name: str,
        referenced: Iterable[InputSelector],
    ) -> tuple[InputSelector, ...]:
        selectors: list[InputSelector] = list(referenced)
        controller_level = selector_to_level.get(optional_name)
        if controller_level is not None and controller_level in remaining_indices:
            selectors.append(optional_name)
        return tuple(selectors)

    def connect_constraints(grouped_constraints: Iterable[tuple[str, Iterable[Constraint]]]) -> None:
        for optional_name, constraints in grouped_constraints:
            for constraint in constraints:
                connect_selectors(branch_selectors(optional_name, _constraint_selectors(constraint)))

    def connect_objectives(grouped_objectives: Iterable[tuple[str, Iterable[Objective]]]) -> None:
        for optional_name, objectives in grouped_objectives:
            for objective in objectives:
                connect_selectors(branch_selectors(optional_name, referenced_inputs(objective.expr)))

    connect_constraints(plan.specs.conditional_constraints.items())
    connect_constraints(plan.specs.conditional_equalities.items())
    connect_objectives(plan.specs.conditional_objectives.items())
    connect_constraints(plan.specs.else_constraints.items())
    connect_constraints(plan.specs.else_equalities.items())
    connect_objectives(plan.specs.else_objectives.items())
