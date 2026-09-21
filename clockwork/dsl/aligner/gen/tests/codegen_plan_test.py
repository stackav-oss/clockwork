# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for codegen plan computation."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest
from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    EqualityConstraint,
    FirstInBatch,
    LastInBatch,
    base_input_name,
)
from clockwork.dsl.aligner.pipeline import analyze_aligner, compute_codegen_plan
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from clockwork.dsl.aligner.gen.codegen_plan import CodegenLevel, CodegenPlan, Constraint


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


_HEADER = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
"""

_SCHEMAS = """\
// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
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
// Sensor with two time fields
schema DualTimeSensor {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
        // Processing timestamp
        #1 processing_time: SyncTime;
    }
}
"""


def _compile_and_plan(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> CodegenPlan:
    """End-to-end: compile -> type-check -> pipeline -> codegen plan."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    resolved = aligner_ir.resolved
    assert resolved is not None
    type_check_aligner(aligner_ir, module.context)
    analysis = analyze_aligner(aligner_ir)
    return compute_codegen_plan(analysis, resolved)


class TestCodegenPlan:
    """Comprehensive test exercising most codegen plan features."""

    _SOURCE = f"""\
{_HEADER}
// Comprehensive test aligner
aligner Comprehensive {{
    inputs {{
        // Sensor (reuse)
        sensor: Tappy<DualTimeSensor>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar (optional, has_candidates)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 75ms;
        }}
        // Pose (optional, auto-drop)
        pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}
    assume(is_strictly_increasing(sensor.observation_time));
    assume(is_strictly_increasing(sensor.processing_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(pose.tov));

    require(|sensor.observation_time - camera.observation_time| <= 500ms);
    require(|sensor.observation_time - sensor.processing_time| <= 50ms);
    require(|sensor.observation_time - pose.tov| <= 200ms);

    if has_candidates(radar)
        then require(|sensor.observation_time - radar.observation_time| <= 150ms)
        else require(|sensor.observation_time - camera.observation_time| <= 100ms);

    minimize(|sensor.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""

    @pytest.fixture(scope="class")
    def plan(self, fs_importer: FilesystemImporter) -> CodegenPlan:
        """Build codegen plan for the comprehensive aligner."""
        return _compile_and_plan(self._SOURCE, "cg_comprehensive", "Comprehensive", fs_importer)

    def test_structure_and_metadata(self, plan: CodegenPlan) -> None:
        """Verify level count, input names, optionals, timeouts, reuse, direction, field property."""
        assert len(plan.levels) == 4
        assert [lev.level_index for lev in plan.levels] == [0, 1, 2, 3]
        assert plan.partition_plan is not None

        assert plan.input_names == ("sensor", "camera", "radar", "pose")
        assert plan.optional_input_names == frozenset({"radar", "pose"})
        assert plan.optional_timeouts == {"radar": 75_000_000, "pose": 50_000_000}

        for lev in plan.levels:
            assert lev.direction is not None

        by_input = {base_input_name(lev.join_level.input): lev for lev in plan.levels}
        assert by_input["sensor"].reuse is True
        assert by_input["camera"].reuse is False
        assert by_input["radar"].reuse is False
        assert by_input["pose"].reuse is False

    def test_constraint_assignment(self, plan: CodegenPlan) -> None:
        """Verify all constraints are assigned and classified correctly."""
        specs = plan.specs

        spec_total = (
            len(specs.unconditional_constraints)
            + len(specs.unconditional_equalities)
            + sum(len(v) for v in specs.conditional_constraints.values())
            + sum(len(v) for v in specs.conditional_equalities.values())
            + sum(len(v) for v in specs.else_constraints.values())
            + sum(len(v) for v in specs.else_equalities.values())
        )

        total_uncond = sum(len(lev.unconditional_checks) for lev in plan.levels)
        total_wp = sum(sum(len(b.when_present) for b in lev.conditional_checks.values()) for lev in plan.levels)
        total_wa = sum(sum(len(b.when_absent) for b in lev.conditional_checks.values()) for lev in plan.levels)
        assigned_total = total_uncond + total_wp + total_wa

        assert assigned_total == spec_total

        # Outermost level: nothing bound yet, so only same-input
        # checks (if the outermost input has them).
        first = plan.levels[0]
        first_input = base_input_name(first.join_level.input)
        for c in first.unconditional_checks:
            # Any unconditional check on the first level must be same-input.
            a, b = _constraint_inputs(c)
            assert a == first_input, f"First level check has unexpected input: {a}"
            assert b == first_input, f"First level check has unexpected input: {b}"

        # has_candidates(radar): some level has conditional_checks with
        # non-empty when_present AND when_absent for "radar".
        radar_wp: list[Constraint] = []
        radar_wa: list[Constraint] = []
        for lev in plan.levels:
            if "radar" in lev.conditional_checks:
                branch = lev.conditional_checks["radar"]
                radar_wp.extend(branch.when_present)
                radar_wa.extend(branch.when_absent)
        assert len(radar_wp) > 0, "has_candidates then-branch should produce when_present"
        assert len(radar_wa) > 0, "Non-trivial else-branch should produce when_absent"

        # Same-input constraint (sensor.ot - sensor.pt) should be
        # in unconditional_checks on sensor's level.
        sensor_level = next(lev for lev in plan.levels if base_input_name(lev.join_level.input) == "sensor")
        same_input_checks = [c for c in sensor_level.unconditional_checks if _are_same_input(c, "sensor")]
        assert len(same_input_checks) > 0, "Same-input constraint should be unconditional on sensor level"

        # Initial windows: inner levels (index > 0) with constraints against
        # outer levels should have maximally permissive windows populated.
        total_windows = sum(len(lev.initial_windows) for lev in plan.levels)
        assert total_windows > 0, "Some inner level should have initial windows"
        # Outermost level has no windows (nothing bound before it).
        assert len(plan.levels[0].initial_windows) == 0


class TestBatchInputs:
    """Batch inputs produce separate levels for FirstInBatch and LastInBatch."""

    _SOURCE = f"""\
{_HEADER}
// Batch input test
aligner BatchTest {{
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

    def test_batch_levels_and_constraints(self, fs_importer: FilesystemImporter) -> None:
        """Batch produces 3 levels (camera + FirstInBatch + LastInBatch).

        All constraints are accounted for across levels.
        """
        plan = _compile_and_plan(self._SOURCE, "cg_batch", "BatchTest", fs_importer)
        assert len(plan.levels) == 3

        level_inputs = {lev.join_level.input for lev in plan.levels}
        assert FirstInBatch("lidar") in level_inputs
        assert LastInBatch("lidar") in level_inputs
        assert "camera" in level_inputs

        spec_total = len(plan.specs.unconditional_constraints) + len(plan.specs.unconditional_equalities)
        assigned_total = sum(len(lev.unconditional_checks) for lev in plan.levels)
        assert assigned_total == spec_total

        non_first = [lev for lev in plan.levels if lev.level_index > 0]
        total_inter = sum(len(lev.unconditional_checks) for lev in non_first)
        assert total_inter > 0


def _constraint_inputs(c: Constraint) -> tuple[str, str]:
    """Extract base input names from a constraint."""
    match c:
        case DifferenceConstraint(minuend=a, subtrahend=b):
            return (base_input_name(a.input_name), base_input_name(b.input_name))
        case EqualityConstraint(left=a, right=b):
            return (base_input_name(a.input_name), base_input_name(b.input_name))


def _are_same_input(c: Constraint, input_name: str) -> bool:
    """Check if both endpoints of a constraint are on the same named input."""
    a, b = _constraint_inputs(c)
    return a == input_name and b == input_name


class TestNonDecreasingMatchesStrict:
    """Semantic codegen-plan equivalence for ND vs strict on the same aligner.

    This is the invariant that makes ``is_non_decreasing`` a no-op at
    codegen today: every sorted-order decision point is blind to the
    difference, so the structural shape of the emitted plan (and
    therefore the generated C++) is the same.  Source-expr spans and
    literal shapes differ (the source texts are different), so we
    compare only the semantic attributes that drive codegen.
    """

    @staticmethod
    def _level_shape(lev: CodegenLevel) -> object:
        """Semantic shape of a CodegenLevel, excluding source spans.

        Captures everything that determines generated C++ while ignoring
        AST node identities (``objective_term``, ``source_expr``) whose
        spans reflect literal source positions.
        """
        jl = lev.join_level
        uncond = tuple(
            (c.minuend, c.subtrahend, type(c.bound).__name__, getattr(c.bound, "value", None))
            for c in lev.unconditional_checks
            if isinstance(c, DifferenceConstraint)
        )
        uncond_eq = tuple((c.left, c.right) for c in lev.unconditional_checks if isinstance(c, EqualityConstraint))
        return (
            lev.level_index,
            jl.input,
            jl.search_type,
            jl.depends_on,
            jl.is_optional,
            tuple((w.reference_accessor, w.target_accessor, w.lo, w.hi) for w in jl.windows),
            tuple((e.target_accessor, e.reference_accessor, e.is_unique) for e in jl.equality_checks),
            lev.reuse,
            lev.direction.required_constraint_direction,
            lev.direction.optional_constraint_direction,
            lev.direction.objective_direction,
            tuple((c.later_accessor, c.current_field, c.bound) for c in lev.direction.right_cutoffs),
            tuple((c.later_accessor, c.current_field, c.bound) for c in lev.direction.left_cutoffs),
            uncond,
            uncond_eq,
            lev.has_downstream_optionals,
        )

    def _assert_shapes_equal(self, strict_plan: CodegenPlan, nd_plan: CodegenPlan) -> None:
        strict_shape = tuple(self._level_shape(lev) for lev in strict_plan.levels)
        nd_shape = tuple(self._level_shape(lev) for lev in nd_plan.levels)
        assert strict_shape == nd_shape

    def test_bounded_difference_levels_match(self, fs_importer: FilesystemImporter) -> None:
        """|a - b| <= d produces identical level structure for strict and ND variants."""
        strict_source = f"""\
{_HEADER}
// bounded
aligner Pair {{
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
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        nd_source = f"""\
{_HEADER}
// bounded
aligner Pair {{
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
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        strict_plan = _compile_and_plan(strict_source, "cg_nd_strict", "Pair", fs_importer)
        nd_plan = _compile_and_plan(nd_source, "cg_nd_nd", "Pair", fs_importer)
        self._assert_shapes_equal(strict_plan, nd_plan)

    def test_equality_on_sorted_target_levels_match(self, fs_importer: FilesystemImporter) -> None:
        """require(a == b) on a sorted target produces identical level structure for strict and ND."""
        strict_source = f"""\
{_HEADER}
// equality
aligner EqPair {{
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
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        nd_source = f"""\
{_HEADER}
// equality
aligner EqPair {{
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
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        strict_plan = _compile_and_plan(strict_source, "cg_nd_eq_strict", "EqPair", fs_importer)
        nd_plan = _compile_and_plan(nd_source, "cg_nd_eq_nd", "EqPair", fs_importer)
        self._assert_shapes_equal(strict_plan, nd_plan)
        # Extra: confirm the sorted target produced EXACT_MATCH in both.
        by_input_strict = {lev.join_level.input: lev for lev in strict_plan.levels}
        by_input_nd = {lev.join_level.input: lev for lev in nd_plan.levels}
        assert by_input_strict["lidar"].join_level.search_type.name == "EXACT_MATCH"
        assert by_input_nd["lidar"].join_level.search_type.name == "EXACT_MATCH"

    def test_nd_plus_unique_matches_strict(self, fs_importer: FilesystemImporter) -> None:
        """`is_non_decreasing` + `is_unique` canonicalizes to strict and yields the same plan shape."""
        strict_source = f"""\
{_HEADER}
// canon
aligner CanonPair {{
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
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        canon_source = f"""\
{_HEADER}
// canon
aligner CanonPair {{
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
    assume(is_unique(lidar.observation_time));
    assume(is_non_decreasing(camera.observation_time));
    assume(is_unique(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        strict_plan = _compile_and_plan(strict_source, "cg_canon_strict", "CanonPair", fs_importer)
        canon_plan = _compile_and_plan(canon_source, "cg_canon_nd", "CanonPair", fs_importer)
        self._assert_shapes_equal(strict_plan, canon_plan)
