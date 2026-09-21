# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for the unified aligner analysis pipeline."""

from __future__ import annotations

from decimal import Decimal
from unittest.mock import patch

import pytest
from clockwork.dsl.aligner import pipeline as pipeline_module
from clockwork.dsl.aligner.extract_specs import FieldAccessor, FirstInBatch, IndexInView, LastInBatch
from clockwork.dsl.aligner.objective_analysis import SearchType
from clockwork.dsl.aligner.pipeline import (
    AlignerAnalysis,
    PipelineError,
    analyze_aligner,
)
from clockwork.dsl.aligner.presence import (
    MAX_OPTIONAL_INPUTS,
    PresenceConfig,
)
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


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
"""


def _compile_and_analyze(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> AlignerAnalysis:
    """Compile source, resolve aligner, type-check, and run the full analysis pipeline."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    assert aligner_ir.resolved is not None
    type_check_aligner(aligner_ir, module.context)
    return analyze_aligner(aligner_ir)


class TestNoOptionals:
    """Pipeline with no optional inputs produces exactly one config."""

    def test_single_config(self, fs_importer: FilesystemImporter) -> None:
        """No optionals → one config with empty present_optionals."""
        source = f"""\
{_HEADER}
// Two required inputs
aligner TwoRequired {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera input
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
        analysis = _compile_and_analyze(source, "no_opt_single", "TwoRequired", fs_importer)
        assert len(analysis.config_analyses) == 1
        assert analysis.optional_inputs == frozenset()

        config = PresenceConfig(present_optionals=frozenset())
        assert config in analysis.config_analyses
        ca = analysis.config_analyses[config]
        assert len(ca.join_plan.levels) == 2
        reps = {lev.input for lev in ca.join_plan.levels}
        assert reps == {"lidar", "camera"}

    def test_three_inputs_all_required(self, fs_importer: FilesystemImporter) -> None:
        """Three required inputs produce a three-level plan."""
        source = f"""\
{_HEADER}
// Three required inputs
aligner ThreeRequired {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "no_opt_three", "ThreeRequired", fs_importer)
        assert len(analysis.config_analyses) == 1
        ca = analysis.config_analyses[PresenceConfig(frozenset())]
        assert len(ca.join_plan.levels) == 3


class TestOneOptional:
    """Pipeline with one optional input uses the all-present config for codegen."""

    _SOURCE = f"""\
{_HEADER}
// One optional input
aligner OneOptional {{
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
    assume(is_strictly_increasing(radar.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""

    @pytest.fixture(scope="class")
    def analysis(self, fs_importer: FilesystemImporter) -> AlignerAnalysis:
        """Compile and analyze the one-optional aligner once per class."""
        return _compile_and_analyze(self._SOURCE, "one_opt", "OneOptional", fs_importer)

    def test_only_all_present_config(self, analysis: AlignerAnalysis) -> None:
        """One optional → one all-present config for codegen."""
        assert len(analysis.config_analyses) == 1
        assert analysis.optional_inputs == frozenset({"radar"})
        assert PresenceConfig(frozenset({"radar"})) in analysis.config_analyses

    def test_all_present_config_has_all_levels(self, analysis: AlignerAnalysis) -> None:
        """The representative config contains all inputs for runtime present/absent branching."""
        present = analysis.config_analyses[PresenceConfig(frozenset({"radar"}))]

        assert len(present.join_plan.levels) == 3
        present_reps = {lev.input for lev in present.join_plan.levels}
        assert present_reps == {"lidar", "camera", "radar"}

    def test_all_present_config_includes_optional_constraints(self, analysis: AlignerAnalysis) -> None:
        """When radar is present in the representative config, its constraints appear in the STN."""
        present = analysis.config_analyses[PresenceConfig(frozenset({"radar"}))]

        present_input_names = {n.input_name for n in present.stn.nodes}
        assert "radar" in present_input_names


class TestTwoOptionals:
    """Pipeline with two optional inputs uses the all-present config for codegen."""

    def test_only_all_present_config(self, fs_importer: FilesystemImporter) -> None:
        """Two optionals → one representative config containing both optionals."""
        source = f"""\
{_HEADER}
// Two optional inputs
aligner TwoOptionals {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (optional)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
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
    assume(is_strictly_increasing(radar.observation_time));
    if has_candidates(camera)
        then require(|lidar.observation_time - camera.observation_time| <= 100ms)
        else require(true);
    if has_candidates(radar)
        then require(|lidar.observation_time - radar.observation_time| <= 100ms)
        else require(true);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "two_opt_count", "TwoOptionals", fs_importer)
        assert len(analysis.config_analyses) == 1
        assert analysis.optional_inputs == frozenset({"camera", "radar"})

        both = analysis.config_analyses[PresenceConfig(frozenset({"camera", "radar"}))]

        assert len(both.join_plan.levels) == 3


