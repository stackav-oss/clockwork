# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for tachyon_layout_reg module."""

from collections.abc import Sequence

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, typesys
from clockwork.dsl.serialization import tachyon_layout, tachyon_layout_reg, tachyon_reg
from typing_extensions import override


# Create a test type that isn't pre-registered in the registry
class MockType(typesys.TypeVal):
    """Test type for unit tests."""

    def __init__(self, name: str) -> None:
        """Create test type."""
        self.test_name = name

    @override
    def value_key(self) -> str:
        """Return a unique key for this type."""
        return f"TestType:{self.test_name}"

    @override
    def generic_parameters(self) -> Sequence[typesys.Parameter] | None:
        """This is not a generic type."""
        return None

    @override
    def __str__(self) -> str:
        """String representation."""
        return f"TestType({self.test_name})"


@pytest.fixture()
def compiler_context() -> CompilerContext:
    """Create a clean compiler context for testing."""
    return CompilerContext()


@pytest.fixture()
def test_type() -> MockType:
    """Create a test type for testing."""
    return MockType("Test")


def test_register_and_lookup(compiler_context: CompilerContext, test_type: MockType) -> None:
    """Test that registration and lookup work properly."""
    layout = tachyon_layout.Layout(
        size=42,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)],
        gaps=[],
    )
    tachyon_layout_reg.register_structured_type(compiler_context, test_type, layout)
    result = tachyon_layout_reg.layout_for_type(compiler_context, test_type)
    assert result == layout


def test_register_with_generic_parameters_raises(compiler_context: CompilerContext) -> None:
    """Test that attempting to register a generic type raises an exception."""
    layout = tachyon_layout.Layout(
        size=42,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)],
        gaps=[],
    )

    class GenericTestType(typesys.TypeVal):
        """Test generic type."""

        @override
        def value_key(self) -> str:
            """Return a unique key for this type."""
            return "GenericTestType"

        @override
        def generic_parameters(self) -> Sequence[typesys.Parameter] | None:
            """This is a generic type."""
            return (typesys.Parameter(name="T", type_bound=clkbuiltins.TYPE_TYPE, default=None),)

        @override
        def __str__(self) -> str:
            """String representation."""
            return "GenericTestType<T>"

    generic_type = GenericTestType(type_info=clkbuiltins.TYPE_TYPE)
    with pytest.raises(RuntimeError, match=r"Attempt to register generic type"):
        tachyon_layout_reg.register_structured_type(compiler_context, generic_type, layout)


def test_register_already_registered_raises(compiler_context: CompilerContext, test_type: MockType) -> None:
    """Test that attempting to register an already registered type raises an exception."""
    layout = tachyon_layout.Layout(
        size=42,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)],
        gaps=[],
    )
    tachyon_layout_reg.register_structured_type(compiler_context, test_type, layout)

    new_layout = tachyon_layout.Layout(
        size=24,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=24)],
        gaps=[],
    )
    with pytest.raises(RuntimeError, match=r"Attempt to register layout .* for type .* that is already registered"):
        tachyon_layout_reg.register_structured_type(compiler_context, test_type, new_layout)


def test_register_also_registers_constraint(compiler_context: CompilerContext, test_type: MockType) -> None:
    """Test that registering a layout also registers the corresponding constraint."""
    layout = tachyon_layout.Layout(
        size=42,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)],
        gaps=[],
    )
    tachyon_layout_reg.register_structured_type(compiler_context, test_type, layout)

    # Check that the type constraint was also registered
    constraint = tachyon_reg.constraint_for_type(compiler_context, test_type)
    assert constraint is not None
    assert constraint.size == layout.size
    assert constraint.alignment == layout.alignment


def test_layout_registry_import() -> None:
    """Test that LayoutRegistry imports work correctly."""
    # Create first registry with a type
    registry1 = tachyon_layout_reg.LayoutRegistry()
    layout1 = tachyon_layout.Layout(
        size=42, alignment=8, fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)], gaps=[]
    )
    test_type1 = MockType("Test1")
    registry1.layout_registry[test_type1.value_key()] = layout1

    # Create second registry with a different type
    registry2 = tachyon_layout_reg.LayoutRegistry()
    layout2 = tachyon_layout.Layout(
        size=24,
        alignment=4,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=24)],
        gaps=[],
    )
    test_type2 = MockType("Test2")
    registry2.layout_registry[test_type2.value_key()] = layout2

    # Import registry2 into registry1
    registry1.import_from(registry2)

    # Check that both types are in registry1
    assert test_type1.value_key() in registry1.layout_registry
    assert test_type2.value_key() in registry1.layout_registry
    assert registry1.layout_registry[test_type1.value_key()] == layout1
    assert registry1.layout_registry[test_type2.value_key()] == layout2


def test_layout_registry_import_conflict() -> None:
    """Test that LayoutRegistry import raises on conflicting layouts."""
    # Create first registry with a type
    registry1 = tachyon_layout_reg.LayoutRegistry()
    layout1 = tachyon_layout.Layout(
        size=42,
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=42)],
        gaps=[],
    )
    test_type = MockType("TestConflict")
    registry1.layout_registry[test_type.value_key()] = layout1

    # Create second registry with the same type but different layout
    registry2 = tachyon_layout_reg.LayoutRegistry()
    layout2 = tachyon_layout.Layout(
        size=24,  # Different size
        alignment=8,
        fields=[tachyon_layout.FieldSpan(field_num=1, offset=0, size=24)],
        gaps=[],
    )
    registry2.layout_registry[test_type.value_key()] = layout2

    # Importing should raise an error
    with pytest.raises(RuntimeError, match=r"Type .* has conflicting layouts"):
        registry1.import_from(registry2)
