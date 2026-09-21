# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner greedy join plan computation.

Tests exercise :func:`compute_join_plan` via end-to-end parsing->IR->analysis
pipelines.  The greedy algorithm picks the best-scoring candidate at each
level, calling :func:`classify_input` with the current bound set.
"""

from __future__ import annotations

from decimal import Decimal

import pytest
from clockwork.dsl.aligner.extract_specs import extract_specs
from clockwork.dsl.aligner.join_plan import (
    JoinPlan,
    JoinPlanError,
    compute_join_plan,
)
from clockwork.dsl.aligner.objective_analysis import (
    SearchType,
)
from clockwork.dsl.aligner.stn import (
    analyze_stn,
)
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _assert_plan_invariants(plan: JoinPlan) -> None:
    """Check structural invariants that must hold for every valid plan."""
    # Every input appears exactly once.
    inputs = [lev.input for lev in plan.levels]
    assert len(inputs) == len(set(inputs))


def _compile_and_plan(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
    *,
    include_conditional: str | None = None,
) -> JoinPlan:
    """End-to-end: compile -> type check -> extract -> STN -> plan."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    type_check_aligner(aligner_ir, module.context)
    specs = extract_specs(aligner_ir)

    constraints = list(specs.unconditional_constraints)
    equalities = list(specs.unconditional_equalities)
    objectives = list(specs.unconditional_objectives)
    if include_conditional is not None:
        constraints.extend(specs.conditional_constraints.get(include_conditional, ()))
        equalities.extend(specs.conditional_equalities.get(include_conditional, ()))
        objectives.extend(specs.conditional_objectives.get(include_conditional, ()))

    stn = analyze_stn(constraints)

    # Compute active inputs from the resolved aligner.
    resolved = aligner_ir.resolve()
    all_inputs = tuple(resolved.inputs.keys())
    active_inputs = frozenset(all_inputs)

    plan = compute_join_plan(
        inputs=all_inputs,
        objectives=tuple(objectives),
        active_inputs=active_inputs,
        stn=stn,
        aligner_node=aligner_ir,
        equalities=tuple(equalities),
        field_assumptions=specs.field_assumptions,
    )
    _assert_plan_invariants(plan)
    return plan


# Shared schema boilerplate
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


