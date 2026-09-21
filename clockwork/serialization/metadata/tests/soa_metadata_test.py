# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for SoA metadata generation and serialization."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.serialization.metadata import tachyon
from clockwork.serialization.metadata import tachyon_model as model


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_fixed_soa_metadata_generation(fs_importer: FilesystemImporter) -> None:
    """Test that FixedSoa types generate correct SoaType metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: 11111111-1111-1111-1111-111111111111;
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}

// Container for points
schema Container
{
  uuid: 22222222-2222-2222-2222-222222222222;
  fields
  {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=10>;
  }
}

cpp_target test_cpp
{
  options { namespace test; }
  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source.strip(), ModuleID("test", "fixed_soa_meta"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)

    # Generate metadata for the container
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema.get_resolved())
    metadata = tachyon.get_metadata(module.context, container_ir)

    # The outer type should be the container schema
    outer_type = metadata.types[metadata.outer_type_id]
    assert isinstance(outer_type, model.SchemaType)
    assert outer_type.fqn == "@test::fixed_soa_meta::Container"

    # Find the FixedSoa type in the metadata
    soa_type = None
    for type_desc in metadata.types:
        if isinstance(type_desc, model.SoaType) and type_desc.fqn == ".FixedSoa":
            soa_type = type_desc
            break

    assert soa_type is not None, "FixedSoa type should be in metadata"
    assert soa_type.uuid == clkbuiltins.FIXED_SOA.uuid
    assert soa_type.container_size == 10
    assert soa_type.size_field_offset is None  # FixedSoa has no size field
    assert soa_type.size_field_type_id is None

    # Verify field layouts
    assert len(soa_type.field_layouts) == 3  # x, y, z
    field_names = [f.name for f in soa_type.field_layouts]
    assert field_names == ["x", "y", "z"]

    # Verify each field has correct metadata
    for field_layout in soa_type.field_layouts:
        assert field_layout.offset >= 0
        assert field_layout.num in [0, 1, 2]
        assert field_layout.type_id >= 0

        # Verify the field type is Float32
        field_type = metadata.types[field_layout.type_id]
        assert isinstance(field_type, model.BuiltInType)
        assert field_type.fqn == ".Float32"


def test_var_soa_metadata_generation(fs_importer: FilesystemImporter) -> None:
    """Test that VarSoa types generate correct SoaType metadata with size field."""
    source = """
