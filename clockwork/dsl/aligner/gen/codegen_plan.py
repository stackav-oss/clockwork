# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Codegen plan data types for aligner C++ code generation.

These types define the interface between the analysis pipeline
(``pipeline.py``) and the C++ code generator.  The plan is
constructed by :func:`~clockwork.dsl.aligner.pipeline.compute_codegen_plan`.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, TypeAlias

from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    EqualityConstraint,
)

if TYPE_CHECKING:
    from clockwork.dsl.aligner.direction_analysis import DirectionAnalysis
    from clockwork.dsl.aligner.extract_specs import ExtractedSpecs, Objective
    from clockwork.dsl.aligner.join_plan import FeasibleWindow, JoinLevel
    from clockwork.dsl.aligner.partition_types import PartitionPlan

Constraint: TypeAlias = DifferenceConstraint | EqualityConstraint


@dataclass(frozen=True, slots=True)
class ConditionalBranch:
    """Constraints conditional on an optional input's binding state.

    Note: Either branch may be empty.
    """

    when_present: tuple[Constraint, ...]
    when_absent: tuple[Constraint, ...]


@dataclass(frozen=True, slots=True)
class CodegenLevel:
    """One level of the generated search function.

    Packages a :class:`~clockwork.dsl.aligner.join_plan.JoinLevel` with
    its pre-computed direction analysis and assigned constraints for
    code generation.

    Attributes:
        level_index: Position in the join plan (0 = outermost loop).
        join_level: The underlying join-plan level (search type, windows,
            objective term, etc.).
        reuse: Whether this input is allowed to reuse messages.
        direction: Direction analysis for scan direction, cutoffs, and
            early termination.
        unconditional_checks: Constraints always checked at this level.
            Both endpoints are required or already bound when this
            level executes, including same-input constraints.
        conditional_checks: Constraints checked conditionally on an
            optional input's binding state.  Keyed by optional input
            name.
        initial_windows: Maximally permissive initial windows for
            iteration windowing.
        has_downstream_optionals: Whether any optional input appears
            at a later (higher-index) level.  Levels with downstream
            optionals use best-so-far tracking to maximise fullness.
    """

    level_index: int
    join_level: JoinLevel
    reuse: bool
    direction: DirectionAnalysis
    unconditional_checks: tuple[Constraint, ...]
    conditional_checks: dict[str, ConditionalBranch]
    initial_windows: tuple[FeasibleWindow, ...]
    has_downstream_optionals: bool


@dataclass(frozen=True, slots=True)
class CodegenPlan:
    """Complete codegen input for one aligner.

    Attributes:
        aligner_name: Name of the aligner.
        levels: Codegen levels in join-plan order (outermost first).
        input_names: All input base names in declaration order.
        optional_input_names: Names of optional inputs.
        batch_input_names: Base names of batch inputs (``batch_size`` is set).
        optional_timeouts: Per-optional timeout in nanoseconds.
            Only populated for optional inputs with nonzero timeouts.
        objectives: All objectives sorted in declaration order.
        time_fields: Set of ``(input_name, field_name)`` pairs whose schema
            type is ``SyncTime`` or ``Duration``.
        specs: The extracted specs (for field accessor metadata).
        partition_plan: Optional residual partition metadata for search codegen.
    """

    aligner_name: str
    levels: tuple[CodegenLevel, ...]
    input_names: tuple[str, ...]
    optional_input_names: frozenset[str]
    batch_input_names: frozenset[str]
    optional_timeouts: dict[str, int]
    objectives: tuple[Objective, ...]
    time_fields: frozenset[tuple[str, str]]
    specs: ExtractedSpecs
    partition_plan: PartitionPlan | None = None
