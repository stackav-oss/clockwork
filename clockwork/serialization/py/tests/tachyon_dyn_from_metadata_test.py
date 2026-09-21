# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for tachyon_dyn_from_metadata."""

from __future__ import annotations

import re
import struct
import uuid
from dataclasses import fields
from decimal import Decimal
from pathlib import Path
from typing import Any, cast

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, compiler, primitive, schema, tensor_builtins
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import tachyon_dyn, tachyon_dyn_from_metadata


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_bitset_from_metadata() -> None:
    bitset_type = tachyon_model.BuiltInType(
        fqn=clkbuiltins.BITSET.fqn,
        uuid=clkbuiltins.BITSET.uuid,
        size=2,
        alignment=1,
        arguments=["10"],
    )
    serdes = tachyon_dyn_from_metadata._bitset_factory(CompilerContext(), 0, [bitset_type], {})
    assert serdes is not None
    buffer = bytearray(2)
    serdes.serializer(0x281, memoryview(buffer))
    assert buffer == b"\x81\x02"
    assert serdes.deserializer(memoryview(b"\x81\xfe")) == 0x281
    with pytest.raises(ValueError, match="Bitset<10> value"):
        serdes.serializer(1 << 10, memoryview(buffer))


def test_tapmsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")), fs_importer
    )
    tap_msg = module.inner_scope.lookup("TapMsg")
    assert isinstance(tap_msg, schema.Schema)
    tap_msg_ir = schema.InstantiatedSchema.make(
        tap_msg.get_resolved(),
        {"signed_value": primitive.DecimalValue(value=Decimal(234), type_info=clkbuiltins.INT64)},
    )

    TapMsgMeta: Any  # noqa: N806 Represents a type and should be camel case
    TapMsgMeta, py_types = tachyon_dyn_from_metadata.py_type_from_metadata(  # noqa: N806  Represents a type and should be camel case
        module.context,
        tap_msg_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, tap_msg_ir),
    )
    SomeEnumMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum"]  # noqa: N806 Represents a type and should be camel case
    SomeFlagsMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags"]  # noqa: N806 Represents a type and should be camel case
    SubMsgMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SubMsg"]  # noqa: N806 Represents a type and should be camel case
    TapMsgDyn, _ = tachyon_dyn.get_instantiation_dataclass(module.context, module, "TapMsg", signed_value=234)  # noqa: N806 Represents a type and should be camel case
    SomeEnumDyn, _ = tachyon_dyn.get_enum(module.context, module, "SomeEnum")  # noqa: N806 Represents a type and should be camel case
    SomeFlagsDyn, _ = tachyon_dyn.get_enum(module.context, module, "SomeFlags")  # noqa: N806 Represents a type and should be camel case
    SubMsgDyn, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "SubMsg")  # noqa: N806 Represents a type and should be camel case
    uuid1 = uuid.uuid4()
    uuid2 = uuid.uuid4()
    # fmt: off
    msg_meta = TapMsgMeta(
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        integer=1,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        floating_point=2.0,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        boolean=True,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        array_of_primitives=list(range(9)),
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        array_of_array=["one", "two"],
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        uuid=uuid1,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        uuid_different_namespace=uuid2,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        default_enum=SomeEnumMeta.second_value,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        enum_with_init=SomeEnumMeta.first_value,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        default_flags=SomeFlagsMeta.flag12,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        flags_with_init=SomeFlagsMeta.flag3 | SomeFlagsMeta.flag12,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        nested_schema=SubMsgMeta(field=42),
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        array_of_schema=[SubMsgMeta(field=11)],
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        duration=1234,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        sync_time=4567,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        optional=None,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        bool_with_init=False,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        strong_type=3,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        external_strong_type=4,
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        fixed_array=list(range(2)),
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        var_string="a",
        # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        integer_with_init=456,
    )
    # fmt: on
    msg_dyn = TapMsgDyn(
        integer=1,
        floating_point=2.0,
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["one", "two"],
        uuid=uuid1,
        uuid_different_namespace=uuid2,
        default_enum=SomeEnumDyn.second_value,
        enum_with_init=SomeEnumDyn.first_value,
        default_flags=SomeFlagsDyn.flag12,
        flags_with_init=SomeFlagsDyn.flag3 | SomeFlagsDyn.flag12,
        nested_schema=SubMsgDyn(field=42),
        array_of_schema=[SubMsgDyn(field=11)],
        duration=1234,
        sync_time=4567,
        optional=None,
        bool_with_init=False,
        strong_type=3,
        external_strong_type=4,
        fixed_array=list(range(2)),
        var_string="a",
        integer_with_init=456,
    )
    assert TapMsgDyn.get_tachyon_constraint() == TapMsgMeta.get_tachyon_constraint()
    assert TapMsgDyn.get_tachyon_metadata() == TapMsgMeta.get_tachyon_metadata()
    assert TapMsgDyn.get_tachyon_metadata_name() == TapMsgMeta.get_tachyon_metadata_name()
    buffer = bytearray(TapMsgDyn.get_tachyon_constraint().size)
    msg_meta.serialize_tachyon(memoryview(buffer))
    msg_meta2 = TapMsgMeta.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_meta2 == msg_meta
    msg_dyn2 = TapMsgDyn.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_dyn2 == msg_dyn
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg_meta.optional = 13
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg_meta.array_of_array = []
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg_meta.array_of_schema.append(SubMsgMeta(field=3))
    msg_meta2.serialize_tachyon(memoryview(buffer))
    msg_meta3 = TapMsgMeta.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_meta3 == msg_meta2

    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg_meta.array_of_schema.append(SubMsgDyn(field=3))
    with pytest.raises(ValueError, match=re.escape("Attempt to serialize array of length 3, max 2")):
        msg_meta.serialize_tachyon(memoryview(buffer))