// 3D point
schema Point3f
{
  uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
  options { soa_enabled: true; }
  fields
  {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}

// Container for points
schema Container
{
  uuid: bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb;
  fields
  {
    // Variable-size SoA of points
    #0 points: VarSoa<type=Point3f, max_size=100>;
  }
}

cpp_target test_cpp
{
  options { namespace test; }
  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source.strip(), ModuleID("test", "var_soa_meta"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)

    metadata = tachyon.get_metadata(
        module.context, schema.InstantiatedSchema.from_typespec(container_schema.get_resolved())
    )

    # Find the VarSoa type
    soa_type = None
    for type_desc in metadata.types:
        if isinstance(type_desc, model.SoaType) and type_desc.fqn == ".VarSoa":
            soa_type = type_desc
            break

    assert soa_type is not None, "VarSoa type should be in metadata"
    assert soa_type.uuid == clkbuiltins.VAR_SOA.uuid
    assert soa_type.container_size == 100

    # VarSoa should have a size field
    assert soa_type.size_field_offset is not None
    assert soa_type.size_field_type_id is not None

    # Size field should be UInt8 for max_size=100
    size_field_type = metadata.types[soa_type.size_field_type_id]
    assert isinstance(size_field_type, model.BuiltInType)
    assert size_field_type.fqn == ".UInt8"

    # Verify field layouts
    assert len(soa_type.field_layouts) == 3


def test_soa_protobuf_roundtrip(fs_importer: FilesystemImporter) -> None:
    """Test that SoaType metadata can be serialized and deserialized through protobuf."""
    source = """
// 3D point
schema Point3f
{
  uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
  options { soa_enabled: true; }
  fields
  {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}

// Container for points
schema Container
{
  uuid: bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb;
  fields
  {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=42>;
  }
}

cpp_target test_cpp
{
  options { namespace test; }
  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source.strip(), ModuleID("test", "soa_roundtrip"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)

    original_metadata = tachyon.get_metadata(
        module.context, schema.InstantiatedSchema.from_typespec(container_schema.get_resolved())
    )

    # Convert to protobuf and back
    pb_metadata = tachyon.to_protobuf(original_metadata)
    roundtrip_metadata = tachyon.from_protobuf(pb_metadata)

    # Find the SoaType in both metadata instances
    original_soa = None
    roundtrip_soa = None

    for type_desc in original_metadata.types:
        if isinstance(type_desc, model.SoaType):
            original_soa = type_desc
            break

    for type_desc in roundtrip_metadata.types:
        if isinstance(type_desc, model.SoaType):
            roundtrip_soa = type_desc
            break

    assert original_soa is not None
    assert roundtrip_soa is not None

    # Verify all fields match
    assert original_soa.fqn == roundtrip_soa.fqn
    assert original_soa.uuid == roundtrip_soa.uuid
    assert original_soa.size == roundtrip_soa.size
    assert original_soa.alignment == roundtrip_soa.alignment
    assert original_soa.container_size == roundtrip_soa.container_size
    assert original_soa.size_field_offset == roundtrip_soa.size_field_offset
    assert original_soa.size_field_type_id == roundtrip_soa.size_field_type_id

    # Verify field layouts
    assert len(original_soa.field_layouts) == len(roundtrip_soa.field_layouts)
    for orig_field, rt_field in zip(original_soa.field_layouts, roundtrip_soa.field_layouts, strict=True):
        assert orig_field.offset == rt_field.offset
        assert orig_field.num == rt_field.num
        assert orig_field.name == rt_field.name
        assert orig_field.type_id == rt_field.type_id


def test_soa_hash_computation(fs_importer: FilesystemImporter) -> None:
    """Test that SoaType hash is computed correctly."""
    source = """
// 3D point
schema Point3f
{
  uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
  options { soa_enabled: true; }
  fields
  {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}

// Container for points
schema Container
{
  uuid: bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb;
  fields
  {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=10>;
  }
}

cpp_target test_cpp
{
  options { namespace test; }
  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source.strip(), ModuleID("test", "soa_hash"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)

    metadata1 = tachyon.get_metadata(
        module.context, schema.InstantiatedSchema.from_typespec(container_schema.get_resolved())
    )
    metadata2 = tachyon.get_metadata(
        module.context, schema.InstantiatedSchema.from_typespec(container_schema.get_resolved())
    )

    # Find SoaTypes
    soa1 = None
    soa2 = None

    for type_desc in metadata1.types:
        if isinstance(type_desc, model.SoaType):
            soa1 = type_desc
            break

    for type_desc in metadata2.types:
        if isinstance(type_desc, model.SoaType):
            soa2 = type_desc
            break

    assert soa1 is not None
    assert soa2 is not None

    # Hashes should be identical for identical types
    hash1 = soa1.get_hash(metadata1.types)
    hash2 = soa2.get_hash(metadata2.types)
    assert hash1 == hash2
    assert len(hash1) == 16  # MD5 hash is 16 bytes


def test_soa_different_sizes_different_hashes(fs_importer: FilesystemImporter) -> None:
    """Test that different container sizes produce different hashes."""
    source_10 = """
// 3D point
schema Point3f
{
  uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
  options { soa_enabled: true; }
  fields {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}
// Container for points
schema Container {
  uuid: cccccccc-cccc-cccc-cccc-cccccccccccc;
  fields {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=10>;
  }
}
cpp_target test_cpp { options { namespace test; } representation Tachyon<Point3f>; representation Tachyon<Container>; }
"""

    source_20 = """
// 3D point
schema Point3f
{
  uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
  options { soa_enabled: true; }
  fields {
    // X coordinate
    #0 x: Float32;
    // Y coordinate
    #1 y: Float32;
    // Z coordinate
    #2 z: Float32;
  }
}
// Container for points
schema Container {
  uuid: dddddddd-dddd-dddd-dddd-dddddddddddd;
  fields {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=20>;
  }
}
cpp_target test_cpp { options { namespace test; } representation Tachyon<Point3f>; representation Tachyon<Container>; }
"""

    module1 = compiler.compile_source_text(source_10, ModuleID("test", "soa_10"), importer=fs_importer)
    module2 = compiler.compile_source_text(source_20, ModuleID("test", "soa_20"), importer=fs_importer)

    container1 = module1.inner_scope.lookup("Container")
    container2 = module2.inner_scope.lookup("Container")
    assert isinstance(container1, schema.Schema)
    assert isinstance(container2, schema.Schema)

    metadata1 = tachyon.get_metadata(
        module1.context, schema.InstantiatedSchema.from_typespec(container1.get_resolved())
    )
    metadata2 = tachyon.get_metadata(
        module2.context, schema.InstantiatedSchema.from_typespec(container2.get_resolved())
    )

    soa1 = next((t for t in metadata1.types if isinstance(t, model.SoaType)), None)
    soa2 = next((t for t in metadata2.types if isinstance(t, model.SoaType)), None)

    assert soa1 is not None
    assert soa2 is not None
    assert soa1.container_size == 10
    assert soa2.container_size == 20

    # Different sizes should produce different hashes
    hash1 = soa1.get_hash(metadata1.types)
    hash2 = soa2.get_hash(metadata2.types)
    assert hash1 != hash2
