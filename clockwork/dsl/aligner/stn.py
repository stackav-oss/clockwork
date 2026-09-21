# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — Simple Temporal Network construction and analysis.

This module builds a Simple Temporal Network (STN) from the difference
constraints produced by spec extraction, then runs Floyd-Warshall all-pairs
shortest paths to compute tightest bounds, check constraint consistency, and
detect equality classes.

See docs/stn_analysis.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import TYPE_CHECKING

from clockwork.dsl.ir import primitive
from clockwork.dsl.ir.clkbuiltins import IntegerPrimitiveType
from clockwork.dsl.ir.typesys import InferenceVar

if TYPE_CHECKING:
    from collections.abc import Iterable, Sequence

    from clockwork.dsl.aligner.extract_specs import BoundValue, DifferenceConstraint, FieldAccessor


@dataclass(frozen=True, slots=True)
class StnEdge:
    """A directed edge in the Simple Temporal Network.

    Represents the constraint ``destination - source <= weight``.

    Attributes:
        source: The subtrahend field accessor.
        destination: The minuend field accessor.
        weight: Upper bound in canonical units, with type preserved
            (``UnitValue`` for durations, ``DecimalValue`` for integers).
        source_constraint: Original constraint, for diagnostics.
    """

    source: FieldAccessor
    destination: FieldAccessor
    weight: BoundValue
    source_constraint: DifferenceConstraint


@dataclass(frozen=True, slots=True)
class StnAnalysis:
    """Result of STN construction and Floyd-Warshall analysis.

    The distance matrix ``d[i][j]`` gives the tightest upper bound on
    ``node_i - node_j``.  That is, node *i* can be at most ``d[i][j]`` ahead
    of node *j*.

    Attributes:
        nodes: Ordered tuple of all field accessors in the STN.
        distance_matrix: ``d[i][j]`` is the tightest bound on ``node_i - node_j``,
            or ``None`` if unconstrained.
        equality_classes: Groups of nodes constrained to be exactly equal.
            Singleton classes (nodes not equal to any other) are included.
        edges: Original edges, retained for diagnostics.
    """

    nodes: tuple[FieldAccessor, ...]
    distance_matrix: tuple[tuple[BoundValue | None, ...], ...]
    equality_classes: tuple[frozenset[FieldAccessor], ...]
    edges: tuple[StnEdge, ...]

    def _index_of(self, node: FieldAccessor) -> int:
        """Return the index of *node* in the node list.

        Raises:
            RuntimeError: If the node is not in the STN.
        """
        try:
            return self.nodes.index(node)
        except ValueError:
            msg = f"Node {node} is not in the STN"
            raise RuntimeError(msg) from None

    def tightest_bound(self, i: FieldAccessor, j: FieldAccessor) -> BoundValue | None:
        """Return the tightest upper bound on ``i - j``, or ``None`` if unconstrained."""
        return self.distance_matrix[self._index_of(i)][self._index_of(j)]

    def feasible_window(
        self,
        i: FieldAccessor,
        j: FieldAccessor,
    ) -> tuple[BoundValue | None, BoundValue | None]:
        """Return the feasible window ``[lo, hi]`` for ``i - j``.

        ``lo = -d[j][i]`` (or ``None`` if unconstrained below).
        ``hi = d[i][j]`` (or ``None`` if unconstrained above).

        Returns typed ``BoundValue`` objects preserving the duration /
        integer type from the original constraints.
        """
        i_idx = self._index_of(i)
        j_idx = self._index_of(j)
        d_ji = self.distance_matrix[j_idx][i_idx]
        d_ij = self.distance_matrix[i_idx][j_idx]
        lo = negate_bound(d_ji) if d_ji is not None else None
        hi = d_ij
        return (lo, hi)

    def are_equal(self, i: FieldAccessor, j: FieldAccessor) -> bool:
        """Return ``True`` if *i* and *j* are constrained to be exactly equal."""
        i_idx = self._index_of(i)
        j_idx = self._index_of(j)
        d_ij = self.distance_matrix[i_idx][j_idx]
        d_ji = self.distance_matrix[j_idx][i_idx]
        zero = Decimal(0)
        return d_ij is not None and d_ji is not None and d_ij.value == zero and d_ji.value == zero

    def equality_class_of(self, node: FieldAccessor) -> frozenset[FieldAccessor]:
        """Return the equality class containing *node*.

        Raises:
            RuntimeError: If the node is not in the STN.
        """
        self._index_of(node)  # validate presence
        for cls in self.equality_classes:
            if node in cls:
                return cls
        # Should not be reachable — every node is in at least a singleton class.
        msg = f"Node {node} not found in any equality class"
        raise RuntimeError(msg)  # pragma: no cover


class InconsistentConstraintsError(Exception):
    """Raised when the STN contains a negative cycle (contradictory constraints).

    Attributes:
        node: The node at which the negative diagonal was detected.
        negative_value: The negative diagonal value.
    """

    def __init__(self, node: FieldAccessor, negative_value: Decimal) -> None:
        """Initialize with the node and the negative diagonal value."""
        self.node = node
        self.negative_value = negative_value
        node_label = f"{node.input_name}.{node.field_name}" if isinstance(node.input_name, str) else repr(node)
        msg = (
            f"Contradictory constraints detected: node {node_label} "
            f"has negative self-distance {negative_value}, indicating a negative cycle"
        )
        super().__init__(msg)


