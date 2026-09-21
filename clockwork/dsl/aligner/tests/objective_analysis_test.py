# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner objective analysis and search type classification.

Tests exercise :func:`classify_input` via end-to-end parsing->IR->analysis
pipelines rather than constructing IR nodes directly.
"""

from __future__ import annotations

import pytest
from clockwork.dsl.aligner.extract_specs import FirstInBatch, InputSelector, LastInBatch, Objective, extract_specs
from clockwork.dsl.aligner.objective_analysis import (
    ObjectiveAnalysisError,
    SearchClassification,
    SearchType,
    _apply_batch_boundary_default,
    classify_input,
)
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _compile_and_build_context(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
    *,
    include_conditional: str | None = None,
) -> tuple[
    tuple[InputSelector, ...],
    tuple[Objective, ...],
    frozenset[InputSelector],
]:
    """End-to-end: compile -> type check -> extract -> active inputs.

    Returns the data needed to call :func:`classify_input`:
    ``(all_inputs, objectives, active_inputs)``.
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    type_check_aligner(aligner_ir, module.context)
    specs = extract_specs(aligner_ir)

    objectives: tuple[Objective, ...] = specs.unconditional_objectives
    if include_conditional is not None:
        cond_objs = specs.conditional_objectives.get(include_conditional, ())
        objectives = (*objectives, *cond_objs)

    # Compute active inputs from the resolved aligner.
    resolved = aligner_ir.resolve()
    all_inputs = list(resolved.inputs)
    active_inputs: frozenset[InputSelector] = frozenset(all_inputs)

    return tuple(all_inputs), objectives, active_inputs


def _classify_all_with_others_bound(
    all_inputs: tuple[InputSelector, ...],
    objectives: tuple[Objective, ...],
    active_inputs: frozenset[InputSelector],
) -> dict[InputSelector, SearchClassification]:
    """Classify every input with all others as bound."""
    result: dict[InputSelector, SearchClassification] = {}
    for inp in all_inputs:
        bound = frozenset(other for other in all_inputs if other != inp)
        classification = classify_input(
            candidate=inp,
            bound_inputs=bound,
            objectives=objectives,
            active_inputs=active_inputs,
        )
        result[inp] = classification
    return result


def _classify_with_bound_reps(
    candidate_rep: InputSelector,
    bound_reps: frozenset[InputSelector],
    objectives: tuple[Objective, ...],
    active_inputs: frozenset[InputSelector],
) -> SearchClassification:
    """Classify an input with a specific set of bound inputs."""
    return classify_input(
        candidate=candidate_rep,
        bound_inputs=bound_reps,
        objectives=objectives,
        active_inputs=active_inputs,
    )


# Shared boilerplate
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


