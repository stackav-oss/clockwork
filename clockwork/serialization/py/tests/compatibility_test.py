# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for compatibility module."""

import unittest.mock
from typing import Any, cast

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import compatibility, protocol
from clockwork.serialization.py.tests.support import (
    simple_schema_v1_clk_py,
    simple_schema_v2_clk_nb,
    simple_schema_v2_clk_py,
)


@pytest.mark.parametrize(
    "expected_class", [simple_schema_v2_clk_py.SimpleSchemaV2, simple_schema_v2_clk_nb.SimpleSchemaV2]
)
def test_create_deserializer_no_upgrade_needed(
    expected_class: type[simple_schema_v2_clk_py.SimpleSchemaV2] | type[simple_schema_v2_clk_nb.SimpleSchemaV2],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Test creating a deserializer when no upgrade is needed."""
    if expected_class == simple_schema_v2_clk_py.SimpleSchemaV2:
        compiler_context = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_compiler_context()
    else:
        compiler_context = CompilerContext()

    expected_metadata = expected_class.get_tachyon_metadata()
    assert isinstance(expected_metadata, tachyon_model.TachyonMetadata)

    # When metadata versions match, should return the original deserializer
    needs_ugprade, deserializer = compatibility.create_deserializer(
        compiler_context, cast("type[protocol.Tachyon[Any]]", expected_class), expected_metadata, "SimpleSchemaV2"
    )
    assert needs_ugprade is False

    # Create a test instance and buffer
    test_instance = expected_class()
    test_instance.integer_field = 42
    test_instance.string_field = "test"
    buffer = bytearray(expected_class.get_tachyon_constraint().size)
    test_instance.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    # Verify the deserializer produces the same result as the class method
    result1: Any = deserializer(memoryview(buffer))
    result2: Any = expected_class.deserialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert isinstance(result1, expected_class)
    assert isinstance(result2, expected_class)
    assert result1.integer_field == result2.integer_field
    assert result1.string_field == result2.string_field

    # Additionally, verify the deserializer is callable with the same signature
    if isinstance(test_instance, simple_schema_v2_clk_py.SimpleSchemaV2):
        assert callable(deserializer)
        assert deserializer.__code__.co_argcount == expected_class.deserialize_tachyon.__code__.co_argcount


@pytest.mark.parametrize(
    "expected_class", [simple_schema_v2_clk_py.SimpleSchemaV2, simple_schema_v2_clk_nb.SimpleSchemaV2]
)
def test_create_deserializer_upgrade_needed(
    expected_class: type[simple_schema_v2_clk_py.SimpleSchemaV2] | type[simple_schema_v2_clk_nb.SimpleSchemaV2],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Test creating a deserializer when upgrade is needed."""
    if expected_class == simple_schema_v2_clk_py.SimpleSchemaV2:
        compiler_context = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_compiler_context()
    else:
        compiler_context = CompilerContext()

    incoming_class = simple_schema_v1_clk_py.SimpleSchemaV1

    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a deserializer that should upgrade from v1 to v2
    needs_upgrade, deserializer = compatibility.create_deserializer(
        compiler_context, cast("type[protocol.Tachyon[Any]]", expected_class), incoming_metadata, "SimpleSchemaV2"
    )
    assert needs_upgrade is True
    # Check that the deserializer is not the original deserialize_tachyon method
    assert deserializer is not expected_class.deserialize_tachyon

    # Create a v1 instance
    instance_v1 = incoming_class(integer_field=42)

    # Serialize to buffer
    buffer = bytearray(incoming_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))

    # Use the deserializer and verify the result
    result: Any = deserializer(memoryview(buffer))

    # Verify the result is a v2 instance with upgraded content
    assert result.integer_field == 42  # Value should be preserved despite type change
    assert result.string_field == ""  # New field should have default value


def test_create_deserializer_missing_schema_ir() -> None:
    """Test creating a deserializer when schema IR is missing."""
    compiler_context = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_compiler_context()

    incoming_class = simple_schema_v1_clk_py.SimpleSchemaV1
    expected_class = simple_schema_v2_clk_py.SimpleSchemaV2

    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a class that explicitly doesn't have get_tachyon_schema_ir method
    # Use spec_set to restrict available attributes
    mocked_class = unittest.mock.MagicMock(spec_set=["get_tachyon_metadata", "deserialize_tachyon", "__name__"])
    mocked_class.get_tachyon_metadata.return_value = expected_class.get_tachyon_metadata()

    # Should raise TypeError when trying to create a deserializer
    with pytest.raises(TypeError, match="is missing get_tachyon_schema_ir method"):
        compatibility.create_deserializer(compiler_context, mocked_class, incoming_metadata, "SimpleSchemaV2")  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip


def test_create_deserializer_missing_metadata() -> None:
    """Test creating a deserializer when metadata is missing."""
    compiler_context = simple_schema_v1_clk_py.SimpleSchemaV1.get_tachyon_compiler_context()

    incoming_class = simple_schema_v1_clk_py.SimpleSchemaV1
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a class that returns None for get_tachyon_metadata
    mocked_class = unittest.mock.MagicMock()
    mocked_class.__name__ = "MockedClass"
    mocked_class.get_tachyon_metadata.return_value = None

    # Should raise ValueError when trying to create a deserializer
    with pytest.raises(ValueError, match="No metadata available"):
        compatibility.create_deserializer(compiler_context, mocked_class, incoming_metadata, "SimpleSchemaV2")  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
