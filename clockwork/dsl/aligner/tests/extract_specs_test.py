# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner spec extraction — constraint and objective normalization."""

from __future__ import annotations

from decimal import Decimal

import pytest
from clockwork.dsl.aligner.extract_specs import (
    INDEX_IN_VIEW,
    DifferenceConstraint,
    EqualityConstraint,
    ExtractedSpecs,
    FieldAccessor,
    FieldProperty,
    FirstInBatch,
    IndexInView,
    InputSelector,
    LastInBatch,
    ObjectiveSense,
    SpecExtractionError,
    extract_specs,
)
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.dfl import TypeCheckError
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.primitive import UnitValue


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _compile_type_check_extract(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> ExtractedSpecs:
    """Compile source, type-check, and extract specs."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    type_check_aligner(aligner_ir, module.context)
    return extract_specs(aligner_ir)


def _find_constraint(
    constraints: tuple[DifferenceConstraint, ...],
    minuend_input: InputSelector,
    minuend_field: str | IndexInView,
    subtrahend_input: InputSelector,
    subtrahend_field: str | IndexInView,
) -> DifferenceConstraint | None:
    """Find a constraint by its field accessor names."""
    for c in constraints:
        if (
            c.minuend.input_name == minuend_input
            and c.minuend.field_name == minuend_field
            and c.subtrahend.input_name == subtrahend_input
            and c.subtrahend.field_name == subtrahend_field
        ):
            return c
    return None


# Shared schema boilerplate.
_SCHEMAS = """\
// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
        // Time of validity
        #1 tov: SyncTime;
    }
}
// Camera image message
schema CameraImage {
    uuid: 22222222-2222-2222-2222-222222222222;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
    }
}
// Radar detection
schema RadarDetection {
    uuid: 33333333-3333-3333-3333-333333333333;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
    }
}
// Vehicle pose
schema VehiclePose {
    uuid: 44444444-4444-4444-4444-444444444444;
    fields {
        // Time of validity
        #0 tov: SyncTime;
    }
}
"""

_HEADER = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
"""


