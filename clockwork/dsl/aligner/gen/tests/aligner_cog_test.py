# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner C++ code generation scaffolding."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import aligner, clkbuiltins, compiler, node, schema, schema_reg
from clockwork.dsl.ir.cog import (
    BinaryConditionExpr,
    ConditionExpr,
    ConditionOp,
    SimpleConditionExpr,
)
from clockwork.dsl.ir.cog_components import ConditionDef, DynamicTimer, InputDef, NewMessagePresent
from clockwork.dsl.ir.cpp_target import CppTarget
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.schema import InstantiatedSchema


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


_SCHEMAS = """\
// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
// Camera image message
schema CameraImage {
    uuid: 22222222-2222-2222-2222-222222222222;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""

_HEADER = "#![generate(cpp, cpp_aligner)]\n#![cpp(namespace=clockwork::test)]"

_HEADER_NO_ALIGNER = "#![generate(cpp)]\n#![cpp(namespace=clockwork::test)]"


def _compile(
    source: str,
    module_name: str,
    fs_importer: FilesystemImporter,
) -> node.Module:
    """Compile CLK source text and return the module."""
    return compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)


class TestAlignerCppTargetCreation:
    """Verify that an aligner is folded into the main ``_clk_cc`` CppTarget."""

    def test_aligner_target_exists(self, fs_importer: FilesystemImporter) -> None:
        """A module with an aligner gets its synthetic cog appended to ``{module}_clk_cc``."""
        source = f"""\
{_HEADER}
// No-op aligner for testing
aligner NoopAligner {{
    inputs {{
        // Lidar input
        input1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(true);
}}

{_SCHEMAS}
"""
        module = _compile(source, "noop_aligner", fs_importer)
        assert module.inner_scope.lookup("noop_aligner_clk_cc_aligner", recursive=False) is None
        main_target = module.inner_scope.lookup("noop_aligner_clk_cc", recursive=False)
        assert isinstance(main_target, CppTarget)
        assert len(main_target.aligner_cogs) == 1
        assert main_target.aligner_cogs[0].name == "NoopAligner"

    def test_no_aligner_target_without_aligners(self, fs_importer: FilesystemImporter) -> None:
        """Modules without aligners have an empty ``aligner_cogs`` list on the main target."""
        source = f"""\
{_HEADER_NO_ALIGNER}
{_SCHEMAS}
"""
        module = _compile(source, "no_aligner", fs_importer)
        assert module.inner_scope.lookup("no_aligner_clk_cc_aligner", recursive=False) is None
        main_target = module.inner_scope.lookup("no_aligner_clk_cc", recursive=False)
        assert isinstance(main_target, CppTarget)
        assert main_target.aligner_cogs == []

    def test_explicit_cpp_target(self, fs_importer: FilesystemImporter) -> None:
        """Aligner target is created alongside an explicit cpp_target (no #![generate(cpp)] macro)."""
        source = f"""\
{_SCHEMAS}

// Explicit aligner
aligner ExplicitAligner {{
    inputs {{
        // Input
        input1: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(true);
}}

cpp_target explicit_target
{{
    options
    {{
        namespace clockwork::test;
    }}

    schema LidarSweep;
    schema CameraImage;
    representation Tachyon<LidarSweep>;
    interface Tappy<LidarSweep>;
    representation Tachyon<CameraImage>;
    interface Tappy<CameraImage>;
    aligner ExplicitAligner;
}}
"""
        module = _compile(source, "explicit_tgt", fs_importer)
        explicit_target = module.inner_scope.lookup("explicit_target", recursive=False)
        assert isinstance(explicit_target, CppTarget)
        assert len(explicit_target.aligner_cogs) == 1
        assert explicit_target.aligner_cogs[0].name == "ExplicitAligner"


def _get_aligner_ir(module: node.Module, aligner_name: str) -> aligner.Aligner:
    """Look up an Aligner IR entity from a compiled module."""
    entity = module.inner_scope.lookup(aligner_name, recursive=False)
    assert isinstance(entity, aligner.Aligner)
    return entity


def _get_alignment_schema(module: node.Module, aligner_name: str) -> InstantiatedSchema:
    """Look up the alignment output schema from a compiled module.

    The schema is registered in the module scope as ``{AlignerName}AlignmentMsg``.
    """
    schema_name = f"{aligner_name}AlignmentMsg"
    schema_entity = module.inner_scope.lookup(schema_name, recursive=False)
    assert schema_entity is not None, f"Schema {schema_name} not found in module scope"
    assert isinstance(schema_entity, schema.Schema)
    return InstantiatedSchema.from_typespec(schema_entity)


def _get_field_names(inst_schema: InstantiatedSchema) -> list[str]:
    """Get schema field names in source order."""
    sorted_fields = sorted(inst_schema.fields.values(), key=lambda f: inst_schema.field_src_order[f.num])
    return [f.cur_name for f in sorted_fields]


def _get_field_type_infos(inst_schema: InstantiatedSchema) -> dict[str, object]:
    """Get a mapping of field name to type_info object for identity comparison."""
    return {f.cur_name: f.type_info for f in inst_schema.fields.values()}


def _collect_condition_leaves(
    condition: ConditionExpr,
) -> list[ConditionDef]:
    """Walk a condition tree and return all leaf ConditionDef nodes."""
    if isinstance(condition, SimpleConditionExpr):
        assert isinstance(condition.condition, ConditionDef)
        return [condition.condition]
    assert isinstance(condition, BinaryConditionExpr)
    assert condition.op is ConditionOp.OR
    return _collect_condition_leaves(condition.lhs) + _collect_condition_leaves(condition.rhs)


class TestKitchenSinkAligner:
    """Comprehensive test of a multi-input aligner exercising every property combination.

    Covers: output schema generation, cog inputs, view params, execution condition,
    and alignment artifacts — all from one compiled module.
    """

    _ALIGNER_SOURCE: str = f"""\
{_HEADER}
use std::aligners::state;
// Kitchen-sink aligner exercising every input property combination
aligner KitchenSinkAligner {{
    inputs {{
        // Required, non-reuse, non-batch (simplest case)
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Optional, non-reuse, non-batch
        radar: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            optional: true;
            timeout: 50ms;
        }}
        // Required, reuse, non-batch, explicit view params
        pose: Tappy<LidarSweep>
        {{
            reuse: true;
            max_msgs: 20;
            arbitrary_selection: true;
        }}
        // Required, non-reuse, batch
        camera_batch: Tappy<CameraImage>
        {{
            max_msgs: 20;
            arbitrary_selection: true;
            batch_size: [1, 10];
        }}
        // Required, reuse, batch
        imu: Tappy<LidarSweep>
        {{
            max_msgs: 40;
            arbitrary_selection: true;
            batch_size: [2, 20];
            reuse: true;
        }}
    }}

    require(true);
}}

{_SCHEMAS}
"""

    @pytest.fixture(scope="class")
    def module(self, fs_importer: FilesystemImporter) -> node.Module:
        """Compile the kitchen-sink aligner once for all tests in this class."""
        return _compile(self._ALIGNER_SOURCE, "kitchen_sink", fs_importer)

    @pytest.fixture(scope="class")
    def aligner_ir(self, module: node.Module) -> aligner.Aligner:
        """Get the Aligner IR entity."""
        return _get_aligner_ir(module, "KitchenSinkAligner")

    @pytest.fixture(scope="class")
    def inst_schema(self, module: node.Module) -> InstantiatedSchema:
        """Get the generated alignment schema."""
        return _get_alignment_schema(module, "KitchenSinkAligner")

    def test_schema_field_names(self, inst_schema: InstantiatedSchema) -> None:
        """Schema fields match all input type combinations."""
        field_names = _get_field_names(inst_schema)
        assert field_names == [
            "lidar_seq",
            "radar_seq",
            "has_radar",
            "pose_seq",
            "pose_is_new",
            "camera_batch_begin_seq",
            "camera_batch_end_seq",
            "imu_begin_seq",
            "imu_end_seq",
            "imu_first_new_seq",
        ]

    def test_schema_field_types(self, inst_schema: InstantiatedSchema) -> None:
        """Sequence fields are UInt64, presence/newness flags are Bool."""
        type_infos = _get_field_type_infos(inst_schema)
        for name, type_info in type_infos.items():
            if "seq" in name:
                assert type_info is clkbuiltins.UINT64, f"{name} should be UInt64"
            else:
                assert type_info is clkbuiltins.BOOL, f"{name} should be Bool"

    def test_schema_registered_in_module_scope(self, module: node.Module) -> None:
        """The alignment schema is accessible via the module scope."""
        schema_entity = module.inner_scope.lookup("KitchenSinkAlignerAlignmentMsg", recursive=False)
        assert schema_entity is not None
        assert isinstance(schema_entity, schema.Schema)

    def test_cog_has_alignment_output(self, aligner_ir: aligner.Aligner) -> None:
        """Synthetic cog has an 'alignment' output with an InterfaceInfo message type."""
        assert aligner_ir.synthetic_cog is not None
        assert "alignment" in aligner_ir.synthetic_cog.outputs
        output_def = aligner_ir.synthetic_cog.outputs["alignment"]
        assert isinstance(output_def.message_type, schema_reg.InterfaceInfo)

    def test_alignment_artifacts_on_aligner(self, aligner_ir: aligner.Aligner) -> None:
        """Generated schema, representation, and interface are stored on the Aligner IR node."""
        assert aligner_ir.alignment_schema is not None
        assert aligner_ir.alignment_repr is not None
        assert aligner_ir.alignment_iface is not None

    def test_cog_inputs_match_aligner_inputs(self, aligner_ir: aligner.Aligner) -> None:
        """Synthetic cog has one InputDef per aligner input, with correct message types."""
        assert aligner_ir.synthetic_cog is not None
        assert set(aligner_ir.synthetic_cog.inputs.keys()) == {
            "lidar",
            "radar",
            "pose",
            "camera_batch",
            "imu",
        }
        for input_def in aligner_ir.synthetic_cog.inputs.values():
            assert isinstance(input_def, InputDef)
            assert isinstance(input_def.message_type, schema_reg.InterfaceInfo)

    def test_all_inputs_have_manual_cursor(self, aligner_ir: aligner.Aligner) -> None:
        """Every aligner input forces manual_cursor=True."""
        assert aligner_ir.synthetic_cog is not None
        for input_def in aligner_ir.synthetic_cog.inputs.values():
            assert input_def.view_params.manual_cursor is True

    def test_view_params_carried_through(self, aligner_ir: aligner.Aligner) -> None:
        """Explicit view params (max_msgs) are preserved."""
        assert aligner_ir.synthetic_cog is not None
        pose = aligner_ir.synthetic_cog.inputs["pose"]
        assert pose.view_params.max_msgs == 20
        lidar = aligner_ir.synthetic_cog.inputs["lidar"]
        assert lidar.view_params.max_msgs == 10

    def test_execution_condition_is_or_chain(self, aligner_ir: aligner.Aligner) -> None:
        """Five inputs plus dynamic timer produce an OR chain of conditions."""
        assert aligner_ir.synthetic_cog is not None
        condition = aligner_ir.synthetic_cog.execution_spec.condition
        leaves = _collect_condition_leaves(condition)
        new_msg_leaves = [leaf for leaf in leaves if isinstance(leaf.condition, NewMessagePresent)]
        timer_leaves = [leaf for leaf in leaves if isinstance(leaf.condition, DynamicTimer)]
        input_names = {
            leaf.condition.input_name for leaf in new_msg_leaves if isinstance(leaf.condition, NewMessagePresent)
        }
        assert input_names == {"lidar", "radar", "pose", "camera_batch", "imu"}
        assert len(timer_leaves) == 1

    def test_conditions_registered_on_cog(self, aligner_ir: aligner.Aligner) -> None:
        """ConditionDef entries: one per input plus one DynamicTimer."""
        assert aligner_ir.synthetic_cog is not None
        conditions = aligner_ir.synthetic_cog.conditions
        assert len(conditions) == 6
        new_msg_count = sum(isinstance(c.condition, NewMessagePresent) for c in conditions.values())
        timer_count = sum(isinstance(c.condition, DynamicTimer) for c in conditions.values())
        assert new_msg_count == 5
        assert timer_count == 1

    def test_state_endpoint_for_timeout(self, aligner_ir: aligner.Aligner) -> None:
        """Aligner with timeout gets an ``aligner_state`` state endpoint."""
        assert aligner_ir.synthetic_cog is not None
        states = aligner_ir.synthetic_cog.states
        assert "aligner_state" in states
        state_def = states["aligner_state"]
        assert state_def.resolved is not None
        assert state_def.resolved.params.mutable is True


class TestSingleInputAligner:
    """Degenerate case: aligner with one input has no OR, just a SimpleConditionExpr."""

    _SOURCE: str = f"""\
{_HEADER}
// Single-input aligner
aligner SingleAligner {{
    inputs {{
        // Only input
        sensor: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(true);
}}

{_SCHEMAS}
"""

    @pytest.fixture(scope="class")
    def module(self, fs_importer: FilesystemImporter) -> node.Module:
        """Compiled module fixture."""
        return _compile(self._SOURCE, "single_input", fs_importer)

    @pytest.fixture(scope="class")
    def aligner_ir(self, module: node.Module) -> aligner.Aligner:
        """Retrieve just the aligner from the compiled module."""
        return _get_aligner_ir(module, "SingleAligner")

    def test_schema_has_one_seq_field(self, module: node.Module) -> None:
        """Single input produces a single seq field."""
        inst_schema = _get_alignment_schema(module, "SingleAligner")
        assert _get_field_names(inst_schema) == ["sensor_seq"]

    def test_single_input_and_output(self, aligner_ir: aligner.Aligner) -> None:
        """Cog has one input and one alignment output."""
        assert aligner_ir.synthetic_cog is not None
        assert set(aligner_ir.synthetic_cog.inputs.keys()) == {"sensor"}
        assert "alignment" in aligner_ir.synthetic_cog.outputs

    def test_execution_condition_is_simple(self, aligner_ir: aligner.Aligner) -> None:
        """Single input produces a bare SimpleConditionExpr, not an OR."""
        assert aligner_ir.synthetic_cog is not None
        condition = aligner_ir.synthetic_cog.execution_spec.condition
        assert isinstance(condition, SimpleConditionExpr)
        assert isinstance(condition.condition, ConditionDef)
        assert isinstance(condition.condition.condition, NewMessagePresent)
        assert condition.condition.condition.input_name == "sensor"

    def test_single_condition_registered(self, aligner_ir: aligner.Aligner) -> None:
        """One new_message condition registered, no DynamicTimer."""
        assert aligner_ir.synthetic_cog is not None
        assert len(aligner_ir.synthetic_cog.conditions) == 1
        cond_def = next(iter(aligner_ir.synthetic_cog.conditions.values()))
        assert isinstance(cond_def.condition, NewMessagePresent)
