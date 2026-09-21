# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Presence configuration utilities and maximally permissive windows."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import combinations
from typing import TYPE_CHECKING

from clockwork.dsl.aligner.extract_specs import BoundValue, ExtractedSpecs
from clockwork.dsl.aligner.join_plan import FeasibleWindow
from clockwork.dsl.aligner.objective_analysis import referenced_base_inputs
from clockwork.dsl.aligner.stn import analyze_stn, bound_lt

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable

    from clockwork.dsl.aligner.extract_specs import DifferenceConstraint, FieldAccessor


# Maximum number of branch-driving optionals before we refuse to enumerate
# ``has_candidates`` branch variants for maximally permissive windows.
# 16 branch-driving optionals yields 65,536 variants, far beyond any
# practical aligner.
MAX_OPTIONAL_INPUTS = 16


@dataclass(frozen=True, slots=True)
class PresenceConfig:
    """Which optional inputs are present in this configuration.

    Attributes:
        present_optionals: The set of optional input names that are present.
    """

    present_optionals: frozenset[str]


def enumerate_presence_configs(
    optional_inputs: frozenset[str],
) -> Iterable[PresenceConfig]:
    """Enumerate all 2^p presence configurations for the given optional inputs.

    Returns:
        ``PresenceConfig`` values, one per combination.
        Ordered by size (fewest present first), then lexicographically.
    """
    sorted_optionals = sorted(optional_inputs)
    for count in range(len(sorted_optionals) + 1):
        yield from (
            PresenceConfig(present_optionals=frozenset(combo)) for combo in combinations(sorted_optionals, count)
        )


def merge_specs_for_config(
    specs: ExtractedSpecs,
    config: PresenceConfig,
    *,
    all_optional_inputs: frozenset[str],
) -> ExtractedSpecs:
    """Build a config-specific ``ExtractedSpecs`` with merged constraints/objectives.

    Merges unconditional specs with conditional specs for present optionals
    and else-branch specs for absent optionals.
    Applies auto-drop semantics: unconditional constraints and objectives
    referencing absent optional inputs are removed.

    Returns a new ``ExtractedSpecs`` with merged constraints/objectives as
    unconditional and empty conditional/else dicts.
    """
    absent_optionals = all_optional_inputs - config.present_optionals

    # Auto-drop: remove unconditional constraints/equalities referencing absent optionals.
    constraints = [
        c
        for c in specs.unconditional_constraints
        if c.minuend.base_input_name not in absent_optionals and c.subtrahend.base_input_name not in absent_optionals
    ]
    equalities = [
        eq
        for eq in specs.unconditional_equalities
        if eq.left.base_input_name not in absent_optionals and eq.right.base_input_name not in absent_optionals
    ]

    # Auto-drop: remove unconditional objectives whose *every* referenced
    # input is absent.  Objectives referencing a mix of present and absent
    # inputs are kept — per-term filtering happens in objective_analysis.
    objectives = [
        obj for obj in specs.unconditional_objectives if not referenced_base_inputs(obj.expr) <= absent_optionals
    ]

    for optional_name in sorted(config.present_optionals):
        constraints.extend(specs.conditional_constraints.get(optional_name, ()))
        equalities.extend(specs.conditional_equalities.get(optional_name, ()))
        objectives.extend(specs.conditional_objectives.get(optional_name, ()))

    absent_has_candidates_constraints = set(specs.else_constraints.keys()) - config.present_optionals
    for optional_name in sorted(absent_has_candidates_constraints):
        constraints.extend(specs.else_constraints[optional_name])

    absent_has_candidates_equalities = set(specs.else_equalities.keys()) - config.present_optionals
    for optional_name in sorted(absent_has_candidates_equalities):
        equalities.extend(specs.else_equalities[optional_name])

    absent_has_candidates_objectives = set(specs.else_objectives.keys()) - config.present_optionals
    for optional_name in sorted(absent_has_candidates_objectives):
        objectives.extend(specs.else_objectives[optional_name])

    objectives.sort(key=lambda o: o.source_index)

    active_accessors = frozenset(fa for fa in specs.field_accessors if fa.base_input_name not in absent_optionals)

    # Filter field assumptions to only include active inputs.
    active_assumptions = {
        fa: prop for fa, prop in specs.field_assumptions.items() if fa.base_input_name not in absent_optionals
    }

    return ExtractedSpecs(
        field_accessors=active_accessors,
        field_assumptions=active_assumptions,
        unconditional_constraints=tuple(constraints),
        unconditional_equalities=tuple(equalities),
        unconditional_objectives=tuple(objectives),
        conditional_constraints={},
        conditional_equalities={},
        conditional_objectives={},
        else_constraints={},
        else_equalities={},
        else_objectives={},
    )