def normalize_bound(bound: BoundValue) -> BoundValue:
    """Convert a constraint bound to canonical units, preserving type.

    For ``UnitValue``, converts to the canonical unit (e.g., seconds).
    For ``DecimalValue``, returns as-is (already unitless).
    """
    match bound:
        case primitive.UnitValue():
            return bound.as_unit(bound.unit.canonical_unit)
        case primitive.DecimalValue():
            return bound


def negate_bound(bound: BoundValue) -> BoundValue:
    """Return ``-bound`` as a new ``BoundValue`` with the same type."""
    match bound:
        case primitive.UnitValue():
            return primitive.UnitValue.make(
                value=-bound.value,
                unit=bound.unit,
            )
        case primitive.DecimalValue():
            return primitive.DecimalValue(
                type_info=bound.type_info,
                value=-bound.value,
            )


def bound_lt(a: BoundValue, b: BoundValue) -> bool:
    """Return ``True`` if ``a < b``.  Checks type compatibility."""
    _check_bound_compatible(a, b)
    match a:
        case primitive.UnitValue():
            assert isinstance(b, primitive.UnitValue)
            return a.as_unit(a.unit.canonical_unit).value < b.as_unit(b.unit.canonical_unit).value
        case primitive.DecimalValue():
            return a.value < b.value


def add_bound(a: BoundValue, b: BoundValue) -> BoundValue:
    """Return ``a + b`` as a new ``BoundValue``.  Checks type compatibility."""
    _check_bound_compatible(a, b)
    match a:
        case primitive.UnitValue():
            assert isinstance(b, primitive.UnitValue)
            a_canon = a.as_unit(a.unit.canonical_unit)
            b_canon = b.as_unit(b.unit.canonical_unit)
            return primitive.UnitValue.make(
                value=a_canon.value + b_canon.value,
                unit=a.unit.canonical_unit,
            )
        case primitive.DecimalValue():
            return primitive.DecimalValue(
                type_info=a.type_info,
                value=a.value + b.value,
            )


def _check_bound_compatible(a: BoundValue, b: BoundValue) -> None:
    """Verify that two bound values have compatible types.

    Raises ``TypeError`` on mismatches: different Python type
    (``UnitValue`` vs ``DecimalValue``), incompatible canonical units,
    mismatched integer types, or unsigned integers.
    """
    match a:
        case primitive.UnitValue():
            if not isinstance(b, primitive.UnitValue):
                msg = f"Incompatible bound types: {type(a).__name__} and {type(b).__name__}"
                raise TypeError(msg)

            if a.unit.canonical_unit != b.unit.canonical_unit:
                msg = f"Incompatible units: {a.unit} and {b.unit}"
                raise TypeError(msg)
        case primitive.DecimalValue():
            if not isinstance(b, primitive.DecimalValue):
                msg = f"Incompatible bound types: {type(a).__name__} and {type(b).__name__}"
                raise TypeError(msg)
            a_info = a.type_info
            b_info = b.type_info
            if isinstance(a_info, InferenceVar):
                a_info = a_info.resolution()
            if isinstance(b_info, InferenceVar):
                b_info = b_info.resolution()
            if a_info != b_info:
                msg = f"Incompatible integer types: {a_info} and {b_info}"
                raise TypeError(msg)
            if isinstance(a_info, IntegerPrimitiveType) and not a_info.signed:
                msg = "Unsigned integer bounds are not supported in STN arithmetic"
                raise TypeError(msg)


def _build_node_zeros(
    constraints: Sequence[DifferenceConstraint],
    nodes: tuple[FieldAccessor, ...],
) -> list[BoundValue]:
    """Create typed zeros for the distance matrix diagonal.

    Each constraint's bound carries its type (``UnitValue`` for durations,
    ``DecimalValue`` for integers).  We use the first bound touching each
    node to create a zero of the correct type.
    """
    field_zeros: dict[FieldAccessor, BoundValue] = {}
    for c in constraints:
        zero = _make_zero_from_bound(c.bound)
        field_zeros.setdefault(c.minuend, zero)
        field_zeros.setdefault(c.subtrahend, zero)
    return [field_zeros[node] for node in nodes]


def _make_zero_from_bound(
    bound: primitive.UnitValue | primitive.DecimalValue,
) -> BoundValue:
    """Create a zero bound with the same type as *bound*."""
    match bound:
        case primitive.UnitValue():
            return primitive.UnitValue.make(
                value=Decimal(0),
                unit=bound.unit.canonical_unit,
            )
        case primitive.DecimalValue():
            return primitive.DecimalValue(
                type_info=bound.type_info,
                value=Decimal(0),
            )


