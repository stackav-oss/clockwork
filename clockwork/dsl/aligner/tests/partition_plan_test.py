# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for residual aligner join partitioning."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest
from clockwork.dsl.aligner.extract_specs import base_input_name
from clockwork.dsl.aligner.pipeline import analyze_aligner, compute_codegen_plan
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from clockwork.dsl.aligner.gen.codegen_plan import CodegenPlan
    from clockwork.dsl.aligner.partition_types import PartitionComponent, PartitionPoint


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


_HEADER = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
"""

_SCHEMAS = """\
// Test message
schema Msg {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Time of validity
        #0 time_of_validity: SyncTime;
    }
}
"""


def _compile_and_plan(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> CodegenPlan:
    """Compile an aligner source and return its codegen plan."""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)
    resolved = aligner_ir.resolved
    assert resolved is not None
    type_check_aligner(aligner_ir, module.context)
    analysis = analyze_aligner(aligner_ir)
    return compute_codegen_plan(analysis, resolved)


def _partition_at(plan: CodegenPlan, start_level_index: int) -> PartitionPoint:
    """Return the partition at a level, failing the test if absent."""
    assert plan.partition_plan is not None
    partition = plan.partition_plan.partition_starting_at(start_level_index)
    assert partition is not None
    return partition


def _component_base_sets(plan: CodegenPlan, start_level_index: int) -> set[frozenset[str]]:
    """Return base-input names for every component at a suffix start."""
    partition = _partition_at(plan, start_level_index)
    return {
        frozenset(base_input_name(plan.levels[index].join_level.input) for index in component.level_indices)
        for component in partition.components
    }


def _component_base_set(plan: CodegenPlan, component: PartitionComponent) -> frozenset[str]:
    """Return base-input names for a component."""
    return frozenset(base_input_name(plan.levels[index].join_level.input) for index in component.level_indices)


def test_independent_unary_optional_objectives_split(fs_importer: FilesystemImporter) -> None:
    """Independent optional monotone objectives become separate residual components."""
    source = f"""\
{_HEADER}
// Independent optional inputs
aligner IndependentOptionals {{
    inputs {{
        anchor: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        camera: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
        radar: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
    }}
    assume(is_strictly_increasing(anchor.time_of_validity));
    assume(is_strictly_increasing(camera.time_of_validity));
    assume(is_strictly_increasing(radar.time_of_validity));
    maximize(camera.time_of_validity);
    maximize(radar.time_of_validity);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_independent", "IndependentOptionals", fs_importer)

    assert _component_base_sets(plan, 0) == {
        frozenset({"anchor"}),
        frozenset({"camera"}),
        frozenset({"radar"}),
    }


def test_hard_constraint_prevents_split(fs_importer: FilesystemImporter) -> None:
    """Remaining inputs connected by a hard constraint stay in one component."""
    source = f"""\
{_HEADER}
// Hard constraint test
aligner HardConstraint {{
    inputs {{
        anchor: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        camera: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
        radar: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
    }}
    assume(is_strictly_increasing(anchor.time_of_validity));
    assume(is_strictly_increasing(camera.time_of_validity));
    assume(is_strictly_increasing(radar.time_of_validity));
    require(|camera.time_of_validity - radar.time_of_validity| <= 10ms);
    maximize(camera.time_of_validity);
    maximize(radar.time_of_validity);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_hard_constraint", "HardConstraint", fs_importer)

    assert frozenset({"camera", "radar"}) in _component_base_sets(plan, 0)


def test_multi_input_objective_prevents_split(fs_importer: FilesystemImporter) -> None:
    """Inputs referenced by the same objective stay in one component."""
    source = f"""\
{_HEADER}
// Objective dependency test
aligner ObjectiveConstraint {{
    inputs {{
        anchor: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        camera: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; arbitrary_selection: true; }}
        radar: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; arbitrary_selection: true; }}
    }}
    assume(is_strictly_increasing(anchor.time_of_validity));
    assume(is_strictly_increasing(camera.time_of_validity));
    assume(is_strictly_increasing(radar.time_of_validity));
    minimize(|camera.time_of_validity - radar.time_of_validity|);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_objective_constraint", "ObjectiveConstraint", fs_importer)

    assert frozenset({"camera", "radar"}) in _component_base_sets(plan, 0)


