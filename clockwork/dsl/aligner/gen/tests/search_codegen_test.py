# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for C++ search code generation."""

from __future__ import annotations

import os
from pathlib import Path
from typing import TYPE_CHECKING, cast
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.aligner.extract_specs import base_input_name
from clockwork.dsl.aligner.gen.codegen_plan import CodegenLevel, CodegenPlan
from clockwork.dsl.aligner.gen.search_codegen import (
    _member_level_index_map,
    _objective_max_member_level_index,
    generate_search_code,
)
from clockwork.dsl.aligner.objective_analysis import SearchType
from clockwork.dsl.aligner.pipeline import analyze_aligner, compute_codegen_plan
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from clockwork.dsl.aligner.partition_types import PartitionComponent
    from clockwork.dsl.compiler_context import CompilerContext

# Set to True and ``bazel run`` to update expectation files. Review the diff
# before committing!
UPDATE_EXPECTATIONS = False

RESOURCES_DIR = Path(__file__).parent / "resources"


def _get_workspace_root() -> Path:
    """Return the Bazel workspace root (requires ``bazel run``)."""
    if "BUILD_WORKSPACE_DIRECTORY" in os.environ:
        return Path(os.environ["BUILD_WORKSPACE_DIRECTORY"])

    path = Path(__file__).absolute()
    while path != Path("/"):
        if (path / "MODULE.bazel").is_file():
            return path
        path = path.parent

    msg = "Could not find workspace root. Run with 'bazel run' to update expectation files."
    raise RuntimeError(msg)


def _load_expected(name: str) -> str:
    """Load expected output from a resource file."""
    filepath = RESOURCES_DIR / f"{name}.txt"
    return filepath.read_text().strip()


def _write_expected(name: str, content: str) -> None:
    """Write expected output to the *source* resource file (not the sandbox)."""
    if not UPDATE_EXPECTATIONS:
        return
    workspace_root = _get_workspace_root()
    resources_dir = workspace_root / "clockwork/dsl/aligner/gen/tests/resources"
    resources_dir.mkdir(parents=True, exist_ok=True)
    filepath = resources_dir / f"{name}.txt"
    filepath.write_text(content.strip() + "\n")


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
// Pose measurement
schema PoseMeasurement {
    uuid: 44444444-3333-3333-3333-333333333333;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
    }
}
"""


def _compile_and_plan(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> tuple[CodegenPlan, CompilerContext]:
    """End-to-end: compile -> type-check -> pipeline -> codegen plan.

    Returns the plan and compiler context needed by generate_search_code.
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    resolved = aligner_ir.resolved
    assert resolved is not None
    type_check_aligner(aligner_ir, module.context)
    analysis = analyze_aligner(aligner_ir)
    plan = compute_codegen_plan(analysis, resolved)
    return plan, module.context


def _gen(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> str:
    """Helper: compile + plan + generate, return code as a single string."""
    plan, compiler_context = _compile_and_plan(source, module_name, aligner_name, fs_importer)
    lines = generate_search_code(
        plan=plan,
        dial_type=f"{aligner_name}Dial",
        compiler_context=compiler_context,
    )
    return "\n".join(lines)


_TWO_INPUT_SOURCE = f"""\
{_HEADER}
// Two-input aligner
aligner TwoInput {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""

_KITCHEN_SINK_SOURCE = f"""\
{_HEADER}
use std::aligners::state;
// Kitchen-sink aligner: optional, NEAREST, reuse, reverse scan, and batch input
aligner KitchenSink {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar sensor (optional with timeout → ANY_MATCH)
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 200ms;
        }}
        // Pose sensor (batch: 2-4 messages per alignment)
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 4];
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 200ms);
    require(min(pose.observation_time) - lidar.observation_time <= 150ms);
    require(radar.observation_time - max(pose.observation_time) <= 300ms);
    require(camera.observation_time - radar.observation_time <= 300ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(pose.observation_time));
    if has_candidates(radar)
        then minimize(|lidar.observation_time - camera.observation_time|
                    + |lidar.observation_time - radar.observation_time|)
        else minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""

_MIXED_FIELDS_SCHEMAS = """\
// Sensor with timestamp and sequence number
schema SensorMsg {
    uuid: 44444444-4444-4444-4444-444444444444;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
        // Monotonically increasing sequence number
        #1 seq_num: Int64;
    }
}
// Another sensor with the same fields
schema OtherSensorMsg {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Observation timestamp
        #0 observation_time: SyncTime;
        // Monotonically increasing sequence number
        #1 seq_num: Int64;
    }
}
"""