def test_fixed_soa_basic(fs_importer: FilesystemImporter) -> None:
    """Test basic FixedSoa serialization and deserialization with metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: d5fe1bc7-3407-4722-b05d-1aff6c19fe09;
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
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Fixed-size SoA of points
    #0 points: FixedSoa<type=Point3f, size=5>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Create a container with FixedSoa
    points_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "points")
    container = container_class(
        points=points_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            x=[1.0, 2.0, 3.0, 4.0, 5.0],
            y=[10.0, 20.0, 30.0, 40.0, 50.0],
            z=[100.0, 200.0, 300.0, 400.0, 500.0],
        )
    )

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify
    assert container2.points.x == [1.0, 2.0, 3.0, 4.0, 5.0]
    assert container2.points.y == [10.0, 20.0, 30.0, 40.0, 50.0]
    assert container2.points.z == [100.0, 200.0, 300.0, 400.0, 500.0]


def test_var_soa_basic(fs_importer: FilesystemImporter) -> None:
    """Test basic VarSoa serialization and deserialization with metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: d5fe1bc7-3407-4722-b05d-1aff6c19fe09;
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
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of points
    #0 points: VarSoa<type=Point3f, max_size=10>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Create a container with VarSoa
    points_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "points")
    container = container_class(
        points=points_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            x=[1.0, 2.0, 3.0],
            y=[10.0, 20.0, 30.0],
            z=[100.0, 200.0, 300.0],
        )
    )

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify
    assert container2.points.x == [1.0, 2.0, 3.0]
    assert container2.points.y == [10.0, 20.0, 30.0]
    assert container2.points.z == [100.0, 200.0, 300.0]


def test_var_soa_empty(fs_importer: FilesystemImporter) -> None:
    """Test VarSoa with empty arrays with metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: d5fe1bc7-3407-4722-b05d-1aff6c19fe09;
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
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of points
    #0 points: VarSoa<type=Point3f, max_size=10>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Create empty VarSoa
    points_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "points")
    container = container_class(points=points_type())  # type: ignore[call-arg,misc] # pyright: ignore[reportCallIssue]

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify all fields are empty
    assert container2.points.x == []
    assert container2.points.y == []
    assert container2.points.z == []


def test_soa_with_padding(fs_importer: FilesystemImporter) -> None:
    """Test SoA with a schema that has padding in AoS form with metadata."""
    source = """
// Message with padding
schema PaddedMsg
{
  uuid: 8fa1ff8b-a84d-45cc-9d93-28b8cc9c3323;
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // Large field
    #0 large: Int64;
    // Small field
    #1 small: Bool;
  }
}

// Container for messages
schema Container
{
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of messages
    #0 messages: VarSoa<type=PaddedMsg, max_size=5>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<PaddedMsg>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "padded"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Create container with SoA
    messages_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "messages")
    container = container_class(
        messages=messages_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            large=[100, 200, 300],
            small=[True, False, True],
        )
    )

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify - SoA should handle padding correctly
    assert container2.messages.large == [100, 200, 300]
    assert container2.messages.small == [True, False, True]


def test_soa_size_validation(fs_importer: FilesystemImporter) -> None:
    """Test that VarSoa validates size limits with metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: d5fe1bc7-3407-4722-b05d-1aff6c19fe09;
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
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of points
    #0 points: VarSoa<type=Point3f, max_size=3>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Try to create with too many elements
    points_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "points")
    container = container_class(
        points=points_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            x=[1.0, 2.0, 3.0, 4.0],  # More than max_size=3
            y=[1.0, 2.0, 3.0, 4.0],
            z=[1.0, 2.0, 3.0, 4.0],
        )
    )

    buffer = bytearray(container_class.get_tachyon_constraint().size)

    with pytest.raises(ValueError, match=r"SoA size 4 exceeds max_size 3"):
        container.serialize_tachyon(memoryview(buffer))


def test_soa_mismatched_field_sizes(fs_importer: FilesystemImporter) -> None:
    """Test that SoA validates all field arrays have the same size with metadata."""
    source = """
// 3D point
schema Point3f
{
  uuid: d5fe1bc7-3407-4722-b05d-1aff6c19fe09;
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
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of points
    #0 points: VarSoa<type=Point3f, max_size=10>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "point3f"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Create with mismatched sizes
    points_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "points")
    container = container_class(
        points=points_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            x=[1.0, 2.0, 3.0],
            y=[1.0, 2.0],  # Different size!
            z=[1.0, 2.0, 3.0],
        )
    )

    buffer = bytearray(container_class.get_tachyon_constraint().size)

    with pytest.raises(ValueError, match=r"All SoA field arrays must have same size"):
        container.serialize_tachyon(memoryview(buffer))


