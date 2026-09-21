# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner STN construction and Floyd-Warshall analysis."""

from __future__ import annotations

from decimal import Decimal
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    FieldAccessor,
    extract_specs,
)
from clockwork.dsl.aligner.stn import (
    InconsistentConstraintsError,
    StnAnalysis,
    analyze_stn,
    normalize_bound,
)
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler, dfl
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.primitive import UnitValue
from clockwork.dsl.ir.units import MILLISECONDS, SECONDS


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
) -> StnAnalysis:
    """Compile source, type-check, extract specs, and run STN analysis.

    If *include_conditional* is given, the conditional constraints for that
    optional input are included alongside the unconditional constraints.
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    type_check_aligner(aligner_ir, module.context)
    specs = extract_specs(aligner_ir)

    constraints = list(specs.unconditional_constraints)
    if include_conditional is not None:
        constraints.extend(specs.conditional_constraints.get(include_conditional, ()))
    return analyze_stn(constraints)


# --------------------------------------------------------------------------
# Shared schema boilerplate
# --------------------------------------------------------------------------

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


def _fa(input_name: str, field_name: str) -> FieldAccessor:
    """Shorthand for FieldAccessor construction."""
    return FieldAccessor(input_name=input_name, field_name=field_name)


class TestNormalizeBound:
    """Tests for bound normalization to canonical typed values."""

    def test_unit_value_milliseconds(self) -> None:
        """UnitValue in milliseconds normalizes to seconds."""
        uv = UnitValue.make(value=Decimal(100), unit=MILLISECONDS)
        result = normalize_bound(uv)
        assert isinstance(result, UnitValue)
        assert result.value == Decimal("0.1")
        assert result.unit == SECONDS

    def test_unit_value_seconds(self) -> None:
        """UnitValue already in seconds stays the same."""
        uv = UnitValue.make(value=Decimal(5), unit=SECONDS)
        result = normalize_bound(uv)
        assert isinstance(result, UnitValue)
        assert result.value == Decimal(5)


class TestSensorFusion:
    """Full SensorFusion-like example.

    Covers equality, bounded-difference, and ordering constraints, plus
    transitive closure, equality classes, and feasible windows.
    """

    def test_sensor_fusion_stn(self, fs_importer: FilesystemImporter) -> None:
        """Multi-input aligner: equality, bounded-difference, ordering, transitive closure."""
        source = f"""\
{_HEADER}
// Sensor fusion aligner
aligner SensorFusion {{
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
        // Radar (optional)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
        // Local map
        local_map: Tappy<LocalMap>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    assume(is_strictly_increasing(local_map.tov));
    require(lidar1.observation_time == lidar2.observation_time);
    require(|lidar1.observation_time - camera1.observation_time| <= 100ms);
    require(|lidar1.observation_time - camera2.observation_time| <= 100ms);
    require(lidar1.observation_time <= vehicle_pose.tov);
    require(lidar2.observation_time <= vehicle_pose.tov);
    require(camera1.observation_time <= vehicle_pose.tov);
    require(camera2.observation_time <= vehicle_pose.tov);
    require(local_map.tov <= vehicle_pose.tov);

    if has_candidates(radar)
        then require(|lidar1.observation_time - radar.observation_time| <= 100ms)
              and require(radar.observation_time <= vehicle_pose.tov)
        else require(true);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(source, "fusion_stn", "SensorFusion", fs_importer)

        l1 = _fa("lidar1", "observation_time")
        l2 = _fa("lidar2", "observation_time")
        c1 = _fa("camera1", "observation_time")
        c2 = _fa("camera2", "observation_time")
        vp = _fa("vehicle_pose", "tov")
        lm = _fa("local_map", "tov")

        # --- Equality ---
        assert result.are_equal(l1, l2)
        assert result.equality_class_of(l1) == frozenset({l1, l2})
        # Cameras are not equal to each other (bounded, not equality)
        assert not result.are_equal(c1, c2)
        assert result.equality_class_of(c1) == frozenset({c1})

        # --- Direct bounds ---
        tb_l1c1 = result.tightest_bound(l1, c1)
        assert tb_l1c1 is not None
        assert tb_l1c1.value == Decimal("0.1")
        tb_c1l1 = result.tightest_bound(c1, l1)
        assert tb_c1l1 is not None
        assert tb_c1l1.value == Decimal("0.1")

        # --- Transitive bounds ---
        # camera1 to camera2 via lidar1: 100ms + 100ms = 200ms
        tb_c1c2 = result.tightest_bound(c1, c2)
        assert tb_c1c2 is not None
        assert tb_c1c2.value == Decimal("0.2")
        tb_c2c1 = result.tightest_bound(c2, c1)
        assert tb_c2c1 is not None
        assert tb_c2c1.value == Decimal("0.2")
        # camera1 <= vehicle_pose transitively via c1→l1→vp
        tb_c1vp = result.tightest_bound(c1, vp)
        assert tb_c1vp is not None
        assert tb_c1vp.value == Decimal(0)

        # --- One-sided ordering ---
        tb_l1vp = result.tightest_bound(l1, vp)
        assert tb_l1vp is not None
        assert tb_l1vp.value == Decimal(0)
        assert result.tightest_bound(vp, l1) is None
        tb_lmvp = result.tightest_bound(lm, vp)
        assert tb_lmvp is not None
        assert tb_lmvp.value == Decimal(0)
        assert result.tightest_bound(vp, lm) is None

        # --- Feasible windows ---
        # Symmetric bounded-difference window
        lo, hi = result.feasible_window(l1, c1)
        assert lo is not None
        assert lo.value == Decimal("-0.1")
        assert hi is not None
        assert hi.value == Decimal("0.1")
        # One-sided ordering window: lidar can be arbitrarily behind pose
        lo, hi = result.feasible_window(l1, vp)
        assert lo is None
        assert hi is not None
        assert hi.value == Decimal(0)
        # Reverse: pose is at least 0 ahead of lidar, no upper limit
        lo, hi = result.feasible_window(vp, l1)
        assert lo is not None
        assert lo.value == Decimal(0)
        assert hi is None

    def test_sensor_fusion_with_radar(self, fs_importer: FilesystemImporter) -> None:
        """SensorFusion with radar present adds radar constraints to the STN."""
        source = f"""\
{_HEADER}
// Sensor fusion with radar
aligner SensorFusionRadar {{
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
        // Camera 1
        camera1: Tappy<CameraImage>
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
        // Vehicle pose
        vehicle_pose: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar1.observation_time == lidar2.observation_time);
    require(|lidar1.observation_time - camera1.observation_time| <= 100ms);
    require(lidar1.observation_time <= vehicle_pose.tov);
    require(camera1.observation_time <= vehicle_pose.tov);

    if has_candidates(radar)
        then require(|lidar1.observation_time - radar.observation_time| <= 100ms)
              and require(radar.observation_time <= vehicle_pose.tov)
        else require(true);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(
            source,
            "fusion_radar_stn",
            "SensorFusionRadar",
            fs_importer,
            include_conditional="radar",
        )

        l1 = _fa("lidar1", "observation_time")
        radar = _fa("radar", "observation_time")

        tb_l1r = result.tightest_bound(l1, radar)
        assert tb_l1r is not None
        assert tb_l1r.value == Decimal("0.1")
        tb_rl1 = result.tightest_bound(radar, l1)
        assert tb_rl1 is not None
        assert tb_rl1.value == Decimal("0.1")


class TestTransitiveEquality:
    """Transitive equality: a == b and b == c implies a == c (3-way class)."""

    def test_three_way_equality(self, fs_importer: FilesystemImporter) -> None:
        """Three inputs chained by equality form a single equality class."""
        source = f"""\
{_HEADER}
// Transitive equality test
aligner TransEqAligner {{
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
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(lidar1.observation_time == lidar2.observation_time);
    require(lidar2.observation_time == camera.observation_time);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(source, "trans_eq_stn", "TransEqAligner", fs_importer)

        l1 = _fa("lidar1", "observation_time")
        l2 = _fa("lidar2", "observation_time")
        cam = _fa("camera", "observation_time")

        assert result.are_equal(l1, cam)
        assert result.equality_class_of(l1) == frozenset({l1, l2, cam})


class TestMultipleEqualityClasses:
    """Separate equality groups remain distinct."""

    def test_two_separate_classes(self, fs_importer: FilesystemImporter) -> None:
        """Two pairs produce two equality classes."""
        source = f"""\
{_HEADER}
// Multiple equality classes
aligner MultiEqAligner {{
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
    }}
    assume(is_strictly_increasing(lidar1.observation_time));
    assume(is_strictly_increasing(lidar2.observation_time));
    assume(is_strictly_increasing(camera1.observation_time));
    assume(is_strictly_increasing(camera2.observation_time));
    require(lidar1.observation_time == lidar2.observation_time);
    require(camera1.observation_time == camera2.observation_time);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(source, "multi_eq_stn", "MultiEqAligner", fs_importer)

        l1 = _fa("lidar1", "observation_time")
        l2 = _fa("lidar2", "observation_time")
        c1 = _fa("camera1", "observation_time")
        c2 = _fa("camera2", "observation_time")

        assert result.equality_class_of(l1) == frozenset({l1, l2})
        assert result.equality_class_of(c1) == frozenset({c1, c2})
        assert result.equality_class_of(l1) != result.equality_class_of(c1)


class TestConsistency:
    """Constraint consistency checking."""

    def test_negative_cycle_raises(self) -> None:
        """Directly constructed negative-bound constraints trigger InconsistentConstraintsError.

        Not reachable through the DSL today due to limited constraint expression patterns; tests forward-compatibility.
        """
        fa_a = _fa("a", "x")
        fa_b = _fa("b", "x")

        neg_bound = UnitValue.make(value=Decimal(-1), unit=SECONDS)
        placeholder_expr: dfl.Expr = MagicMock(spec=dfl.Ref)

        constraints = [
            DifferenceConstraint(minuend=fa_a, subtrahend=fa_b, bound=neg_bound, source_expr=placeholder_expr),
            DifferenceConstraint(minuend=fa_b, subtrahend=fa_a, bound=neg_bound, source_expr=placeholder_expr),
        ]

        with pytest.raises(InconsistentConstraintsError, match="negative cycle"):
            analyze_stn(constraints)


class TestEdgeCases:
    """Edge cases for STN analysis."""

    def test_empty_constraints(self) -> None:
        """No constraints produces an empty STN."""
        result = analyze_stn([])
        assert result.nodes == ()
        assert result.distance_matrix == ()
        assert result.equality_classes == ()
        assert result.edges == ()

    def test_tighter_bound_wins(self, fs_importer: FilesystemImporter) -> None:
        """Multiple constraints on the same pair: tightest bound prevails."""
        source = f"""\
{_HEADER}
// Tighter bound test
aligner TighterAligner {{
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
    require(|lidar.observation_time - camera.observation_time| <= 50ms);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(source, "tighter_stn", "TighterAligner", fs_importer)

        lidar = _fa("lidar", "observation_time")
        camera = _fa("camera", "observation_time")

        tb_lc = result.tightest_bound(lidar, camera)
        assert tb_lc is not None
        assert tb_lc.value == Decimal("0.05")
        tb_cl = result.tightest_bound(camera, lidar)
        assert tb_cl is not None
        assert tb_cl.value == Decimal("0.05")

    def test_node_not_in_stn_raises(self) -> None:
        """Querying a node not in the STN raises RuntimeError."""
        result = analyze_stn([])
        with pytest.raises(RuntimeError, match="not in the STN"):
            result.tightest_bound(_fa("x", "y"), _fa("a", "b"))

    def test_equality_class_of_missing_node_raises(self) -> None:
        """equality_class_of for a missing node raises RuntimeError."""
        result = analyze_stn([])
        with pytest.raises(RuntimeError, match="not in the STN"):
            result.equality_class_of(_fa("x", "y"))


class TestNonDecreasingAdmission:
    """``is_non_decreasing`` fields participate in STN analysis like strictly-increasing ones."""

    def test_non_decreasing_equality_class(self, fs_importer: FilesystemImporter) -> None:
        """Two non-decreasing fields related by equality form an equality class in the STN."""
        source = f"""\
{_HEADER}
// ND equality → STN class
aligner NdEqStn {{
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
        result = _compile_and_analyze(source, "nd_eq_stn", "NdEqStn", fs_importer)

        lidar = _fa("lidar", "observation_time")
        camera = _fa("camera", "observation_time")

        assert result.are_equal(lidar, camera)
        assert result.equality_class_of(lidar) == frozenset({lidar, camera})

    def test_mixed_nd_and_strict_admission(self, fs_importer: FilesystemImporter) -> None:
        """Mixing is_non_decreasing and is_strictly_increasing produces a joint equality class."""
        source = f"""\
{_HEADER}
// Mixed sorted variants in one STN
aligner MixedSortedStn {{
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
    assume(is_strictly_increasing(camera.observation_time));
    require(lidar.observation_time == camera.observation_time);
}}
{_SCHEMAS}
"""
        result = _compile_and_analyze(source, "mixed_sorted_stn", "MixedSortedStn", fs_importer)

        lidar = _fa("lidar", "observation_time")
        camera = _fa("camera", "observation_time")

        assert result.are_equal(lidar, camera)
