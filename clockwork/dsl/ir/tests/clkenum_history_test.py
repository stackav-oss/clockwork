# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for enum history functionality."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler
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
            versions: [3, 4];
            values
            {
                // third value
                #3 third -> removed #4;
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_basic_enum_history"), fs_importer)
    enum_ir = module.inner_scope.lookup("BasicHistoryEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.versions == [3, 4]

    # Check historical values
    assert len(resolved.history.values) == 1
    assert 3 in resolved.history.values
    hist_value = resolved.history.values[3]
    assert hist_value.name == "third"
    assert hist_value.removed_in_version == 4
    assert hist_value.became_field_num is None

    # Test get_enum_at_version for version 3
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert set(version3.values.keys()) == {1, 2, 3}
    assert version3.values[1].name == "first"
    assert version3.values[2].name == "second"
    assert version3.values[3].name == "third"
    assert version3.values[3].integer_value == 2
    assert version3.values[1].is_default

    # Test get_enum_at_version for version 4 (current version)
    version4 = resolved.get_enum_at_version(4)
    assert len(version4.values) == 3
    assert set(version4.values.keys()) == {1, 2, 4}
    assert version4.values[1].name == "first"
    assert version4.values[2].name == "second"
    assert version4.values[4].name == "fourth"
    assert version4.values[4].integer_value == 2

    # Verify that getting the current version returns the same object
    assert version4 is resolved


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
            versions: [3, 4];
            values
            {
                // old name for fourth
                #3 old_fourth -> became #4;
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_history_became"), fs_importer)
    enum_ir = module.inner_scope.lookup("BecameEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None

    # Check historical values
    assert len(resolved.history.values) == 1
    assert 3 in resolved.history.values
    hist_value = resolved.history.values[3]
    assert hist_value.name == "old_fourth"
    assert hist_value.removed_in_version is None
    assert hist_value.became_field_num == 4

    # Test get_enum_at_version for version 3
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert set(version3.values.keys()) == {1, 2, 3}
    assert version3.values[1].name == "first"
    assert version3.values[2].name == "second"
    assert version3.values[3].name == "old_fourth"  # The old name
    assert version3.values[1].is_default

    # Auto-assigned values should be correctly ordered
    assert version3.values[1].integer_value == 0
    assert version3.values[2].integer_value == 1
    assert version3.values[3].integer_value == 2


def test_enum_history_options(fs_importer: FilesystemImporter) -> None:
    """Test enum history with options changes."""
    source = """
    // An enum with historical options changes
    enum OptionsEnum
    {
        options
        {
            underlying_type: UInt16;
        }

        values
        {
            // first value
            #1 first default { underlying_value: 0; }
            // second value
            #2 second { underlying_value: 1; }
            // third value
            #3 third { underlying_value: 2; }
        }

        history
        {
            versions: [2, 3];
            enum
            {
                options
                {
                    #2
                    {
                        bit_flags;
                        underlying_type: UInt8;
                    }
                }
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_history_options"), fs_importer)
    enum_ir = module.inner_scope.lookup("OptionsEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.options is not None

    # Check historical options
    assert len(resolved.history.options) == 1
    hist_option = resolved.history.options[0]
    assert hist_option.version == 2
    assert hist_option.bit_flags is True
    assert hist_option.underlying_type is clkbuiltins.UINT8

    # Test get_enum_at_version for version 2
    version2 = resolved.get_enum_at_version(2)
    assert len(version2.values) == 2
    assert set(version2.values.keys()) == {1, 2}
    assert version2.bit_flags is True
    assert version2.underlying_type is clkbuiltins.UINT8
    assert version2.has_explicit_underlying_type is True
    assert version2.has_explicit_values is True

    # Test get_enum_at_version for version 3 (current version)
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert version3.underlying_type is clkbuiltins.UINT16
    assert version3.has_explicit_underlying_type is True


def test_enum_history_both_values_and_options(fs_importer: FilesystemImporter) -> None:
    """Test enum history with both value changes and options changes."""
    source = """
    // An enum with both value and option changes
    enum ComplexEnum
    {
        options
        {
            underlying_type: UInt16;
        }

        values
        {
            // first value
            #1 first default { underlying_value: 0; }
            // second value
            #2 second { underlying_value: 1; }
            // fourth value
            #4 fourth { underlying_value: 3; }
        }

        history
        {
            versions: [2, 3, 4];
            values
            {
                // old third value
                #3 third { underlying_value: 2; } -> removed #4;
            }
            enum
            {
                options
                {
                    #2
                    {
                        bit_flags;
                        underlying_type: UInt8;
                    }
                }
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_history_complex"), fs_importer)
    enum_ir = module.inner_scope.lookup("ComplexEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None

    # Check historical values
    assert len(resolved.history.values) == 1
    assert 3 in resolved.history.values
    hist_value = resolved.history.values[3]
    assert hist_value.name == "third"
    assert hist_value.integer_value == 2
    assert hist_value.removed_in_version == 4

    # Check historical options
    assert resolved.history.options is not None
    assert len(resolved.history.options) == 1
    hist_option = resolved.history.options[0]
    assert hist_option.version == 2
    assert hist_option.bit_flags is True
    assert hist_option.underlying_type is clkbuiltins.UINT8

    # Test get_enum_at_version for version 2
    version2 = resolved.get_enum_at_version(2)
    assert len(version2.values) == 2
    assert set(version2.values.keys()) == {1, 2}
    assert version2.bit_flags is True
    assert version2.underlying_type is clkbuiltins.UINT8

    # Test get_enum_at_version for version 3
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert set(version3.values.keys()) == {1, 2, 3}
    assert version3.values[3].name == "third"
    assert version3.values[3].integer_value == 2
    assert version3.underlying_type is clkbuiltins.UINT16


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
            versions: [2, 3];
            values
            {
                // Invalid: #3 is already defined
                #3 old_third -> removed #4;
            }
        }
    }
    """
    with pytest.raises(ValueError, match=r"Value numbers \{3\} are used in both current and historical values"):
        compiler.compile_source_text(source1, ModuleID(CLK_REPO, "test_enum_history_overlap"), fs_importer)

    # Test invalid version reference
    source2 = """
    // An enum with invalid version reference
    enum InvalidVersionEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
        }

        history
        {
            versions: [2, 3];
            values
            {
                // Invalid: #5 doesn't exist
                #3 third -> became #5;
            }
        }
    }
    """
    with pytest.raises(ValueError, match=r"Historical value 3 references non-existent version 5"):
        compiler.compile_source_text(source2, ModuleID(CLK_REPO, "test_enum_history_invalid_version"), fs_importer)

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
            versions: [2, 3, 4];
            values
            {
                // Both becoming #4
                #2 old_second -> became #4;

                // third value
                #3 third -> became #4;
            }
        }
    }
    """
    with pytest.raises(ValueError, match=r"Historical values \d+ and \d+ cannot both become value 4"):
        compiler.compile_source_text(source3, ModuleID(CLK_REPO, "test_enum_history_multiple_became"), fs_importer)

    # Test missing current version
    source4 = """
    // An enum with missing current version
    enum MissingVersionEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #4 second;
            // third value
            #3 third;
        }

        history
        {
            versions: [1, 2];
            values
            {
                // No issue with the history itself
                #2 fourth -> removed #4;
            }
        }
    }
    """
    with pytest.raises(ValueError, match=r"Historical version does not include current version 4"):
        compiler.compile_source_text(source4, ModuleID(CLK_REPO, "test_enum_history_missing_version"), fs_importer)

    # Test invalid version in get_enum_at_version
    source5 = """
    // A valid enum for get_enum_at_version test
    enum ValidEnum
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
        }

        history
        {
            versions: [1, 2];
        }
    }
    """
    module = compiler.compile_source_text(source5, ModuleID(CLK_REPO, "test_get_enum_at_invalid_version"), fs_importer)
    enum_ir = module.inner_scope.lookup("ValidEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    resolved = enum_ir.get_resolved()

    # Check that requesting an invalid version raises an error
    with pytest.raises(ValueError, match=r"Version 3 not found in enum history"):
        resolved.get_enum_at_version(3)


def test_enum_history_with_default_value_change(fs_importer: FilesystemImporter) -> None:
    """Test enum history with default value change."""
    source = """
    // An enum with default value change
    enum DefaultChangeEnum
    {
        values
        {
            // first value
            #3 first;
            // second value is now default
            #4 second default;
        }

        history
        {
            versions: [2, 4];
            values
            {
                // first was default
                #1 first_old default -> became #3;
                // second
                #2 second -> became #4;
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_enum_history_default_change"), fs_importer)
    enum_ir = module.inner_scope.lookup("DefaultChangeEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None

    # Check historical values
    assert len(resolved.history.values) == 2
    assert 1 in resolved.history.values
    hist_value = resolved.history.values[1]
    assert hist_value.name == "first_old"
    assert hist_value.is_default is True
    assert hist_value.became_field_num == 3

    # Test get_enum_at_version for version 2
    version2 = resolved.get_enum_at_version(2)
    assert len(version2.values) == 2
    assert set(version2.values.keys()) == {1, 2}
    assert version2.values[1].name == "first_old"
    assert version2.values[1].is_default
    assert version2.default_field_num == 1

    # Verify default value switched in current version
    assert resolved.default_field_num == 4


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
            versions: [1, 2, 3, 4];
            values
            {
                // historical value with explicit value
                #3 fourth { underlying_value: 30; } -> removed #4;
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_auto_assign_vs_explicit"), fs_importer)
    enum_ir = module.inner_scope.lookup("ExplicitValuesEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()

    # Test get_enum_at_version for version 3
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert version3.values[1].integer_value == 0
    assert version3.values[2].integer_value == 10
    assert version3.values[3].integer_value == 30
    assert version3.has_explicit_values is True

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
            versions: [1, 2, 3, 4];
            values
            {
                // historical value
                #3 old_third -> removed #4;
            }
        }
    }
    """
    module = compiler.compile_source_text(source2, ModuleID(CLK_REPO, "test_auto_assigned"), fs_importer)
    enum_ir = module.inner_scope.lookup("AutoAssignEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    resolved = enum_ir.get_resolved()

    # Test get_enum_at_version for version 2 with auto-assigned values
    version3 = resolved.get_enum_at_version(3)
    assert len(version3.values) == 3
    assert version3.values[1].integer_value == 0  # default value is 0
    assert version3.values[2].integer_value == 1  # others get sequential values
    assert version3.values[3].integer_value == 2
    assert version3.has_explicit_values is False


def test_underlying_type_calculation(fs_importer: FilesystemImporter) -> None:
    """Test underlying type calculation for historical versions."""
    source = """
    // An enum with underlying type changes
    enum TypeChangeEnum
    {
        options
        {
            underlying_type: UInt16;
        }

        values
        {
            // first value
            #1 first default { underlying_value: 0; }
            // second value
            #3 second { underlying_value: 300; }
        }

        history
        {
            versions: [1, 2, 3];
            values
            {
                // will require uint16 to hold
                #2 big_value { underlying_value: 1000; } -> removed #3;
            }
            enum
            {
                options
                {
                    #1
                    {
                        underlying_type: UInt8;
                    }

                    #2
                    {
                        underlying_type: UInt16;
                    }
                }
            }
        }
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_underlying_type_calc"), fs_importer)
    enum_ir = module.inner_scope.lookup("TypeChangeEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Verify the enum was resolved correctly
    resolved = enum_ir.get_resolved()

    # Test get_enum_at_version for version 1 - should use uint8 from history options
    version1 = resolved.get_enum_at_version(1)
    assert version1.underlying_type is clkbuiltins.UINT8
    assert version1.has_explicit_underlying_type is True

    # Version 2 should have to use uint16 to hold the big value
    version2 = resolved.get_enum_at_version(2)
    assert len(version2.values) == 2
    assert version2.values[2].integer_value == 1000
    assert version2.underlying_type is clkbuiltins.UINT16
    assert version2.has_explicit_underlying_type is True

    # Version 3 should retain the uint16 type because it's still explicit
    version3 = resolved.get_enum_at_version(3)
    assert version3.underlying_type is clkbuiltins.UINT16
    assert version3.has_explicit_underlying_type is True


def test_enum_pseudoversions() -> None:
    """Test pseudoversions in enum history."""
    source = """
    // An enum with pseudoversions
    enum PseudoversionEnum
    {
        values
        {
            // first value
            #1 first default;
            // third value
            #4 third;
        }

        history
        {
            versions: [1, 2, 4, 5];
            version_pseudofields: [5];
            values
            {
                // Old name for third
                #3 old_third -> became #4;

                // second value
                #2 second -> removed #5;
            }
        }
    }
    """
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "pseudover_test"), fs_importer)
    enum_ir = module.inner_scope.lookup("PseudoversionEnum", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Test history parsing
    resolved = enum_ir.get_resolved()
    assert resolved.history is not None
    assert resolved.history.versions == [1, 2, 4, 5]
    assert resolved.history.pseudoversions == [5]

    # Test current version includes pseudoversions
    assert resolved.cur_version() == 5


def test_enum_pseudoversion_conflict() -> None:
    """Test that pseudoversions can't conflict with value numbers."""
    source = """
    // Test pseudoversion conflicts
    enum ConflictingPseudoversions
    {
        values
        {
            // first value
            #1 first default;
            // second value with number that conflicts with pseudoversion
            #3 second;
        }

        history
        {
            versions: [1, 3];
            version_pseudofields: [3];  // Conflicts with value #3
            values {}
        }
    }
    """
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(ValueError, match=r"Pseudoversions \{3\} conflict with value numbers"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "conflict_test"), fs_importer)


def test_empty_enum_pseudoversions() -> None:
    """Test enum with empty pseudoversions list."""
    source = """
    // Test empty pseudoversions
    enum EmptyPseudoversions
    {
        values
        {
            // first value
            #1 first default;
        }

        history
        {
            versions: [1];
            version_pseudofields: [];  // Empty pseudoversions list
            values {}
        }
    }
    """
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "empty_pseudover_test"), fs_importer)
    enum_ir = module.inner_scope.lookup("EmptyPseudoversions", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)
    assert enum_ir.history is not None
    assert enum_ir.history.pseudoversions == []


def test_enum_pseudoversion_in_current_version() -> None:
    """Test that pseudoversions are included in current version calculation."""
    source = """
    // Test pseudoversion in current version
    enum PseudoCurrentVersion
    {
        values
        {
            // first value
            #1 first default;
            // second value
            #2 second;
        }

        history
        {
            versions: [8];
            version_pseudofields: [8];  // Should become current version
            values {}
        }
    }
    """
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "pseudover_current_test"), fs_importer)
    enum_ir = module.inner_scope.lookup("PseudoCurrentVersion", recursive=False)
    assert isinstance(enum_ir, clkenum.ClkEnum)

    # Test current version calculation
    resolved = enum_ir.get_resolved()
    # current_version should be 8 (the pseudoversion), not 2 (the highest value number)
    assert enum_ir.cur_version() == 8
    assert resolved.cur_version() == 8