def test_prefix_bound_middle_variable_disconnects_suffix(fs_importer: FilesystemImporter) -> None:
    """A shared dependency through an already-bound prefix variable does not connect residual islands."""
    source = f"""\
{_HEADER}
// Prefix disconnect test
aligner PrefixDisconnect {{
    inputs {{
        middle: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        left: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        right: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
    }}
    assume(is_strictly_increasing(middle.time_of_validity));
    assume(is_strictly_increasing(left.time_of_validity));
    assume(is_strictly_increasing(right.time_of_validity));
    require(|left.time_of_validity - middle.time_of_validity| <= 10ms);
    require(|right.time_of_validity - middle.time_of_validity| <= 10ms);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_prefix_disconnect", "PrefixDisconnect", fs_importer)
    middle_level = next(
        level.level_index for level in plan.levels if base_input_name(level.join_level.input) == "middle"
    )

    assert _component_base_sets(plan, middle_level + 1) == {frozenset({"left"}), frozenset({"right"})}


def test_partition_analysis_recurses_inside_connected_component(fs_importer: FilesystemImporter) -> None:
    """A root component can split again after its own prefix is bound."""
    source = f"""\
{_HEADER}
// Nested partition test
aligner NestedPartition {{
    inputs {{
        anchor: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        left: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
        right: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
        solo: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
    }}
    assume(is_strictly_increasing(anchor.time_of_validity));
    assume(is_strictly_increasing(left.time_of_validity));
    assume(is_strictly_increasing(right.time_of_validity));
    assume(is_strictly_increasing(solo.time_of_validity));
    require(left.time_of_validity <= anchor.time_of_validity);
    require(right.time_of_validity <= anchor.time_of_validity);
    maximize(left.time_of_validity);
    maximize(right.time_of_validity);
    maximize(solo.time_of_validity);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_nested", "NestedPartition", fs_importer)
    assert plan.partition_plan is not None

    root_scope = tuple(level.level_index for level in plan.levels)
    root_partition = plan.partition_plan.partition_for_scope(root_scope)
    assert root_partition is not None
    root_components = {_component_base_set(plan, component) for component in root_partition.components}
    assert root_components == {frozenset({"anchor", "left", "right"}), frozenset({"solo"})}

    connected_component = next(
        component
        for component in root_partition.components
        if _component_base_set(plan, component) == frozenset({"anchor", "left", "right"})
    )
    anchor_level = next(
        level.level_index for level in plan.levels if base_input_name(level.join_level.input) == "anchor"
    )
    assert connected_component.level_indices[0] == anchor_level

    nested_scope = connected_component.level_indices[1:]
    nested_partition = plan.partition_plan.partition_for_scope(nested_scope)
    assert nested_partition is not None
    nested_components = {_component_base_set(plan, component) for component in nested_partition.components}
    assert nested_components == {frozenset({"left"}), frozenset({"right"})}


def test_batch_endpoints_remain_connected(fs_importer: FilesystemImporter) -> None:
    """Unbound low/high selectors for one batch input remain one component."""
    source = f"""\
{_HEADER}
// Batch invariant test
aligner BatchInvariant {{
    inputs {{
        pose: Tappy<Msg> {{ max_msgs: 5; batch_size: [1, 4]; arbitrary_selection: true; }}
    }}
    assume(is_strictly_increasing(pose.time_of_validity));
    minimize(min(pose.time_of_validity));
    maximize(max(pose.time_of_validity));
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_batch_invariant", "BatchInvariant", fs_importer)

    assert plan.partition_plan is not None
    assert plan.partition_plan.partition_starting_at(0) is None


def test_has_candidates_branch_connects_controller_to_branch_references(fs_importer: FilesystemImporter) -> None:
    """A remaining has_candidates controller connects to inputs referenced by branch effects."""
    source = f"""\
{_HEADER}
// Has candidates dependency test
aligner HasCandidates {{
    inputs {{
        anchor: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        radar: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; arbitrary_selection: true; }}
        camera: Tappy<Msg> {{ max_msgs: 4; arbitrary_selection: true; }}
        lidar: Tappy<Msg> {{ max_msgs: 4; optional: true; timeout: 0ms; }}
    }}
    assume(is_strictly_increasing(anchor.time_of_validity));
    assume(is_strictly_increasing(radar.time_of_validity));
    assume(is_strictly_increasing(camera.time_of_validity));
    assume(is_strictly_increasing(lidar.time_of_validity));
    maximize(lidar.time_of_validity);
    if has_candidates(radar)
        then require(|camera.time_of_validity - anchor.time_of_validity| <= 10ms)
        else require(|camera.time_of_validity - anchor.time_of_validity| <= 20ms);
}}
{_SCHEMAS}
"""
    plan = _compile_and_plan(source, "partition_has_candidates", "HasCandidates", fs_importer)

    assert any({"radar", "camera"} <= component for component in _component_base_sets(plan, 0))
