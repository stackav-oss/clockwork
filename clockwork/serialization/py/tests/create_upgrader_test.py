# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for create_upgrader module."""

import pytest
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import create_upgrader
from clockwork.serialization.py.tests.support import (
    py_broken_schema_v1,
    py_broken_schema_v2,
    py_simple_schema_v1,
    py_simple_schema_v2,
)


def test_create_upgrader_no_upgrade_needed() -> None:
    """Test creating a deserializer when no upgrade is needed."""
    incoming_metadata = py_simple_schema_v2.SimpleSchemaV2.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)

    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    needs_ugprade, upgrader = create_upgrader.create_upgrader(
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_module_name(),
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_source_file_name(),
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_class_name(),
        memoryview(serialized_incoming_metadata),
        memoryview(serialized_incoming_metadata),
        "SimpleSchemaV2",
    )
    assert needs_ugprade is False
    assert upgrader is None


def test_create_upgrader_upgrade_needed() -> None:
    """Test creating a deserializer when upgrade is needed."""
    incoming_metadata = py_simple_schema_v1.SimpleSchemaV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_simple_schema_v2.SimpleSchemaV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    needs_ugprade, upgrader = create_upgrader.create_upgrader(
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_module_name(),
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_source_file_name(),
        py_simple_schema_v2.SimpleSchemaV2.get_tachyon_class_name(),
        memoryview(serialized_current_metadata),
        memoryview(serialized_incoming_metadata),
        "SimpleSchemaV1",
    )
    assert needs_ugprade is True
    assert upgrader is not None

    # Create a test instance and buffer
    test_instance = py_simple_schema_v1.SimpleSchemaV1()
    test_instance.integer_field = 42
    input_buffer = bytearray(py_simple_schema_v1.SimpleSchemaV1.get_tachyon_constraint().size)
    test_instance.serialize_tachyon(memoryview(input_buffer))

    # Verify the upgrader works and the upgraded buffer has expected contents
    output_buffer = bytearray(py_simple_schema_v2.SimpleSchemaV2.get_tachyon_constraint().size)
    upgrader(memoryview(input_buffer), memoryview(output_buffer))
    upgraded_instance = py_simple_schema_v2.SimpleSchemaV2.deserialize_tachyon(memoryview(output_buffer))

    assert upgraded_instance.integer_field == test_instance.integer_field
    assert not upgraded_instance.string_field


def test_create_upgrader_broken_int() -> None:
    """Test creating a deserializer when an integer field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenIntV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenIntV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported field type change for .*BrokenIntV2.integer_field"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenIntV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenIntV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenIntV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenIntV1",
        )


def test_create_upgrader_broken_optional() -> None:
    """Test creating a deserializer when an optional field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenOptionalV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenOptionalV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported field type change for .*BrokenOptionalV2.maybe_integer_field"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenOptionalV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenOptionalV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenOptionalV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenOptionalV1",
        )


def test_create_upgrader_broken_array() -> None:
    """Test creating a deserializer when an array field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenArrayV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenArrayV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(
        ValueError, match="Unsupported field type change for .*BrokenArrayV2.broken_array.integer_field"
    ):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenArrayV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenArrayV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenArrayV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenArrayV1",
        )


def test_create_upgrader_broken_string() -> None:
    """Test creating a deserializer when an string field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenStringV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenStringV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported parameter change for .*BrokenStringV2.string_field"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenStringV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenStringV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenStringV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenStringV1",
        )


def test_create_upgrader_broken_builtin() -> None:
    """Test creating a deserializer when an builtin field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenBuiltInV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenBuiltInV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported field type change for .*BrokenBuiltInV2.builtin_field"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenBuiltInV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenBuiltInV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenBuiltInV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenBuiltInV1",
        )


def test_create_upgrader_broken_name() -> None:
    """Test creating a deserializer when field name has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenNameV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenNameV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(
        ValueError, match="Unsupported field name change for .*integer_field: new name is renamed_integer_field"
    ):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenNameV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenNameV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenNameV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenNameV1",
        )


def test_create_upgrader_broken_removed() -> None:
    """Test creating a deserializer when field been removed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenRemovedV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenRemovedV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported field removal: integer_field2"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenRemovedV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenRemovedV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenRemovedV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenRemovedV1",
        )


def test_create_upgrader_broken_strong_type() -> None:
    """Test creating a deserializer when strong type field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenStrongTypeV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenStrongTypeV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported field type change for .*BrokenStrongTypeV2.strong_type_field"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenStrongTypeV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenStrongTypeV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenStrongTypeV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenStrongTypeV1",
        )


def test_create_upgrader_broken_enum_name() -> None:
    """Test creating a deserializer when enum field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenEnumNameV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenEnumNameV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported value name change for.*value_one"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenEnumNameV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenEnumNameV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenEnumNameV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenEnumNameV1",
        )


def test_create_upgrader_broken_enum_value() -> None:
    """Test creating a deserializer when enum field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenEnumValueV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenEnumValueV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported enum value change for.*value1 from 0 to 1"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenEnumValueV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenEnumValueV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenEnumValueV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenEnumValueV1",
        )


def test_create_upgrader_broken_enum_removed() -> None:
    """Test creating a deserializer when enum field has been changed without history."""
    incoming_metadata = py_broken_schema_v1.BrokenEnumRemovedV1.get_tachyon_metadata()
    assert isinstance(incoming_metadata, tachyon_model.TachyonMetadata)
    current_metadata = py_broken_schema_v2.BrokenEnumRemovedV2.get_tachyon_metadata()
    assert isinstance(current_metadata, tachyon_model.TachyonMetadata)

    serialized_current_metadata = bytearray(tachyon_metadata.to_protobuf(current_metadata).SerializeToString())
    serialized_incoming_metadata = bytearray(tachyon_metadata.to_protobuf(incoming_metadata).SerializeToString())
    with pytest.raises(ValueError, match="Unsupported value removal: value2 removed"):
        create_upgrader.create_upgrader(
            py_broken_schema_v2.BrokenEnumRemovedV2.get_tachyon_module_name(),
            py_broken_schema_v2.BrokenEnumRemovedV2.get_tachyon_source_file_name(),
            py_broken_schema_v2.BrokenEnumRemovedV2.get_tachyon_class_name(),
            memoryview(serialized_current_metadata),
            memoryview(serialized_incoming_metadata),
            "BrokenEnumRemovedV1",
        )
