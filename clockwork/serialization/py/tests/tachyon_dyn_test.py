# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
import uuid
from pathlib import Path

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, schema, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tachyon_reg
from clockwork.serialization.py import tachyon_dyn


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.mark.parametrize(
    ("clk_type", "val", "tachyon"),
    [
        (clkbuiltins.UINT8, 0x81, b"\x81"),
        (clkbuiltins.UINT16, 0x81, b"\x81\0"),
        (clkbuiltins.UINT32, 0x81, b"\x81\0\0\0"),
        (clkbuiltins.UINT64, 0x81, b"\x81\0\0\0\0\0\0\0"),
        (clkbuiltins.INT8, -127, b"\x81"),
        (clkbuiltins.INT16, -127, b"\x81\xff"),
        (clkbuiltins.INT32, -127, b"\x81\xff\xff\xff"),
        (clkbuiltins.INT64, -127, b"\x81\xff\xff\xff\xff\xff\xff\xff"),
        (clkbuiltins.FLOAT32, 1.0, b"\0\0\x80\x3f"),
        (clkbuiltins.FLOAT64, 1.0, b"\0\0\0\0\0\0\xf0\x3f"),
    ],
)
def test_primitive(clk_type: clkbuiltins.PrimitiveType, val: float, tachyon: bytes) -> None:
    context = CompilerContext()
    serdes = tachyon_dyn.serdes_for_type(context, clk_type)
    assert serdes is not None
    buffer = bytearray(clk_type.bit_width // 8)
    serdes.serializer(val, memoryview(buffer))
    assert buffer == tachyon
    deser = serdes.deserializer(memoryview(tachyon))
    assert deser == val


def test_time() -> None:
    context = CompilerContext()
    duration = tachyon_dyn.serdes_for_type(context, clkbuiltins.DURATION)
    assert duration is not None
    buffer = bytearray(8)
    py = 0x0102030405
    tachyon = b"\x05\x04\x03\x02\x01\0\0\0"
    duration.serializer(py, memoryview(buffer))
    assert buffer == tachyon
    assert duration.deserializer(memoryview(tachyon)) == py
    synctime = tachyon_dyn.serdes_for_type(context, clkbuiltins.SYNC_TIME)
    assert synctime is not None
    buffer = bytearray(8)
    synctime.serializer(py, memoryview(buffer))
    assert buffer == tachyon
    assert synctime.deserializer(memoryview(tachyon)) == py


def test_hellomsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    hello_msg_ir = module.inner_scope.lookup("HelloMsg")
    assert isinstance(hello_msg_ir, schema.Schema)
    serdes = tachyon_dyn.serdes_for_type(module.context, hello_msg_ir)
    HelloMsg = serdes.type_  # noqa: N806
    hello = HelloMsg(seqno=42, data=[x % 256 for x in range(1024)], greeting_id=uuid.uuid4(), msg_id=uuid.uuid4())
    assert HelloMsg.get_tachyon_metadata_name() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomsg::HelloMsg"
    buffer = bytearray(HelloMsg.get_tachyon_constraint().size)
    serdes.serializer(hello, memoryview(buffer))
    hello2 = serdes.deserializer(memoryview(bytes(buffer)))
    assert hello2 == hello


def test_nested_and_instantiated(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    bti_ir = module.inner_scope.lookup("BetterThanInheritance")
    hellomsg_ir = module.inner_scope.lookup("HelloMsg")
    helloenum_ir = module.inner_scope.lookup("HelloEnum")
    assert isinstance(bti_ir, schema.Schema)
    assert isinstance(hellomsg_ir, schema.Schema)
    assert isinstance(helloenum_ir, clkenum.ClkEnum)
    serdes = tachyon_dyn.serdes_for_type(module.context, bti_ir)
    HelloMsg = tachyon_dyn.serdes_for_type(module.context, hellomsg_ir).type_  # noqa: N806
    HelloEnum = tachyon_dyn.serdes_for_type(module.context, helloenum_ir).type_  # noqa: N806
    BetterThanInheritance = serdes.type_  # noqa: N806
    generic_msg_ir = bti_ir.fields[4].type_info
    assert isinstance(generic_msg_ir, typesys.Instantiation)
    GenericMsg_Float32_32 = tachyon_dyn.serdes_for_type(module.context, generic_msg_ir).type_  # noqa: N806
    hello = BetterThanInheritance(
        hello=HelloMsg(seqno=42, data=[x % 256 for x in range(1024)], greeting_id=uuid.uuid4(), msg_id=uuid.uuid4()),
        generic=GenericMsg_Float32_32(data=[1.0, 3.0, 0.5]),
        desc="Description",
        greeting=HelloEnum.namaste,
    )
    assert serdes.constraint == tachyon_reg.FieldConstraint(size=1280, alignment=8)
    buffer = bytearray(serdes.constraint.size)
    serdes.serializer(hello, memoryview(buffer))
    hello2 = serdes.deserializer(memoryview(bytes(buffer)))
    assert hello2 == hello


def test_tapmsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")), fs_importer
    )
    TapMsg, _TapMsgTap = tachyon_dyn.get_instantiation_dataclass(module.context, module, "TapMsg", signed_value=234)  # noqa: N806 Represents a type and should be camel case
    SomeEnum, _ = tachyon_dyn.get_enum(module.context, module, "SomeEnum")  # noqa: N806 Represents a type and should be camel case
    SomeFlags, _ = tachyon_dyn.get_enum(module.context, module, "SomeFlags")  # noqa: N806 Represents a type and should be camel case
    SubMsg, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "SubMsg")  # noqa: N806 Represents a type and should be camel case
    msg = TapMsg(
        integer=1,
        floating_point=2.0,
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["one", "two"],
        uuid=uuid.uuid4(),
        uuid_different_namespace=uuid.uuid4(),
        default_enum=SomeEnum.second_value,
        enum_with_init=SomeEnum.first_value,
        default_flags=SomeFlags.flag12,
        flags_with_init=SomeFlags.flag3 | SomeFlags.flag12,
        nested_schema=SubMsg(field=42),
        array_of_schema=[SubMsg(field=11)],
        duration=1234,
        sync_time=4567,
        optional=None,
        bool_with_init=False,
        strong_type=3,
        external_strong_type=4,
        fixed_array=list(range(2)),
        var_string="a",
    )
    assert (
        TapMsg.get_tachyon_metadata_name()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::TapMsg<signed_value=234>"
    )
    buffer = bytearray(TapMsg.get_tachyon_constraint().size)
    msg.serialize_tachyon(memoryview(buffer))
    msg2 = TapMsg.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg2 == msg
    msg.optional = 13
    msg.array_of_array = []
    msg.array_of_schema.append(SubMsg(field=3))
    msg.serialize_tachyon(memoryview(buffer))
    msg2 = TapMsg.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg2 == msg

    msg.array_of_schema.append(SubMsg(field=3))
    with pytest.raises(
        ValueError, match=re.escape("Object <class 'types.TapMsg'> failed to serialize array_of_schema")
    ):
        msg.serialize_tachyon(memoryview(buffer))


