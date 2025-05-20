# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for ir.compile."""

from __future__ import annotations

import re
import uuid
from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hello_enum(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    enum_ir = module.inner_scope.lookup("HelloEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.name == "HelloEnum"
    assert enum_ir.uuid == uuid.UUID(hex="bea931ce-fba6-4abd-941c-16ab44088aec")
    assert enum_ir.default_field_num == 1
    assert [(num, value.field_num, value.name, value.is_default) for num, value in enum_ir.values.items()] == [
        (1, 1, "hi", True),
        (2, 2, "hello", False),
        (3, 3, "hola", False),
        (4, 4, "ni_hao", False),
        (5, 5, "namaste", False),
    ]
    hola = enum_ir.lookup("hola")
    assert isinstance(hola, clkenum.ValueRef)
    assert hola.value_def is enum_ir.values[3]
    assert enum_ir.lookup("goodbye") is None


def test_no_default(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // No default
    enum NoDefault
    {
        values
        {
            // foo
            #1 foo;
            // bar
            #2 bar;
        }
    }
"""
    with pytest.raises(ValueError, match=re.escape("No value designated as default")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_underlying_type(fs_importer: FilesystemImporter) -> None:
    counts = [10, 256, 257]
    expected = [clkbuiltins.UINT8, clkbuiltins.UINT8, clkbuiltins.UINT16]
    for count, underlying_type in zip(counts, expected, strict=False):
        source = [
            "// Doc",
            "enum SomeEnum",
            "{",
            "    values",
            "    {",
        ]
        for index in range(count):
            source.append("        // Doc")
            default = " default" if index == 0 else ""
            source.append(f"        #{index + 1} field_{index}{default};")
        source.append("    }")
        source.append("}")

        module = compiler.compile_source_text("\n".join(source) + "\n", ModuleID(CLK_REPO, "foo"), fs_importer)
        enum_ir = module.inner_scope.lookup("SomeEnum", recursive=False)
        assert isinstance(enum_ir, clkenum.ClkEnum)
        assert enum_ir.get_underlying_type() == underlying_type


def test_multi_default(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // No default
    enum NoDefault
    {
        values
        {
            // foo
            #1 foo default;
            // bar
            #2 bar default;
        }
    }
"""
    with pytest.raises(ValueError, match=re.escape("Multiple values tagged as default")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_duplicate_number(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // No default
    enum NoDefault
    {
        values
        {
            // foo
            #1 foo default;
            // bar
            #1 bar;
        }
    }
"""
    with pytest.raises(ValueError, match=re.escape("Duplicate value number #1")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_duplicate_name(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // No default
    enum NoDefault
    {
        values
        {
            // foo
            #1 foo default;
            // bar
            #2 foo;
        }
    }
"""
    with pytest.raises(ValueError, match=re.escape('Redefinition of name "foo"')):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_underlying_values_signed(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying values
    enum Picky
    {
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: -32768; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    enum_ir = module.inner_scope.lookup("Picky", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.underlying_type is clkbuiltins.INT16
    assert enum_ir.values[1].integer_value == 0
    assert enum_ir.values[2].integer_value == -32768
    assert enum_ir.values[3].integer_value == 4


def test_underlying_values_unsigned(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying values
    enum Picky
    {
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: 65536; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    enum_ir = module.inner_scope.lookup("Picky", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.underlying_type is clkbuiltins.UINT32
    assert enum_ir.values[1].integer_value == 0
    assert enum_ir.values[2].integer_value == 65536
    assert enum_ir.values[3].integer_value == 4


def test_underlying_values_auto(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with auto underlying values
    enum Relaxed
    {
        values
        {
            // foo
            #11 foo;
            // bar
            #12 bar default;
            // baz
            #1024 baz;
        }
    }
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    enum_ir = module.inner_scope.lookup("Relaxed", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.underlying_type is clkbuiltins.UINT8
    assert enum_ir.linter_overrides == set()
    assert enum_ir.values[11].integer_value == 1
    assert enum_ir.values[12].integer_value == 0
    assert enum_ir.values[1024].integer_value == 2


def test_explicit_underlying_type(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying type
    enum Picky
    {
        options
        {
            underlying_type: UInt16;
            enable_linter_override: "performance-enum-size";
        }
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: 1; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    enum_ir = module.inner_scope.lookup("Picky", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.underlying_type is clkbuiltins.UINT16
    assert enum_ir.values[1].integer_value == 0
    assert enum_ir.values[2].integer_value == 1
    assert enum_ir.values[3].integer_value == 4
    assert isinstance(enum_ir.linter_overrides, set)
    assert enum_ir.linter_overrides == {"performance-enum-size"}


def test_explicit_underlying_type_fail_size(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying type
    enum Picky
    {
        options
        {
            underlying_type: UInt16;
        }
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: 65536; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Requested underlying type has 16 bits but 32 bits are required to hold all values."),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_explicit_underlying_type_fail_sign(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying type
    enum Picky
    {
        options
        {
            underlying_type: UInt16;
        }
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: -1; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Requested underlying type is unsigned but some values are specified as negative."),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_explicit_linter_override_fail_type(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying type
    enum Picky
    {
        options
        {
            enable_linter_override: 123;
        }
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: 65536; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    with pytest.raises(
        TypeError,
        match=re.escape("Expected a string literal for linter override but got"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_explicit_linter_override_fail_value(fs_importer: FilesystemImporter) -> None:
    source: Final = """
    // An enum with specified underlying type
    enum Picky
    {
        options
        {
            enable_linter_override: "foo";
        }
        values
        {
            // foo
            #1 foo default { underlying_value: 0; }
            // bar
            #2 bar { underlying_value: 65536; }
            // baz
            #3 baz { underlying_value: 4; }
        }
    }
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Linter override foo not recognized; expected one of {'performance-enum-size'}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_enum_with_history(fs_importer: FilesystemImporter) -> None:
    """Test an enum with history block."""
    source = """
    // An enum with history
    enum HistoryEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #3 second;
        }

        history
        {
            versions: [2, 3];
            values
            {
                // old value
                #2 old -> removed #3;
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_with_history"), fs_importer)
    enum_ir = module.inner_scope.lookup("HistoryEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.cur_version() == 3

    # Check that history was parsed correctly
    assert enum_ir.history is not None
    assert enum_ir.history.versions == [2, 3]
    assert len(enum_ir.history.values) == 1
    assert 2 in enum_ir.history.values

    # Check resolved history
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.versions == [2, 3]
    assert len(resolved.history.values) == 1
    assert 2 in resolved.history.values
    assert resolved.history.values[2].name == "old"
    assert resolved.history.values[2].removed_in_version == 3


def test_version_calculation(fs_importer: FilesystemImporter) -> None:
    """Test that enum version is calculated correctly."""
    source = """
    // An enum with high field numbers
    enum VersionEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
            // high value
            #100 high;
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_version_calculation"), fs_importer)
    enum_ir = module.inner_scope.lookup("VersionEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.cur_version() == 100

    # Check resolved version
    resolved = enum_ir.get_resolved()
    assert resolved.cur_version() == 100