class TestNearestFromAbsDifference:
    """NEAREST classification via |a - b| absolute difference pattern."""

    def test_abs_difference_gives_nearest(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|camera - lidar|) -> lidar is NEAREST when camera is bound."""
        source = f"""\
{_HEADER}
// Absolute difference
aligner AbsDiff {{
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
        ctx = _compile_and_build_context(source, "abs_diff", "AbsDiff", fs_importer)
        _, objectives, active_inputs = ctx

        result = _classify_with_bound_reps(
            "lidar",
            frozenset({"camera"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.NEAREST
        assert result.nearest_reference is not None
        assert result.nearest_reference.reference.input_name == "camera"
        assert result.nearest_reference.reference.field_name == "observation_time"
        assert result.nearest_reference.target.input_name == "lidar"
        assert result.nearest_reference.target.field_name == "observation_time"

        result = _classify_with_bound_reps(
            "camera",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.NEAREST
        assert result.nearest_reference is not None
        assert result.nearest_reference.reference.input_name == "lidar"
        assert result.nearest_reference.target.input_name == "camera"

    def test_abs_difference_add_gives_nearest(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|c1 - ref| + |c2 - ref|) -> c1, c2 both NEAREST when ref is bound."""
        source = f"""\
{_HEADER}
// Additive abs differences
aligner AddAbsDiff {{
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
    minimize(
        |camera1.observation_time - lidar.observation_time|
        + |camera2.observation_time - lidar.observation_time|
    );
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "add_abs", "AddAbsDiff", fs_importer)
        _, objectives, active_inputs = ctx

        c1 = _classify_with_bound_reps(
            "camera1",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        c2 = _classify_with_bound_reps(
            "camera2",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert c1.search_type == SearchType.NEAREST
        assert c2.search_type == SearchType.NEAREST

        lidar = _classify_with_bound_reps(
            "lidar",
            frozenset({"camera1", "camera2"}),
            objectives,
            active_inputs,
        )
        assert lidar.search_type == SearchType.NEAREST


class TestNearestFromSumDecomposition:
    """NEAREST classification via sum(ExprTuple(...)) from built-in spread function."""

    def test_spread_builtin_gives_nearest(self, fs_importer: FilesystemImporter) -> None:
        """minimize(spread(ref, t1, t2, t3)) -> all NEAREST via sum(ExprTuple) decomposition."""
        source = f"""\
{_HEADER}
// Spread built-in
aligner SpreadBuiltin {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera1.observation_time| <= 100ms);
    require(|lidar.observation_time - camera2.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
    minimize(spread(
        lidar.observation_time,
        camera1.observation_time,
        camera2.observation_time,
        radar.observation_time
    ));
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "spread_fn", "SpreadBuiltin", fs_importer)
        _, objectives, active_inputs = ctx

        bound_lidar = frozenset({"lidar"})
        for name in ("camera1", "camera2", "radar"):
            result = _classify_with_bound_reps(
                name,
                bound_lidar,
                objectives,
                active_inputs,
            )
            assert result.search_type == SearchType.NEAREST, f"Expected NEAREST for {name}"


class TestMonotoneClassification:
    """Monotone objectives: maximize -> LAST_IN_RANGE, minimize -> FIRST_IN_RANGE."""

    def test_maximize_gives_last_in_range(self, fs_importer: FilesystemImporter) -> None:
        """maximize(local_map.tov) -> LAST_IN_RANGE regardless of bound set."""
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
        ctx = _compile_and_build_context(source, "last_range", "LastRange", fs_importer)
        _, objectives, active_inputs = ctx

        lm = _classify_with_bound_reps(
            "local_map",
            frozenset({"vehicle_pose", "lidar"}),
            objectives,
            active_inputs,
        )
        assert lm.search_type == SearchType.LAST_IN_RANGE
        assert lm.objective_term is not None
        assert lm.nearest_reference is None

        vp = _classify_with_bound_reps(
            "vehicle_pose",
            frozenset({"lidar", "local_map"}),
            objectives,
            active_inputs,
        )
        assert vp.search_type == SearchType.LAST_IN_RANGE
        assert vp.objective_term is not None

    def test_minimize_gives_first_in_range(self, fs_importer: FilesystemImporter) -> None:
        """minimize(vehicle_pose.tov) -> FIRST_IN_RANGE."""
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
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    minimize(vehicle_pose.tov);
    minimize(lidar.observation_time);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "first_range", "FirstRange", fs_importer)
        _, objectives, active_inputs = ctx

        vp = _classify_with_bound_reps(
            "vehicle_pose",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert vp.search_type == SearchType.FIRST_IN_RANGE

        lidar = _classify_with_bound_reps(
            "lidar",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert lidar.search_type == SearchType.FIRST_IN_RANGE
        assert lidar.objective_term is not None


class TestEnumerate:
    """ENUMERATE classification for non-separable objectives."""

    def test_non_separable_gives_enumerate(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|c1 - c2|) with only vp bound -> both c1, c2 ENUMERATE."""
        source = f"""\
{_HEADER}
// Non-separable
aligner NonSep {{
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
        ctx = _compile_and_build_context(source, "non_sep", "NonSep", fs_importer)
        _, objectives, active_inputs = ctx

        # With only vp bound, |c1 - c2| references two unbound inputs -> ENUMERATE.
        c1 = _classify_with_bound_reps(
            "camera1",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        c2 = _classify_with_bound_reps(
            "camera2",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert c1.search_type == SearchType.ENUMERATE
        assert c2.search_type == SearchType.ENUMERATE

    def test_non_separable_becomes_nearest_when_other_bound(self, fs_importer: FilesystemImporter) -> None:
        """minimize(|c1 - c2|): ENUMERATE when both unbound, NEAREST once other is bound.

        This is the key improvement of per-level classification: the greedy
        join planner binds c1 first (ENUMERATE), then at the next level
        c2 sees c1 as bound and the term becomes separable -> NEAREST.
        """
        source = f"""\
{_HEADER}
// Non-separable becomes separable
aligner BecomesSep {{
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
        ctx = _compile_and_build_context(source, "becomes_sep", "BecomesSep", fs_importer)
        _, objectives, active_inputs = ctx

        # Phase 1: only vp bound -> both ENUMERATE.
        c1_enum = _classify_with_bound_reps(
            "camera1",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        c2_enum = _classify_with_bound_reps(
            "camera2",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert c1_enum.search_type == SearchType.ENUMERATE
        assert c2_enum.search_type == SearchType.ENUMERATE

        # Phase 2: with vp AND c1 bound, c2 -> NEAREST (|c1 - c2| separable).
        c2 = _classify_with_bound_reps(
            "camera2",
            frozenset({"vehicle_pose", "camera1"}),
            objectives,
            active_inputs,
        )
        assert c2.search_type == SearchType.NEAREST

        # Symmetrically: with vp AND c2 bound, c1 -> NEAREST.
        c1 = _classify_with_bound_reps(
            "camera1",
            frozenset({"vehicle_pose", "camera2"}),
            objectives,
            active_inputs,
        )
        assert c1.search_type == SearchType.NEAREST


class TestNonSeparableIgnored:
    """Non-separable terms are ignored when separable terms exist."""

    def test_separable_wins_over_non_separable(self, fs_importer: FilesystemImporter) -> None:
        """Non-separable + monotone objective -> monotone wins (not error).

        In the old analysis, this was an error ("Competing objectives").
        With per-level classification, non-separable terms are ignored when
        a separable term provides a starting point.
        """
        source = f"""\
{_HEADER}
// Non-separable ignored
aligner NonSepIgnored {{
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
    maximize(camera1.observation_time);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "non_sep_ignored", "NonSepIgnored", fs_importer)
        _, objectives, active_inputs = ctx

        # c1: non-separable from |c1-c2| + separable LAST_IN_RANGE from maximize(c1)
        # -> LAST_IN_RANGE (non-separable ignored).
        c1 = _classify_with_bound_reps(
            "camera1",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert c1.search_type == SearchType.LAST_IN_RANGE

        # c2: only non-separable from |c1-c2| -> ENUMERATE.
        c2 = _classify_with_bound_reps(
            "camera2",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert c2.search_type == SearchType.ENUMERATE


class TestMergeRules:
    """Tests for classification merge logic across multiple objectives."""

    def test_nearest_subsumes_last_in_range(self, fs_importer: FilesystemImporter) -> None:
        """NEAREST + LAST_IN_RANGE -> NEAREST (subsumes monotone)."""
        source = f"""\
{_HEADER}
// Nearest subsumes monotone
aligner NearestSubsumes {{
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
    maximize(camera.observation_time);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "nearest_subsumes", "NearestSubsumes", fs_importer)
        _, objectives, active_inputs = ctx

        # camera: NEAREST from abs diff + LAST_IN_RANGE from maximize -> NEAREST.
        result = _classify_with_bound_reps(
            "camera",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.NEAREST

    def test_nearest_subsumes_first_in_range(self, fs_importer: FilesystemImporter) -> None:
        """NEAREST + FIRST_IN_RANGE -> NEAREST."""
        source = f"""\
{_HEADER}
// Nearest subsumes first
aligner NearestFirst {{
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
    minimize(camera.observation_time);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "nearest_first", "NearestFirst", fs_importer)
        _, objectives, active_inputs = ctx

        result = _classify_with_bound_reps(
            "camera",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.NEAREST

    def test_conflicting_first_last_raises(self, fs_importer: FilesystemImporter) -> None:
        """FIRST_IN_RANGE + LAST_IN_RANGE -> ObjectiveAnalysisError."""
        source = f"""\
{_HEADER}
// Conflicting directions
aligner ConflictDir {{
    inputs {{
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
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    maximize(lidar.observation_time);
    minimize(lidar.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "conflict_dir", "ConflictDir", fs_importer)
        _, objectives, active_inputs = ctx

        with pytest.raises(ObjectiveAnalysisError, match="Conflicting objectives"):
            _classify_with_bound_reps(
                "lidar",
                frozenset({"vehicle_pose"}),
                objectives,
                active_inputs,
            )

    def test_same_type_merges(self, fs_importer: FilesystemImporter) -> None:
        """Two LAST_IN_RANGE objectives on same input -> LAST_IN_RANGE."""
        source = f"""\
{_HEADER}
// Same type merge
aligner SameType {{
    inputs {{
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
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    maximize(lidar.observation_time);
    maximize(lidar.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "same_type", "SameType", fs_importer)
        _, objectives, active_inputs = ctx

        result = _classify_with_bound_reps(
            "lidar",
            frozenset({"vehicle_pose"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.LAST_IN_RANGE


class TestFullExample:
    """Multi-input example with multiple search types."""

    def test_multi_type_classification(self, fs_importer: FilesystemImporter) -> None:
        """Combined NEAREST + LAST_IN_RANGE with lidar bound."""
        source = f"""\
{_HEADER}
// Multi-type classification
aligner MultiType {{
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
    minimize(
        |camera1.observation_time - lidar.observation_time|
        + |camera2.observation_time - lidar.observation_time|
    );
    maximize(local_map.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "multi_type", "MultiType", fs_importer)
        _, objectives, active_inputs = ctx

        # With lidar bound: cameras NEAREST, local_map LAST_IN_RANGE.
        c1 = _classify_with_bound_reps(
            "camera1",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        c2 = _classify_with_bound_reps(
            "camera2",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        lm = _classify_with_bound_reps(
            "local_map",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert c1.search_type == SearchType.NEAREST
        assert c2.search_type == SearchType.NEAREST
        assert lm.search_type == SearchType.LAST_IN_RANGE

        # lidar with all others bound -> NEAREST from the abs diff terms.
        lidar = _classify_with_bound_reps(
            "lidar",
            frozenset({"camera1", "camera2", "local_map"}),
            objectives,
            active_inputs,
        )
        assert lidar.search_type == SearchType.NEAREST


class TestGreedyWorkedExample:
    """The motivating scenario from the design document.

    ``maximize(lidar.tov)`` + ``minimize(spread(lidar.tov, camera.tov))``
    with constraints ``lidar < pose`` and ``camera < pose``.

    Level-by-level greedy analysis should produce:
    lidar(LAST_IN_RANGE) -> camera(NEAREST) -> pose(ANY_MATCH).
    """

    def test_greedy_level_by_level(self, fs_importer: FilesystemImporter) -> None:
        """Simulate greedy placement and verify per-level classifications."""
        source = f"""\
{_HEADER}
// Greedy worked example
aligner GreedyExample {{
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
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.tov <= vehicle_pose.tov);
    require(camera.observation_time <= vehicle_pose.tov);
    maximize(lidar.tov);
    minimize(spread(lidar.tov, camera.observation_time));
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "greedy_ex", "GreedyExample", fs_importer)
        _, objectives, active_inputs = ctx

        # Level 0: nothing bound.
        # lidar: maximize(lidar.tov) -> LAST_IN_RANGE.
        #   spread term |lidar.tov - camera.obs| references camera (unbound) -> non-separable, ignored.
        lidar_l0 = _classify_with_bound_reps(
            "lidar",
            frozenset(),
            objectives,
            active_inputs,
        )
        assert lidar_l0.search_type == SearchType.LAST_IN_RANGE

        # camera: spread term |lidar.tov - camera.obs| references lidar (unbound) -> non-separable.
        #   No other objective -> ENUMERATE.
        camera_l0 = _classify_with_bound_reps(
            "camera",
            frozenset(),
            objectives,
            active_inputs,
        )
        assert camera_l0.search_type == SearchType.ENUMERATE

        # pose: no objective references pose -> ANY_MATCH.
        pose_l0 = _classify_with_bound_reps(
            "vehicle_pose",
            frozenset(),
            objectives,
            active_inputs,
        )
        assert pose_l0.search_type == SearchType.ANY_MATCH

        # Level 1: lidar bound.
        # camera: |lidar.tov - camera.obs| now separable -> NEAREST.
        camera_l1 = _classify_with_bound_reps(
            "camera",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert camera_l1.search_type == SearchType.NEAREST

        # pose: still no objective -> ANY_MATCH.
        pose_l1 = _classify_with_bound_reps(
            "vehicle_pose",
            frozenset({"lidar"}),
            objectives,
            active_inputs,
        )
        assert pose_l1.search_type == SearchType.ANY_MATCH


class TestEdgeCases:
    """Edge cases and special scenarios."""

    def test_no_objectives_all_any_match(self, fs_importer: FilesystemImporter) -> None:
        """Aligner with no objectives -> all inputs are ANY_MATCH."""
        source = f"""\
{_HEADER}
// No objectives
aligner NoObjectives {{
    inputs {{
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
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "no_objectives", "NoObjectives", fs_importer)
        classifications = _classify_all_with_others_bound(*ctx)
        for classification in classifications.values():
            assert classification.search_type == SearchType.ANY_MATCH

    def test_single_input_trivial(self, fs_importer: FilesystemImporter) -> None:
        """Single-input aligner: trivial classification."""
        source = f"""\
{_HEADER}
// Single input
aligner SingleInput {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    require(lidar.observation_time <= lidar.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "single_input", "SingleInput", fs_importer)
        classifications = _classify_all_with_others_bound(*ctx)
        assert len(classifications) == 1
        assert classifications["lidar"].search_type == SearchType.ANY_MATCH

    def test_let_binding_in_objective(self, fs_importer: FilesystemImporter) -> None:
        """Let-binding reference in objective is correctly resolved."""
        source = f"""\
{_HEADER}
// Let binding
aligner LetObj {{
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
    let cam_time = camera.observation_time;
    let lidar_time = lidar.observation_time;
    minimize(|cam_time - lidar_time|);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "let_obj", "LetObj", fs_importer)
        _, objectives, active_inputs = ctx

        # After let-binding resolution: minimize(|camera.obs - lidar.obs|).
        # With camera bound -> lidar is NEAREST.
        result = _classify_with_bound_reps(
            "lidar",
            frozenset({"camera"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.NEAREST

    def test_multiple_independent_objectives(self, fs_importer: FilesystemImporter) -> None:
        """Multiple objectives affecting disjoint inputs: valid composition."""
        source = f"""\
{_HEADER}
// Multiple independent objectives
aligner MultiObj {{
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
        // Local map
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(local_map.tov));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(local_map.tov <= lidar.observation_time);
    minimize(|camera.observation_time - lidar.observation_time|);
    maximize(local_map.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "multi_obj", "MultiObj", fs_importer)
        _, objectives, active_inputs = ctx

        camera = _classify_with_bound_reps(
            "camera",
            frozenset({"lidar", "local_map"}),
            objectives,
            active_inputs,
        )
        lm = _classify_with_bound_reps(
            "local_map",
            frozenset({"lidar", "camera"}),
            objectives,
            active_inputs,
        )
        assert camera.search_type == SearchType.NEAREST
        assert lm.search_type == SearchType.LAST_IN_RANGE


class TestSelfReferencedObjective:
    """Objectives referencing a single input's own fields."""

    def test_spread_self_gives_enumerate(self, fs_importer: FilesystemImporter) -> None:
        """spread(lidar.obs, lidar.tov) -> |lidar.tov - lidar.obs| -> ENUMERATE.

        The abs-diff pattern does not match because both sides reference
        the same (candidate) input, not a bound input.
        """
        source = f"""\
{_HEADER}
// Spread on single input
aligner SpreadSelf {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
    minimize(spread(lidar.observation_time, lidar.tov));
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "spread_self", "SpreadSelf", fs_importer)
        _, objectives, active_inputs = ctx

        result = _classify_with_bound_reps(
            "lidar",
            frozenset({"camera", "radar"}),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.ENUMERATE

    def test_competing_same_input_objectives_raises(self, fs_importer: FilesystemImporter) -> None:
        """Two separable objectives claiming same input with conflicting directions -> error."""
        source = f"""\
{_HEADER}
// Competing same-input objectives
aligner CompetingSame {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(lidar.tov));
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
    maximize(lidar.observation_time);
    minimize(lidar.tov);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "competing_same", "CompetingSame", fs_importer)
        _, objectives, active_inputs = ctx

        with pytest.raises(ObjectiveAnalysisError, match="Conflicting objectives"):
            _classify_with_bound_reps(
                "lidar",
                frozenset({"camera", "radar"}),
                objectives,
                active_inputs,
            )


class TestEmptyBoundSet:
    """Classification with an empty bound set (first level in greedy plan)."""

    def test_monotone_with_empty_bound(self, fs_importer: FilesystemImporter) -> None:
        """maximize(lidar.tov) with no inputs bound -> LAST_IN_RANGE.

        Monotone objectives produce single-input separable terms
        regardless of bound set.
        """
        source = f"""\
{_HEADER}
// Monotone empty bound
aligner MonoEmpty {{
    inputs {{
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
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    maximize(lidar.observation_time);
}}
{_SCHEMAS}
"""
        ctx = _compile_and_build_context(source, "mono_empty", "MonoEmpty", fs_importer)
        _, objectives, active_inputs = ctx

        # With empty bound set, maximize(lidar.obs) is still separable.
        result = _classify_with_bound_reps(
            "lidar",
            frozenset(),
            objectives,
            active_inputs,
        )
        assert result.search_type == SearchType.LAST_IN_RANGE


class TestBatchBoundaryDefaults:
    """Batch boundary inputs get directional defaults."""

    @pytest.mark.parametrize(
        ("representative", "input_type", "expected"),
        [
            (FirstInBatch("lidar"), SearchType.ANY_MATCH, SearchType.FIRST_IN_RANGE),
            (LastInBatch("lidar"), SearchType.ANY_MATCH, SearchType.LAST_IN_RANGE),
            ("lidar", SearchType.ANY_MATCH, SearchType.ANY_MATCH),
            (FirstInBatch("lidar"), SearchType.NEAREST, SearchType.NEAREST),
        ],
        ids=["first_in_batch", "last_in_batch", "non_batch", "non_any_match"],
    )
    def test_batch_boundary_promotion(
        self,
        representative: InputSelector,
        input_type: SearchType,
        expected: SearchType,
    ) -> None:
        """Batch boundary defaults: FirstInBatch->FIRST_IN_RANGE, LastInBatch->LAST_IN_RANGE."""
        classification = SearchClassification(input_type, None)
        result = _apply_batch_boundary_default(representative, classification)
        assert result.search_type == expected
