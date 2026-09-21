# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner constraint direction analysis."""

from __future__ import annotations

from decimal import Decimal

import pytest
from clockwork.dsl.aligner.direction_analysis import (
    Direction,
    DirectionAnalysis,
    compute_direction_analysis,
)
from clockwork.dsl.aligner.extract_specs import (
    ExtractedSpecs,
    extract_specs,
)
from clockwork.dsl.aligner.join_plan import JoinPlan, compute_join_plan
from clockwork.dsl.aligner.stn import analyze_stn
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _compile_and_analyze(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
    *,
    include_conditional: str | None = None,
) -> tuple[JoinPlan, tuple[DirectionAnalysis, ...], ExtractedSpecs]:
    """End-to-end: compile -> type check -> extract -> STN -> plan -> direction."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    type_check_aligner(aligner_ir, module.context)
    specs = extract_specs(aligner_ir)

    constraints = list(specs.unconditional_constraints)
    objectives = list(specs.unconditional_objectives)
    if include_conditional is not None:
        constraints.extend(specs.conditional_constraints.get(include_conditional, ()))
        objectives.extend(specs.conditional_objectives.get(include_conditional, ()))

    equalities = list(specs.unconditional_equalities)
    if include_conditional is not None:
        equalities.extend(specs.conditional_equalities.get(include_conditional, ()))

    stn = analyze_stn(constraints)
    active_inputs = frozenset(name for name in aligner_ir.resolve().inputs)

    plan = compute_join_plan(
        inputs=tuple(sorted(active_inputs)),
        objectives=tuple(objectives),
        active_inputs=active_inputs,
        stn=stn,
        aligner_node=aligner_ir,
        equalities=tuple(equalities),
        field_assumptions=specs.field_assumptions,
    )
    directions = compute_direction_analysis(plan, specs, {lev.input: i for i, lev in enumerate(plan.levels)})
    return plan, directions, specs


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
// Local map
schema LocalMap {
    uuid: 55555555-5555-5555-5555-555555555555;
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


def _level_index(plan: JoinPlan, representative: str) -> int:
    """Find the index of a level by input name."""
    for idx, level in enumerate(plan.levels):
        if level.input == representative:
            return idx
    msg = f"No level with input '{representative}'"
    raise ValueError(msg)


def _dir_for(
    plan: JoinPlan,
    directions: tuple[DirectionAnalysis, ...],
    representative: str,
) -> DirectionAnalysis:
    """Get the DirectionAnalysis for a level by representative name."""
    return directions[_level_index(plan, representative)]


class TestOneSidedConstraint:
    """One-sided constraint: single direction preference, last level UNCONSTRAINED."""

    def test_ordering_constraint(self, fs_importer: FilesystemImporter) -> None:
        """require(X <= Y) produces ALL_MAX for the pivot, UNCONSTRAINED for last level."""
        source = f"""\
{_HEADER}
// One-sided ordering
aligner OneSided {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(lidar.observation_time <= camera.observation_time);
    maximize(camera.observation_time);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(source, "one_sided", "OneSided", fs_importer)
        assert len(directions) == len(plan.levels)

        # Pivot (camera, LAST_IN_RANGE): lidar.obs - camera.obs <= 0 -> PREFER_MAX.
        camera_dir = _dir_for(plan, directions, "camera")
        assert camera_dir.required_constraint_direction == Direction.ALL_MAX
        assert camera_dir.objective_direction == Direction.ALL_MAX
        assert len(camera_dir.left_cutoffs) == 1
        assert camera_dir.left_cutoffs[0].bound.value == Decimal(0)
        assert camera_dir.left_cutoffs[0].current_field == "observation_time"
        assert camera_dir.right_cutoffs == ()
        assert camera_dir.optional_right_cutoffs == ()
        assert camera_dir.optional_left_cutoffs == ()

        # Last level (lidar): no later levels -> UNCONSTRAINED.
        lidar_dir = _dir_for(plan, directions, "lidar")
        assert lidar_dir.required_constraint_direction == Direction.UNCONSTRAINED
        assert lidar_dir.objective_direction == Direction.UNCONSTRAINED


class TestSymmetricConstraint:
    """Symmetric (|X - Y| <= w) produces MIXED with paired cutoffs."""

    def test_symmetric_produces_mixed_with_bounds(self, fs_importer: FilesystemImporter) -> None:
        """Absolute difference generates two constraints -> MIXED + cutoffs with bound."""
        source = f"""\
{_HEADER}
// Symmetric constraint
aligner Symmetric {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 200ms);
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        _plan, directions, _ = _compile_and_analyze(source, "sym", "Symmetric", fs_importer)
        pivot_dir = directions[0]
        assert pivot_dir.required_constraint_direction == Direction.MIXED
        # 200ms = 0.2s canonical. Both right and left cutoffs present.
        assert len(pivot_dir.right_cutoffs) == 1
        assert len(pivot_dir.left_cutoffs) == 1
        assert pivot_dir.right_cutoffs[0].bound.value == Decimal("0.2")
        assert pivot_dir.right_cutoffs[0].current_field == "observation_time"
        assert pivot_dir.left_cutoffs[0].bound.value == Decimal("0.2")
        assert pivot_dir.left_cutoffs[0].current_field == "observation_time"
        assert pivot_dir.optional_right_cutoffs == ()
        assert pivot_dir.optional_left_cutoffs == ()


class TestConflictingLaterConstraints:
    """Later constraints pulling opposite directions produce MIXED."""

    def test_conflicting_directions(self, fs_importer: FilesystemImporter) -> None:
        """Two required later levels with opposed constraints -> MIXED + both cutoffs."""
        source = f"""\
{_HEADER}
// Conflicting directions
aligner Conflict {{
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
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    require(vehicle_pose.tov <= camera.observation_time);
    maximize(vehicle_pose.tov);
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(source, "conflict", "Conflict", fs_importer)
        # Pose is pivot: lidar pulls MAX, camera pulls MIN -> MIXED.
        pose_dir = _dir_for(plan, directions, "vehicle_pose")
        assert pose_dir.required_constraint_direction == Direction.MIXED
        assert len(pose_dir.right_cutoffs) == 1
        assert len(pose_dir.left_cutoffs) == 1
        assert pose_dir.optional_right_cutoffs == ()
        assert pose_dir.optional_left_cutoffs == ()


class TestOptionalInputDirection:
    """Optional later-level inputs: separate bucket, no cutoffs."""

    def test_optional_separated_and_has_optional_cutoffs(self, fs_importer: FilesystemImporter) -> None:
        """Auto-drop optional: constraints produce optional cutoffs."""
        source = f"""\
{_HEADER}
// Optional direction
aligner OptDir {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar (optional)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    require(lidar.observation_time <= radar.observation_time);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(
            source, "opt_dir", "OptDir", fs_importer, include_conditional="radar"
        )
        lidar_dir = _dir_for(plan, directions, "lidar")
        assert lidar_dir.required_constraint_direction == Direction.UNCONSTRAINED
        assert lidar_dir.optional_constraint_direction == Direction.ALL_MIN
        # No required cutoffs (radar is optional).
        assert lidar_dir.right_cutoffs == ()
        assert lidar_dir.left_cutoffs == ()
        # Auto-drop optional produces optional cutoffs.
        assert len(lidar_dir.optional_right_cutoffs) == 1
        assert lidar_dir.optional_right_cutoffs[0].bound.value == Decimal(0)
        assert lidar_dir.optional_right_cutoffs[0].current_field == "observation_time"
        assert lidar_dir.optional_left_cutoffs == ()


class TestSensorFusion:
    """Full sensor fusion aligner: multi-level, multiple constraint types, cutoffs."""

    def test_sensor_fusion_directions(self, fs_importer: FilesystemImporter) -> None:
        """Canonical sensor fusion: three required inputs, ordering + symmetric constraints."""
        source = f"""\
{_HEADER}
// Sensor fusion
aligner SensorFusion {{
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
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(lidar.observation_time <= vehicle_pose.tov);
    require(camera.observation_time <= vehicle_pose.tov);
    maximize(vehicle_pose.tov);
    minimize(|camera.observation_time - lidar.observation_time|);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(source, "sf", "SensorFusion", fs_importer)
        assert len(directions) == len(plan.levels)

        # Level 0: vehicle_pose (LAST_IN_RANGE pivot).
        # Both lidar and camera are later and constrained X <= pose -> ALL_MAX.
        pose_dir = _dir_for(plan, directions, "vehicle_pose")
        assert pose_dir.required_constraint_direction == Direction.ALL_MAX
        assert pose_dir.objective_direction == Direction.ALL_MAX
        assert len(pose_dir.left_cutoffs) == 2
        assert all(c.bound.value == Decimal(0) for c in pose_dir.left_cutoffs)
        assert all(c.current_field == "tov" for c in pose_dir.left_cutoffs)
        assert pose_dir.right_cutoffs == ()
        assert pose_dir.optional_right_cutoffs == ()
        assert pose_dir.optional_left_cutoffs == ()

        # Level 1: camera (ENUMERATE) — symmetric constraint -> MIXED.
        camera_dir = _dir_for(plan, directions, "camera")
        assert camera_dir.required_constraint_direction == Direction.MIXED

        # Level 2: lidar (NEAREST) — last level -> UNCONSTRAINED constraints.
        lidar_dir = _dir_for(plan, directions, "lidar")
        assert lidar_dir.required_constraint_direction == Direction.UNCONSTRAINED
        assert lidar_dir.objective_direction == Direction.MIXED  # NEAREST


class TestConditionalConstraint:
    """has_candidates constraints: direction contribution and optional cutoffs."""

    def test_conditional_direction_and_cutoff_exclusion(self, fs_importer: FilesystemImporter) -> None:
        """Conditional then-branch constraint against required later level: direction, no cutoffs."""
        source = f"""\
{_HEADER}
// Conditional constraint
aligner Conditional {{
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
        // Radar (optional)
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
    require(lidar.observation_time <= camera.observation_time);
    if has_candidates(radar)
        then require(camera.observation_time - lidar.observation_time <= 200ms)
        else require(true);
    maximize(camera.observation_time);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(source, "cond", "Conditional", fs_importer)
        # Camera pivot. Lidar at later level (required).
        # Unconditional: lidar.obs - camera.obs <= 0 -> PREFER_MAX
        # Conditional then: camera.obs - lidar.obs <= 0.2 -> PREFER_MIN
        # Combined: MIXED.
        camera_dir = _dir_for(plan, directions, "camera")
        assert camera_dir.required_constraint_direction == Direction.MIXED
        # Only unconditional constraint produces required cutoffs.
        assert len(camera_dir.left_cutoffs) == 1
        assert camera_dir.right_cutoffs == ()
        # No optional cutoffs (lidar is required, not optional).
        assert camera_dir.optional_right_cutoffs == ()
        assert camera_dir.optional_left_cutoffs == ()

    def test_has_candidates_optional_cutoffs(self, fs_importer: FilesystemImporter) -> None:
        """Then-branch constraint against optional later level produces optional cutoffs."""
        source = f"""\
{_HEADER}
// has_candidates optional cutoffs
aligner HCOptCutoff {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar (optional with has_candidates)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    if has_candidates(radar)
        then require(|lidar.observation_time - radar.observation_time| <= 150ms)
        else require(true);
}}
{_SCHEMAS}
"""
        plan, directions, _ = _compile_and_analyze(source, "hc_opt", "HCOptCutoff", fs_importer)
        lidar_dir = _dir_for(plan, directions, "lidar")
        # No required cutoffs (radar is optional).
        assert lidar_dir.right_cutoffs == ()
        assert lidar_dir.left_cutoffs == ()
        # Then-branch constraint produces optional cutoffs.
        assert len(lidar_dir.optional_right_cutoffs) == 1
        assert len(lidar_dir.optional_left_cutoffs) == 1
        assert lidar_dir.optional_right_cutoffs[0].bound.value == Decimal("0.15")
        assert lidar_dir.optional_left_cutoffs[0].bound.value == Decimal("0.15")
        assert lidar_dir.optional_right_cutoffs[0].current_field == "observation_time"
        assert lidar_dir.optional_left_cutoffs[0].current_field == "observation_time"