def _is_required_only(
    constraint: DifferenceConstraint,
    optional_inputs: frozenset[str],
) -> bool:
    """Return whether both operands of a constraint reference required inputs."""
    return (
        constraint.minuend.base_input_name not in optional_inputs
        and constraint.subtrahend.base_input_name not in optional_inputs
    )


def _involves_optional(
    constraint: DifferenceConstraint,
    optional_name: str,
    optional_inputs: frozenset[str],
) -> bool:
    """Return whether this constraint links the given optional to a required input.

    Returns True only when exactly one side is ``optional_name`` and the
    other side is a required input (not another optional).  This ensures
    the STN for optional-input windows does not include transitive paths
    through other optionals whose presence varies at runtime.

    The STN produces windows in both directions; directional filtering
    is handled downstream by ``_compute_initial_windows``.
    """
    m_base = constraint.minuend.base_input_name
    s_base = constraint.subtrahend.base_input_name
    names = {m_base, s_base}
    if optional_name not in names:
        return False
    other_names = names - {optional_name}
    if not other_names:
        # Both sides reference the same optional (e.g., batch boundaries).
        return False
    return other_names.pop() not in optional_inputs


def _relax_window(
    existing: FeasibleWindow,
    variant_lo: BoundValue | None,
    variant_hi: BoundValue | None,
) -> FeasibleWindow:
    """Return a window relaxed to include the variant's bounds.

    Relaxation means taking the most permissive (widest) bounds:
    - ``lo = min(existing.lo, variant_lo)`` — most negative.
    - ``hi = max(existing.hi, variant_hi)`` — most positive.
    ``None`` (unbounded) always wins as most permissive.
    """
    if existing.lo is None or variant_lo is None:
        new_lo: BoundValue | None = None
    else:
        new_lo = variant_lo if bound_lt(variant_lo, existing.lo) else existing.lo

    if existing.hi is None or variant_hi is None:
        new_hi: BoundValue | None = None
    else:
        new_hi = variant_hi if bound_lt(existing.hi, variant_hi) else existing.hi

    if new_lo is existing.lo and new_hi is existing.hi:
        return existing

    return FeasibleWindow(
        reference_accessor=existing.reference_accessor,
        target_accessor=existing.target_accessor,
        lo=new_lo,
        hi=new_hi,
    )


