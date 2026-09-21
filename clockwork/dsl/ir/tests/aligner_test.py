# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""End-to-end tests for aligner IR parsing and compilation."""

import pytest
from clockwork.dsl.ir import aligner, aligner_builtins, clkbuiltins, compiler, dfl, dfl_types, policy, primitive
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


class TestBasicAlignerParsing:
    """Tests for basic aligner parsing and IR construction."""

    def test_minimal_aligner(self, fs_importer: FilesystemImporter) -> None:
        """Test compiling a minimal aligner with one required input."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// A minimal aligner for testing
aligner MinimalAligner {
    inputs {
        // Primary lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }
}

// Dummy schema for the test
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp field
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_minimal_aligner"), fs_importer)

        aligner_ir = module.inner_scope.lookup("MinimalAligner")
        assert aligner_ir is not None
        assert isinstance(aligner_ir, aligner.Aligner)
        assert aligner_ir.name == "MinimalAligner"
        assert aligner_ir.doc is not None
        assert "minimal aligner" in aligner_ir.doc.value.lower()

        assert "lidar" in aligner_ir.inputs
        lidar_input = aligner_ir.inputs["lidar"]
        assert lidar_input.name == "lidar"
        assert lidar_input.doc is not None
        assert "lidar" in lidar_input.doc.value.lower()
        assert lidar_input.type_info is clkbuiltins.ALIGNER_INPUT_TYPE

    def test_multiple_inputs(self, fs_importer: FilesystemImporter) -> None:
        """Test aligner with multiple inputs."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Multi-sensor aligner