class TestConstraintExtraction:
    """Tests for constraint pattern recognition and normalization."""

    def test_equality_constraint_strictly_increasing(self, fs_importer: FilesystemImporter) -> None:
        """require(a == b) with both fields strictly_increasing produces DifferenceConstraint pair."""
        source = f"""\
{_HEADER}
// Equality test
aligner EqAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(lidar.observation_time == camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_eq", "EqAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 2
        assert len(specs.unconditional_equalities) == 0
        assert all(c.bound.value == Decimal(0) for c in specs.unconditional_constraints)
        assert _find_constraint(
            specs.unconditional_constraints, "lidar", "observation_time", "camera", "observation_time"
        )
        assert _find_constraint(
            specs.unconditional_constraints, "camera", "observation_time", "lidar", "observation_time"
        )
        # Accessors are deduplicated
        assert specs.field_accessors == frozenset(
            {
                FieldAccessor("lidar", "observation_time"),
                FieldAccessor("camera", "observation_time"),
            }
        )

    def test_equality_constraint_non_increasing(self, fs_importer: FilesystemImporter) -> None:
        """require(a == b) without strictly_increasing produces EqualityConstraint."""
        source = f"""\
{_HEADER}
// Equality test non-increasing
aligner EqNonIncAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(lidar.observation_time == camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_eq_noninc", "EqNonIncAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 0
        assert len(specs.unconditional_equalities) == 1
        eq = specs.unconditional_equalities[0]
        assert isinstance(eq, EqualityConstraint)
        assert eq.left == FieldAccessor("lidar", "observation_time")
        assert eq.right == FieldAccessor("camera", "observation_time")

    def test_ordering_constraints(self, fs_importer: FilesystemImporter) -> None:
        """require(a <= b) and require(a >= b) each produce one zero-bound constraint."""
        source = f"""\
{_HEADER}
// Ordering test
aligner OrdAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(pose.tov));
    require(lidar.observation_time <= camera.observation_time);
    require(pose.tov >= lidar.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_ord", "OrdAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 2
        # From the LE constraint
        c_le = _find_constraint(
            specs.unconditional_constraints, "lidar", "observation_time", "camera", "observation_time"
        )
        assert c_le is not None
        assert c_le.bound.value == Decimal(0)
        # From the GE constraint (reversed)
        c_ge = _find_constraint(specs.unconditional_constraints, "lidar", "observation_time", "pose", "tov")
        assert c_ge is not None
        assert c_ge.bound.value == Decimal(0)

    def test_bounded_difference(self, fs_importer: FilesystemImporter) -> None:
        """require(|a - b| <= d) produces two constraints with bound d."""
        source = f"""\
{_HEADER}
// Bounded difference test
aligner BoundedAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_bounded", "BoundedAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 2
        for c in specs.unconditional_constraints:
            assert isinstance(c.bound, UnitValue)
            assert c.bound.value == Decimal(100)

    def test_composed_and_multi_field(self, fs_importer: FilesystemImporter) -> None:
        """Composed specs flatten via AND; distinct fields on same input are tracked."""
        source = f"""\
{_HEADER}
// Composed + multi-field
aligner ComposedAligner {{
    inputs {{
        // Lidar (has observation_time and tov)
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(pose.tov));
    require(lidar.observation_time <= pose.tov) and require(lidar.tov <= pose.tov);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_composed", "ComposedAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 2
        assert FieldAccessor("lidar", "observation_time") in specs.field_accessors
        assert FieldAccessor("lidar", "tov") in specs.field_accessors
        assert FieldAccessor("pose", "tov") in specs.field_accessors

    def test_mixed_constraints_and_objectives(self, fs_importer: FilesystemImporter) -> None:
        """Constraints and objectives coexist; minimize/maximize produce correct sense."""
        source = f"""\
{_HEADER}
// Mixed
aligner MixedAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(pose.tov));
    require(lidar.observation_time <= pose.tov);
    let x = 42;
    minimize(|lidar.observation_time - camera.observation_time|);
    maximize(pose.tov);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_mixed", "MixedAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 1
        assert len(specs.unconditional_objectives) == 2
        senses = {o.sense for o in specs.unconditional_objectives}
        assert senses == {ObjectiveSense.MINIMIZE, ObjectiveSense.MAXIMIZE}


class TestConditionalSpecs:
    """Tests for conditional spec handling (if has_candidates)."""

    def test_conditional_constraint(self, fs_importer: FilesystemImporter) -> None:
        """Conditional constraint is tagged; else require(true) produces nothing."""
        source = f"""\
{_HEADER}
// Conditional constraint
aligner CondAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Required camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    if has_candidates(radar)
        then require(|lidar.observation_time - radar.observation_time| <= 100ms)
        else require(true);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_cond", "CondAligner", fs_importer)

        # Unconditional: the lidar-camera constraint
        assert len(specs.unconditional_constraints) == 2
        # Conditional: the lidar-radar constraint
        assert "radar" in specs.conditional_constraints
        assert len(specs.conditional_constraints["radar"]) == 2
        # Conditional accessors included in global set
        assert FieldAccessor("radar", "observation_time") in specs.field_accessors

    def test_conditional_compound_with_objective(self, fs_importer: FilesystemImporter) -> None:
        """Compound spec + objective inside conditional; else-branch stored separately."""
        source = f"""\
{_HEADER}
// Conditional compound + objective
aligner CondCompoundAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    if has_candidates(radar)
        then require(|lidar.observation_time - radar.observation_time| <= 100ms) and
             minimize(|lidar.observation_time - radar.observation_time|)
        else minimize(|lidar.observation_time - camera.observation_time|);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_cond_compound", "CondCompoundAligner", fs_importer)

        # Then-branch: 2 constraints + 1 objective conditional on radar
        assert "radar" in specs.conditional_constraints
        assert len(specs.conditional_constraints["radar"]) == 2
        assert "radar" in specs.conditional_objectives
        assert len(specs.conditional_objectives["radar"]) == 1
        assert specs.conditional_objectives["radar"][0].sense is ObjectiveSense.MINIMIZE
        # Else-branch: 1 objective in else_objectives
        assert len(specs.unconditional_objectives) == 0
        assert "radar" in specs.else_objectives
        assert len(specs.else_objectives["radar"]) == 1
        assert specs.else_objectives["radar"][0].sense is ObjectiveSense.MINIMIZE


