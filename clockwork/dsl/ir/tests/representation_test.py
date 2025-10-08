# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for representation."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    representation,
    schema,
    schema_reg,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_representation_nominal(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    # Named representation
    pachyon = module.inner_scope.lookup("Pachyon", recursive=False)
    assert isinstance(pachyon, representation.Representation)
    assert pachyon.get_repr_type() is clkbuiltins.TACHYON
    schema_ir = pachyon.get_schema()
    hellomsg_schema = module.inner_scope.lookup("HelloMsg")
    assert isinstance(hellomsg_schema, schema.Schema)
    assert schema_ir is hellomsg_schema
    assert pachyon.options.align_bytes is None
    assert not pachyon.field_options

    # Default representation
    default_repr = schema_reg.lookup_default_representation(module.context, clkbuiltins.TACHYON, hellomsg_schema)
    assert default_repr is not None
    assert default_repr is not pachyon
    assert isinstance(default_repr.options.align_bytes, representation.DocableOption)
    assert default_repr.options.align_bytes.value == 64
    assert list(default_repr.field_options.keys()) == [2]
    fld2 = default_repr.field_options[2]
    assert isinstance(fld2.align_bytes, representation.DocableOption)
    assert fld2.align_bytes.value == 16


def test_representation_bad_type(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        representation UInt16 {}
"""
    with pytest.raises(TypeError, match=re.escape("Expected an instantiation but got ")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_not_tachyon(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        // Foo
        schema Foo { fields {
          // foo
          #1 foo: Int16;
        } }
        representation Pod<Foo> {}
"""
    with pytest.raises(TypeError, match=re.escape("Only Tachyon and Protobuf representation options implemented")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_not_schema(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        representation Tachyon<UInt16> {}
"""
    with pytest.raises(TypeError, match=re.escape("Tachyon or Protobuf must be instantiated for a schema type.")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_field_mismatch(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        // Foo
        schema Foo { fields {
          // foo
          #1 foo: Int16;
        } }
        representation Tachyon<Foo> { fields { #1 bar {}}}
"""
    with pytest.raises(ValueError, match=re.escape('Field 1 in schema "Foo" has name "foo", not "bar"')):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_bad_unit(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        // Foo
        schema Foo { fields {
          // foo
          #1 foo: Int16;
        } }
        representation Tachyon<Foo> { options { align: 3ms; }}
"""
    with pytest.raises(TypeError, match=re.escape("Expected a bit or byte unit value")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_bad_align(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        // Foo
        schema Foo { fields {
          // foo
          #1 foo: Int16;
        } }
        representation Tachyon<Foo> { options { align: 3byte; }}
"""
    with pytest.raises(ValueError, match=re.escape("Alignment 3 bytes is not an integral power of two")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_multiple_defaults(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        // Foo
        schema Foo { fields {
          // foo
          #1 foo: Int16;
        } }
        representation Tachyon<Foo> {}
        representation Tachyon<Foo> {}
"""
    with pytest.raises(
        ValueError,
        match=re.escape(
            "Cannot define more than one default representation options for each representation/schema pair.",
        ),
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_wrong_module(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        use clockwork::dsl::tests::support::hellomsg;
        representation Tachyon<hellomsg::GenericMsg> {}
"""
    with pytest.raises(
        ValueError,
        match=re.escape(
            "Default representation options must be defined in the same module as the schema they are for.",
        ),
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)


def test_representation_named_different_module(fs_importer: FilesystemImporter) -> None:
    source_text: Final = """
        use clockwork::dsl::tests::support::hellomsg;
        representation Test: Tachyon<hellomsg::GenericMsg> {}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "bad"), fs_importer)
    test = module.inner_scope.lookup("Test")
    assert isinstance(test, representation.Representation)
