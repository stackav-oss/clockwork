# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for ir.compile."""

from __future__ import annotations

from copy import copy
from pathlib import Path
from typing import Any
from unittest.mock import MagicMock, patch

import pytest
from clockwork.dsl.bazel import clk_targets, targets
from clockwork.dsl.cpp import context, typereg
from clockwork.dsl.ir import clkenum, cog, compiler, importer, node, schema, strongtypes, uuid_reg
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.tests.node_test import MockImporter


def test_compile_source_text() -> None:
    source_text = """
use a::b::c;

// Test schema
schema TestSchema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}

// Test cog
cog TestCog
{
    execution
    {
        // Execute at least at 2Hz
        condition periodic: time_since_last_exec(500ms);

        // That's our only condition actually
        execute when: periodic;
    }
    metrics_options
    {
        enabled: false;
    }
}
"""
    mock_importer = MockImporter()
    mock_importer.import_specs = {
        node.Module.UseResult(repo=None, path=("a", "b", "c"), alias=None): node.ImportSpec(
            ModuleID.from_path("", Path("a/b/c.clk")),
            entity_name=None,
            import_name="c",
        ),
    }
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test::module"), mock_importer)
    assert isinstance(module, node.Module)
    c = module.inner_scope.lookup("c")
    assert isinstance(c, node.NamespacedModule)
    assert c.extern_module is mock_importer.result_entities["c"]
    test_cog_ir = module.inner_scope.lookup("TestCog")
    assert isinstance(test_cog_ir, cog.Cog)
    uuid_reg.lookup_uuid(module.context, test_cog_ir.conditions["periodic"])
    assert isinstance(module.inner_scope.lookup("TestSchema"), schema.Schema)
    assert len(module.inner_scope.names) == 2
    assert module.inner_scope.parent is not None
    assert len(module.inner_scope.parent.names) == 1


def test_schema_tag() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        importer=fs_importer,
    )
    schema_ir = module.inner_scope.lookup("HelloMsg")
    assert isinstance(schema_ir, schema.Schema)
    cpp_type_info = typereg.get_cpp_type(schema_ir.module.context, schema_ir)
    assert cpp_type_info.includes == [context.Header(CLK_REPO, "clockwork/dsl/tests/support/hello_msg_onboard.hh")]
    assert cpp_type_info.render("") == "::clockwork::demo::HelloMsg"


def test_tag() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        importer=fs_importer,
    )
    tag_ir = module.inner_scope.lookup("SampleTag")
    assert isinstance(tag_ir, strongtypes.Tag)
    cpp_type_info = typereg.get_cpp_type(tag_ir.module.context, tag_ir)
    assert cpp_type_info.includes == [context.Header(CLK_REPO, "clockwork/dsl/tests/support/hello_msg_onboard.hh")]
    assert cpp_type_info.render("") == "::clockwork::demo::SampleTag"


def test_enum() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        importer=fs_importer,
    )
    enum_ir = module.inner_scope.lookup("HelloEnum")
    assert isinstance(enum_ir, clkenum.ClkEnum)
    cpp_type_info = typereg.get_cpp_type(enum_ir.module.context, enum_ir)
    assert cpp_type_info.includes == [context.Header(CLK_REPO, "clockwork/dsl/tests/support/hello_msg_onboard.hh")]
    assert cpp_type_info.render("") == "::clockwork::demo::HelloEnum"


def test_error_resolution_changes_entity_identity(monkeypatch: pytest.MonkeyPatch) -> None:
    def badresolver(parent: Any, scope: node.Scope) -> Any:  # noqa: ARG001, ANN401
        return copy(parent)

    source_text = """
    // Test
    schema Test
    {
        fields
        {
            // Docs
            #1 field: Int64;
        }
    }
"""

    monkeypatch.setattr(node, "resolve_names", badresolver)
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(RuntimeError, match="Internal error: Module entity identity changed by name resolution:"):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


@patch("clockwork.dsl.bazel.clk_targets.module_to_clk")
def test_clk_target(mock_path_to_clk: MagicMock, tmp_path: Path) -> None:
    mock_path_to_clk.side_effect = lambda _, module: targets.Label(  # pyright: ignore[reportUnknownLambdaType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        value=f"//{Path(module.get_base_path().parent)!s}:{Path(module.get_base_path().stem)!s}_clk"
    )

    source_text = """
use a::b::c;
use d::{e, f};

    // Valid syntax requires some element beyond just `use`.
    schema Test
    {
        fields
        {
            // Docs
            #0 value: UInt8;
        }
    }

    """

    expected_clk = clk_targets.Clk(
        name="module_clk",
        srcs=[Path("module.clk")],
        deps=[targets.Label("//a/b:c_clk"), targets.Label("//d:e_clk"), targets.Label("//d:f_clk")],
        outs=[],
    )

    (tmp_path / Path("a/b")).mkdir(parents=True)
    (tmp_path / Path("d")).mkdir()

    (tmp_path / Path("a/b/c.clk")).touch()
    (tmp_path / Path("d/e.clk")).touch()
    (tmp_path / Path("d/f.clk")).touch()

    module_id = ModuleID("repo", "path::to::module")
    clk_from_text = compiler.to_clk_target_from_text(source_text, module_id, search_paths=[tmp_path])
    assert clk_from_text == expected_clk

    (tmp_path / Path("path/to")).mkdir(parents=True)
    with (tmp_path / "path/to/module.clk").open("w") as f:
        f.write(source_text)

    clk_from_file = compiler.to_clk_target(module_id, search_paths=[tmp_path])
    assert clk_from_file == expected_clk