class TestAnalysisScaling:
    """Pipeline work scales with codegen requirements, not optional-subset count."""

    def test_many_auto_drop_optionals_analyzes_one_config(self, fs_importer: FilesystemImporter) -> None:
        """Many optionals without ``has_candidates`` do not create a 2^p config product."""
        optional_inputs = "\n".join(
            f"""\
        // Optional radar {idx}
        radar_{idx}: Tappy<RadarDetection>
        {{
            max_msgs: 2;
            arbitrary_selection: true;
            optional: true;
            timeout: 0ms;
        }}"""
            for idx in range(15)
        )
        optional_assumptions = "\n".join(
            f"    assume(is_strictly_increasing(radar_{idx}.observation_time));" for idx in range(15)
        )
        source = f"""\
{_HEADER}
// Many optional inputs without has_candidates conditionals
aligner ManyAutoDropOptionals {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 2;
            arbitrary_selection: true;
        }}
{optional_inputs}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
{optional_assumptions}
}}
{_SCHEMAS}
"""
        with patch("clockwork.dsl.aligner.pipeline._analyze_config", wraps=pipeline_module._analyze_config) as wrapped:
            analysis = _compile_and_analyze(source, "many_auto_drop_optionals", "ManyAutoDropOptionals", fs_importer)

        assert wrapped.call_count == 1
        assert len(analysis.optional_inputs) == 15
        assert len(analysis.config_analyses) == 1


class TestErrorPaths:
    """Error conditions raise appropriate exceptions."""

    def test_unresolved_aligner_raises(self, fs_importer: FilesystemImporter) -> None:
        """Calling analyze_aligner on an unresolved aligner raises PipelineError."""
        source = f"""\
{_HEADER}
// Unresolved test
aligner Unresolved {{
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
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "unresolved"), fs_importer)
        aligner_ir = module.inner_scope.lookup("Unresolved")
        assert isinstance(aligner_ir, aligner.Aligner)

        # Force unresolved state.
        aligner_ir.resolved = None
        with pytest.raises(PipelineError, match="must be resolved"):
            analyze_aligner(aligner_ir)

    def test_too_many_optionals_raises(self, fs_importer: FilesystemImporter) -> None:
        """Exceeding MAX_OPTIONAL_INPUTS raises PipelineError."""
        assert MAX_OPTIONAL_INPUTS == 16  # Sanity-check the constant value.

        source = f"""\
{_HEADER}
// Too many optionals
aligner TooManyOpt {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (optional)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    if has_candidates(camera)
        then require(|lidar.observation_time - camera.observation_time| <= 100ms)
        else require(true);
}}
{_SCHEMAS}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "too_many"), fs_importer)
        aligner_ir = module.inner_scope.lookup("TooManyOpt")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert aligner_ir.resolved is not None
        type_check_aligner(aligner_ir, module.context)

        with (
            patch("clockwork.dsl.aligner.pipeline.MAX_OPTIONAL_INPUTS", 0),
            pytest.raises(PipelineError, match="exceeding the maximum"),
        ):
            analyze_aligner(aligner_ir)