aligner MultiSensorAligner {
    inputs {
        // Lidar sweep
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
        // Camera image
        camera: Tappy<CameraImage> { max_msgs: 10; }
        // Vehicle pose
        pose: Tappy<VehiclePose> { max_msgs: 10; }
    }
}

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
// Vehicle pose message
schema VehiclePose {
    uuid: 33333333-3333-3333-3333-333333333333;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_multi_input_aligner"), fs_importer)

        aligner_ir = module.inner_scope.lookup("MultiSensorAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert len(aligner_ir.inputs) == 3
        assert "lidar" in aligner_ir.inputs
        assert "camera" in aligner_ir.inputs
        assert "pose" in aligner_ir.inputs


class TestInputProperties:
    """Tests for aligner input property parsing."""

    def test_optional_input(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing optional input with timeout."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with optional input
aligner OptionalInputAligner {
    inputs {
        // Required input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
        // Optional radar with timeout
        radar: Tappy<RadarDetection> {
            max_msgs: 10;
            optional: true;
            timeout: 100ms;
        }
    }
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
// Radar detection message
schema RadarDetection {
    uuid: 44444444-4444-4444-4444-444444444444;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_optional_input"), fs_importer)

        aligner_ir = module.inner_scope.lookup("OptionalInputAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        lidar_input = aligner_ir.inputs["lidar"]
        assert lidar_input.optional_expr is None

        radar_input = aligner_ir.inputs["radar"]
        assert radar_input.optional_expr is not None
        assert radar_input.timeout_expr is not None

    def test_reuse_input(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing reuse property."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with reusable input
aligner ReuseInputAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
        // Vehicle pose can be reused
        pose: Tappy<VehiclePose> {
            max_msgs: 10;
            reuse: true;
        }
    }
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
// Vehicle pose message
schema VehiclePose {
    uuid: 33333333-3333-3333-3333-333333333333;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_reuse_input"), fs_importer)

        aligner_ir = module.inner_scope.lookup("ReuseInputAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        pose_input = aligner_ir.inputs["pose"]
        assert pose_input.reuse_expr is not None

    def test_batch_size_input(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing batch_size property as [lo, hi] list."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with batch input
aligner BatchInputAligner {
    inputs {
        // Lidar batch of 2-5 messages
        lidar: Tappy<LidarSweep> {
            max_msgs: 10;
            batch_size: [2, 5];
        }
    }
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_batch_size"), fs_importer)

        aligner_ir = module.inner_scope.lookup("BatchInputAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        lidar_input = aligner_ir.inputs["lidar"]
        assert lidar_input.batch_size_lo_expr is not None
        assert lidar_input.batch_size_hi_expr is not None
        assert isinstance(lidar_input.type_info, dfl_types.CollectionType)
        assert lidar_input.type_info.element_type is clkbuiltins.ALIGNER_INPUT_TYPE


class TestAlignerInputValidation:
    """Tests for mandatory max_msgs and batch-vs-max_msgs validation on aligner inputs."""

    _LIDAR_SCHEMA = """\
// Lidar sweep message
schema LidarSweep
{
    uuid: 11111111-1111-1111-1111-111111111111;
    fields
    {
        // Timestamp
        #0 observation_time: SyncTime;
    }
}
"""

    def _compile_and_resolve(
        self,
        source: str,
        module_id: str,
        aligner_name: str,
        fs_importer: FilesystemImporter,
    ) -> aligner.ResolvedAligner:
        """Compile source and resolve the named aligner."""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_id), fs_importer)
        aligner_ir = module.inner_scope.lookup(aligner_name)
        assert isinstance(aligner_ir, aligner.Aligner)
        return aligner_ir.resolve()

    def test_missing_max_msgs_raises(self, fs_importer: FilesystemImporter) -> None:
        """Omitting ``max_msgs`` on an aligner input must raise a descriptive error."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner missing max_msgs on its only input
aligner NoMaxMsgs
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>;
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        with pytest.raises(ValueError, match=r"must specify 'max_msgs' explicitly"):
            self._compile_and_resolve(source, "test_missing_max_msgs", "NoMaxMsgs", fs_importer)

    def test_missing_max_msgs_empty_block_raises(self, fs_importer: FilesystemImporter) -> None:
        """An empty input block counts as omitting ``max_msgs``."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner with an input block but no max_msgs
aligner NoMaxMsgsBlock
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            reuse: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        with pytest.raises(ValueError, match=r"must specify 'max_msgs' explicitly"):
            self._compile_and_resolve(source, "test_missing_max_msgs_block", "NoMaxMsgsBlock", fs_importer)

    def test_explicit_max_msgs_two_is_allowed(self, fs_importer: FilesystemImporter) -> None:
        """A small explicit ``max_msgs`` is allowed when no ``batch_size`` is set.

        ``max_msgs: 1`` remains rejected by ``_validate_max_msgs_param`` (trivially
        ambiguous); ``2`` is the smallest value that exercises the explicit-set path.
        """
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner whose input explicitly uses max_msgs: 2
aligner ExplicitTwo
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 2;
            arbitrary_selection: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_explicit_two", "ExplicitTwo", fs_importer)
        assert resolved.inputs["lidar"].view_params.max_msgs == 2

    def test_max_msgs_equal_to_batch_max_is_allowed(self, fs_importer: FilesystemImporter) -> None:
        """``max_msgs == max(batch_size)`` allows the largest batch to fill the view."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner where max_msgs equals max batch size
aligner TightBatch
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 5;
            batch_size: [1, 5];
            arbitrary_selection: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_tight_batch", "TightBatch", fs_importer)
        assert resolved.inputs["lidar"].view_params.max_msgs == 5
        assert resolved.inputs["lidar"].batch_size == (1, 5)

    def test_max_msgs_less_than_batch_max_raises(self, fs_importer: FilesystemImporter) -> None:
        """``max_msgs < max(batch_size)`` is rejected."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner where max_msgs is less than max batch size
aligner SmallBatch
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 3;
            batch_size: [1, 5];
            arbitrary_selection: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        with pytest.raises(ValueError, match=r"max_msgs=3 < max\(batch_size\)=5"):
            self._compile_and_resolve(source, "test_small_batch", "SmallBatch", fs_importer)

    def test_max_msgs_strictly_greater_than_batch_max_is_allowed(self, fs_importer: FilesystemImporter) -> None:
        """``max_msgs > max(batch_size)`` is the canonical sizing and must pass."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner whose max_msgs exceeds max batch size
aligner HealthyBatch
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
            batch_size: [1, 5];
            arbitrary_selection: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_healthy_batch", "HealthyBatch", fs_importer)
        assert resolved.inputs["lidar"].view_params.max_msgs == 10
        assert resolved.inputs["lidar"].batch_size == (1, 5)

    def test_arbitrary_selection_default_false(self, fs_importer: FilesystemImporter) -> None:
        """``arbitrary_selection`` defaults to False on ``ResolvedAlignerInput``."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner where arbitrary_selection is left at its default
aligner DefaultAS
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
        }
    }
    maximize(lidar.observation_time);
}
"""
            + self._LIDAR_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_default_as", "DefaultAS", fs_importer)
        assert resolved.inputs["lidar"].arbitrary_selection is False

    def test_arbitrary_selection_true_propagated(self, fs_importer: FilesystemImporter) -> None:
        """``arbitrary_selection: true`` is propagated to ``ResolvedAlignerInput``."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner that explicitly enables arbitrary_selection
aligner TrueAS
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
            arbitrary_selection: true;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_true_as", "TrueAS", fs_importer)
        assert resolved.inputs["lidar"].arbitrary_selection is True

    def test_arbitrary_selection_non_boolean_raises(self, fs_importer: FilesystemImporter) -> None:
        """``arbitrary_selection`` must be a boolean literal."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner with a non-boolean arbitrary_selection value
aligner BadAS
{
    inputs
    {
        // Lidar
        lidar: Tappy<LidarSweep>
        {
            max_msgs: 10;
            arbitrary_selection: 1;
        }
    }
}
"""
            + self._LIDAR_SCHEMA
        )
        with pytest.raises((TypeError, ValueError)):
            self._compile_and_resolve(source, "test_bad_as", "BadAS", fs_importer)


class TestBodyStatements:
    """Tests for aligner body statement parsing."""

    def test_let_binding(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing let bindings in aligner body."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with let binding
aligner LetBindingAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }

    let x = 42;
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_let_binding"), fs_importer)

        aligner_ir = module.inner_scope.lookup("LetBindingAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert len(aligner_ir.body_stmts) == 1
        assert isinstance(aligner_ir.body_stmts[0], aligner.AlignerLetBinding)
        assert aligner_ir.body_stmts[0].name == "x"

    def test_inline_function(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing inline function definitions in aligner body."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with inline function
aligner InlineFnAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }

    fn double(x) { x + x }
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_inline_fn"), fs_importer)

        aligner_ir = module.inner_scope.lookup("InlineFnAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert len(aligner_ir.body_stmts) == 1
        assert isinstance(aligner_ir.body_stmts[0], dfl.FnDef)
        assert aligner_ir.body_stmts[0].name == "double"

    def test_spec_statement(self, fs_importer: FilesystemImporter) -> None:
        """Test parsing spec expression statements in aligner body."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with spec statement
aligner SpecStmtAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }

    require(1 <= 2);
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_spec_stmt"), fs_importer)

        aligner_ir = module.inner_scope.lookup("SpecStmtAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert len(aligner_ir.body_stmts) == 1
        assert isinstance(aligner_ir.body_stmts[0], aligner.AlignerSpecStmt)

    def test_mixed_body_statements(self, fs_importer: FilesystemImporter) -> None:
        """Test aligner with mix of let bindings, functions, and spec statements."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with mixed body statements
aligner MixedBodyAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }

    let x = 10;
    fn square(n) { n * n }
    let y = 20;
    require(x <= y);
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_mixed_body"), fs_importer)

        aligner_ir = module.inner_scope.lookup("MixedBodyAligner")
        assert isinstance(aligner_ir, aligner.Aligner)
        assert len(aligner_ir.body_stmts) == 4

        assert isinstance(aligner_ir.body_stmts[0], aligner.AlignerLetBinding)
        assert aligner_ir.body_stmts[0].name == "x"

        assert isinstance(aligner_ir.body_stmts[1], dfl.FnDef)
        assert aligner_ir.body_stmts[1].name == "square"

        assert isinstance(aligner_ir.body_stmts[2], aligner.AlignerLetBinding)
        assert aligner_ir.body_stmts[2].name == "y"

        assert isinstance(aligner_ir.body_stmts[3], aligner.AlignerSpecStmt)


class TestScopeAccess:
    """Tests for scope and name resolution."""

    def test_inputs_in_scope(self, fs_importer: FilesystemImporter) -> None:
        """Test that inputs are accessible in inner scope."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner for scope test
aligner ScopeTestAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
        // Camera input
        camera: Tappy<CameraImage> { max_msgs: 10; }
    }
}

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
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_scope_access"), fs_importer)

        aligner_ir = module.inner_scope.lookup("ScopeTestAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        # Inputs should be in inner_scope
        lidar = aligner_ir.inner_scope.lookup("lidar")
        assert lidar is not None
        assert isinstance(lidar, aligner.AlignerInputDef)

        camera = aligner_ir.inner_scope.lookup("camera")
        assert camera is not None
        assert isinstance(camera, aligner.AlignerInputDef)

    def test_let_bindings_in_body_scope(self, fs_importer: FilesystemImporter) -> None:
        """Test that let bindings are in body_scope."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with let binding scope test
aligner LetScopeAligner {
    inputs {
        // Lidar input
        lidar: Tappy<LidarSweep> { max_msgs: 10; }
    }

    let ref_time = 100;
}

// Lidar sweep message
schema LidarSweep {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Timestamp
        #0 timestamp: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_let_scope"), fs_importer)

        aligner_ir = module.inner_scope.lookup("LetScopeAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        # Let binding should be in body_scope
        ref_time = aligner_ir.body_scope.lookup("ref_time", recursive=False)
        assert ref_time is not None
        assert isinstance(ref_time, aligner.AlignerLetBinding)

        # Body scope should inherit inputs from inner_scope
        lidar = aligner_ir.body_scope.lookup("lidar")
        assert lidar is not None


class TestAlignerInModuleScope:
    """Tests for aligner accessibility in module scope."""

    def test_aligner_in_module_scope(self, fs_importer: FilesystemImporter) -> None:
        """Test that aligner is accessible via module.inner_scope."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Test aligner
aligner TestAligner {
    inputs {
        // Input
        data: Tappy<DataMsg> { max_msgs: 10; }
    }
}

// Data message
schema DataMsg {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Value field
        #0 value: Int64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_module_scope"), fs_importer)

        test_aligner = module.inner_scope.lookup("TestAligner")
        assert test_aligner is not None
        assert isinstance(test_aligner, aligner.Aligner)


_GENERATE_CPP_HEADER = """\
#![generate(cpp)]
#![cpp(namespace=test)]
"""

_DATA_MSG_SCHEMA = """\
// Data message
schema DataMsg {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Value field
        #0 value: Int64;
    }
}
"""


class TestViewParamsOnAlignerInputs:
    """Tests for cog view parameters on aligner inputs."""

    def _compile_and_resolve(
        self,
        source: str,
        module_id: str,
        aligner_name: str,
        fs_importer: FilesystemImporter,
    ) -> aligner.ResolvedAligner:
        """Compile source and resolve the named aligner."""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_id), fs_importer)
        aligner_ir = module.inner_scope.lookup(aligner_name)
        assert isinstance(aligner_ir, aligner.Aligner)
        return aligner_ir.resolve()

    def test_cog_and_aligner_params_combined(self, fs_importer: FilesystemImporter) -> None:
        """Test all cog view params and aligner params on a single input."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner with all params
aligner AllParamsAligner {
    inputs {
        // Input with all cog and aligner params
        data: Tappy<DataMsg> {
            max_msgs: 10;
            no_dial: true;
            copy_inputs: true;
            connect_optional: true;
            optional: true;
            timeout: 200ms;
            reuse: true;
            batch_size: [2, 5];
        }
    }
}
"""
            + _DATA_MSG_SCHEMA
        )
        resolved = self._compile_and_resolve(source, "test_all_params_aligner", "AllParamsAligner", fs_importer)

        data_input = resolved.inputs["data"]
        # Cog params go to view_params (resolved to concrete values)
        assert data_input.view_params.max_msgs == 10
        assert data_input.view_params.manual_cursor is True
        assert data_input.view_params.no_dial is True
        assert data_input.view_params.copy_inputs is True
        assert data_input.view_params.is_optional is True
        # Aligner-specific params
        assert data_input.optional is True
        assert data_input.timeout is not None
        assert data_input.reuse is True
        assert data_input.batch_size == (2, 5)

    def test_manual_cursor_false_raises_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that manual_cursor: false raises an error on aligner inputs."""
        source = (
            _GENERATE_CPP_HEADER
            + """\
// Aligner with explicit false manual_cursor
aligner BadCursorAligner {
    inputs {
        // Input with bad cursor
        data: Tappy<DataMsg> {
            manual_cursor: false;
        }
    }
}
"""
            + _DATA_MSG_SCHEMA
        )
        with pytest.raises(ValueError, match="manual_cursor"):
            self._compile_and_resolve(source, "test_bad_cursor_aligner", "BadCursorAligner", fs_importer)

    def test_unknown_param_raises_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that unknown params raise an error."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with bad param
aligner BadParamAligner {
    inputs {
        // Input with unknown param
        data: Tappy<DataMsg> {
            nonexistent_param: true;
        }
    }
}

// Data message
schema DataMsg {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Value field
        #0 value: Int64;
    }
}
"""
        with pytest.raises(ValueError, match="Unknown aligner input parameter"):
            compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_bad_param_aligner"), fs_importer)


class TestAlignerBuiltinsLoader:
    """Tests for the aligner builtins loader."""

    def test_ensure_builtins_loaded(self, fs_importer: FilesystemImporter) -> None:
        """Test that loading builtins returns a valid registry with spread fn.

        Also verifies idempotency: calling twice returns the same registry.
        """
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Dummy module for context
schema Dummy {
    uuid: 99999999-9999-9999-9999-999999999999;
    fields {
        // Value
        #0 value: Int64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_loader"), fs_importer)

        registry = aligner_builtins.ensure_aligner_builtins_loaded(module.context)
        spread = registry.entities.aligner_scope_template.lookup("spread", recursive=False)
        assert isinstance(spread, dfl.FnDef)
        assert spread.name == "spread"

        # Idempotent: same object returned.
        registry2 = aligner_builtins.ensure_aligner_builtins_loaded(module.context)
        assert registry is registry2

    def test_and_spec_trait_registered(self, fs_importer: FilesystemImporter) -> None:
        """Test that And<Spec> trait impl is registered via builtins.clk."""
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Dummy module for trait test
schema Dummy3 {
    uuid: 77777777-7777-7777-7777-777777777777;
    fields {
        // Value
        #0 value: Int64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_trait"), fs_importer)

        aligner_builtins.ensure_aligner_builtins_loaded(module.context)

        trait_registry = dfl_types.get_trait_registry(module)
        and_trait = trait_registry.get_trait("@clockwork::std::traits.And")
        assert and_trait is not None

        impl = trait_registry.find_impl(and_trait, clkbuiltins.SPEC_TYPE, clkbuiltins.SPEC_TYPE)
        assert impl is not None

    def test_spread_body_resolves_dfl_builtins(self, fs_importer: FilesystemImporter) -> None:
        """Spread fn body references map and sum which are DFL builtins.

        These must be resolvable through spread's body scope chain.
        """
        source = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Module for spread scope test
schema Dummy4 {
    uuid: 66666666-6666-6666-6666-666666666666;
    fields {
        // Value
        #0 value: Int64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_spread_scope"), fs_importer)

        aligner_builtins.ensure_aligner_builtins_loaded(module.context)
        registry = aligner_builtins.ensure_aligner_builtins_loaded(module.context)
        spread = registry.entities.aligner_scope_template.lookup("spread", recursive=False)
        assert isinstance(spread, dfl.FnDef)

        # All refs in spread's body should be resolvable (map, sum, fn params, lambda params)
        for ref in dfl.find_refs(spread.body):
            ref.lookup()  # Should not raise NameValidationError


_ALIGNER_POLICY_SOURCE = """\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
use std::aligner_metrics_policy::{AlignerEventMetricsPolicy, AlignerTelemetryMetricsPolicy};

// Schema for aligner messages.
schema SensorMsg {
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields {
        // Observation timestamp.
        #0 observation_time: SyncTime;
    }
}

// Aligner under test.
aligner PolicyTestAligner {
    inputs {
        // Primary sensor input.
        primary: Tappy<SensorMsg> {
            max_msgs: 10;
        }
    }
}

policy AlignerEventMetricsPolicy for PolicyTestAligner { enabled = false; }
policy AlignerTelemetryMetricsPolicy for PolicyTestAligner { enabled = true; }
"""


class TestAlignerMetricsPolicy:
    """Tests for AlignerEventMetricsPolicy and AlignerTelemetryMetricsPolicy binding."""

    def test_policy_lookup_on_aligner(self, fs_importer: FilesystemImporter) -> None:
        """Test that both aligner metrics policies can be applied and looked up."""
        module = compiler.compile_source_text(
            _ALIGNER_POLICY_SOURCE,
            ModuleID(CLK_REPO, "test_aligner_metrics_policy"),
            fs_importer,
        )

        aligner_ir = module.inner_scope.lookup("PolicyTestAligner")
        assert isinstance(aligner_ir, aligner.Aligner)

        # Retrieve the policy classes from the use-imported definitions.
        event_policy_def = module.inner_scope.lookup("AlignerEventMetricsPolicy")
        assert isinstance(event_policy_def, policy.PolicyDef)
        event_policy_class = event_policy_def.get_resolved()

        telemetry_policy_def = module.inner_scope.lookup("AlignerTelemetryMetricsPolicy")
        assert isinstance(telemetry_policy_def, policy.PolicyDef)
        telemetry_policy_class = telemetry_policy_def.get_resolved()

        # Verify that lookup_policy resolves to a PolicyData bound to the aligner.
        event_pd = policy.lookup_policy(module, event_policy_class, aligner_ir)
        assert isinstance(event_pd, policy.PolicyData)
        assert event_pd.policy_class is event_policy_class
        assert event_pd.target is aligner_ir
        assert not primitive.value_to_bool(event_pd.data.data["enabled"])

        telemetry_pd = policy.lookup_policy(module, telemetry_policy_class, aligner_ir)
        assert isinstance(telemetry_pd, policy.PolicyData)
        assert telemetry_pd.policy_class is telemetry_policy_class
        assert telemetry_pd.target is aligner_ir
        assert primitive.value_to_bool(telemetry_pd.data.data["enabled"])