class TestExtractionErrors:
    """Tests for error conditions during spec extraction."""

    def test_require_false_error(self, fs_importer: FilesystemImporter) -> None:
        """require(false) raises SpecExtractionError (trivially unsatisfiable)."""
        source = f"""\
{_HEADER}
// require(false)
aligner FalseAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(false);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_false"), fs_importer)
        aligner_ir = module.inner_scope.lookup("FalseAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="trivially unsatisfiable"):
            extract_specs(aligner_ir)

    def test_unrecognized_pattern(self, fs_importer: FilesystemImporter) -> None:
        """require() with an unrecognized pattern raises SpecExtractionError."""
        source = f"""\
{_HEADER}
// Unrecognized pattern
aligner BadAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(lidar.observation_time != camera.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_bad"), fs_importer)
        aligner_ir = module.inner_scope.lookup("BadAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="Cannot normalize constraint"):
            extract_specs(aligner_ir)


class TestDirectedBoundedConstraints:
    """Tests for directed bounded difference constraints (a - b <= d, a <= b + d, etc.).

    Each test exercises multiple syntactic forms in a single aligner to reduce
    boilerplate.  Constraints use distinct bound values so they are
    distinguishable in assertions.
    """

    def test_positive_bound_forms(self, fs_importer: FilesystemImporter) -> None:
        """Syntactic forms that produce positive-bound constraints."""
        source = f"""\
{_HEADER}
// Positive-bound directed constraints
aligner PositiveBoundAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(pose.tov));
    require(lidar.observation_time - camera.observation_time <= 200ms);
    require(camera.observation_time <= pose.tov + 300ms);
    require(pose.tov >= lidar.observation_time - 400ms);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_pos_bound", "PositiveBoundAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 3

        # (a - b) <= d  ->  a - b <= d
        c1 = _find_constraint(
            specs.unconditional_constraints, "lidar", "observation_time", "camera", "observation_time"
        )
        assert c1 is not None
        assert isinstance(c1.bound, UnitValue)
        assert c1.bound.value == Decimal(200)

        # a <= (b + d)  ->  a - b <= d
        c2 = _find_constraint(specs.unconditional_constraints, "camera", "observation_time", "pose", "tov")
        assert c2 is not None
        assert isinstance(c2.bound, UnitValue)
        assert c2.bound.value == Decimal(300)

        # a >= (b - d)  via GE flip -> (b - d) <= a  ->  b - a <= d
        c3 = _find_constraint(specs.unconditional_constraints, "lidar", "observation_time", "pose", "tov")
        assert c3 is not None
        assert isinstance(c3.bound, UnitValue)
        assert c3.bound.value == Decimal(400)

    def test_negative_bound_forms(self, fs_importer: FilesystemImporter) -> None:
        """Syntactic forms that produce negative-bound constraints."""
        source = f"""\
{_HEADER}
// Negative-bound directed constraints
aligner NegativeBoundAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(pose.tov));
    require(lidar.observation_time - camera.observation_time >= 200ms);
    require(camera.observation_time <= pose.tov - 300ms);
    require(pose.tov >= lidar.observation_time + 400ms);
    require(500ms <= lidar.tov - camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_neg_bound", "NegativeBoundAligner", fs_importer)

        assert len(specs.unconditional_constraints) == 4

        # (a - b) >= d  via GE flip -> d <= (a - b)  ->  b - a <= -d
        c1 = _find_constraint(
            specs.unconditional_constraints, "camera", "observation_time", "lidar", "observation_time"
        )
        assert c1 is not None
        assert isinstance(c1.bound, UnitValue)
        assert c1.bound.value == Decimal(-200)

        # a <= (b - d)  ->  a - b <= -d
        c2 = _find_constraint(specs.unconditional_constraints, "camera", "observation_time", "pose", "tov")
        assert c2 is not None
        assert isinstance(c2.bound, UnitValue)
        assert c2.bound.value == Decimal(-300)

        # a >= (b + d)  via GE flip -> (b + d) <= a  ->  b - a <= -d
        c3 = _find_constraint(specs.unconditional_constraints, "lidar", "observation_time", "pose", "tov")
        assert c3 is not None
        assert isinstance(c3.bound, UnitValue)
        assert c3.bound.value == Decimal(-400)

        # d <= (a - b)  ->  b - a <= -d
        c4 = _find_constraint(specs.unconditional_constraints, "camera", "observation_time", "lidar", "tov")
        assert c4 is not None
        assert isinstance(c4.bound, UnitValue)
        assert c4.bound.value == Decimal(-500)

    def test_commuted_addition_rejected(self, fs_importer: FilesystemImporter) -> None:
        """Commuted addition (d + b) is rejected by DFL type-checking for SyncTime.

        DFL implements Add(SyncTime, Duration) but not Add(Duration, SyncTime).
        The commuted patterns in _try_directed_le exist for robustness with
        commutative types but are unreachable for timestamp fields.
        """
        source = f"""\
{_HEADER}
// Commuted addition
aligner CommutedAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(lidar.observation_time <= 200ms + camera.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_commuted"), fs_importer)
        aligner_ir = module.inner_scope.lookup("CommutedAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        with pytest.raises(TypeCheckError, match="No implementation of Add for"):
            type_check_aligner(aligner_ir, module.context)


class TestFieldAccessor:
    """Tests for FieldAccessor."""

    def test_base_input_name(self) -> None:
        """base_input_name strips batch wrappers to return the underlying name."""
        assert FieldAccessor("lidar", "observation_time").base_input_name == "lidar"
        fa_first = FieldAccessor(FirstInBatch("pose"), "tov")
        assert fa_first.base_input_name == "pose"
        assert fa_first.input_name == FirstInBatch("pose")
        fa_last = FieldAccessor(LastInBatch("pose"), "tov")
        assert fa_last.base_input_name == "pose"
        assert fa_last.input_name == LastInBatch("pose")


class TestFieldAssumptions:
    """Tests for assume(is_strictly_increasing/is_unique) extraction."""

    def test_assumptions_extracted(self, fs_importer: FilesystemImporter) -> None:
        """assume() calls populate field_assumptions on ExtractedSpecs."""
        source = f"""\
{_HEADER}
// Assumptions extraction
aligner AssumptionsAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_unique(camera.observation_time));
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_assumptions", "AssumptionsAligner", fs_importer)

        assert specs.field_assumptions == {
            FieldAccessor("lidar", "observation_time"): FieldProperty.STRICTLY_INCREASING,
            FieldAccessor("camera", "observation_time"): FieldProperty.UNIQUE,
        }

    def test_conflicting_assumptions_error(self, fs_importer: FilesystemImporter) -> None:
        """Declaring both is_strictly_increasing and is_unique on the same field raises error."""
        source = f"""\
{_HEADER}
// Conflicting assumptions
aligner ConflictAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_unique(lidar.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_conflict"), fs_importer)
        aligner_ir = module.inner_scope.lookup("ConflictAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="Conflicting assumptions"):
            extract_specs(aligner_ir)

    def test_duplicate_assumption_error(self, fs_importer: FilesystemImporter) -> None:
        """Declaring the same assumption twice on the same field raises error."""
        source = f"""\
{_HEADER}
// Duplicate assumption
aligner DuplicateAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_duplicate"), fs_importer)
        aligner_ir = module.inner_scope.lookup("DuplicateAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="Duplicate assumption"):
            extract_specs(aligner_ir)

    def test_unique_field_ordering_error(self, fs_importer: FilesystemImporter) -> None:
        """Ordering constraint on is_unique (not is_strictly_increasing) fields raises error."""
        source = f"""\
{_HEADER}
// Unique ordering error
aligner UniqueOrdAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_unique(lidar.observation_time));
    assume(is_unique(camera.observation_time));
    require(lidar.observation_time <= camera.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_unique_ord"), fs_importer)
        aligner_ir = module.inner_scope.lookup("UniqueOrdAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="not declared as strictly increasing"):
            extract_specs(aligner_ir)

    def test_assume_in_conditional_error(self, fs_importer: FilesystemImporter) -> None:
        """assume() inside a conditional branch raises SpecExtractionError."""
        source = f"""\
{_HEADER}
// Assume in conditional
aligner AssumeCondAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}

    if has_candidates(radar)
        then assume(is_strictly_increasing(radar.observation_time))
        else require(true);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_assume_cond"), fs_importer)
        aligner_ir = module.inner_scope.lookup("AssumeCondAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match=r"assume.*cannot appear inside a conditional"):
            extract_specs(aligner_ir)

    def test_ordering_without_assumption_error(self, fs_importer: FilesystemImporter) -> None:
        """Ordering constraint on non-strictly-increasing field raises SpecExtractionError."""
        source = f"""\
{_HEADER}
// Missing assumption
aligner MissingAssumptionAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(lidar.observation_time <= camera.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_missing_assume"), fs_importer)
        aligner_ir = module.inner_scope.lookup("MissingAssumptionAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="not declared as strictly increasing"):
            extract_specs(aligner_ir)

    def test_assume_bad_argument_error(self, fs_importer: FilesystemImporter) -> None:
        """assume() with a non-predicate argument raises SpecExtractionError."""
        source = f"""\
{_HEADER}
// Bad assume argument
aligner BadAssumeArgAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(lidar.observation_time <= lidar.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_bad_assume_arg"), fs_importer)
        aligner_ir = module.inner_scope.lookup("BadAssumeArgAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match=r"assume.*must be"):
            extract_specs(aligner_ir)


class TestNonDecreasingAssumption:
    """Tests for ``is_non_decreasing`` assumption handling."""

    def test_non_decreasing_stored(self, fs_importer: FilesystemImporter) -> None:
        """assume(is_non_decreasing(x.f)) stores FieldProperty.NON_DECREASING."""
        source = f"""\
{_HEADER}
// Non-decreasing assumption
aligner NonDecAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_nondec", "NonDecAligner", fs_importer)
        assert specs.field_assumptions == {
            FieldAccessor("lidar", "observation_time"): FieldProperty.NON_DECREASING,
        }

    @pytest.mark.parametrize(
        ("first", "second"),
        [
            pytest.param("is_non_decreasing", "is_unique", id="nondec_then_unique"),
            pytest.param("is_unique", "is_non_decreasing", id="unique_then_nondec"),
        ],
    )
    def test_canonicalize_nondec_plus_unique(self, fs_importer: FilesystemImporter, first: str, second: str) -> None:
        """is_non_decreasing + is_unique canonicalizes to STRICTLY_INCREASING (either order)."""
        source = f"""\
{_HEADER}
// Canonicalize
aligner CanonAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume({first}(lidar.observation_time));
    assume({second}(lidar.observation_time));
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, f"test_canon_{first}_{second}", "CanonAligner", fs_importer)
        assert specs.field_assumptions == {
            FieldAccessor("lidar", "observation_time"): FieldProperty.STRICTLY_INCREASING,
        }

    def test_duplicate_non_decreasing_error(self, fs_importer: FilesystemImporter) -> None:
        """Declaring is_non_decreasing twice on the same field is an error."""
        source = f"""\
{_HEADER}
// Duplicate non-decreasing
aligner DupNonDecAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
    assume(is_non_decreasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_dup_nondec"), fs_importer)
        aligner_ir = module.inner_scope.lookup("DupNonDecAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="Duplicate assumption"):
            extract_specs(aligner_ir)

    def test_nondec_then_strict_redundant_error(self, fs_importer: FilesystemImporter) -> None:
        """is_non_decreasing then is_strictly_increasing is a redundancy error."""
        source = f"""\
{_HEADER}
// ND then strict
aligner NdStrictAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_nd_strict"), fs_importer)
        aligner_ir = module.inner_scope.lookup("NdStrictAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match=r"already declared is_non_decreasing"):
            extract_specs(aligner_ir)

    def test_strict_then_nondec_redundant_error(self, fs_importer: FilesystemImporter) -> None:
        """is_strictly_increasing then is_non_decreasing is a redundancy error."""
        source = f"""\
{_HEADER}
// Strict then ND
aligner StrictNdAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_non_decreasing(lidar.observation_time));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_strict_nd"), fs_importer)
        aligner_ir = module.inner_scope.lookup("StrictNdAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match=r"already declared is_strictly_increasing"):
            extract_specs(aligner_ir)

    def test_ordering_constraints_accept_non_decreasing(self, fs_importer: FilesystemImporter) -> None:
        """Ordering constraints accept non-decreasing fields."""
        source = f"""\
{_HEADER}
// ND ordering
aligner NdOrdAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
    assume(is_non_decreasing(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(lidar.observation_time <= camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_nd_ord", "NdOrdAligner", fs_importer)
        # abs-bounded produces 2 diff constraints; bare LE produces 1 → 3 total.
        assert len(specs.unconditional_constraints) == 3

    def test_equality_constraint_non_decreasing(self, fs_importer: FilesystemImporter) -> None:
        """require(a == b) with both fields non-decreasing produces DifferenceConstraint pair."""
        source = f"""\
{_HEADER}
// ND equality
aligner NdEqAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
    assume(is_non_decreasing(camera.observation_time));
    require(lidar.observation_time == camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_nd_eq", "NdEqAligner", fs_importer)
        assert len(specs.unconditional_constraints) == 2
        assert len(specs.unconditional_equalities) == 0

    def test_mixed_nd_and_unique_error_ordering(self, fs_importer: FilesystemImporter) -> None:
        """Ordering constraint on is_unique field (not sorted) still errors."""
        source = f"""\
{_HEADER}
// ND + unique different fields, ordering on unique
aligner NdUniqOrdAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.observation_time));
    assume(is_unique(camera.observation_time));
    require(lidar.observation_time <= camera.observation_time);
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_nd_uniq_ord"), fs_importer)
        aligner_ir = module.inner_scope.lookup("NdUniqOrdAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)

        with pytest.raises(SpecExtractionError, match="not declared as strictly increasing or non-decreasing"):
            extract_specs(aligner_ir)


class TestBatchInputExtraction:
    """Tests for batch input handling in spec extraction."""

    def test_min_max_in_constraint(self, fs_importer: FilesystemImporter) -> None:
        """min(batch.field) and max(batch.field) produce FirstInBatch/LastInBatch accessors."""
        source = f"""\
{_HEADER}
// Batch min/max in constraints
aligner BatchConstraintAligner {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 5];
        }}
    }}

    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(min(lidar.observation_time) >= camera.observation_time);
    require(max(lidar.observation_time) <= camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_batch_minmax", "BatchConstraintAligner", fs_importer)

        user_constraints = [
            c
            for c in specs.unconditional_constraints
            if not isinstance(c.minuend.field_name, IndexInView)
            and not isinstance(c.subtrahend.field_name, IndexInView)
        ]
        assert len(user_constraints) == 2

        c_min = _find_constraint(
            tuple(user_constraints),
            "camera",
            "observation_time",
            FirstInBatch("lidar"),
            "observation_time",
        )
        assert c_min is not None
        assert c_min.bound.value == Decimal(0)

        c_max = _find_constraint(
            tuple(user_constraints),
            LastInBatch("lidar"),
            "observation_time",
            "camera",
            "observation_time",
        )
        assert c_max is not None
        assert c_max.bound.value == Decimal(0)

        assert FieldAccessor(FirstInBatch("lidar"), "observation_time") in specs.field_accessors
        assert FieldAccessor(LastInBatch("lidar"), "observation_time") in specs.field_accessors

        first_accessor = FieldAccessor(FirstInBatch("lidar"), "observation_time")
        last_accessor = FieldAccessor(LastInBatch("lidar"), "observation_time")
        assert specs.field_assumptions[first_accessor] is FieldProperty.STRICTLY_INCREASING
        assert specs.field_assumptions[last_accessor] is FieldProperty.STRICTLY_INCREASING

    def test_batch_type_errors(self, fs_importer: FilesystemImporter) -> None:
        """Bare batch.field and min(non_batch.field) both fail type checking.

        - ``batch.field`` yields a ``CollectionType`` which has no ``Ord`` impl.
        - ``min(scalar)`` explicitly requires a collection argument.
        """
        for source, aligner_name in [
            (
                f"""\
{_HEADER}
// Bare batch field
aligner BareBatchAligner {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch
        lidar: Tappy<LidarSweep> {{ max_msgs: 10; batch_size: [2, 5]; }}
    }}
    require(lidar.observation_time <= camera.observation_time);
}}
{_SCHEMAS}
""",
                "BareBatchAligner",
            ),
            (
                f"""\
{_HEADER}
// min on non-batch field
aligner MinNonBatchAligner {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(min(lidar.observation_time) <= camera.observation_time);
}}
{_SCHEMAS}
""",
                "MinNonBatchAligner",
            ),
        ]:
            module = compiler.compile_source_text(source, ModuleID(CLK_REPO, f"test_{aligner_name}"), fs_importer)
            aligner_ir = module.inner_scope.lookup(aligner_name)
            assert isinstance(aligner_ir, aligner.Aligner)
            with pytest.raises(TypeCheckError):
                type_check_aligner(aligner_ir, module.context)

    @pytest.mark.parametrize(
        ("lo", "hi"),
        [
            pytest.param(2, 5, id="lo2_hi5"),
            pytest.param(1, 4, id="lo1_hi4"),
        ],
    )
    def test_auto_generated_batch_constraints(self, fs_importer: FilesystemImporter, lo: int, hi: int) -> None:
        """Batch inputs produce the correct number and shape of auto-generated constraints."""
        source = f"""\
{_HEADER}
// Batch auto constraints lo={lo} hi={hi}
aligner AutoBatchAligner {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch
        lidar: Tappy<LidarSweep> {{
            max_msgs: 10;
            batch_size: [{lo}, {hi}];
        }}
    }}

    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(min(lidar.observation_time) >= camera.observation_time);
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, f"test_auto_batch_{lo}_{hi}", "AutoBatchAligner", fs_importer)

        auto_constraints = [
            c
            for c in specs.unconditional_constraints
            if isinstance(c.minuend.field_name, IndexInView) or isinstance(c.subtrahend.field_name, IndexInView)
        ]
        assert len(auto_constraints) == 2

        max_size = _find_constraint(
            tuple(auto_constraints),
            LastInBatch("lidar"),
            INDEX_IN_VIEW,
            FirstInBatch("lidar"),
            INDEX_IN_VIEW,
        )
        assert max_size is not None
        assert max_size.bound.value == Decimal(hi - 1)

        min_size_constraints = [
            c
            for c in auto_constraints
            if c.minuend.input_name == FirstInBatch("lidar") and c.subtrahend.input_name == LastInBatch("lidar")
        ]
        assert len(min_size_constraints) == 1
        assert min_size_constraints[0].bound.value == Decimal(1 - lo)

        assert FieldAccessor(FirstInBatch("lidar"), INDEX_IN_VIEW) in specs.field_accessors
        assert FieldAccessor(LastInBatch("lidar"), INDEX_IN_VIEW) in specs.field_accessors

    def test_min_max_in_objective(self, fs_importer: FilesystemImporter) -> None:
        """minimize(min(batch.field)) and maximize(max(batch.field)) extract correctly."""
        source = f"""\
{_HEADER}
// Batch objectives
aligner BatchObjectiveAligner {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 5];
        }}
    }}

    minimize(min(lidar.observation_time));
    maximize(max(lidar.observation_time));
}}

{_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_batch_obj", "BatchObjectiveAligner", fs_importer)

        assert len(specs.unconditional_objectives) == 2
        min_obj = next(o for o in specs.unconditional_objectives if o.sense == ObjectiveSense.MINIMIZE)
        max_obj = next(o for o in specs.unconditional_objectives if o.sense == ObjectiveSense.MAXIMIZE)
        assert min_obj.sense == ObjectiveSense.MINIMIZE
        assert max_obj.sense == ObjectiveSense.MAXIMIZE

    def test_assume_min_batch_field_rejected(self, fs_importer: FilesystemImporter) -> None:
        """``assume(is_strictly_increasing(min(batch.field)))`` is rejected."""
        source = f"""\
{_HEADER}
// Reject min in assume
aligner RejectMinAssumeAligner {{
    inputs {{
        // Lidar batch
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 5];
        }}
    }}

    assume(is_strictly_increasing(min(lidar.observation_time)));
}}