def compute_maximally_permissive_windows(
    specs: ExtractedSpecs,
    optional_inputs: frozenset[str],
) -> dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow]:
    """Compute maximally permissive feasible windows for all input pairs.

    Produces two categories of windows:

    **Required-only windows:** For each pair of required field accessors,
    computes the widest feasible window across all 2^k
    ``has_candidates`` branch combinations.

    **Optional-input windows:** For each optional input ``O``, computes
    windows relating ``O``'s field accessors to already-bound inputs.
    These are relaxed across the 2^k ``has_candidates`` variants
    (where ``O`` is always assumed present).  The resulting window is
    the widest range that is valid across all presence configurations
    where ``O`` is present, and can be applied unconditionally in the
    generated "present" code path.

    Each variant builds a constraint set from unconditional constraints
    plus the selected constraint branch (then or else) for each
    ``has_candidates`` optional.  The resulting STN is analyzed, and
    fractional windows are accumulated by element-wise relaxation (most
    permissive bounds).

    Optionals without ``has_candidates`` branch constraints are always
    treated as absent — their auto-drop only removes constraints, never
    adds tighter ones.

    Args:
        specs: Extracted specs (un-merged, with conditional/else dicts).
        optional_inputs: Names of optional inputs.

    Returns:
        Mapping from ``(target_accessor, reference_accessor)`` to the
        maximally permissive ``FeasibleWindow``.

    Raises:
        InconsistentConstraintsError: If any branch combination produces
            contradictory constraints (negative cycle in the STN).
    """
    hc_optionals = sorted(set(specs.conditional_constraints.keys()) | set(specs.else_constraints.keys()))
    num_variants = 1 << len(hc_optionals)

    result: dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow] = {}

    # --- Pass 1: required-only windows ---
    result = _compute_windows_for_constraint_filter(
        specs=specs,
        hc_optionals=hc_optionals,
        num_variants=num_variants,
        constraint_filter=lambda c: _is_required_only(c, optional_inputs),
    )

    # --- Pass 2: per-optional windows ---
    for opt_name in sorted(optional_inputs):
        opt_result = _compute_windows_for_constraint_filter(
            specs=specs,
            hc_optionals=hc_optionals,
            num_variants=num_variants,
            constraint_filter=lambda c, name=opt_name: (
                _is_required_only(c, optional_inputs) or _involves_optional(c, name, optional_inputs)
            ),
        )
        # Merge optional windows into the result.  Only add windows that
        # involve the optional — required-only pairs were already computed
        # in pass 1 with potentially different variant counts and should
        # not be overwritten.
        for key, window in opt_result.items():
            target_acc, ref_acc = key
            if opt_name in {target_acc.base_input_name, ref_acc.base_input_name}:
                result[key] = window

    return result


def _compute_windows_for_constraint_filter(
    *,
    specs: ExtractedSpecs,
    hc_optionals: list[str],
    num_variants: int,
    constraint_filter: Callable[[DifferenceConstraint], bool],
) -> dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow]:
    """Compute maximally permissive windows using a constraint filter.

    Iterates over all 2^k ``has_candidates`` variants, building an STN
    from constraints passing ``constraint_filter``.  Returns the relaxed
    (widest) window for each accessor pair.
    """
    base_constraints = [c for c in specs.unconditional_constraints if constraint_filter(c)]

    result: dict[tuple[FieldAccessor, FieldAccessor], FeasibleWindow] = {}
    pair_variant_count: dict[tuple[FieldAccessor, FieldAccessor], int] = {}

    for branch_bits in range(num_variants):
        variant_constraints = list(base_constraints)
        for bit_idx, opt_name in enumerate(hc_optionals):
            use_then_branch = bool(branch_bits & (1 << bit_idx))
            if use_then_branch:
                branch_constraints = specs.conditional_constraints.get(opt_name, ())
            else:
                branch_constraints = specs.else_constraints.get(opt_name, ())
            variant_constraints.extend(c for c in branch_constraints if constraint_filter(c))

        stn = analyze_stn(variant_constraints)

        for i_idx, node_i in enumerate(stn.nodes):
            for j_idx, node_j in enumerate(stn.nodes):
                if i_idx == j_idx:
                    continue
                lo, hi = stn.feasible_window(node_i, node_j)
                key = (node_i, node_j)
                pair_variant_count[key] = pair_variant_count.get(key, 0) + 1
                existing = result.get(key)
                if existing is None:
                    result[key] = FeasibleWindow(
                        reference_accessor=node_j,
                        target_accessor=node_i,
                        lo=lo,
                        hi=hi,
                    )
                else:
                    result[key] = _relax_window(existing, lo, hi)

    # Any pair that appeared in some variants but not all has at least one
    # unconstrained variant, making the maximally permissive window unbounded.
    for key, count in pair_variant_count.items():
        if count < num_variants:
            result[key] = FeasibleWindow(
                reference_accessor=key[1],
                target_accessor=key[0],
                lo=None,
                hi=None,
            )

    return result