_MIXED_CONSTRAINTS_SOURCE = f"""\
{_HEADER}
// Aligner with mixed constraint types: time bound, integer bound, equality
aligner MixedConstraints {{
    inputs {{
        // Primary sensor
        primary: Tappy<SensorMsg>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Secondary sensor
        secondary: Tappy<OtherSensorMsg>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Tertiary sensor
        tertiary: Tappy<SensorMsg>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(|primary.observation_time - secondary.observation_time| <= 50ms);
    require(|primary.seq_num - secondary.seq_num| <= 5);
    require(tertiary.seq_num == secondary.seq_num);
    require(secondary.observation_time - tertiary.observation_time <= -100ms);
    assume(is_strictly_increasing(primary.observation_time));
    assume(is_strictly_increasing(secondary.observation_time));
    assume(is_strictly_increasing(tertiary.observation_time));
    assume(is_strictly_increasing(primary.seq_num));
    assume(is_strictly_increasing(secondary.seq_num));
    maximize(primary.observation_time);
    minimize(|primary.observation_time - secondary.observation_time|);
}}
{_MIXED_FIELDS_SCHEMAS}
"""

_ANY_MATCH_SOURCE = f"""\
{_HEADER}
// No-objective aligner: both levels have ANY_MATCH
aligner NoObjective {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
}}
{_SCHEMAS}
"""

_LAST_IN_RANGE_SOURCE = f"""\
{_HEADER}
// Maximize objective: inner level has LAST_IN_RANGE (reverse scan)
aligner LatestCamera {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    maximize(camera.observation_time);
}}
{_SCHEMAS}
"""

_FIRST_IN_RANGE_SOURCE = f"""\
{_HEADER}
// Minimize single-field objective: inner level has FIRST_IN_RANGE
aligner EarliestCamera {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    minimize(camera.observation_time);
}}
{_SCHEMAS}
"""