def _collect_nodes(constraints: Sequence[DifferenceConstraint]) -> tuple[FieldAccessor, ...]:
    """Collect and deduplicate all field accessors from constraints, in stable order."""
    seen: dict[FieldAccessor, None] = {}
    for c in constraints:
        seen.setdefault(c.minuend, None)
        seen.setdefault(c.subtrahend, None)
    return tuple(seen)


def _build_edges(
    constraints: Sequence[DifferenceConstraint],
) -> tuple[StnEdge, ...]:
    """Build STN edges from difference constraints."""
    edges = [
        StnEdge(
            source=c.subtrahend,
            destination=c.minuend,
            weight=normalize_bound(c.bound),
            source_constraint=c,
        )
        for c in constraints
    ]
    return tuple(edges)


def _compute_distance_matrix(
    n: int,
    edges: Sequence[StnEdge],
    node_index: dict[FieldAccessor, int],
    node_zeros: list[BoundValue],
) -> tuple[tuple[BoundValue | None, ...], ...]:
    """Run Floyd-Warshall all-pairs shortest paths.

    Returns an immutable distance matrix ``d`` where ``d[i][j]`` is the tightest
    upper bound on ``node_i - node_j``, or ``None`` if no path exists.
    """
    d: list[list[BoundValue | None]] = [[None] * n for _ in range(n)]
    for i in range(n):
        d[i][i] = node_zeros[i]

    for edge in edges:
        src_idx = node_index[edge.source]
        dst_idx = node_index[edge.destination]
        existing = d[dst_idx][src_idx]
        if existing is None or bound_lt(edge.weight, existing):
            d[dst_idx][src_idx] = edge.weight

    for k in range(n):
        for i in range(n):
            d_ik = d[i][k]
            if d_ik is None:
                continue
            for j in range(n):
                d_kj = d[k][j]
                if d_kj is None:
                    continue
                new_val = add_bound(d_ik, d_kj)
                existing_ij = d[i][j]
                if existing_ij is None or bound_lt(new_val, existing_ij):
                    d[i][j] = new_val

    return tuple(tuple(row) for row in d)


def _check_consistency(
    d: tuple[tuple[BoundValue | None, ...], ...],
    nodes: tuple[FieldAccessor, ...],
) -> None:
    """Check for negative cycles (contradictory constraints).

    Raises:
        InconsistentConstraintsError: If a negative diagonal entry is found.
    """
    zero = Decimal(0)
    for i, node in enumerate(nodes):
        val = d[i][i]
        if val is not None and val.value < zero:
            raise InconsistentConstraintsError(node=node, negative_value=val.value)


def _find_equality_classes(
    d: tuple[tuple[BoundValue | None, ...], ...],
    nodes: tuple[FieldAccessor, ...],
) -> tuple[frozenset[FieldAccessor], ...]:
    """Detect equality classes from the distance matrix.

    Two nodes *i*, *j* are equal iff ``d[i][j] == 0`` and ``d[j][i] == 0``.
    Uses union-find to group equal nodes.
    """
    n = len(nodes)

    parent = list(range(n))

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]  # path compression
            x = parent[x]
        return x

    def union(x: int, y: int) -> None:
        rx, ry = find(x), find(y)
        if rx != ry:
            parent[rx] = ry

    zero = Decimal(0)
    for i in range(n):
        for j in range(i + 1, n):
            d_ij = d[i][j]
            d_ji = d[j][i]
            if d_ij is not None and d_ji is not None and d_ij.value == zero and d_ji.value == zero:
                union(i, j)

    groups: dict[int, set[FieldAccessor]] = {}
    for i, node in enumerate(nodes):
        root = find(i)
        groups.setdefault(root, set()).add(node)

    return tuple(frozenset(g) for g in groups.values())


def analyze_stn(constraints: Iterable[DifferenceConstraint]) -> StnAnalysis:
    """Build and analyze an STN from difference constraints.

    This is the main entry point.  It:

    1. Collects all field accessor nodes from the constraints.
    2. Builds the edge set with normalized (canonical-unit) bounds.
    3. Runs Floyd-Warshall to compute tightest bounds.
    4. Checks consistency (raises on negative cycles).
    5. Detects equality classes.

    The caller decides which constraints to pass — typically the unconditional
    constraints for one presence configuration at a time.  Call this function
    multiple times with different constraint sets for different presence
    configurations.

    Args:
        constraints: Iterable of difference constraints.

    Returns:
        An StnAnalysis containing the distance matrix and derived
        results.

    Raises:
        InconsistentConstraintsError: If the constraints are contradictory
            (negative cycle in the STN).
    """
    constraint_list = list(constraints)

    nodes = _collect_nodes(constraint_list)
    edges = _build_edges(constraint_list)
    node_index = {node: i for i, node in enumerate(nodes)}
    n = len(nodes)

    node_zeros = _build_node_zeros(constraint_list, nodes)

    distance_matrix = _compute_distance_matrix(n, edges, node_index, node_zeros)
    _check_consistency(distance_matrix, nodes)
    equality_classes = _find_equality_classes(distance_matrix, nodes)

    return StnAnalysis(
        nodes=nodes,
        distance_matrix=distance_matrix,
        equality_classes=equality_classes,
        edges=edges,
    )
