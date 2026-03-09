# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for common module."""

import uuid
from typing import Any, cast

import pytest
from clockwork.dsl.ir import clkbuiltins
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import common
from clockwork.serialization.py.tests.support import (
    simple_schema_v1_clk_nb,
    simple_schema_v1_clk_py,
    simple_schema_v2_clk_nb,
    simple_schema_v2_clk_py,
)


@pytest.mark.parametrize(
    "expected_class", [simple_schema_v2_clk_py.SimpleSchemaV2, simple_schema_v2_clk_nb.SimpleSchemaV2]
)
def test_validate_tachyon_types_compatibility_same_version(
    expected_class: type[simple_schema_v2_clk_py.SimpleSchemaV2] | type[simple_schema_v2_clk_nb.SimpleSchemaV2],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Test validation when metadata versions match."""
    expected_metadata = expected_class.get_tachyon_metadata()
    assert isinstance(expected_metadata, tachyon_model.TachyonMetadata)

    # Same metadata should validate without needing upgrade
    result = common.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=expected_metadata)
    assert result is False  # No upgrade needed


@pytest.mark.parametrize(
    "expected_class", [simple_schema_v2_clk_py.SimpleSchemaV2, simple_schema_v2_clk_nb.SimpleSchemaV2]
)
def test_validate_tachyon_types_compatibility_older_version(
    expected_class: type[simple_schema_v2_clk_py.SimpleSchemaV2] | type[simple_schema_v2_clk_nb.SimpleSchemaV2],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Test validation when incoming metadata is older version."""
    incoming_class = simple_schema_v1_clk_py.SimpleSchemaV1

    expected_metadata = expected_class.get_tachyon_metadata()
    assert isinstance(expected_metadata, tachyon_model.TachyonMetadata)
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Older version should require upgrade
    result = common.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)
    assert result is True  # Upgrade needed


@pytest.mark.parametrize(
    "expected_class", [simple_schema_v1_clk_py.SimpleSchemaV1, simple_schema_v1_clk_nb.SimpleSchemaV1]
)
def test_validate_tachyon_types_compatibility_newer_version(
    expected_class: type[simple_schema_v1_clk_py.SimpleSchemaV1] | type[simple_schema_v1_clk_nb.SimpleSchemaV1],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
) -> None:
    """Test validation when incoming metadata is newer version."""
    incoming_class = simple_schema_v2_clk_py.SimpleSchemaV2

    expected_metadata = expected_class.get_tachyon_metadata()
    assert isinstance(expected_metadata, tachyon_model.TachyonMetadata)
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Newer version should raise ValueError
    with pytest.raises(ValueError, match=r"Incoming version for .* is beyond latest version"):
        common.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)


def test_validate_tachyon_types_compatibility_different_uuid() -> None:
    """Test validation when schemas have different UUIDs."""
    expected_metadata = simple_schema_v2_clk_py.SimpleSchemaV2.get_tachyon_metadata()

    # Create a modified metadata with different UUID
    incoming_metadata = tachyon_model.TachyonMetadata(
        version=0, outer_type_id=expected_metadata.outer_type_id, types=expected_metadata.types[:]
    )

    # Modify the UUID in the schema type
    schema_type = cast("tachyon_model.SchemaType", incoming_metadata.types[incoming_metadata.outer_type_id])
    modified_schema_type = tachyon_model.SchemaType(
        schema_uuid=uuid.UUID("00000000-0000-0000-0000-000000000000"),
        fqn=schema_type.fqn,
        version=schema_type.version,
        size=schema_type.size,
        alignment=schema_type.alignment,
        hash=schema_type.hash,
        arguments=schema_type.arguments,
        fields=schema_type.fields,
    )
    cast("list[Any]", incoming_metadata.types)[incoming_metadata.outer_type_id] = modified_schema_type

    # Different UUID should raise ValueError
    with pytest.raises(ValueError, match=r"Tachyon metadata UUID mismatch"):
        common.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)


def test_validate_tachyon_types_compatibility_non_schema() -> None:
    """Test validation when outer type is not a schema."""
    expected_class = simple_schema_v2_clk_py.SimpleSchemaV2

    expected_metadata = expected_class.get_tachyon_metadata()

    # Create a modified metadata with non-schema outer type
    incoming_metadata = tachyon_model.TachyonMetadata(
        version=0, outer_type_id=expected_metadata.outer_type_id, types=expected_metadata.types[:]
    )

    # Replace schema type with non-schema type
    cast("list[Any]", incoming_metadata.types)[incoming_metadata.outer_type_id] = tachyon_model.BuiltInType(
        fqn=".Int32", uuid=uuid.uuid5(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, ".Int32"), size=4, alignment=4, arguments=()
    )

    # Non-schema type should raise TypeError
    with pytest.raises(TypeError, match=r"Expected incoming type to be a schema"):
        common.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)