def test_fixed_soa_basic(fs_importer: FilesystemImporter) -> None:
    """Test basic FixedSoa serialization and deserialization."""
    source = """
// 3D point
schema Point3f
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create a container with FixedSoa
    container = container_class()

    # Set some values in the SoA
    container.points.x = [1.0, 2.0, 3.0, 4.0, 5.0]
    container.points.y = [10.0, 20.0, 30.0, 40.0, 50.0]
    container.points.z = [100.0, 200.0, 300.0, 400.0, 500.0]

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
    """Test basic VarSoa serialization and deserialization."""
    source = """
// 3D point
schema Point3f
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create a container with VarSoa (empty by default)
    container = container_class()

    # Add some points
    container.points.x = [1.0, 2.0, 3.0]
    container.points.y = [10.0, 20.0, 30.0]
    container.points.z = [100.0, 200.0, 300.0]

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
    """Test VarSoa with empty arrays."""
    source = """
// 3D point
schema Point3f
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create empty VarSoa
    container = container_class()

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
    """Test SoA with a schema that has padding in AoS form."""
    source = """
// Message with padding
schema PaddedMsg
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create container with SoA
    container = container_class()
    container.messages.large = [100, 200, 300]
    container.messages.small = [True, False, True]

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify - SoA should handle padding correctly
    assert container2.messages.large == [100, 200, 300]
    assert container2.messages.small == [True, False, True]


def test_soa_size_validation(fs_importer: FilesystemImporter) -> None:
    """Test that VarSoa validates size limits."""
    source = """
// 3D point
schema Point3f
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Try to create with too many elements
    container = container_class()
    container.points.x = [1.0, 2.0, 3.0, 4.0]  # More than max_size=3
    container.points.y = [1.0, 2.0, 3.0, 4.0]
    container.points.z = [1.0, 2.0, 3.0, 4.0]

    buffer = bytearray(container_class.get_tachyon_constraint().size)

    with pytest.raises(ValueError, match=r"failed to serialize points"):
        container.serialize_tachyon(memoryview(buffer))


def test_soa_mismatched_field_sizes(fs_importer: FilesystemImporter) -> None:
    """Test that SoA validates all field arrays have the same size."""
    source = """
// 3D point
schema Point3f
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create with mismatched sizes
    container = container_class()
    container.points.x = [1.0, 2.0, 3.0]
    container.points.y = [1.0, 2.0]  # Different size!
    container.points.z = [1.0, 2.0, 3.0]

    buffer = bytearray(container_class.get_tachyon_constraint().size)

    with pytest.raises(ValueError, match=r"failed to serialize points"):
        container.serialize_tachyon(memoryview(buffer))


