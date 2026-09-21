# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for aligner body type-checking."""

import re

import pytest
from clockwork.dsl.aligner.type_check import type_check_aligner
from clockwork.dsl.ir import aligner, aligner_builtins, clkbuiltins, compiler, dfl, dfl_types
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _compile_and_type_check(
    source: str,
    module_name: str,
    aligner_name: str,
    fs_importer: FilesystemImporter,
) -> aligner.Aligner:
    """Compile source text and type-check the named aligner's body.

    Args:
        source: CLK source text.
        module_name: Module ID name.
        aligner_name: Name of the aligner entity in the module.
        fs_importer: Filesystem importer.

    Returns:
        The compiled Aligner IR node (with body type-checked).
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), fs_importer)
    aligner_ir = module.inner_scope.lookup(aligner_name)
    assert isinstance(aligner_ir, aligner.Aligner)

    type_check_aligner(aligner_ir, module.context)
    return aligner_ir


# Shared schema boilerplate used by most tests.
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


class TestBodyTypeChecking:
    """Tests for type-checking of aligner body statements."""

    def test_spec_statements(self, fs_importer: FilesystemImporter) -> None:
        """Test require, minimize, maximize, assume, and composed specs in one aligner."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner exercising all spec statement forms
aligner AllSpecsAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(lidar.timestamp));
    assume(is_unique(lidar.timestamp));
    require(true);
    minimize(42);
    maximize(100);
    require(true) and require(true);
}}

{_SCHEMAS}
"""
        aligner_ir = _compile_and_type_check(source, "test_specs", "AllSpecsAligner", fs_importer)

        assert len(aligner_ir.body_stmts) == 6
        for stmt in aligner_ir.body_stmts:
            assert isinstance(stmt, aligner.AlignerSpecStmt)

    def test_let_and_fn_with_specs(self, fs_importer: FilesystemImporter) -> None:
        """Test let bindings, fn defs, and specs that reference them."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with let, fn, and spec statements
aligner LetFnSpecAligner {{
    inputs {{
        // Camera input
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Lidar input (batch)
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 5];
        }}
    }}

    let tolerance = 100;
    let batch_ts = lidar.timestamp;
    fn double(n) {{ n * n }}
    minimize(min(lidar.timestamp));
    require(true);
}}

{_SCHEMAS}
"""
        aligner_ir = _compile_and_type_check(source, "test_let_fn", "LetFnSpecAligner", fs_importer)

        assert len(aligner_ir.body_stmts) == 5

        let_stmt = aligner_ir.body_stmts[0]
        assert isinstance(let_stmt, aligner.AlignerLetBinding)
        assert let_stmt.type_info is not None

        batch_let = aligner_ir.body_stmts[1]
        assert isinstance(batch_let, aligner.AlignerLetBinding)
        assert isinstance(batch_let.type_info, dfl_types.CollectionType)
        assert batch_let.type_info.element_type is clkbuiltins.SYNC_TIME

        assert isinstance(aligner_ir.body_stmts[2], dfl.FnDef)
        assert isinstance(aligner_ir.body_stmts[3], aligner.AlignerSpecStmt)
        assert isinstance(aligner_ir.body_stmts[4], aligner.AlignerSpecStmt)

    def test_has_candidates(self, fs_importer: FilesystemImporter) -> None:
        """Test has_candidates with both non-batch and batch input references."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with has_candidates
aligner HasCandidatesAligner {{
    inputs {{
        // Non-batch input
        camera: Tappy<CameraImage>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
        // Batch input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
            batch_size: [2, 5];
        }}
    }}

    if has_candidates(camera) then require(true) else require(true);
    if has_candidates(lidar) then require(true) else require(true);
}}