{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_reject_min_assume"), fs_importer)
        aligner_ir = module.inner_scope.lookup("RejectMinAssumeAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)
        with pytest.raises(SpecExtractionError, match=r"not min\(\).*Assumptions apply to the entire input stream"):
            extract_specs(aligner_ir)


_NESTED_SCHEMAS = """\
// Inner data with a time field
schema InnerData {
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields {
        // Vehicle frame time of validity
        #0 vehicle_frame_tov: SyncTime;
    }
}
// Outer message containing a sub-schema
schema OuterMessage {
    uuid: bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb;
    fields {
        // Nested inner data
        #0 inner: InnerData;
    }
}
// Simple timestamped message
schema SimpleTimestamped {
    uuid: cccccccc-cccc-cccc-cccc-cccccccccccc;
    fields {
        // Time of validity
        #0 tov: SyncTime;
    }
}
"""


class TestNestedFieldAccess:
    """Tests for nested sub-schema field access (e.g. input.sub.field)."""

    def test_nested_field_assume_require_maximize(self, fs_importer: FilesystemImporter) -> None:
        """Nested field access works in assume, require, and maximize statements."""
        source = f"""\
{_HEADER}
// Nested field access test
aligner NestedAligner {{
    inputs {{
        // Simple input
        simple: Tappy<SimpleTimestamped>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Outer input with nested field
        outer: Tappy<OuterMessage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(simple.tov));
    assume(is_strictly_increasing(outer.inner.vehicle_frame_tov));

    require(simple.tov >= outer.inner.vehicle_frame_tov);

    maximize(simple.tov);
    maximize(outer.inner.vehicle_frame_tov);
}}

{_NESTED_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_nested", "NestedAligner", fs_importer)

        nested_accessor = FieldAccessor(input_name="outer", field_name="inner.vehicle_frame_tov")
        assert nested_accessor in specs.field_assumptions
        assert specs.field_assumptions[nested_accessor] == FieldProperty.STRICTLY_INCREASING

        # GE normalizes to LE: outer.inner.vehicle_frame_tov - simple.tov <= 0
        c = _find_constraint(
            specs.unconditional_constraints,
            "outer",
            "inner.vehicle_frame_tov",
            "simple",
            "tov",
        )
        assert c is not None

        assert len(specs.unconditional_objectives) == 2

    def test_nested_field_batch_min_max(self, fs_importer: FilesystemImporter) -> None:
        """min/max on nested batch fields produce FirstInBatch/LastInBatch with dotted field_name."""
        source = f"""\
{_HEADER}
// Batch nested field test
aligner BatchNestedAligner {{
    inputs {{
        // Simple
        simple: Tappy<SimpleTimestamped>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Outer batch with nested field
        outer: Tappy<OuterMessage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 4];
        }}
    }}

    assume(is_strictly_increasing(simple.tov));
    assume(is_strictly_increasing(outer.inner.vehicle_frame_tov));
    require(min(outer.inner.vehicle_frame_tov) >= simple.tov);
    require(max(outer.inner.vehicle_frame_tov) <= simple.tov);
}}

{_NESTED_SCHEMAS}
"""
        specs = _compile_type_check_extract(source, "test_batch_nested", "BatchNestedAligner", fs_importer)

        first_acc = FieldAccessor(FirstInBatch("outer"), "inner.vehicle_frame_tov")
        last_acc = FieldAccessor(LastInBatch("outer"), "inner.vehicle_frame_tov")
        assert first_acc in specs.field_accessors
        assert last_acc in specs.field_accessors
        assert specs.field_assumptions[first_acc] is FieldProperty.STRICTLY_INCREASING
        assert specs.field_assumptions[last_acc] is FieldProperty.STRICTLY_INCREASING

        user_constraints = [
            c
            for c in specs.unconditional_constraints
            if not isinstance(c.minuend.field_name, IndexInView)
            and not isinstance(c.subtrahend.field_name, IndexInView)
        ]
        assert _find_constraint(
            tuple(user_constraints), "simple", "tov", FirstInBatch("outer"), "inner.vehicle_frame_tov"
        )
        assert _find_constraint(
            tuple(user_constraints), LastInBatch("outer"), "inner.vehicle_frame_tov", "simple", "tov"
        )
