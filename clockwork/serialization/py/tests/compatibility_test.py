# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for compatibility module."""

import unittest.mock
import uuid
from typing import Any, cast

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import compatibility, tachyon_dyn


def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for tests."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def compile_simple_schema_v1() -> tuple[Any, schema.InstantiatedSchema]:
    """Compile a simple schema version 1."""
    schema_source = """
    // Test schema version 1
    schema SimpleSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SimpleSchema;
        representation Tachyon<SimpleSchema>;
        interface Tappy<SimpleSchema>;
    }
    """
    module = compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "simple_schema"), importer=fs_importer())
    schema_ir = module.inner_scope.lookup("SimpleSchema")
    assert isinstance(schema_ir, schema.Schema)
    schema_instance = schema.InstantiatedSchema.from_typespec(schema_ir)
    return module, schema_instance


def compile_simple_schema_v2() -> tuple[Any, schema.InstantiatedSchema]:
    """Compile a simple schema version 2."""
    schema_source = """
    // Test schema version 2
    schema SimpleSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field (upgraded)
        #1 integer_field: Int64;
        // String field
        #2 string_field: VarString<max_size=64>;
      }
      history
      {
        versions: [1, 2];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SimpleSchema;
        representation Tachyon<SimpleSchema>;
        interface Tappy<SimpleSchema>;
    }
    """
    module = compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "simple_schema"), importer=fs_importer())
    schema_ir = module.inner_scope.lookup("SimpleSchema")
    assert isinstance(schema_ir, schema.Schema)
    schema_instance = schema.InstantiatedSchema.from_typespec(schema_ir)
    return module, schema_instance


def test_validate_tachyon_types_compatibility_same_version() -> None:
    """Test validation when metadata versions match."""
    module_v2, schema_v2 = compile_simple_schema_v2()
    serdes: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    expected_class = serdes.py_class

    expected_metadata = expected_class.get_tachyon_metadata()

    # Same metadata should validate without needing upgrade
    result = compatibility.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=expected_metadata)
    assert result is False  # No upgrade needed


def test_validate_tachyon_types_compatibility_older_version() -> None:
    """Test validation when incoming metadata is older version."""
    module_v1, schema_v1 = compile_simple_schema_v1()
    module_v2, schema_v2 = compile_simple_schema_v2()

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    expected_class = serdes_v2.py_class
    incoming_class = serdes_v1.py_class

    expected_metadata = expected_class.get_tachyon_metadata()
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Older version should require upgrade
    result = compatibility.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)
    assert result is True  # Upgrade needed


def test_validate_tachyon_types_compatibility_newer_version() -> None:
    """Test validation when incoming metadata is newer version."""
    module_v1, schema_v1 = compile_simple_schema_v1()
    module_v2, schema_v2 = compile_simple_schema_v2()

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    expected_class = serdes_v1.py_class
    incoming_class = serdes_v2.py_class

    expected_metadata = expected_class.get_tachyon_metadata()
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Newer version should raise ValueError
    with pytest.raises(ValueError, match="Incoming version is beyond expected version"):
        compatibility.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)


def test_validate_tachyon_types_compatibility_different_uuid() -> None:
    """Test validation when schemas have different UUIDs."""
    module_v2, schema_v2 = compile_simple_schema_v2()
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    expected_class = serdes_v2.py_class

    expected_metadata = expected_class.get_tachyon_metadata()

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
        fields=schema_type.fields,
    )
    cast("list[Any]", incoming_metadata.types)[incoming_metadata.outer_type_id] = modified_schema_type

    # Different UUID should raise ValueError
    with pytest.raises(ValueError, match="Tachyon metadata UUID mismatch"):
        compatibility.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)


def test_validate_tachyon_types_compatibility_non_schema() -> None:
    """Test validation when outer type is not a schema."""
    module_v2, schema_v2 = compile_simple_schema_v2()
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    expected_class = serdes_v2.py_class

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
    with pytest.raises(TypeError, match="Expected outer type to be a schema"):
        compatibility.validate_tachyon_types_compatibility(expected=expected_metadata, incoming=incoming_metadata)


def test_create_deserializer_no_upgrade_needed() -> None:
    """Test creating a deserializer when no upgrade is needed."""
    module_v2, schema_v2 = compile_simple_schema_v2()

    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    expected_class = serdes_v2.py_class

    expected_metadata = expected_class.get_tachyon_metadata()

    # When metadata versions match, should return the original deserializer
    needs_ugprade, deserializer = compatibility.create_deserializer(
        module_v2.context, expected_class, expected_metadata, "SimpleSchema"
    )
    assert needs_ugprade is False

    # Create a test instance and buffer
    test_instance = expected_class(integer_field=42, string_field="test")
    buffer = bytearray(expected_class.get_tachyon_constraint().size)
    test_instance.serialize_tachyon(memoryview(buffer))

    # Verify the deserializer produces the same result as the class method
    result1: Any = deserializer(memoryview(buffer))
    result2: Any = expected_class.deserialize_tachyon(memoryview(buffer))

    assert isinstance(result1, expected_class)
    assert isinstance(result2, expected_class)
    assert result1.integer_field == result2.integer_field
    assert result1.string_field == result2.string_field

    # Additionally, verify the deserializer is callable with the same signature
    assert callable(deserializer)
    assert deserializer.__code__.co_argcount == expected_class.deserialize_tachyon.__code__.co_argcount


def test_create_deserializer_upgrade_needed() -> None:
    """Test creating a deserializer when upgrade is needed."""
    module_v1, schema_v1 = compile_simple_schema_v1()
    module_v2, schema_v2 = compile_simple_schema_v2()

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    incoming_class = serdes_v1.py_class
    expected_class = serdes_v2.py_class

    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a deserializer that should upgrade from v1 to v2
    needs_upgrade, deserializer = compatibility.create_deserializer(
        module_v2.context, expected_class, incoming_metadata, "SimpleSchema"
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
    module_v1, schema_v1 = compile_simple_schema_v1()
    module_v2, schema_v2 = compile_simple_schema_v2()

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    incoming_class = serdes_v1.py_class
    expected_class = serdes_v2.py_class

    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a class that explicitly doesn't have get_tachyon_schema_ir method
    # Use spec_set to restrict available attributes
    mocked_class = unittest.mock.MagicMock(spec_set=["get_tachyon_metadata", "deserialize_tachyon", "__name__"])
    mocked_class.get_tachyon_metadata.return_value = expected_class.get_tachyon_metadata()

    # Should raise TypeError when trying to create a deserializer
    with pytest.raises(TypeError, match="is missing get_tachyon_schema_ir method"):
        compatibility.create_deserializer(module_v2.context, mocked_class, incoming_metadata, "SimpleSchema")  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip


def test_create_deserializer_missing_metadata() -> None:
    """Test creating a deserializer when metadata is missing."""
    module_v1, schema_v1 = compile_simple_schema_v1()

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    incoming_class = serdes_v1.py_class
    incoming_metadata = incoming_class.get_tachyon_metadata()

    # Create a class that returns None for get_tachyon_metadata
    mocked_class = unittest.mock.MagicMock()
    mocked_class.__name__ = "MockedClass"
    mocked_class.get_tachyon_metadata.return_value = None

    # Should raise ValueError when trying to create a deserializer
    with pytest.raises(ValueError, match="No metadata available"):
        compatibility.create_deserializer(module_v1.context, mocked_class, incoming_metadata, "SimpleSchema")  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