class TestGreedyPivotSelection:
    """The greedy algorithm selects the best-scoring candidate as pivot."""

    def test_monotone_objective_wins_pivot(self, fs_importer: FilesystemImporter) -> None:
        """maximize(vp.tov) is LAST_IN_RANGE at step 0 (rank 3), beating abs-diff ENUMERATE (rank 1)."""
        source = f"""\
{_HEADER}
// Monotone wins pivot
aligner MonotonePivot {{
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
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(lidar.observation_time <= vehicle_pose.tov);
    require(camera.observation_time <= vehicle_pose.tov);
    minimize(|camera.observation_time - lidar.observation_time|);
    maximize(vehicle_pose.tov);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "mono_pivot", "MonotonePivot", fs_importer)

        # VP wins pivot at step 0 (LAST_IN_RANGE rank 3 beats ENUMERATE rank 1).
        assert plan.levels[0].input == "vehicle_pose"
        assert plan.levels[0].search_type == SearchType.LAST_IN_RANGE

        # camera is ENUMERATE (lidar still unbound), then lidar is NEAREST.
        reps = [lev.input for lev in plan.levels]
        assert reps.index("camera") < reps.index("lidar")

        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert level_by_rep["lidar"].search_type == SearchType.NEAREST


class TestGreedyReclassification:
    """The key feature: non-separable objectives become separable as inputs are bound."""

    def test_enumerate_becomes_nearest(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|c1 - c2|): c1 is ENUMERATE, then c2 becomes NEAREST once c1 is bound."""
        source = f"""\
{_HEADER}
// Non-separable reclassification
aligner Reclass {{
    inputs {{
        // Camera 1
        camera1: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera 2
        camera2: Tappy<CameraImage>
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
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(camera1.observation_time <= vehicle_pose.tov);
    require(camera2.observation_time <= vehicle_pose.tov);
    minimize(|camera1.observation_time - camera2.observation_time|);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "reclass", "Reclass", fs_importer)

        # VP wins pivot (ANY_MATCH rank 2). Then c1 ENUMERATE, c2 NEAREST.
        level_by_rep = {lev.input: lev for lev in plan.levels}
        reps = [lev.input for lev in plan.levels]
        assert reps[0] == "vehicle_pose"

        # Only one ENUMERATE level (c1); c2 is reclassified to NEAREST.
        enumerate_levels = [lev for lev in plan.levels if lev.search_type is SearchType.ENUMERATE]
        assert len(enumerate_levels) == 1
        assert enumerate_levels[0].input == "camera1"
        assert level_by_rep["camera2"].search_type == SearchType.NEAREST

    def test_spread_reclassification(self, fs_importer: FilesystemImporter) -> None:
        """minimize(spread(lidar, c1, c2)): lidar ENUMERATE, then c1/c2 NEAREST."""
        source = f"""\
{_HEADER}
// Spread reclassification
aligner SpreadReclass {{
    inputs {{
        // Camera 1
        camera1: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera 2
        camera2: Tappy<CameraImage>
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
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(|lidar.observation_time - camera1.observation_time| <= 100ms);
    require(|lidar.observation_time - camera2.observation_time| <= 100ms);
    minimize(spread(
        lidar.observation_time,
        camera1.observation_time,
        camera2.observation_time
    ));
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "spread_reclass", "SpreadReclass", fs_importer)

        # lidar has highest connectivity to unbound (c1, c2) → picked first among ENUMERATEs.
        # After lidar bound, c1 and c2 become NEAREST.
        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert level_by_rep["camera1"].search_type == SearchType.NEAREST
        assert level_by_rep["camera2"].search_type == SearchType.NEAREST
        assert all(lev.search_type is not SearchType.ENUMERATE for lev in plan.levels[1:])
        assert plan.levels[0].search_type == SearchType.ENUMERATE


class TestSearchTypePreservation:
    """Monotone and separable objectives maintain correct search types."""

    def test_last_in_range_preserved(self, fs_importer: FilesystemImporter) -> None:
        """maximize(local_map.tov) → LAST_IN_RANGE at any level."""
        source = f"""\
{_HEADER}
// Last in range
aligner LastRange {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Local map
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
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
    assume(is_strictly_increasing(local_map.tov));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    require(local_map.tov <= vehicle_pose.tov);
    maximize(local_map.tov);
    maximize(vehicle_pose.tov);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "last_range", "LastRange", fs_importer)

        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert level_by_rep["local_map"].search_type == SearchType.LAST_IN_RANGE
        assert level_by_rep["vehicle_pose"].search_type == SearchType.LAST_IN_RANGE

    def test_first_in_range_preserved(self, fs_importer: FilesystemImporter) -> None:
        """minimize(local_map.tov) → FIRST_IN_RANGE at any level."""
        source = f"""\
{_HEADER}
// First in range
aligner FirstRange {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Local map
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
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
    assume(is_strictly_increasing(local_map.tov));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    require(local_map.tov <= vehicle_pose.tov);
    minimize(local_map.tov);
    minimize(vehicle_pose.tov);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "first_range", "FirstRange", fs_importer)

        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert level_by_rep["local_map"].search_type == SearchType.FIRST_IN_RANGE
        assert level_by_rep["vehicle_pose"].search_type == SearchType.FIRST_IN_RANGE

    def test_any_match_no_objectives(self, fs_importer: FilesystemImporter) -> None:
        """Constrained inputs with no objectives → ALL ANY_MATCH."""
        source = f"""\
{_HEADER}
// All any match
aligner AllAny {{
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
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "all_any", "AllAny", fs_importer)

        for level in plan.levels:
            assert level.search_type == SearchType.ANY_MATCH


class TestConnectivityScoring:
    """Higher connectivity to unbound inputs is preferred (makes more terms separable)."""

    def test_higher_connectivity_wins(self, fs_importer: FilesystemImporter) -> None:
        """Among ENUMERATE candidates, higher connectivity to unbound breaks ties."""
        source = f"""\
{_HEADER}
// Connectivity scoring
aligner ConnScore {{
    inputs {{
        // Camera 1
        camera1: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera 2
        camera2: Tappy<CameraImage>
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
        // Local map
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(local_map.tov));
    require(|lidar.observation_time - camera1.observation_time| <= 100ms);
    require(|lidar.observation_time - camera2.observation_time| <= 100ms);
    require(local_map.tov <= lidar.observation_time);
    minimize(spread(
        lidar.observation_time,
        camera1.observation_time,
        camera2.observation_time
    ));
    maximize(local_map.tov);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "conn_score", "ConnScore", fs_importer)

        # local_map wins pivot (LAST_IN_RANGE, rank 3).
        assert plan.levels[0].input == "local_map"

        # Among remaining ENUMERATEs: lidar has connectivity 2 (c1, c2),
        # cameras have connectivity 1 each. Lidar placed first.
        reps = [lev.input for lev in plan.levels]
        assert reps[1] == "lidar"
        # After lidar is bound, cameras become NEAREST (spread terms separable).
        assert plan.levels[2].search_type is SearchType.NEAREST
        assert plan.levels[3].search_type is SearchType.NEAREST


class TestMaxMsgsOrdering:
    """ENUMERATE inputs ordered by ascending max_msgs (cheaper loops first)."""

    def test_smaller_buffer_first(self, fs_importer: FilesystemImporter) -> None:
        """Camera1 (max_msgs=5) is placed before camera2 (max_msgs=20) among ENUMERATEs."""
        source = f"""\
{_HEADER}
// Max msgs ordering
aligner MsgsOrder {{
    inputs {{
        // Camera 1
        camera1: Tappy<CameraImage>
        {{
            max_msgs: 5;
            arbitrary_selection: true;
        }}
        // Camera 2
        camera2: Tappy<CameraImage>
        {{
            max_msgs: 20;
            arbitrary_selection: true;
        }}
        // Camera 3
        camera3: Tappy<CameraImage>
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
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(camera3.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(camera1.observation_time <= vehicle_pose.tov);
    require(camera2.observation_time <= vehicle_pose.tov);
    require(camera3.observation_time <= vehicle_pose.tov);
    minimize(spread(
        camera1.observation_time,
        camera2.observation_time,
        camera3.observation_time
    ));
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "msgs_ord", "MsgsOrder", fs_importer)

        # VP is pivot (ANY_MATCH, rank 2 beats all ENUMERATEs).
        assert plan.levels[0].input == "vehicle_pose"

        # With VP bound, all cameras are ENUMERATE (spread has cross-terms).
        # First ENUMERATE pick: c1 (max_msgs=5, smallest). After c1 bound,
        # spread terms involving c1 become separable → c2/c3 reclassify NEAREST.
        reps = [lev.input for lev in plan.levels]
        assert reps[1] == "camera1"  # Smallest max_msgs among ENUMERATEs
        assert plan.levels[1].search_type is SearchType.ENUMERATE
        # Remaining cameras become NEAREST because |c1-c2| and |c1-c3| are separable.
        assert plan.levels[2].search_type is SearchType.NEAREST
        assert plan.levels[3].search_type is SearchType.NEAREST


class TestEqualityConstraints:
    """Equality constraints: EXACT_MATCH, equality checks, and scoring."""

    def test_monotonic_equality_produces_exact_match(self, fs_importer: FilesystemImporter) -> None:
        """require(l1.obs == l2.obs) with both is_strictly_increasing → EXACT_MATCH, no eq checks."""
        source = f"""\
{_HEADER}
// Equality constrained
aligner EqualTime {{
    inputs {{
        // Lidar 1
        lidar1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar 2
        lidar2: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    require(lidar1.observation_time == lidar2.observation_time);
    minimize(|lidar1.observation_time - lidar2.observation_time|);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "equal", "EqualTime", fs_importer)
        assert len(plan.levels) == 2
        second = plan.levels[1]
        # STN equality on monotonic fields → EXACT_MATCH (overrides NEAREST from objective).
        assert second.search_type is SearchType.EXACT_MATCH
        # No equality checks — constraint went through STN, not EqualityConstraint.
        assert len(second.equality_checks) == 0

    def test_mixed_property_monotonic_target(self, fs_importer: FilesystemImporter) -> None:
        """require(cam.obs == lidar.obs) where lidar monotonic, camera unique → EXACT_MATCH for lidar."""
        source = f"""\
{_HEADER}
// Mixed property equality
aligner MixedProp {{
    inputs {{
        // Camera (unique, not monotonic)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar (monotonic)
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_unique(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(camera.observation_time == lidar.observation_time);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "mixed_prop", "MixedProp", fs_importer)
        level_by_input = {lev.input: lev for lev in plan.levels}

        # Step 0: both are ANY_MATCH with no bound inputs. Alphabetical tiebreak: camera first.
        # Step 1: lidar.observation_time is_strictly_increasing + EqualityConstraint to
        # bound camera → EXACT_MATCH via Path B.
        assert level_by_input["lidar"].search_type is SearchType.EXACT_MATCH

    def test_unique_equality_filter(self, fs_importer: FilesystemImporter) -> None:
        """require(l1.obs == l2.obs) with both is_unique → equality check with is_unique=True."""
        source = f"""\
{_HEADER}
// Unique equality filter
aligner UniqueFilter {{
    inputs {{
        // Lidar 1
        lidar1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar 2
        lidar2: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_unique(lidar1.observation_time));
    assume(is_unique(lidar2.observation_time));
    require(lidar1.observation_time == lidar2.observation_time);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "uniq_filter", "UniqueFilter", fs_importer)
        assert len(plan.levels) == 2

        # Neither is is_strictly_increasing, so no EXACT_MATCH.
        # Both are ANY_MATCH with unique equality quality.
        second = plan.levels[1]
        assert second.search_type is not SearchType.EXACT_MATCH

        # The second level should have an equality check attached.
        assert len(second.equality_checks) == 1
        eq_check = second.equality_checks[0]
        assert eq_check.is_unique is True
        assert eq_check.target_accessor.input_name == second.input

    def test_no_assumption_equality_filter(self, fs_importer: FilesystemImporter) -> None:
        """require(l1.obs == l2.obs) with no assumptions → equality check with is_unique=False."""
        source = f"""\
{_HEADER}
// No-assumption equality filter
aligner NoAssumFilter {{
    inputs {{
        // Lidar 1
        lidar1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar 2
        lidar2: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(lidar1.observation_time == lidar2.observation_time);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "no_assum_filter", "NoAssumFilter", fs_importer)
        assert len(plan.levels) == 2

        second = plan.levels[1]
        assert second.search_type is not SearchType.EXACT_MATCH

        # Equality check attached, but not unique.
        assert len(second.equality_checks) == 1
        eq_check = second.equality_checks[0]
        assert eq_check.is_unique is False

    def test_exact_match_plus_equality_check_on_different_fields(self, fs_importer: FilesystemImporter) -> None:
        """EXACT_MATCH on one field + equality check on another field, same input pair."""
        source = f"""\
{_HEADER}
// EXACT_MATCH + equality check on different fields
aligner DualConstraint {{
    inputs {{
        // Lidar 1
        lidar1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar 2
        lidar2: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_unique(lidar1.tov));
    assume(is_unique(lidar2.tov));
    require(lidar1.observation_time == lidar2.observation_time);
    require(lidar1.tov == lidar2.tov);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "dual_constr", "DualConstraint", fs_importer)
        second = plan.levels[1]
        # EXACT_MATCH from monotonic observation_time equality (STN).
        assert second.search_type is SearchType.EXACT_MATCH
        # tov equality → EqualityCheck filter (is_unique, not monotonic).
        assert len(second.equality_checks) == 1
        assert second.equality_checks[0].is_unique is True

    def test_equality_quality_scoring(self, fs_importer: FilesystemImporter) -> None:
        """Unique equality constraint is a tiebreaker: lidar (has equality) placed before radar."""
        source = f"""\
{_HEADER}
// Equality quality scoring
aligner EqQualScore {{
    inputs {{
        // Camera (has equality constraint with lidar)
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
        // Radar (no equality constraint)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_unique(camera.observation_time));
    assume(is_unique(lidar.observation_time));
    require(camera.observation_time == lidar.observation_time);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "eq_qual_score", "EqQualScore", fs_importer)
        # Camera first (alphabetical). Lidar has unique equality with camera → placed before radar.
        reps = [lev.input for lev in plan.levels]
        assert reps.index("lidar") < reps.index("radar")


class TestOptionalInputs:
    """Optional inputs: required first, is_optional flag correct."""

    def test_optional_placed_after_required(self, fs_importer: FilesystemImporter) -> None:
        """Optional inputs score (1, ...) which is worse than required (0, ...)."""
        source = f"""\
{_HEADER}
// Optional placement
aligner WithOptional {{
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
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
    minimize(|lidar.observation_time - radar.observation_time|);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(
            source,
            "opt_place",
            "WithOptional",
            fs_importer,
            include_conditional="radar",
        )
        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert not level_by_rep["lidar"].is_optional
        assert level_by_rep["radar"].is_optional

        # Required lidar is placed before optional radar.
        reps = [lev.input for lev in plan.levels]
        assert reps.index("lidar") < reps.index("radar")


class TestTwoInputBaseline:
    """Canonical camera+lidar pair exercises multiple features in one setup.

    Covers: alphabetical tiebreak, NEAREST classification, dependency
    tracking, feasible windows, and all structural invariants (checked
    automatically by ``_assert_plan_invariants``).
    """

    def test_camera_lidar_plan(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|camera - lidar|): camera pivot (alphabetical), lidar NEAREST."""
        source = f"""\
{_HEADER}
// Two-input baseline
aligner TwoInput {{
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
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    minimize(|camera.observation_time - lidar.observation_time|);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "two_input", "TwoInput", fs_importer)

        # Alphabetical tiebreak: 'camera' < 'lidar'.
        assert plan.levels[0].input == "camera"

        # Lidar is NEAREST (bound reference = camera).
        lidar_level = plan.levels[1]
        assert lidar_level.input == "lidar"
        assert lidar_level.search_type == SearchType.NEAREST

        # First level has no dependencies; lidar depends on camera.
        assert plan.levels[0].depends_on == frozenset()
        assert lidar_level.depends_on == frozenset({"camera"})

        # ±100ms feasible window from the |c-l| <= 100ms constraint.
        assert len(lidar_level.windows) == 1
        w = lidar_level.windows[0]
        assert w.lo is not None
        assert w.lo.value == Decimal("-0.1")
        assert w.hi is not None
        assert w.hi.value == Decimal("0.1")

        # Fully separable (no ENUMERATE after first level).
        assert all(lev.search_type is not SearchType.ENUMERATE for lev in plan.levels[1:])


class TestEmptyInputError:
    """Edge case: empty input list."""

    def test_empty_inputs_raises(self, fs_importer: FilesystemImporter) -> None:
        """compute_join_plan raises JoinPlanError on empty input."""
        source = f"""\
{_HEADER}
// Minimal for STN
aligner Minimal {{
    inputs {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
}}
{_SCHEMAS}
"""
        module_id = ModuleID(CLK_REPO, "err_empty")
        module = compiler.compile_source_text(source, module_id, fs_importer)
        aligner_ir = module.inner_scope.lookup("Minimal")
        assert isinstance(aligner_ir, aligner.Aligner)
        type_check_aligner(aligner_ir, module.context)
        specs = extract_specs(aligner_ir)
        stn = analyze_stn(list(specs.unconditional_constraints))

        with pytest.raises(JoinPlanError, match="no inputs"):
            compute_join_plan(
                inputs=(),
                objectives=(),
                active_inputs=frozenset(),
                stn=stn,
                aligner_node=aligner_ir,
            )


class TestSensorFusionIntegration:
    """Multi-input SensorFusion aligner exercises greedy ordering end-to-end."""

    def test_sensor_fusion_greedy_plan(self, fs_importer: FilesystemImporter) -> None:
        """SensorFusion: local_map wins pivot (LAST_IN_RANGE), cameras become NEAREST."""
        source = _sensor_fusion_source()
        plan = _compile_and_plan(source, "sf_greedy", "SensorFusion", fs_importer)

        level_by_rep = {lev.input: lev for lev in plan.levels}

        # local_map wins pivot: maximize(local_map.tov) → LAST_IN_RANGE (rank 3).
        assert plan.levels[0].input == "local_map"
        assert plan.levels[0].search_type == SearchType.LAST_IN_RANGE

        # vehicle_pose is ANY_MATCH (no objective references it).
        assert level_by_rep["vehicle_pose"].search_type == SearchType.ANY_MATCH

        # lidar1 and lidar2 are individual levels (equality-constrained).
        # With both is_strictly_increasing and require(l1 == l2), the second
        # lidar gets EXACT_MATCH via STN equality once the first is bound.
        assert "lidar1" in level_by_rep
        assert "lidar2" in level_by_rep
        lidar_types = {level_by_rep["lidar1"].search_type, level_by_rep["lidar2"].search_type}
        assert SearchType.EXACT_MATCH in lidar_types

        assert level_by_rep["camera1"].search_type == SearchType.NEAREST
        assert level_by_rep["camera2"].search_type == SearchType.NEAREST

    def test_sensor_fusion_with_radar(self, fs_importer: FilesystemImporter) -> None:
        """With radar included, radar is optional and NEAREST (lidar bound)."""
        source = _sensor_fusion_source()
        plan = _compile_and_plan(
            source,
            "sf_radar",
            "SensorFusion",
            fs_importer,
            include_conditional="radar",
        )
        level_by_rep = {lev.input: lev for lev in plan.levels}
        assert level_by_rep["radar"].is_optional
        assert level_by_rep["radar"].search_type == SearchType.NEAREST


def _sensor_fusion_source() -> str:
    """The canonical SensorFusion aligner from the design doc."""
    return f"""\
{_HEADER}
// Sensor fusion aligner
aligner SensorFusion {{
    inputs {{
        // Lidar sweep 1
        lidar1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar sweep 2
        lidar2: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera image 1
        camera1: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera image 2
        camera2: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar detections (optional)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Vehicle pose (reusable)
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
        // Local map (reusable)
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_strictly_increasing(local_map.tov));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar1.observation_time == lidar2.observation_time);
    require(|lidar1.observation_time - camera1.observation_time| <= 100ms);
    require(|lidar1.observation_time - camera2.observation_time| <= 100ms);
    require(|lidar1.observation_time - radar.observation_time| <= 100ms);
    require(lidar1.observation_time <= vehicle_pose.tov);
    require(lidar2.observation_time <= vehicle_pose.tov);
    require(camera1.observation_time <= vehicle_pose.tov);
    require(camera2.observation_time <= vehicle_pose.tov);
    require(radar.observation_time <= vehicle_pose.tov);
    require(local_map.tov <= vehicle_pose.tov);
    minimize(spread(lidar1.observation_time, camera1.observation_time, camera2.observation_time, radar.observation_time));
    maximize(local_map.tov);
}}
{_SCHEMAS}
"""


class TestNonDecreasingEquivalentToStrict:
    """Targets declared ``is_non_decreasing`` behave like ``is_strictly_increasing`` at the plan layer."""

    def test_nondec_equality_promotes_to_exact_match(self, fs_importer: FilesystemImporter) -> None:
        """``require(a == b)`` with both fields non-decreasing promotes the second level to EXACT_MATCH."""
        strict_source = f"""\
{_HEADER}
// Strict version
aligner StrictEq {{
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
    maximize(lidar.observation_time);
}}
{_SCHEMAS}
"""
        nondec_source = f"""\
{_HEADER}
// Non-decreasing version
aligner NdEq {{
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
    maximize(lidar.observation_time);
}}
{_SCHEMAS}
"""
        strict_plan = _compile_and_plan(strict_source, "strict_eq", "StrictEq", fs_importer)
        nondec_plan = _compile_and_plan(nondec_source, "nd_eq", "NdEq", fs_importer)

        # Both plans should have identical structure: same input ordering and same search types.
        strict_shape = [(lev.input, lev.search_type) for lev in strict_plan.levels]
        nondec_shape = [(lev.input, lev.search_type) for lev in nondec_plan.levels]
        assert strict_shape == nondec_shape
        # Second level is EXACT_MATCH (equality on sorted field with the first bound input).
        assert strict_plan.levels[1].search_type == SearchType.EXACT_MATCH
        assert nondec_plan.levels[1].search_type == SearchType.EXACT_MATCH

    def test_nondec_equality_check_skipped(self, fs_importer: FilesystemImporter) -> None:
        """An equality with a non-decreasing target does NOT emit an EqualityCheck (EXACT_MATCH takes over)."""
        source = f"""\
{_HEADER}
// ND equality check skipped
aligner NdEqCheck {{
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
    maximize(lidar.observation_time);
}}
{_SCHEMAS}
"""
        plan = _compile_and_plan(source, "nd_eq_check", "NdEqCheck", fs_importer)
        for lev in plan.levels:
            # No EqualityCheck should remain — sorted target means EXACT_MATCH.
            assert lev.equality_checks == ()