_REUSE_SOURCE = f"""\
{_HEADER}
// Reuse test: camera input uses get_view() instead of get_cursor_view()
aligner WithReuse {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor with reuse
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            reuse: true;
        }}
    }}

    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""

_OPTIONAL_REFERENCE_NEAREST_SOURCE = f"""\
{_HEADER}
// Optional reference inputs for NEAREST search
aligner OptionalReferenceNearest {{
    inputs {{
        // Required tick sensor
        tick: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Primary anchor sensor
        anchor: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
        }}
        // Fallback anchor sensor
        fallback_anchor: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
        }}
        // Target sensor
        target: Tappy<CameraImage>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
    }}

    assume(is_strictly_increasing(tick.observation_time));
    assume(is_strictly_increasing(anchor.observation_time));
    assume(is_strictly_increasing(fallback_anchor.observation_time));
    assume(is_strictly_increasing(target.observation_time));
    require(anchor.observation_time == tick.observation_time);
    require(fallback_anchor.observation_time == tick.observation_time);
    require(|target.observation_time - anchor.observation_time| <= 100ms);
    require(|target.observation_time - fallback_anchor.observation_time| <= 100ms);
    minimize(|target.observation_time - anchor.observation_time|);
    minimize(|target.observation_time - fallback_anchor.observation_time|);
}}
{_SCHEMAS}
"""

_OPTIONAL_BATCH_SOURCE = f"""\
{_HEADER}
// Optional batch input aligner
aligner OptionalBatch {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional pose batch
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(pose.observation_time));
    require(min(pose.observation_time) - lidar.observation_time <= 100ms);
    require(lidar.observation_time - max(pose.observation_time) <= 100ms);
}}
{_SCHEMAS}
"""

_OPTIONAL_BATCH_REFERENCE_NEAREST_SOURCE = f"""\
{_HEADER}
// Optional batch reference input for NEAREST search
aligner OptionalBatchReferenceNearest {{
    inputs {{
        // Required tick sensor
        tick: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional pose batch
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
    }}

    assume(is_strictly_increasing(tick.observation_time));
    assume(is_strictly_increasing(pose.observation_time));
    require(min(pose.observation_time) == tick.observation_time);
    minimize(|max(pose.observation_time) - min(pose.observation_time)|);
}}
{_SCHEMAS}
"""

_BATCH_BOUNDARY_OBJECTIVES_SOURCE = f"""\
{_HEADER}
// Batch objective aligner
aligner BatchBoundaryObjectives {{
    inputs {{
        // Pose batch
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [1, 4];
        }}
    }}

    assume(is_strictly_increasing(pose.observation_time));
    minimize(min(pose.observation_time));
    maximize(max(pose.observation_time));
}}
{_SCHEMAS}
"""

_TIE_AWARE_PRUNE_SOURCE = f"""\
{_HEADER}
// Optional batch objective aligner with downstream tie-break
aligner TieAwarePrune {{
    inputs {{
        // Optional pose batch
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
        // Optional tracks batch
        tracks: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
    }}

    assume(is_strictly_increasing(pose.observation_time));
    assume(is_strictly_increasing(tracks.observation_time));
    maximize(max(pose.observation_time));
    maximize(max(tracks.observation_time));
    minimize(|max(pose.observation_time) - max(tracks.observation_time)|);
}}
{_SCHEMAS}
"""

_NO_DOMINATING_PRUNE_SOURCE = f"""\
{_HEADER}
// Downstream objective appears first, so current-level pruning is unsafe
aligner NoDominatingPrune {{
    inputs {{
        // Optional pose batch
        pose: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
        // Optional tracks batch
        tracks: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
            batch_size: [1, 4];
        }}
    }}

    assume(is_strictly_increasing(pose.observation_time));
    assume(is_strictly_increasing(tracks.observation_time));
    maximize(max(tracks.observation_time));
    maximize(max(pose.observation_time));
    minimize(|max(pose.observation_time) - max(tracks.observation_time)|);
}}
{_SCHEMAS}
"""

_PARTITIONED_SUFFIX_SOURCE = f"""\
{_HEADER}
// Independent optional suffix components after a required prefix
aligner PartitionedSuffix {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
        // Radar sensor
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
    }}

    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    require(camera.observation_time <= lidar.observation_time);
    require(radar.observation_time <= lidar.observation_time);
    maximize(camera.observation_time);
    maximize(radar.observation_time);
}}
{_SCHEMAS}
"""

_NESTED_PARTITION_SOURCE = f"""\
{_HEADER}
// Connected root component that splits again after its prefix is bound
aligner NestedPartition {{
    inputs {{
        // Required anchor
        anchor: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Left optional input
        left: Tappy<CameraImage>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
        // Right optional input
        right: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
        // Independent optional input
        solo: Tappy<PoseMeasurement>
        {{
            max_msgs: 10;
            optional: true;
            timeout: 0ms;
        }}
    }}

    assume(is_strictly_increasing(anchor.observation_time));
    assume(is_strictly_increasing(left.observation_time));
    assume(is_strictly_increasing(right.observation_time));
    assume(is_strictly_increasing(solo.observation_time));
    require(left.observation_time <= anchor.observation_time);
    require(right.observation_time <= anchor.observation_time);
    maximize(left.observation_time);
    maximize(right.observation_time);
    maximize(solo.observation_time);
}}
{_SCHEMAS}
"""

EXPECTED_TWO_INPUT = _load_expected("expected_two_input_search")
EXPECTED_KITCHEN_SINK = _load_expected("expected_kitchen_sink_search")
EXPECTED_MIXED = _load_expected("expected_mixed_constraints_search")
EXPECTED_ANY_MATCH = _load_expected("expected_any_match_search")
EXPECTED_LAST_IN_RANGE = _load_expected("expected_last_in_range_search")
EXPECTED_FIRST_IN_RANGE = _load_expected("expected_first_in_range_search")
EXPECTED_REUSE = _load_expected("expected_reuse_search")


def test_two_input_golden(fs_importer: FilesystemImporter) -> None:
    """Two-input aligner search code matches golden file."""
    actual = _gen(_TWO_INPUT_SOURCE, "sc_golden_two", "TwoInput", fs_importer)
    _write_expected("expected_two_input_search", actual)
    assert actual.strip() == EXPECTED_TWO_INPUT


def test_kitchen_sink_golden(fs_importer: FilesystemImporter) -> None:
    """Kitchen-sink aligner: optional, NEAREST, reuse, batch — matches golden file."""
    actual = _gen(_KITCHEN_SINK_SOURCE, "sc_golden_kitchen", "KitchenSink", fs_importer)
    _write_expected("expected_kitchen_sink_search", actual)
    assert "suffix_optionals_present_" not in actual
    assert actual.strip() == EXPECTED_KITCHEN_SINK


def test_mixed_constraints_golden(fs_importer: FilesystemImporter) -> None:
    """Mixed constraint types (time, integer, equality) match golden file."""
    actual = _gen(_MIXED_CONSTRAINTS_SOURCE, "sc_mixed", "MixedConstraints", fs_importer)
    _write_expected("expected_mixed_constraints_search", actual)
    assert actual.strip() == EXPECTED_MIXED


def test_non_time_window_requires_specialized_numeric_limits(fs_importer: FilesystemImporter) -> None:
    """Non-time range-search fields should require specialized numeric limits."""
    actual = _gen(_MIXED_CONSTRAINTS_SOURCE, "sc_numeric_limits", "MixedConstraints", fs_importer)

    assert (
        "static_assert(std::numeric_limits<std::decay_t<decltype((*view.begin()).get_seq_num())>>::is_specialized, "
        '"Aligner range-search field types must specialize std::numeric_limits");'
    ) in actual


def test_any_match_golden(fs_importer: FilesystemImporter) -> None:
    """No-objective aligner: both levels use ANY_MATCH forward scan."""
    actual = _gen(_ANY_MATCH_SOURCE, "sc_any", "NoObjective", fs_importer)
    _write_expected("expected_any_match_search", actual)
    assert actual.strip() == EXPECTED_ANY_MATCH


def test_last_in_range_golden(fs_importer: FilesystemImporter) -> None:
    """Maximize objective: inner level uses LAST_IN_RANGE reverse scan."""
    actual = _gen(_LAST_IN_RANGE_SOURCE, "sc_lir", "LatestCamera", fs_importer)
    _write_expected("expected_last_in_range_search", actual)
    assert actual.strip() == EXPECTED_LAST_IN_RANGE


def test_first_in_range_golden(fs_importer: FilesystemImporter) -> None:
    """Minimize single-field: inner level uses FIRST_IN_RANGE forward scan."""
    actual = _gen(_FIRST_IN_RANGE_SOURCE, "sc_fir", "EarliestCamera", fs_importer)
    _write_expected("expected_first_in_range_search", actual)
    assert actual.strip() == EXPECTED_FIRST_IN_RANGE


def test_reuse_golden(fs_importer: FilesystemImporter) -> None:
    """Reuse flag: reused input (camera) uses get_view() instead of get_cursor_view()."""
    actual = _gen(_REUSE_SOURCE, "sc_reuse", "WithReuse", fs_importer)
    _write_expected("expected_reuse_search", actual)
    assert actual.strip() == EXPECTED_REUSE


def test_search_levels_use_cached_input_views(fs_importer: FilesystemImporter) -> None:
    """Search levels should use per-execute cached views instead of input handles."""
    actual = _gen(_REUSE_SOURCE, "sc_cached_views", "WithReuse", fs_importer)

    assert "struct InputViews {" in actual
    assert "decltype(std::declval<WithReuseDialInputs&>().get_lidar().get_cursor_view())" in actual
    assert "decltype(std::declval<WithReuseDialInputs&>().get_camera().get_view())" in actual
    assert "const auto views = make_input_views(inputs);" in actual
    assert "const InputViews& views)" in actual
    assert "const auto& view = views.lidar_view;" in actual
    assert "const auto& view = views.camera_view;" in actual
    assert "const auto& view = inputs.get_lidar().get_cursor_view();" not in actual
    assert "const auto& view = inputs.get_camera().get_view();" not in actual


def test_nearest_search_unwraps_optional_reference(fs_importer: FilesystemImporter) -> None:
    """NEAREST search should unwrap optional refs and compare fallback candidates."""
    plan, compiler_context = _compile_and_plan(
        _OPTIONAL_REFERENCE_NEAREST_SOURCE,
        "sc_optional_reference_nearest",
        "OptionalReferenceNearest",
        fs_importer,
    )
    actual = "\n".join(generate_search_code(plan, "OptionalReferenceNearestDial", compiler_context))

    target_level = _member_level_index_map(plan)["target_it"]
    assert plan.levels[target_level].join_level.search_type == SearchType.NEAREST

    target_block = _search_level_block(actual, target_level)
    assert "if (candidate_anchor_it(*candidate).has_value()) {" in target_block
    assert "const auto ref = (*candidate_anchor_it(*candidate).value()).get_observation_time();" in target_block
    assert "const auto ref = (*candidate_anchor_it(*candidate)).get_observation_time();" not in target_block
    assert "Reference optional is absent; scan feasible candidates and compare active objectives." in target_block
    assert "std::optional<BestSuffix" in target_block
    assert "is_better_than_best_suffix" in target_block


def test_nearest_search_guards_optional_batch_reference_member(fs_importer: FilesystemImporter) -> None:
    """NEAREST search should guard the referenced optional batch boundary."""
    plan, compiler_context = _compile_and_plan(
        _OPTIONAL_BATCH_REFERENCE_NEAREST_SOURCE,
        "sc_optional_batch_reference_nearest",
        "OptionalBatchReferenceNearest",
        fs_importer,
    )
    actual = "\n".join(generate_search_code(plan, "OptionalBatchReferenceNearestDial", compiler_context))

    pose_hi_level = _member_level_index_map(plan)["pose_hi_it"]
    assert plan.levels[pose_hi_level].join_level.search_type == SearchType.NEAREST

    pose_hi_block = _search_level_block(actual, pose_hi_level)
    assert "if (candidate_pose_lo_it(*candidate).has_value()) {" in pose_hi_block
    assert "const auto ref = (*candidate_pose_lo_it(*candidate).value()).get_observation_time();" in pose_hi_block
    assert (
        "candidate_pose_lo_it(*candidate).has_value() && candidate_pose_hi_it(*candidate).has_value()"
        not in pose_hi_block
    )


def test_optional_batch_uses_optional_boundary_iterators(fs_importer: FilesystemImporter) -> None:
    """Optional batch inputs should store optional low/high boundary iterators."""
    actual = _gen(_OPTIONAL_BATCH_SOURCE, "sc_optional_batch", "OptionalBatch", fs_importer)

    assert "std::optional<PoseIt> pose_lo_it;" in actual
    assert "std::optional<PoseIt> pose_hi_it;" in actual
    assert (
        "const bool is_present = candidate_pose_lo_it(candidate).has_value() "
        "&& candidate_pose_hi_it(candidate).has_value();"
    ) in actual
    assert "refresh_pose_presence(*candidate);" in actual
    assert (
        "set_has_pose(candidate_pose_lo_it(candidate).has_value() && candidate_pose_hi_it(candidate).has_value())"
    ) in actual
    assert "const auto clamp_index_offset = [&view](auto ref, auto offset) {" in actual
    assert "auto it_start = candidate_pose_lo_it(*candidate).value();" in actual
    assert "auto it_end = clamp_index_offset(candidate_pose_lo_it(*candidate).value(), 4);" in actual
    assert "// Linear field narrowing for tiny IndexInView range." in actual
    assert "candidate_pose_it" not in actual


def test_batch_boundary_objective_levels_use_concrete_members(fs_importer: FilesystemImporter) -> None:
    """Batch objectives should track low/high iterator members independently."""
    plan, _ = _compile_and_plan(
        _BATCH_BOUNDARY_OBJECTIVES_SOURCE,
        "sc_batch_boundary_objectives",
        "BatchBoundaryObjectives",
        fs_importer,
    )

    member_levels = _member_level_index_map(plan)
    objective_levels = tuple(_objective_max_member_level_index(obj, member_levels) for obj in plan.objectives)

    assert objective_levels == (member_levels["pose_lo_it"], member_levels["pose_hi_it"])


def _search_level_block(actual: str, level_index: int) -> str:
    """Return the generated C++ block for one search level."""
    start = actual.index(f"// Level {level_index}:")
    end = actual.find("\n// Level ", start + 1)
    if end == -1:
        end = actual.find("\nstatic void publish_alignment", start)
    return actual[start:end]


def _search_function_block(actual: str, function_name: str) -> str:
    """Return the generated C++ block for one named search function."""
    signature = f"static jewels::BinaryOutcome {function_name}("
    search_from = 0
    while True:
        function_start = actual.index(signature, search_from)
        declaration_end = actual.find("\n\n", function_start)
        if declaration_end == -1:
            declaration_end = len(actual)
        body_start = actual.find("{", function_start, declaration_end)
        semicolon = actual.find(";", function_start, declaration_end)
        if body_start != -1 and (semicolon == -1 or body_start < semicolon):
            break
        search_from = function_start + len(signature)
    start = function_start
    comment_start = actual.rfind("\n// Level ", 0, function_start)
    if comment_start != -1:
        start = comment_start + 1
    end = actual.find("\nstatic jewels::BinaryOutcome ", function_start + 1)
    if end == -1:
        end = actual.find("\nstatic void publish_alignment", start)
    return actual[start:end]


def _struct_block(actual: str, struct_name: str) -> str:
    """Return the generated C++ block for one named struct."""
    start = actual.index(f"struct {struct_name} {{")
    end = actual.index("};", start)
    return actual[start : end + len("};")]


def _component_base_set(plan: CodegenPlan, component: PartitionComponent) -> frozenset[str]:
    """Return base-input names for a partition component."""
    return frozenset(base_input_name(plan.levels[index].join_level.input) for index in component.level_indices)


def test_tie_aware_pruning_for_dominating_current_objective(fs_importer: FilesystemImporter) -> None:
    """Prune after leaving the current objective tie group, not before."""
    plan, compiler_context = _compile_and_plan(
        _TIE_AWARE_PRUNE_SOURCE,
        "sc_tie_aware_prune",
        "TieAwarePrune",
        fs_importer,
    )
    actual = "\n".join(generate_search_code(plan, "TieAwarePruneDial", compiler_context))

    pose_hi_level = _member_level_index_map(plan)["pose_hi_it"]
    level_block = _search_function_block(actual, f"search_level_{pose_hi_level}")
    suffix_guard = f"suffix_optionals_present_{pose_hi_level}"

    assert f"static bool {suffix_guard}(const PresenceState& presence)" in actual
    assert "presence.has_pose()" in actual
    assert "presence.has_tracks()" in actual
    assert f"best.has_value() && {suffix_guard}(best->presence)" in level_block
    assert "PresenceState::optional_count" not in level_block
    assert "&& objective_0_is_active((*candidate).presence)" in level_block
    assert "&& objective_0_is_active(best->presence)" in level_block
    assert "const auto my_prune_obj = objective_0_value(*candidate);" in level_block
    assert "if (my_prune_obj != best_prune_obj) {" in level_block
    assert "if (!best.has_value()) {" in level_block


def test_no_tie_aware_pruning_when_first_varying_objective_is_downstream(
    fs_importer: FilesystemImporter,
) -> None:
    """Do not prune when a downstream objective can beat the current level."""
    plan, compiler_context = _compile_and_plan(
        _NO_DOMINATING_PRUNE_SOURCE,
        "sc_no_dominating_prune",
        "NoDominatingPrune",
        fs_importer,
    )
    actual = "\n".join(generate_search_code(plan, "NoDominatingPruneDial", compiler_context))

    pose_hi_level = _member_level_index_map(plan)["pose_hi_it"]
    level_block = _search_level_block(actual, pose_hi_level)

    assert "my_prune_obj" not in level_block
    assert "best_prune_obj" not in level_block


def test_partitioned_suffix_emits_component_searches(fs_importer: FilesystemImporter) -> None:
    """Independent residual suffix components should be composed without nested recursion."""
    plan, compiler_context = _compile_and_plan(
        _PARTITIONED_SUFFIX_SOURCE,
        "sc_partitioned_suffix",
        "PartitionedSuffix",
        fs_importer,
    )
    assert plan.partition_plan is not None
    partition = plan.partition_plan.partition_starting_at(1)
    assert partition is not None
    assert len(partition.components) == 2

    actual = "\n".join(generate_search_code(plan, "PartitionedSuffixDial", compiler_context))
    level_0_block = _search_function_block(actual, "search_level_0")

    assert "search_partition_1(jewels::InOut{*candidate}, views)" in level_0_block
    assert "static jewels::BinaryOutcome search_partition_1(" in actual

    component_names = [
        f"search_component_{partition.partition_id}_{component.component_id}_{component.level_indices[0]}"
        for component in partition.components
    ]
    for component_name in component_names:
        component_block = _search_function_block(actual, component_name)
        other_component_names = [name for name in component_names if name != component_name]
        assert all(name not in component_block for name in other_component_names)


def test_partitioned_component_enters_nested_partition(fs_importer: FilesystemImporter) -> None:
    """A component with a disconnected residual tail should enter a nested partition."""
    plan, compiler_context = _compile_and_plan(
        _NESTED_PARTITION_SOURCE,
        "sc_nested_partition",
        "NestedPartition",
        fs_importer,
    )
    assert plan.partition_plan is not None

    root_scope = tuple(level.level_index for level in plan.levels)
    root_partition = plan.partition_plan.partition_for_scope(root_scope)
    assert root_partition is not None
    connected_component = next(
        component
        for component in root_partition.components
        if _component_base_set(plan, component) == frozenset({"anchor", "left", "right"})
    )
    nested_scope = connected_component.level_indices[1:]
    nested_partition = plan.partition_plan.partition_for_scope(nested_scope)
    assert nested_partition is not None

    actual = "\n".join(generate_search_code(plan, "NestedPartitionDial", compiler_context))
    component_entry_name = (
        f"search_component_{root_partition.partition_id}_"
        f"{connected_component.component_id}_{connected_component.level_indices[0]}"
    )
    component_entry_block = _search_function_block(actual, component_entry_name)

    assert f"search_partition_{nested_partition.start_level_index}(jewels::InOut{{*candidate}}, views)" in (
        component_entry_block
    )
    assert f"static jewels::BinaryOutcome search_partition_{nested_partition.start_level_index}(" in actual
    assert (
        f"search_component_{root_partition.partition_id}_{connected_component.component_id}_{nested_scope[0]}("
        not in component_entry_block
    )
    component_best_type = (
        f"BestComponentP{root_partition.partition_id}C"
        f"{connected_component.component_id}L{connected_component.level_indices[0]}"
    )
    component_best_block = _struct_block(actual, component_best_type)
    assert "[[no_unique_address]] PresenceState presence;" in component_best_block


class TestValidation:
    """Tests for input validation / unsupported feature rejection."""

    def test_rejects_nearest_without_reference(self) -> None:
        """Raise ValueError when NEAREST level has no nearest_reference."""
        join_level = MagicMock()
        join_level.search_type = SearchType.NEAREST
        join_level.input = "test_input"
        join_level.nearest_reference = None

        level = CodegenLevel(
            level_index=0,
            join_level=join_level,
            reuse=False,
            direction=MagicMock(),
            unconditional_checks=(),
            conditional_checks={},
            initial_windows=(),
            has_downstream_optionals=False,
        )
        plan = CodegenPlan(
            aligner_name="test",
            levels=(level,),
            input_names=("test_input",),
            optional_input_names=frozenset(),
            batch_input_names=frozenset(),
            optional_timeouts={},
            objectives=(),
            time_fields=frozenset(),
            specs=MagicMock(),
        )
        with pytest.raises(ValueError, match="NEAREST search type at level 0"):
            generate_search_code(plan=plan, dial_type="TestDial", compiler_context=cast("CompilerContext", MagicMock()))


class TestNonDecreasingGeneratedCppIdentical:
    """Generated C++ is identical for ``is_non_decreasing`` and ``is_strictly_increasing``.

    This is the safety invariant: today, ND and strict differ only in
    classification and produce byte-identical C++ for the same aligner.
    """

    def test_bounded_difference_cpp_identical(self, fs_importer: FilesystemImporter) -> None:
        """|a - b| <= d emits identical C++ for strict and ND variants."""
        strict = f"""\
{_HEADER}
// code
aligner Pair {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        nd = f"""\
{_HEADER}
// code
aligner Pair {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    assume(is_non_decreasing(lidar.observation_time));
    assume(is_non_decreasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        assert _gen(strict, "sc_nd_strict", "Pair", fs_importer) == _gen(nd, "sc_nd_nd", "Pair", fs_importer)

    def test_equality_on_sorted_target_cpp_identical(self, fs_importer: FilesystemImporter) -> None:
        """require(a == b) on sorted target emits identical C++ for strict and ND variants."""
        strict = f"""\
{_HEADER}
// code
aligner EqPair {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(lidar.observation_time == camera.observation_time);
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        nd = f"""\
{_HEADER}
// code
aligner EqPair {{
    inputs {{
        // Lidar sensor
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera sensor
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    require(lidar.observation_time == camera.observation_time);
    assume(is_non_decreasing(lidar.observation_time));
    assume(is_non_decreasing(camera.observation_time));
    minimize(|lidar.observation_time - camera.observation_time|);
}}
{_SCHEMAS}
"""
        assert _gen(strict, "sc_nd_eq_strict", "EqPair", fs_importer) == _gen(nd, "sc_nd_eq_nd", "EqPair", fs_importer)