def test_soa_with_different_types(fs_importer: FilesystemImporter) -> None:
    """Test SoA with various field types with metadata."""
    source = """
// Color enum
enum Color
{
  uuid: 489c0935-f5c0-4bf8-813a-4eee01a826d3;
  values
  {
    // Red
    #0 red default;
    // Green
    #1 green;
    // Blue
    #2 blue;
  }
}

// Schema with mixed types
schema MixedTypes
{
  uuid: 6a50f228-6577-49f5-96fa-4d703a33fd7e;
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // Integer field
    #0 int_field: Int32;
    // Float field
    #1 float_field: Float64;
    // Boolean field
    #2 bool_field: Bool;
    // Enum field
    #3 enum_field: Color;
  }
}

// Container for mixed types
schema Container
{
  uuid: 37be6478-70c6-43ac-81e6-01a4ecc1b157;
  fields
  {
    // Variable-size SoA of mixed types
    #0 data: VarSoa<type=MixedTypes, max_size=4>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<MixedTypes>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "mixed"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, py_types = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    color_class = py_types["@test::mixed::Color"]

    # Create container
    data_type = next(f.type for f in fields(cast("Any", container_class)) if f.name == "data")
    container = container_class(
        data=data_type(  # type: ignore[misc] # pyright: ignore[reportCallIssue]
            int_field=[10, 20],
            float_field=[1.5, 2.5],
            bool_field=[True, False],
            enum_field=[color_class.red, color_class.blue],
        )
    )

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify
    assert container2.data.int_field == [10, 20]
    assert container2.data.float_field == [1.5, 2.5]
    assert container2.data.bool_field == [True, False]
    assert container2.data.enum_field == [color_class.red, color_class.blue]


def test_soa_class_naming(fs_importer: FilesystemImporter) -> None:
    """Test that SoA types generate correct Python class names."""
    source = """
// 3D point with namespace delimiters in FQN
schema Point3f
{
  uuid: 11111111-1111-1111-1111-111111111111;
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

// Container with both Fixed and Var SoA
schema Container
{
  uuid: 22222222-2222-2222-2222-222222222222;
  fields
  {
    // Fixed-size SoA of points
    #0 fixed_points: FixedSoa<type=Point3f, size=10>;
    // Variable-size SoA of points
    #1 var_points: VarSoa<type=Point3f, max_size=5>;
  }
}

cpp_target test_cpp
{
  options { namespace test; }
  representation Tachyon<Point3f>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "soa_naming"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_ir = schema.InstantiatedSchema.from_typespec(container_schema)

    container_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        container_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, container_ir),
    )

    # Deserialize empty buffer to get instances with the SoA dataclasses
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container_instance = container_class.deserialize_tachyon(memoryview(buffer))

    # Verify class names use schema name (not field names concatenated)
    # The FQN is "@test::soa_naming::Point3f" and we extract "Point3f"
    assert type(container_instance.fixed_points).__name__ == "FixedSoa_Point3f_10"
    assert type(container_instance.var_points).__name__ == "VarSoa_Point3f_5"


def test_tensor(fs_importer: FilesystemImporter) -> None:
    """Test basic Tensor serialization and deserialization."""
    source = """
// A message
schema TensorMessage
{
  uuid: 11111111-1111-1111-1111-111111111111;
  fields
  {
    // A tensor
    #0 tensor: Tensor<Float32, [2, 3, 4], TensorLayout::column_major>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<TensorMessage>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "tensor_dyn_metadata"), importer=fs_importer)

    tensor_msg_schema = module.inner_scope.lookup("TensorMessage")
    assert isinstance(tensor_msg_schema, schema.Schema)
    tensor_msg_ir = schema.InstantiatedSchema.from_typespec(tensor_msg_schema)

    tensor_msg_class: Any
    tensor_msg_class, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
        module.context,
        tensor_msg_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, tensor_msg_ir),
    )

    # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg = tensor_msg_class(tensor=tensor_builtins.TensorData(data=list(range(24)), shape=[2, 3, 4], strides=[1, 2, 6]))
    buffer = bytearray(tensor_msg_class.get_tachyon_constraint().size)
    msg.serialize_tachyon(memoryview(buffer))
    msg2 = tensor_msg_class.deserialize_tachyon(memoryview(bytes(buffer)))
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    assert msg2.tensor == msg.tensor

    # Seriealization should fail if the size doesn't match
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg.tensor.data = [1, 2, 3]
    with pytest.raises(ValueError, match=r"Attempt to serialize tensor of length 3, expected 24"):
        msg.serialize_tachyon(memoryview(buffer))

    # Seriealization should fail if the buffer elements are the wrong type
    # pyrefly: ignore[missing-attribute] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    msg.tensor.data = ["wrong"] * 24
    with pytest.raises(struct.error):
        msg.serialize_tachyon(memoryview(buffer))
