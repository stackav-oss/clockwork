# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for the Clockwork importer."""

from pathlib import Path
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import cog, compiler, importer, node, schema
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_filesystem_import() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    hellocog = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")), fs_importer
    )
    assert isinstance(hellocog, node.Module)
    externs = hellocog.inner_scope.parent
    assert externs is not None
    hellomsg = externs.lookup("hellomsg")
    assert isinstance(hellomsg, node.NamespacedModule)
    assert isinstance(hellomsg.extern_module, node.Module)
    assert isinstance(hellomsg.extern_module.inner_scope.lookup("HelloMsg"), schema.Schema)
    assert isinstance(hellocog.inner_scope.lookup("HelloCog"), cog.Cog)


def test_multiple_import() -> None:
    """Tests that modules/entities used more than once are only imported once."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;
use clockwork::dsl::tests::support::hellomsg as hellomsg2
use clockwork::dsl::tests::support::hellomsg::HelloMsg;
use clockwork::dsl::tests::support::hellomsg::HelloMsg as HelloMsg2;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)
    assert isinstance(module, node.Module)
    externs = module.inner_scope.parent
    assert externs is not None
    hellomsg = externs.lookup("hellomsg")
    assert isinstance(hellomsg, node.NamespacedModule)
    assert isinstance(hellomsg.extern_module, node.Module)
    hellomsg2 = externs.lookup("hellomsg2")
    assert isinstance(hellomsg2, node.NamespacedModule)
    assert hellomsg2.extern_module is hellomsg.extern_module

    HelloMsg = module.inner_scope.lookup("HelloMsg")  # noqa: N806 (type names are be camel case by convention)
    assert isinstance(HelloMsg, schema.Schema)
    HelloMsg2 = module.inner_scope.lookup("HelloMsg2")  # noqa: N806 (type names are be camel case by convention)
    assert HelloMsg2 is HelloMsg


def test_empty_use() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = node.Module(
        doc=None,
        module_id=ModuleID("", ""),
        inner_scope=MagicMock(),
        terminals=None,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
    )
    with pytest.raises(ValueError, match="Cannot import an empty path: "):
        fs_importer.resolve_import(module, node.Module.UseResult(repo=None, path=(), alias=None))


def test_missing_module() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = node.Module(
        doc=None,
        module_id=ModuleID("", ""),
        inner_scope=MagicMock(),
        terminals=None,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
    )
    with pytest.raises(FileNotFoundError, match="Module not found: doesnotexist::Foo"):
        fs_importer.resolve_import(module, node.Module.UseResult(repo=None, path=("doesnotexist", "Foo"), alias=None))


def test_missing_entity() -> None:
    source_text = """
use clockwork::dsl::tests::support::hellomsg::Foo;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(
        ValueError,
        match='Entity named "Foo" not found in module at "clockwork/dsl/tests/support/hellomsg.clk"',
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)


def test_no_reexport() -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog::hellomsg;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(
        ValueError,
        match='Entity named "hellomsg" not found in module at "clockwork/dsl/tests/support/hellocog.clk"',
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)

    source_text = """
use clockwork::dsl::tests::support::hellocog;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)
    namespace = module.inner_scope.lookup("hellocog")
    assert isinstance(namespace, node.NamespacedModule)
    print(list(namespace.extern_module.inner_scope.names.keys()))
    assert namespace.lookup("hellomsg") is None


def test_same_repo() -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;

// Schema
schema Schema
{
    fields
    {
        // foo
        #1 foo: Int64;
    }
}

"""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)
    namespace = module.inner_scope.lookup("hellocog")
    assert isinstance(namespace, node.NamespacedModule)
    hellocog_module = namespace.extern_module
    assert isinstance(hellocog_module, node.Module)
    assert hellocog_module.module_id == ModuleID(CLK_REPO, "clockwork::dsl::tests::support::hellocog")


def test_different_repo() -> None:
    source_text = f"""
use @{CLK_REPO}::clockwork::dsl::tests::support::hellocog;

// Schema
schema Schema
{{
    fields
    {{
        // foo
        #1 foo: Int64;
    }}
}}

"""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source_text, ModuleID("test_repo", "test"), fs_importer)
    namespace = module.inner_scope.lookup("hellocog")
    assert isinstance(namespace, node.NamespacedModule)
    hellocog_module = namespace.extern_module
    assert isinstance(hellocog_module, node.Module)
    assert hellocog_module.module_id == ModuleID(CLK_REPO, "clockwork::dsl::tests::support::hellocog")