def test_soa_with_different_types(fs_importer: FilesystemImporter) -> None:
    """Test SoA with various field types."""
    source = """
// Color enum
enum Color
{
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
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    color_enum = module.inner_scope.lookup("Color")
    assert isinstance(color_enum, clkenum.ClkEnum)
    color_class = tachyon_dyn.serdes_for_type(module.context, color_enum.get_resolved()).type_

    # Create container
    container = container_class()
    container.data.int_field = [10, 20]
    container.data.float_field = [1.5, 2.5]
    container.data.bool_field = [True, False]
    container.data.enum_field = [color_class.red, color_class.blue]

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


def test_fixed_soa_with_init_values(fs_importer: FilesystemImporter) -> None:
    """Test that FixedSoa properly handles schema fields with init_value declarations."""
    source = """
// Schema with init values
schema PointWithDefaults
{
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // X with explicit init value
    #0 x: Float32 = 1.5;
    // Y with explicit init value
    #1 y: Float32 = 2.5;
    // Z without init value (should default to 0.0)
    #2 z: Float32;
  }
}

// Schema without any init values
schema PointNoDefaults
{
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // X field
    #0 x: Float32;
    // Y field
    #1 y: Float32;
    // Z field
    #2 z: Float32;
  }
}

// Container for testing
schema Container
{
  fields
  {
    // FixedSoa with init values
    #0 with_defaults: FixedSoa<type=PointWithDefaults, size=3>;
    // FixedSoa without init values
    #1 no_defaults: FixedSoa<type=PointNoDefaults, size=3>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<PointWithDefaults>;
  representation Tachyon<PointNoDefaults>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "soa_init_values"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create a container with default values
    container = container_class()

    # Verify FixedSoa with init values has correct defaults
    assert container.with_defaults.x == [1.5, 1.5, 1.5], "x should default to init_value of 1.5"
    assert container.with_defaults.y == [2.5, 2.5, 2.5], "y should default to init_value of 2.5"
    assert container.with_defaults.z == [0.0, 0.0, 0.0], "z should default to type default of 0.0"

    # Verify FixedSoa without init values has correct defaults (all zeros)
    assert container.no_defaults.x == [0.0, 0.0, 0.0], "x should default to type default of 0.0"
    assert container.no_defaults.y == [0.0, 0.0, 0.0], "y should default to type default of 0.0"
    assert container.no_defaults.z == [0.0, 0.0, 0.0], "z should default to type default of 0.0"

    # Modify values and serialize
    container.with_defaults.x = [10.0, 20.0, 30.0]
    container.with_defaults.y = [100.0, 200.0, 300.0]
    container.with_defaults.z = [1000.0, 2000.0, 3000.0]

    container.no_defaults.x = [11.0, 21.0, 31.0]
    container.no_defaults.y = [101.0, 201.0, 301.0]
    container.no_defaults.z = [1001.0, 2001.0, 3001.0]

    # Serialize
    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    # Deserialize
    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    # Verify serialization/deserialization works correctly
    assert container2.with_defaults.x == [10.0, 20.0, 30.0]
    assert container2.with_defaults.y == [100.0, 200.0, 300.0]
    assert container2.with_defaults.z == [1000.0, 2000.0, 3000.0]

    assert container2.no_defaults.x == [11.0, 21.0, 31.0]
    assert container2.no_defaults.y == [101.0, 201.0, 301.0]
    assert container2.no_defaults.z == [1001.0, 2001.0, 3001.0]


def test_var_soa_with_init_values(fs_importer: FilesystemImporter) -> None:
    """Test that VarSoa properly handles schema fields with init_value declarations."""
    source = """
// Schema with init values
schema PointWithDefaults
{
  options
  {
    soa_enabled: true;
  }
  fields
  {
    // X with explicit init value
    #0 x: Float32 = 1.5;
    // Y with explicit init value
    #1 y: Float32 = 2.5;
    // Z without init value
    #2 z: Float32;
  }
}

// Container for testing
schema Container
{
  fields
  {
    // VarSoa field
    #0 points: VarSoa<type=PointWithDefaults, max_size=10>;
  }
}

cpp_target test_cpp
{
  options
  {
    namespace test;
  }

  representation Tachyon<PointWithDefaults>;
  representation Tachyon<Container>;
}
"""
    module = compiler.compile_source_text(source, ModuleID("test", "var_soa_init_values"), importer=fs_importer)

    container_schema = module.inner_scope.lookup("Container")
    assert isinstance(container_schema, schema.Schema)
    container_class = tachyon_dyn.serdes_for_type(module.context, container_schema).type_

    # Create a container - VarSoa should default to empty lists
    container = container_class()

    # VarSoa always defaults to empty lists regardless of init_value
    assert container.points.x == []
    assert container.points.y == []
    assert container.points.z == []

    # Add values and verify serialization works
    container.points.x = [1.0, 2.0]
    container.points.y = [10.0, 20.0]
    container.points.z = [100.0, 200.0]

    buffer = bytearray(container_class.get_tachyon_constraint().size)
    container.serialize_tachyon(memoryview(buffer))

    container2 = container_class.deserialize_tachyon(memoryview(bytes(buffer)))

    assert container2.points.x == [1.0, 2.0]
    assert container2.points.y == [10.0, 20.0]
    assert container2.points.z == [100.0, 200.0]