class TestSearchTypePreservation:
    """Verify that analysis correctly classifies search types."""

    def test_nearest_classification(self, fs_importer: FilesystemImporter) -> None:
        """An abs-diff minimization objective produces NEAREST search type."""
        source = f"""\
{_HEADER}
// Nearest search type
aligner NearestSearch {{
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
        analysis = _compile_and_analyze(source, "nearest", "NearestSearch", fs_importer)
        ca = analysis.config_analyses[PresenceConfig(frozenset())]

        # The second level (non-pivot) gets NEAREST from the minimize objective.
        assert len(ca.join_plan.levels) == 2
        assert ca.join_plan.levels[1].search_type == SearchType.NEAREST

    def test_last_in_range_classification(self, fs_importer: FilesystemImporter) -> None:
        """A maximize-field objective produces LAST_IN_RANGE search type."""
        source = f"""\
{_HEADER}
// Last-in-range search type
aligner LastInRange {{
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
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(vehicle_pose.tov));
    require(lidar.observation_time <= vehicle_pose.tov);
    maximize(vehicle_pose.tov);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "lir", "LastInRange", fs_importer)
        ca = analysis.config_analyses[PresenceConfig(frozenset())]
        level_by_input = {lev.input: lev for lev in ca.join_plan.levels}
        assert level_by_input["vehicle_pose"].search_type == SearchType.LAST_IN_RANGE
        assert level_by_input["lidar"].search_type == SearchType.ANY_MATCH