{_SCHEMAS}
"""
        aligner_ir = _compile_and_type_check(source, "test_has_candidates", "HasCandidatesAligner", fs_importer)
        assert len(aligner_ir.body_stmts) == 2
        for stmt in aligner_ir.body_stmts:
            assert isinstance(stmt, aligner.AlignerSpecStmt)
            assert isinstance(stmt.expr, dfl.IfElse)
            assert isinstance(stmt.expr.test, dfl.Call)
            assert isinstance(stmt.expr.test.func, dfl.Ref)
            assert stmt.expr.test.func.lookup() is aligner_builtins.ALIGNER_BUILTINS_SCOPE.lookup("has_candidates")


class TestBodyTypeCheckingErrors:
    """Tests for type-checking error detection in aligner bodies."""

    def test_non_spec_body_statement_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that a bare expression (non-Spec) as a spec stmt raises TypeCheckError."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid spec stmt
aligner BadSpecAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    42;
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="Spec statement must have type Spec"):
            _compile_and_type_check(source, "test_non_spec_err", "BadSpecAligner", fs_importer)

    def test_require_non_bool_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that require(non-bool) raises TypeCheckError."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid require
aligner BadRequireAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    require(42);
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match=r"require.*expects Bool"):
            _compile_and_type_check(source, "test_require_non_bool", "BadRequireAligner", fs_importer)

    def test_minimize_maximize_non_ord_errors(self, fs_importer: FilesystemImporter) -> None:
        """Test that minimize/maximize with non-Ord types raise TypeCheckError."""
        minimize_source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid minimize
aligner BadMinimizeAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    minimize(true);
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="does not implement Ord"):
            _compile_and_type_check(minimize_source, "test_minimize_non_ord", "BadMinimizeAligner", fs_importer)

        maximize_source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid maximize
aligner BadMaximizeAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    maximize(true);
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="does not implement Ord"):
            _compile_and_type_check(maximize_source, "test_maximize_non_ord", "BadMaximizeAligner", fs_importer)

        has_candidates_source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid has_candidates
aligner BadHasCandidatesAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    has_candidates(true);
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match=re.escape("has_candidates() expects AlignerInput")):
            _compile_and_type_check(
                has_candidates_source, "test_has_candidates_non_ord", "BadHasCandidatesAligner", fs_importer
            )

    def test_min_on_nonbatch_scalar_field_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that min(nonbatch_input.field) is a type error (scalar, not collection)."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid min on non-batch scalar field
aligner ScalarMinAligner {{
    inputs {{
        // Non-batch input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    minimize(min(lidar.timestamp));
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="requires a collection"):
            _compile_and_type_check(source, "test_scalar_min_err", "ScalarMinAligner", fs_importer)

    def test_assume_non_bool_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that assume(non-bool) raises TypeCheckError."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with invalid assume
aligner BadAssumeAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(42);
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match=r"assume.*expects Bool"):
            _compile_and_type_check(source, "test_assume_non_bool", "BadAssumeAligner", fs_importer)

    def test_is_strictly_increasing_non_ord_error(self, fs_importer: FilesystemImporter) -> None:
        """Test that is_strictly_increasing on a non-Ord type raises TypeCheckError."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with non-Ord strictly increasing
aligner BadStrictlyIncAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_strictly_increasing(true));
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="does not implement Ord"):
            _compile_and_type_check(source, "test_strictly_inc_non_ord", "BadStrictlyIncAligner", fs_importer)

    def test_is_non_decreasing_returns_bool(self, fs_importer: FilesystemImporter) -> None:
        """is_non_decreasing(input.field) type-checks and returns Bool."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with is_non_decreasing
aligner NonDecAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(lidar.timestamp));
}}

{_SCHEMAS}
"""
        # Should not raise.
        _compile_and_type_check(source, "test_nondec_ok", "NonDecAligner", fs_importer)

    def test_is_non_decreasing_non_ord_error(self, fs_importer: FilesystemImporter) -> None:
        """is_non_decreasing on a non-Ord type raises TypeCheckError."""
        source = f"""\
#![generate(cpp)]
#![cpp(namespace=clockwork::test)]
// Aligner with non-Ord non-decreasing
aligner BadNonDecAligner {{
    inputs {{
        // Lidar input
        lidar: Tappy<LidarSweep>
        {{
            max_msgs: 10;
            arbitrary_selection: true;
        }}
    }}

    assume(is_non_decreasing(true));
}}

{_SCHEMAS}
"""
        with pytest.raises(dfl.TypeCheckError, match="does not implement Ord"):
            _compile_and_type_check(source, "test_nondec_non_ord", "BadNonDecAligner", fs_importer)
