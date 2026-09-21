# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for enum history functionality."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import clkenum, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_basic_enum_history(fs_importer: FilesystemImporter) -> None:
    """Test basic enum history block parsing."""
    source = """
    // An enum with history
    enum BasicHistoryEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
            // fourth value
            #4 fourth;
        }

        history
        {
            version: 4;
            removed: [3];
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_basic_enum_history"), fs_importer)
    enum_ir = module.inner_scope.lookup("BasicHistoryEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.version == 4
    assert resolved.history.removed == {3}


def test_enum_history_became(fs_importer: FilesystemImporter) -> None:
    """Test enum history with 'became' relationship."""
    source = """
    // An enum with history and became relationship
    enum BecameEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
            // fourth value renamed from old_fourth
            #4 fourth;
        }

        history
        {
            version: 4;
            legacy_became: [3->4];
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_history_became"), fs_importer)
    enum_ir = module.inner_scope.lookup("BecameEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.legacy_became == {3: 4}


def test_enum_history_validation_errors(fs_importer: FilesystemImporter) -> None:
    """Test validation errors in enum history."""
    # Test overlapping field numbers
    source1 = """
    // An enum with overlapping field numbers
    enum OverlapEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
            // third value
            #3 third;
        }

        history
        {
            version: 3;
            removed: [3];
        }
    }
    """
    with pytest.raises(ValueError, match=r"Value numbers \{3\} are used in both current and historical values"):
        compiler.compile_source_text(source1, ModuleID(CLK_REPO, "test_enum_history_overlap"), fs_importer)

    # Test multiple values becoming the same value
    source3 = """
    // An enum with multiple values becoming same value
    enum MultipleBecameEnum
    {
        values
        {
            // first value
            #1 first default;
            // fourth value
            #4 fourth;
        }

        history
        {
            version: 4;
            legacy_became: [2->4, 3->4];
        }
    }
    """
    with pytest.raises(ValueError, match=r"Duplicate new field number 4 in legacy became block.*"):
        compiler.compile_source_text(source3, ModuleID(CLK_REPO, "test_enum_history_multiple_became"), fs_importer)


def test_auto_assign_vs_explicit_values(fs_importer: FilesystemImporter) -> None:
    """Test auto-assignment vs explicit values in historical enums."""
    source = """
    // An enum with explicit values
    enum ExplicitValuesEnum
    {
        values
        {
            // first value
            #1 first default { underlying_value: 0; }
            // second value
            #2 second { underlying_value: 10; }
            // third value
            #4 third { underlying_value: 20; }
        }

        history
        {
            version: 4;
            removed: [3];
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_auto_assign_vs_explicit"), fs_importer)
    enum_ir = module.inner_scope.lookup("ExplicitValuesEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()

    assert len(resolved.values) == 3
    assert resolved.values[1].integer_value == 0
    assert resolved.values[2].integer_value == 10
    assert resolved.values[4].integer_value == 20
    assert resolved.has_explicit_values is True

    # Test auto-assigned values
    source2 = """
    // An enum with auto-assigned values
    enum AutoAssignEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
            // third value
            #4 third;
        }

        history
        {
            version: 4;
            removed: [3];
        }
    }
    """
    module = compiler.compile_source_text(source2, ModuleID(CLK_REPO, "test_auto_assigned"), fs_importer)
    enum_ir = module.inner_scope.lookup("AutoAssignEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    resolved = enum_ir.get_resolved()

    assert len(resolved.values) == 3
    assert resolved.values[1].integer_value == 0  # default value is 0
    assert resolved.values[2].integer_value == 1  # others get sequential values
    assert resolved.values[4].integer_value == 2
    assert resolved.has_explicit_values is False