class TestAutoDropWithConditionalInteraction:
    """Auto-drop and has_candidates conditionals work together on different optionals."""

    def test_mixed_mechanisms(self, fs_importer: FilesystemImporter) -> None:
        """Bare constraints (auto-drop) and has_candidates on different optionals."""
        source = f"""\
{_HEADER}
// Auto-drop + has_candidates on different optionals
aligner MixedOptionals {{
    inputs {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (optional, with has_candidates)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Radar (optional, bare constraint — auto-drop)
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
    if has_candidates(camera)
        then require(|lidar.observation_time - camera.observation_time| <= 100ms)
        else require(true);
    require(|lidar.observation_time - radar.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "ad_mixed", "MixedOptionals", fs_importer)
        assert analysis.optional_inputs == frozenset({"camera", "radar"})

        both = analysis.config_analyses[PresenceConfig(frozenset({"camera", "radar"}))]
        assert {n.input_name for n in both.stn.nodes} == {"lidar", "camera", "radar"}


class TestHasCandidatesElseBranch:
    """Else-branch constraints apply when the optional is absent, not when present."""

    def test_representative_config_uses_present_branch(self, fs_importer: FilesystemImporter) -> None:
        """The all-present representative includes the then-branch constraints."""
        source = f"""\
{_HEADER}
// has_candidates with non-trivial else-branch
aligner ElseBranchAligner {{
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
        // Optional radar with has_candidates
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
    if has_candidates(radar)
        then require(|lidar.observation_time - camera.observation_time| <= 500ms)
        else require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "else_branch", "ElseBranchAligner", fs_importer)
        assert analysis.optional_inputs == frozenset({"radar"})

        present = analysis.config_analyses[PresenceConfig(frozenset({"radar"}))]

        assert {n.input_name for n in present.stn.nodes} == {"lidar", "camera"}

        present_lc_bounds = {
            (e.source.input_name, e.destination.input_name): e.weight
            for e in present.stn.edges
            if {e.source.input_name, e.destination.input_name} == {"lidar", "camera"}
        }
        for weight in present_lc_bounds.values():
            assert weight.value == Decimal("0.5")


class TestBatchInputPipeline:
    """Tests for the analysis pipeline with batch inputs."""

    def test_batch_input_pipeline(self, fs_importer: FilesystemImporter) -> None:
        """Batch input runs through full pipeline: STN nodes, super-inputs, and default search types."""
        source = f"""\
{_HEADER}
// Batch pipeline test
aligner BatchAligner {{
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
        analysis = _compile_and_analyze(source, "batch_pipeline", "BatchAligner", fs_importer)

        assert analysis.optional_inputs == frozenset()
        assert len(analysis.config_analyses) == 1

        ca = analysis.config_analyses[PresenceConfig(frozenset())]

        node_inputs = {n.input_name for n in ca.stn.nodes}
        assert "camera" in node_inputs
        assert FirstInBatch("lidar") in node_inputs
        assert LastInBatch("lidar") in node_inputs

        idx_edges = [
            e
            for e in ca.stn.edges
            if isinstance(e.source.field_name, IndexInView) or isinstance(e.destination.field_name, IndexInView)
        ]
        assert len(idx_edges) == 2

        # Batch boundaries without user objectives get default search types.
        level_by_input = {lev.input: lev for lev in ca.join_plan.levels}
        assert level_by_input[FirstInBatch("lidar")].search_type == SearchType.FIRST_IN_RANGE
        assert level_by_input[LastInBatch("lidar")].search_type == SearchType.LAST_IN_RANGE

        # Join plan: 3 levels — batch boundaries get default search types
        # (FIRST_IN_RANGE, LAST_IN_RANGE), which score higher than camera's
        # ANY_MATCH, so the greedy algorithm places them first.
        assert len(ca.join_plan.levels) == 3
        inputs = {lev.input for lev in ca.join_plan.levels}
        assert inputs == {"camera", FirstInBatch("lidar"), LastInBatch("lidar")}

        # FirstInBatch is the pivot (FIRST_IN_RANGE scores higher than ANY_MATCH).
        assert ca.join_plan.levels[0].input == FirstInBatch("lidar")

        # LastInBatch depends on FirstInBatch through auto-generated ordering/size constraints.
        last_batch_level = level_by_input[LastInBatch("lidar")]
        assert FirstInBatch("lidar") in last_batch_level.depends_on

        # Camera depends on both batch boundaries.
        camera_level = level_by_input["camera"]
        assert FirstInBatch("lidar") in camera_level.depends_on
        assert LastInBatch("lidar") in camera_level.depends_on

    def test_batch_with_user_objective(self, fs_importer: FilesystemImporter) -> None:
        """User-specified objective on batch boundary overrides the default search type."""
        source = f"""\
{_HEADER}
// Batch with user objective
aligner BatchUserObjAligner {{
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
    maximize(min(lidar.observation_time));
}}

{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "batch_user_obj", "BatchUserObjAligner", fs_importer)
        ca = analysis.config_analyses[PresenceConfig(frozenset())]

        level_by_input = {lev.input: lev for lev in ca.join_plan.levels}
        assert level_by_input[FirstInBatch("lidar")].search_type == SearchType.LAST_IN_RANGE


class TestMaximallyPermissiveWindows:
    """Tests for the maximally permissive window analysis."""

    def test_no_optionals(self, fs_importer: FilesystemImporter) -> None:
        """No optionals: single variant, windows match direct STN analysis."""
        source = f"""\
{_HEADER}
// Two required inputs
aligner TwoRequired {{
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
        analysis = _compile_and_analyze(source, "mp_no_opt", "TwoRequired", fs_importer)
        windows = analysis.maximally_permissive_windows

        lidar_obs = FieldAccessor("lidar", "observation_time")
        camera_obs = FieldAccessor("camera", "observation_time")

        w_lc = windows[(lidar_obs, camera_obs)]
        assert w_lc.lo is not None
        assert w_lc.lo.value == Decimal("-0.1")
        assert w_lc.hi is not None
        assert w_lc.hi.value == Decimal("0.1")

        w_cl = windows[(camera_obs, lidar_obs)]
        assert w_cl.lo is not None
        assert w_cl.lo.value == Decimal("-0.1")
        assert w_cl.hi is not None
        assert w_cl.hi.value == Decimal("0.1")

    def test_has_candidates_else_tighter_with_transitive(self, fs_importer: FilesystemImporter) -> None:
        """has_candidates where else-branch is tighter, with transitive tightening.

        Tests two things:
        - Maximally permissive window uses the wider then-branch (500ms), not
          the tighter else-branch (100ms).
        - Transitive tightening through a third required input:
          R1-R3 <= 300ms and R3-R2 <= 300ms gives R1-R2 <= 600ms via STN,
          which is tighter than the direct 2000ms constraint.
        """
        source = f"""\
{_HEADER}
// has_candidates else-branch is tighter + transitive tightening
aligner ElseTighterTransitive {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Pose
        pose: Tappy<VehiclePose>
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
    require(|lidar.observation_time - camera.observation_time| <= 2000ms);
    require(|lidar.observation_time - radar.observation_time| <= 300ms);
    require(|radar.observation_time - camera.observation_time| <= 300ms);
    if has_candidates(pose)
        then require(|lidar.observation_time - camera.observation_time| <= 500ms)
        else require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "mp_else_tighter", "ElseTighterTransitive", fs_importer)
        windows = analysis.maximally_permissive_windows

        lidar_obs = FieldAccessor("lidar", "observation_time")
        camera_obs = FieldAccessor("camera", "observation_time")
        radar_obs = FieldAccessor("radar", "observation_time")

        # lidar-camera: direct 2000ms, transitive 600ms, then-branch 500ms.
        # Two variants:
        #   then-branch: min(2000ms, 600ms, 500ms) = 500ms → [-0.5, 0.5]
        #   else-branch: min(2000ms, 600ms, 100ms) = 100ms → [-0.1, 0.1]
        # Maximally permissive = max(0.5, 0.1) = 0.5
        w_lc = windows[(lidar_obs, camera_obs)]
        assert w_lc.hi is not None
        assert w_lc.hi.value == Decimal("0.5")
        assert w_lc.lo is not None
        assert w_lc.lo.value == Decimal("-0.5")

        # lidar-radar: only unconditional 300ms, same in both variants.
        w_lr = windows[(lidar_obs, radar_obs)]
        assert w_lr.hi is not None
        assert w_lr.hi.value == Decimal("0.3")
        assert w_lr.lo is not None
        assert w_lr.lo.value == Decimal("-0.3")

        # radar-camera: only unconditional 300ms.
        w_rc = windows[(radar_obs, camera_obs)]
        assert w_rc.hi is not None
        assert w_rc.hi.value == Decimal("0.3")
        assert w_rc.lo is not None
        assert w_rc.lo.value == Decimal("-0.3")

    def test_two_has_candidates_with_auto_drop(self, fs_importer: FilesystemImporter) -> None:
        """Two has_candidates optionals (k=2) → four variants, plus a non-has_candidates optional.

        Also tests that the non-has_candidates optional's constraints are properly
        dropped (auto-drop always treats them as absent).
        """
        source = f"""\
{_HEADER}
// Two has_candidates optionals + one auto-drop optional
aligner TwoHC {{
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
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Pose opt1
        pose_opt1: Tappy<VehiclePose>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Pose opt2
        pose_opt2: Tappy<VehiclePose>
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
    require(|lidar.observation_time - radar.observation_time| <= 200ms);
    if has_candidates(pose_opt1)
        then require(|lidar.observation_time - camera.observation_time| <= 200ms)
        else require(|lidar.observation_time - camera.observation_time| <= 50ms);
    if has_candidates(pose_opt2)
        then require(|lidar.observation_time - camera.observation_time| <= 300ms)
        else require(|lidar.observation_time - camera.observation_time| <= 80ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "mp_two_hc", "TwoHC", fs_importer)
        windows = analysis.maximally_permissive_windows

        lidar_obs = FieldAccessor("lidar", "observation_time")
        camera_obs = FieldAccessor("camera", "observation_time")

        # Four variants (then1/else1 x then2/else2):
        #   then1+then2: min(200ms, 300ms) = 200ms
        #   then1+else2: min(200ms, 80ms) = 80ms
        #   else1+then2: min(50ms, 300ms) = 50ms
        #   else1+else2: min(50ms, 80ms) = 50ms
        # Maximally permissive = max(200ms, 80ms, 50ms, 50ms) = 200ms
        w_lc = windows[(lidar_obs, camera_obs)]
        assert w_lc.hi is not None
        assert w_lc.hi.value == Decimal("0.2")
        assert w_lc.lo is not None
        assert w_lc.lo.value == Decimal("-0.2")

        # Radar is auto-drop but still gets optional-input windows.
        # Direct constraint: |lidar - radar| <= 200ms → radar-lidar window = ±200ms.
        # Derived: radar-camera transits through lidar.  Widest variant
        # (then1+then2, camera-lidar=200ms) gives radar-camera = ±400ms.
        radar_obs = FieldAccessor("radar", "observation_time")
        w_rl = windows[(radar_obs, lidar_obs)]
        assert w_rl.lo is not None
        assert w_rl.lo.value == Decimal("-0.2")
        assert w_rl.hi is not None
        assert w_rl.hi.value == Decimal("0.2")

        w_rc = windows[(radar_obs, camera_obs)]
        assert w_rc.lo is not None
        assert w_rc.lo.value == Decimal("-0.4")
        assert w_rc.hi is not None
        assert w_rc.hi.value == Decimal("0.4")

    def test_optional_to_optional_windows(self, fs_importer: FilesystemImporter) -> None:
        """Optional-to-optional constraint produces correctly relaxed windows.

        When two optionals constrain each other, the window for the later
        optional must be relaxed across configs where the earlier optional
        is absent (yielding unbounded windows for that pair).
        """
        source = f"""\
{_HEADER}
// Two optionals constraining each other
aligner OptOpt {{
    inputs {{
        // Tick
        tick: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Radar
        radar: Tappy<RadarDetection>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
    }}
    assume(is_strictly_increasing(tick.observation_time));
    assume(is_strictly_increasing(radar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|tick.observation_time - radar.observation_time| <= 100ms);
    require(|tick.observation_time - camera.observation_time| <= 200ms);
    require(|radar.observation_time - camera.observation_time| <= 50ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "mp_opt_opt", "OptOpt", fs_importer)
        windows = analysis.maximally_permissive_windows

        tick_obs = FieldAccessor("tick", "observation_time")
        radar_obs = FieldAccessor("radar", "observation_time")
        camera_obs = FieldAccessor("camera", "observation_time")

        # Radar windows — only constraint is |tick - radar| <= 100ms.
        # No has_candidates branches, so only one variant → window = ±100ms.
        w_rt = windows[(radar_obs, tick_obs)]
        assert w_rt.lo is not None
        assert w_rt.lo.value == Decimal("-0.1")
        assert w_rt.hi is not None
        assert w_rt.hi.value == Decimal("0.1")

        # Camera windows from tick: |tick - camera| <= 200ms.
        # The |radar - camera| <= 50ms constraint is excluded from
        # camera's window computation because radar is another optional.
        # Only required-to-optional constraints participate.
        w_ct = windows[(camera_obs, tick_obs)]
        assert w_ct.lo is not None
        assert w_ct.lo.value == Decimal("-0.2")
        assert w_ct.hi is not None
        assert w_ct.hi.value == Decimal("0.2")

        # Camera-radar window is not produced: optional-to-optional
        # constraints are excluded from the per-optional window pass
        # to avoid unsafe dereferences when the reference optional is
        # absent at runtime.
        camera_radar_keys = [k for k in windows if camera_obs in k and radar_obs in k]
        assert camera_radar_keys == []

    def test_batch_input_required(self, fs_importer: FilesystemImporter) -> None:
        """Required batch input boundaries participate as required accessors."""
        source = f"""\
{_HEADER}
// Batch input in maximally permissive analysis
aligner BatchMP {{
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
        analysis = _compile_and_analyze(source, "mp_batch", "BatchMP", fs_importer)
        windows = analysis.maximally_permissive_windows

        camera_obs = FieldAccessor("camera", "observation_time")
        first_lidar = FieldAccessor(FirstInBatch("lidar"), "observation_time")
        last_lidar = FieldAccessor(LastInBatch("lidar"), "observation_time")

        # min(lidar.obs) >= camera.obs → first_lidar - camera >= 0 → [0, None]
        # But also batch ordering: first <= last and idx constraints.
        assert (first_lidar, camera_obs) in windows
        w_fc = windows[(first_lidar, camera_obs)]
        assert w_fc.lo is not None
        assert w_fc.lo.value == Decimal(0)
        # Upper bound may be None (unconstrained above) or finite via transitive path.
        assert w_fc.hi is None or w_fc.hi.value >= Decimal(0)

        # max(lidar.obs) <= camera.obs → last_lidar - camera <= 0 → [None, 0]
        assert (last_lidar, camera_obs) in windows
        w_lc = windows[(last_lidar, camera_obs)]
        assert w_lc.hi is not None
        assert w_lc.hi.value == Decimal(0)
        # Lower bound may be None (unconstrained below) or finite via transitive path.
        assert w_lc.lo is None or w_lc.lo.value <= Decimal(0)

        # Batch idx boundaries should also appear.
        idx_keys = [k for k in windows if any(isinstance(a.field_name, IndexInView) for a in k)]
        assert len(idx_keys) > 0


class TestArbitrarySelectionValidation:
    """Tests for the objective-coverage / arbitrary-selection validation pass."""

    def test_non_batched_any_match_without_objective_raises(self, fs_importer: FilesystemImporter) -> None:
        """Rule A: an ``ANY_MATCH`` input with no objective and no opt-in raises."""
        source = f"""\
{_HEADER}
// Nothing constrains camera selection → ANY_MATCH without objective
aligner RuleAFail
{{
    inputs
    {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (unconstrained)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        with pytest.raises(PipelineError, match=r"would be selected arbitrarily"):
            _compile_and_analyze(source, "rule_a_fail", "RuleAFail", fs_importer)

    def test_non_batched_any_match_with_objective_passes(self, fs_importer: FilesystemImporter) -> None:
        """Rule A: adding an objective referencing the input silences the check."""
        source = f"""\
{_HEADER}
// Camera is pinned by `maximize(camera.observation_time)`
aligner RuleAObjective
{{
    inputs
    {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (objective)
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|lidar.observation_time - camera.observation_time| <= 100ms);
    maximize(camera.observation_time);
}}
{_SCHEMAS}
"""
        # No exception expected.
        analysis = _compile_and_analyze(source, "rule_a_ok", "RuleAObjective", fs_importer)
        assert analysis is not None

    def test_non_batched_any_match_with_opt_in_passes(self, fs_importer: FilesystemImporter) -> None:
        """Rule A: ``arbitrary_selection: true`` opts out of the check."""
        source = f"""\
{_HEADER}
// Camera is unconstrained but explicitly opts into arbitrary selection
aligner RuleAOptIn
{{
    inputs
    {{
        // Lidar
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Camera (opt-in)
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
        analysis = _compile_and_analyze(source, "rule_a_optin", "RuleAOptIn", fs_importer)
        assert analysis is not None

    def test_batched_one_endpoint_unpinned_raises(self, fs_importer: FilesystemImporter) -> None:
        """Rule B: pinning only ``max(batch)`` leaves the first endpoint unconstrained."""
        source = f"""\
{_HEADER}
// Only `max(lidar.observation_time)` is pinned; first-in-batch slides freely
aligner RuleBHalfPinned
{{
    inputs
    {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch — only last is pinned
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            batch_size: [2, 5];
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|max(lidar.observation_time) - camera.observation_time| <= 100ms);
    maximize(max(lidar.observation_time));
}}
{_SCHEMAS}
"""
        with pytest.raises(PipelineError, match=r"first-in-batch"):
            _compile_and_analyze(source, "rule_b_half", "RuleBHalfPinned", fs_importer)

    def test_batched_both_endpoints_pinned_passes(self, fs_importer: FilesystemImporter) -> None:
        """Rule B: pinning both ``min`` and ``max`` endpoints passes."""
        source = f"""\
{_HEADER}
// Both endpoints pinned via min/max objectives
aligner RuleBBothPinned
{{
    inputs
    {{
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
            batch_size: [2, 5];
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|max(lidar.observation_time) - camera.observation_time| <= 100ms);
    minimize(min(lidar.observation_time));
    maximize(max(lidar.observation_time));
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "rule_b_both", "RuleBBothPinned", fs_importer)
        assert analysis is not None

    def test_batched_opt_in_passes(self, fs_importer: FilesystemImporter) -> None:
        """Rule B: ``arbitrary_selection: true`` silences the endpoint-coverage check."""
        source = f"""\
{_HEADER}
// Batched input explicitly opts into arbitrary endpoint selection
aligner RuleBOptIn
{{
    inputs
    {{
        // Camera
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar batch with opt-in
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            batch_size: [2, 5];
            arbitrary_selection: true;
        }}
    }}
    assume(is_strictly_increasing(lidar.observation_time));
    assume(is_strictly_increasing(camera.observation_time));
    require(|max(lidar.observation_time) - camera.observation_time| <= 100ms);
}}
{_SCHEMAS}
"""
        analysis = _compile_and_analyze(source, "rule_b_optin", "RuleBOptIn", fs_importer)
        assert analysis is not None
