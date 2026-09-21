# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for Tachyon dynamic schema upgrade involving SoA conversions."""

from typing import Any, cast

import pytest
from clockwork.dsl.ir import compiler, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn


def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_array_to_soa_conversion() -> None:
    """Test upgrading from VarArray/FixedArray to VarSoa/FixedSoa."""
    schema_v1_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 1 with arrays
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarArray field
        #1 var_points: VarArray<Point3f, max_size=10>;
        // FixedArray field
        #2 fixed_points: FixedArray<Point3f, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 2 with SoAs
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarArray -> VarSoa
        #3 var_points: VarSoa<Point3f, max_size=10>;
        // FixedArray -> FixedSoa
        #4 fixed_points: FixedSoa<Point3f, size=3>;
      }
      history
      {
        version: 4;
        legacy_became: [1->3, 2->4];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "array_to_soa_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "array_to_soa_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_point_v1 = module_v1.inner_scope.lookup("Point3f")
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_point_v1, schema.Schema)
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_point_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_point_v1)
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_point_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_point_v1)
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    Point3fV1 = serdes_point_v1.py_class  # noqa: N806 it's a type and should be camel case
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    point1 = Point3fV1(x=1.0, y=2.0, z=3.0)
    point2 = Point3fV1(x=4.0, y=5.0, z=6.0)
    point3 = Point3fV1(x=7.0, y=8.0, z=9.0)

    instance_v1 = PointsSchemaV1(
        var_points=[point1, point2],
        fixed_points=[point1, point2, point3],
    )

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))
    instance_v1_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))

    # Verify the deserialized v1 instance has correct structure
    assert len(instance_v1_copy.var_points) == 2
    assert instance_v1_copy.var_points[0].x == 1.0
    assert instance_v1_copy.var_points[1].x == 4.0
    assert len(instance_v1_copy.fixed_points) == 3
    assert instance_v1_copy.fixed_points[2].x == 7.0

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1_copy))

    # Verify we can serialize and deserialize the upgraded instance
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))

    # Verify the upgrade - VarArray -> VarSoa
    # After upgrade, var_points should be an SoA with separate field arrays
    assert hasattr(instance_v2_copy.var_points, "x")
    assert hasattr(instance_v2_copy.var_points, "y")
    assert hasattr(instance_v2_copy.var_points, "z")
    assert instance_v2_copy.var_points.x == [1.0, 4.0]
    assert instance_v2_copy.var_points.y == [2.0, 5.0]
    assert instance_v2_copy.var_points.z == [3.0, 6.0]

    # Verify the upgrade - FixedArray -> FixedSoa
    assert hasattr(instance_v2_copy.fixed_points, "x")
    assert hasattr(instance_v2_copy.fixed_points, "y")
    assert hasattr(instance_v2_copy.fixed_points, "z")
    assert instance_v2_copy.fixed_points.x == [1.0, 4.0, 7.0]
    assert instance_v2_copy.fixed_points.y == [2.0, 5.0, 8.0]
    assert instance_v2_copy.fixed_points.z == [3.0, 6.0, 9.0]


def test_soa_to_array_conversion() -> None:
    """Test upgrading from VarSoa/FixedSoa to VarArray/FixedArray."""
    schema_v1_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 1 with SoAs
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarSoa field
        #1 var_points: VarSoa<Point3f, max_size=10>;
        // FixedSoa field
        #2 fixed_points: FixedSoa<Point3f, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 2 with arrays
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarSoa -> VarArray
        #3 var_points: VarArray<Point3f, max_size=10>;
        // FixedSoa -> FixedArray
        #4 fixed_points: FixedArray<Point3f, size=3>;
      }
      history
      {
        version: 4;
        legacy_became: [1->3, 2->4];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "soa_to_array_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "soa_to_array_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    # Note: v1 has VarSoa and FixedSoa, so we create SoA dataclasses with field arrays
    # Get the SoA types from the serdes
    var_soa_type = serdes_v1.field_serdeses[0][2].type_
    fixed_soa_type = serdes_v1.field_serdeses[1][2].type_

    instance_v1 = PointsSchemaV1(
        var_points=var_soa_type(x=[1.0, 4.0], y=[2.0, 5.0], z=[3.0, 6.0]),
        fixed_points=fixed_soa_type(x=[1.0, 4.0, 7.0], y=[2.0, 5.0, 8.0], z=[3.0, 6.0, 9.0]),
    )

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))
    instance_v1_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))

    # Verify the deserialized v1 instance
    assert instance_v1_copy.var_points.x == [1.0, 4.0]
    assert instance_v1_copy.var_points.y == [2.0, 5.0]
    assert instance_v1_copy.var_points.z == [3.0, 6.0]
    assert instance_v1_copy.fixed_points.x == [1.0, 4.0, 7.0]
    assert instance_v1_copy.fixed_points.y == [2.0, 5.0, 8.0]
    assert instance_v1_copy.fixed_points.z == [3.0, 6.0, 9.0]

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1_copy))

    # Serialize and deserialize the upgraded v2 instance to ensure it's valid
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer_v2 = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer_v2))
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_v2))

    # Verify the upgrade - VarSoa -> VarArray
    assert len(instance_v2_copy.var_points) == 2
    assert instance_v2_copy.var_points[0].x == 1.0
    assert instance_v2_copy.var_points[0].y == 2.0
    assert instance_v2_copy.var_points[0].z == 3.0
    assert instance_v2_copy.var_points[1].x == 4.0
    assert instance_v2_copy.var_points[1].y == 5.0
    assert instance_v2_copy.var_points[1].z == 6.0

    # Verify the upgrade - FixedSoa -> FixedArray
    assert len(instance_v2_copy.fixed_points) == 3
    assert instance_v2_copy.fixed_points[0].x == 1.0
    assert instance_v2_copy.fixed_points[1].y == 5.0
    assert instance_v2_copy.fixed_points[2].z == 9.0


def test_var_fixed_soa_interchangeability() -> None:
    """Test that VarArray/FixedArray can convert to FixedSoa/VarSoa and vice versa."""
    schema_v1_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 1
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarArray field
        #1 var_to_fixed: VarArray<Point3f, max_size=10>;
        // FixedArray field
        #2 fixed_to_var: FixedArray<Point3f, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 2
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarArray -> FixedSoa
        #3 var_to_fixed: FixedSoa<Point3f, size=3>;
        // FixedArray -> VarSoa
        #4 fixed_to_var: VarSoa<Point3f, max_size=10>;
      }
      history
      {
        version: 4;
        legacy_became: [1->3, 2->4];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "var_fixed_soa_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "var_fixed_soa_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_point_v1 = module_v1.inner_scope.lookup("Point3f")
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_point_v1, schema.Schema)
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_point_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_point_v1)
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_point_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_point_v1)
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    Point3fV1 = serdes_point_v1.py_class  # noqa: N806 it's a type and should be camel case
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    point1 = Point3fV1(x=1.0, y=2.0, z=3.0)
    point2 = Point3fV1(x=4.0, y=5.0, z=6.0)
    point3 = Point3fV1(x=7.0, y=8.0, z=9.0)

    instance_v1 = PointsSchemaV1(
        var_to_fixed=[point1, point2, point3],  # 3 elements fits in FixedSoa<3>
        fixed_to_var=[point1, point2, point3],  # FixedArray<3> fits in VarSoa
    )

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))
    instance_v1_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))
    assert len(instance_v1_copy.var_to_fixed) == 3
    assert len(instance_v1_copy.fixed_to_var) == 3

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1_copy))

    # Serialize and deserialize the upgraded v2 instance to ensure it's valid
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer_v2 = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer_v2))
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_v2))

    # Verify the upgrade - VarArray -> FixedSoa (now an SoA with field arrays)
    assert hasattr(instance_v2_copy.var_to_fixed, "x")
    assert hasattr(instance_v2_copy.var_to_fixed, "y")
    assert hasattr(instance_v2_copy.var_to_fixed, "z")
    assert instance_v2_copy.var_to_fixed.x == [1.0, 4.0, 7.0]
    assert instance_v2_copy.var_to_fixed.y == [2.0, 5.0, 8.0]
    assert instance_v2_copy.var_to_fixed.z == [3.0, 6.0, 9.0]

    # Verify the upgrade - FixedArray -> VarSoa (now an SoA with field arrays)
    assert hasattr(instance_v2_copy.fixed_to_var, "x")
    assert hasattr(instance_v2_copy.fixed_to_var, "y")
    assert hasattr(instance_v2_copy.fixed_to_var, "z")
    assert instance_v2_copy.fixed_to_var.x == [1.0, 4.0, 7.0]
    assert instance_v2_copy.fixed_to_var.y == [2.0, 5.0, 8.0]
    assert instance_v2_copy.fixed_to_var.z == [3.0, 6.0, 9.0]


def test_optional_to_soa_conversion() -> None:
    """Test upgrading from Optional to VarSoa/FixedSoa."""
    schema_v1_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 1 with Optional
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Optional field
        #1 opt_point: Optional<Point3f>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 2 with VarSoa
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Optional -> VarSoa
        #2 opt_point: VarSoa<Point3f, max_size=1>;
      }
      history
      {
        version: 2;
        legacy_became: [1->2];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "optional_to_soa_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "optional_to_soa_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_point_v1 = module_v1.inner_scope.lookup("Point3f")
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_point_v1, schema.Schema)
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_point_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_point_v1)
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_point_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_point_v1)
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    Point3fV1 = serdes_point_v1.py_class  # noqa: N806 it's a type and should be camel case
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    # Test case 1: None -> empty VarSoa
    instance_none = PointsSchemaV1(opt_point=None)
    upgraded_none = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_none))

    # Serialize and deserialize to verify
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    upgraded_none.serialize_tachyon(memoryview(buffer))
    upgraded_none_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))

    # Empty VarSoa has empty field arrays
    assert upgraded_none_copy.opt_point.x == []
    assert upgraded_none_copy.opt_point.y == []
    assert upgraded_none_copy.opt_point.z == []

    # Test case 2: Some value -> single-element VarSoa
    point1 = Point3fV1(x=1.0, y=2.0, z=3.0)
    instance_some = PointsSchemaV1(opt_point=point1)
    upgraded_some = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_some))

    # Serialize and deserialize to verify
    buffer_some = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    upgraded_some.serialize_tachyon(memoryview(buffer_some))
    upgraded_some_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_some))

    # Single-element VarSoa has single-element field arrays
    assert upgraded_some_copy.opt_point.x == [1.0]
    assert upgraded_some_copy.opt_point.y == [2.0]
    assert upgraded_some_copy.opt_point.z == [3.0]


def test_soa_to_optional_conversion() -> None:
    """Test upgrading from VarSoa/FixedSoa to Optional."""
    schema_v1_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 1 with VarSoa
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarSoa field
        #1 soa_point: VarSoa<Point3f, max_size=10>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema version 2 with Optional
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // VarSoa -> Optional
        #2 soa_point: Optional<Point3f>;
      }
      history
      {
        version: 2;
        legacy_became: [1->2];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "soa_to_optional_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "soa_to_optional_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    # Get the VarSoa type from the serdes
    var_soa_type = serdes_v1.field_serdeses[0][2].type_

    # Test case 1: Empty VarSoa -> None
    instance_empty = PointsSchemaV1(soa_point=var_soa_type(x=[], y=[], z=[]))

    # Serialize and deserialize to ensure it's valid
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_empty.serialize_tachyon(memoryview(buffer))
    instance_empty_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))

    upgraded_empty = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_empty_copy))

    # Serialize and deserialize the upgraded instance
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer_v2 = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    upgraded_empty.serialize_tachyon(memoryview(buffer_v2))
    upgraded_empty_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_v2))

    assert upgraded_empty_copy.soa_point is None

    # Test case 2: Single-element VarSoa -> Some value
    instance_single = PointsSchemaV1(soa_point=var_soa_type(x=[1.0], y=[2.0], z=[3.0]))

    # Serialize and deserialize to ensure it's valid
    buffer_single = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_single.serialize_tachyon(memoryview(buffer_single))
    instance_single_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer_single))

    upgraded_single = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_single_copy))

    # Serialize and deserialize the upgraded instance
    buffer_single_v2 = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    upgraded_single.serialize_tachyon(memoryview(buffer_single_v2))
    upgraded_single_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_single_v2))

    assert upgraded_single_copy.soa_point is not None
    assert upgraded_single_copy.soa_point.x == 1.0
    assert upgraded_single_copy.soa_point.y == 2.0
    assert upgraded_single_copy.soa_point.z == 3.0

    # Test case 3: Multi-element VarSoa -> Error
    instance_multi = PointsSchemaV1(soa_point=var_soa_type(x=[1.0, 4.0], y=[2.0, 5.0], z=[3.0, 6.0]))

    # Serialize and deserialize to ensure it's valid
    buffer_multi = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_multi.serialize_tachyon(memoryview(buffer_multi))
    instance_multi_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer_multi))

    with pytest.raises(ValueError, match=r"Cannot convert multi-element SoA to Optional"):
        tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_multi_copy)


def test_soa_element_schema_upgrade() -> None:
    """Test upgrading SoA when the element schema evolves."""
    schema_v1_source = """
    // Point schema v1
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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

    // Schema with SoA
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Array of points
        #1 points: VarSoa<Point3f, max_size=10>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    schema_v2_source = """
    // Point schema v2 with upgraded fields
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      options { soa_enabled: true; }
      fields
      {
        // X coordinate
        #0 x: Float64;
        // Y coordinate
        #1 y: Float64;
        // Z coordinate
        #2 z: Float64;
        // W coordinate
        #3 w: Float64 = 1.0;
      }
    }

    // Schema with SoA (unchanged container)
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Array of points
        #1 points: VarSoa<Point3f, max_size=10>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "soa_element_upgrade_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "soa_element_upgrade_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("PointsSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create test instances
    PointsSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    # Get the VarSoa type from the serdes
    var_soa_type = serdes_v1.field_serdeses[0][2].type_

    # Create VarSoa instance with field arrays
    instance_v1 = PointsSchemaV1(points=var_soa_type(x=[1.0, 4.0], y=[2.0, 5.0], z=[3.0, 6.0]))

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))
    instance_v1_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v1_copy.points.x == [1.0, 4.0]
    assert instance_v1_copy.points.y == [2.0, 5.0]
    assert instance_v1_copy.points.z == [3.0, 6.0]

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1_copy))

    # Serialize and deserialize the upgraded v2 instance to ensure it's valid
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer_v2 = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer_v2))
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer_v2))

    # Verify the upgrade - element schema fields upgraded (SoA with upgraded field types)
    assert hasattr(instance_v2_copy.points, "x")
    assert hasattr(instance_v2_copy.points, "y")
    assert hasattr(instance_v2_copy.points, "z")
    assert hasattr(instance_v2_copy.points, "w")
    assert instance_v2_copy.points.x == [1.0, 4.0]  # Float32 -> Float64
    assert instance_v2_copy.points.y == [2.0, 5.0]
    assert instance_v2_copy.points.z == [3.0, 6.0]
    assert instance_v2_copy.points.w == [1.0, 1.0]  # New field with default value


def test_fixed_to_fixed_size_compatibility() -> None:  # noqa: PLR0915 (comprehensive test with multiple scenarios)
    """Test that Fixed->Fixed conversions require matching sizes."""
    schema_base = """
    // Point schema
    schema Point3f
    {
      uuid: a1e6ee0b-ec41-40ce-8587-fb3583e5385d;
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
    """

    # Test FixedSoa -> FixedSoa with same size (should succeed)
    schema_v1_soa = (
        schema_base
        + """
    // Schema with fixed SoA
    schema PointsSchema
    {
      uuid: c1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Fixed SoA
        #1 points: FixedSoa<Point3f, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """
    )

    schema_v2_soa = (
        schema_base
        + """
    // Schema with fixed SoA
    schema PointsSchema
    {
      uuid: c1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Fixed SoA with same size
        #2 points: FixedSoa<Point3f, size=3>;
      }
      history
      {
        version: 2;
        legacy_became: [1->2];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """
    )

    module_v1_soa = compiler.compile_source_text(
        schema_v1_soa, ModuleID(CLK_REPO, "fixed_soa_same_test"), importer=fs_importer()
    )
    module_v2_soa = compiler.compile_source_text(
        schema_v2_soa, ModuleID(CLK_REPO, "fixed_soa_same_test"), importer=fs_importer()
    )

    schema_ir_v1_soa = module_v1_soa.inner_scope.lookup("PointsSchema")
    schema_ir_v2_soa = module_v2_soa.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_v1_soa, schema.Schema)
    assert isinstance(schema_ir_v2_soa, schema.Schema)

    schema_v1_soa_inst = schema.InstantiatedSchema.from_typespec(schema_ir_v1_soa)
    schema_v2_soa_inst = schema.InstantiatedSchema.from_typespec(schema_ir_v2_soa)

    serdes_v1_soa: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v1_soa.context, schema_v1_soa_inst
    )
    PointsSchemaSOA = serdes_v1_soa.py_class  # noqa: N806

    # Get the FixedSoa type from the serdes
    fixed_soa_type = serdes_v1_soa.field_serdeses[0][2].type_

    # Create FixedSoa instance with field arrays
    instance_soa = PointsSchemaSOA(points=fixed_soa_type(x=[1.0, 4.0, 7.0], y=[2.0, 5.0, 8.0], z=[3.0, 6.0, 9.0]))

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer_soa = bytearray(serdes_v1_soa.py_class.get_tachyon_constraint().size)
    instance_soa.serialize_tachyon(memoryview(buffer_soa))
    instance_soa_copy = serdes_v1_soa.py_class.deserialize_tachyon(memoryview(buffer_soa))
    assert instance_soa_copy.points.x == [1.0, 4.0, 7.0]
    assert instance_soa_copy.points.y == [2.0, 5.0, 8.0]
    assert instance_soa_copy.points.z == [3.0, 6.0, 9.0]

    # This should succeed
    upgraded_soa = tachyon_dyn.upgrade_schema(module_v2_soa.context, schema_v2_soa_inst, instance_soa_copy)
    assert upgraded_soa is not None

    # Serialize and deserialize the upgraded instance
    serdes_v2_soa: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v2_soa.context, schema_v2_soa_inst
    )
    buffer_soa_v2 = bytearray(serdes_v2_soa.py_class.get_tachyon_constraint().size)
    cast("Any", upgraded_soa).serialize_tachyon(memoryview(buffer_soa_v2))
    upgraded_soa_copy = serdes_v2_soa.py_class.deserialize_tachyon(memoryview(buffer_soa_v2))
    assert upgraded_soa_copy.points.x == [1.0, 4.0, 7.0]
    assert upgraded_soa_copy.points.y == [2.0, 5.0, 8.0]
    assert upgraded_soa_copy.points.z == [3.0, 6.0, 9.0]

    # Test FixedArray -> FixedSoa with same size (should succeed)
    schema_v2_array_to_soa = (
        schema_base
        + """
    // Schema with fixed SoA
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // FixedArray -> FixedSoa with same size
        #2 points: FixedSoa<Point3f, size=3>;
      }
      history
      {
        version: 2;
        legacy_became: [1->2];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """
    )

    module_v2_array_to_soa = compiler.compile_source_text(
        schema_v2_array_to_soa, ModuleID(CLK_REPO, "fixed_array_to_soa_test"), importer=fs_importer()
    )

    schema_ir_v2_array_to_soa = module_v2_array_to_soa.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_v2_array_to_soa, schema.Schema)
    schema_v2_array_to_soa_inst = schema.InstantiatedSchema.from_typespec(schema_ir_v2_array_to_soa)

    # Need to create a FixedArray instance from the base schema for FixedArray -> FixedSoa test
    # Reuse the Point3f and PointsSchema types we created earlier for the FixedArray
    schema_v1_array = (
        schema_base
        + """
    // Schema with fixed array
    schema PointsSchema
    {
      uuid: b1e6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // FixedArray field
        #1 points: FixedArray<Point3f, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Point3f;
        schema PointsSchema;
        representation Tachyon<Point3f>;
        representation Tachyon<PointsSchema>;
        interface Tappy<Point3f>;
        interface Tappy<PointsSchema>;
    }
    """
    )

    module_v1_array = compiler.compile_source_text(
        schema_v1_array, ModuleID(CLK_REPO, "fixed_array_v1_test"), importer=fs_importer()
    )

    schema_ir_point_v1_array = module_v1_array.inner_scope.lookup("Point3f")
    schema_ir_v1_array = module_v1_array.inner_scope.lookup("PointsSchema")
    assert isinstance(schema_ir_point_v1_array, schema.Schema)
    assert isinstance(schema_ir_v1_array, schema.Schema)

    schema_point_v1_array = schema.InstantiatedSchema.from_typespec(schema_ir_point_v1_array)
    schema_v1_array_inst = schema.InstantiatedSchema.from_typespec(schema_ir_v1_array)

    serdes_point_v1_array: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v1_array.context, schema_point_v1_array
    )
    serdes_v1_array: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v1_array.context, schema_v1_array_inst
    )

    Point3fV1Array = serdes_point_v1_array.py_class  # noqa: N806
    PointsSchemaV1Array = serdes_v1_array.py_class  # noqa: N806

    point1 = Point3fV1Array(x=1.0, y=2.0, z=3.0)
    point2 = Point3fV1Array(x=4.0, y=5.0, z=6.0)
    point3 = Point3fV1Array(x=7.0, y=8.0, z=9.0)

    instance_same = PointsSchemaV1Array(points=[point1, point2, point3])

    # Serialize and deserialize the v1 instance to ensure it's valid
    buffer_same = bytearray(serdes_v1_array.py_class.get_tachyon_constraint().size)
    instance_same.serialize_tachyon(memoryview(buffer_same))
    instance_same_copy = serdes_v1_array.py_class.deserialize_tachyon(memoryview(buffer_same))

    # This should succeed (FixedArray<3> -> FixedSoa<3>)
    upgraded_array_to_soa = tachyon_dyn.upgrade_schema(
        module_v2_array_to_soa.context, schema_v2_array_to_soa_inst, instance_same_copy
    )
    assert upgraded_array_to_soa is not None

    # Serialize and deserialize to verify
    serdes_v2_array_to_soa: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v2_array_to_soa.context, schema_v2_array_to_soa_inst
    )
    buffer_array_to_soa = bytearray(serdes_v2_array_to_soa.py_class.get_tachyon_constraint().size)
    cast("Any", upgraded_array_to_soa).serialize_tachyon(memoryview(buffer_array_to_soa))
    upgraded_array_to_soa_copy = serdes_v2_array_to_soa.py_class.deserialize_tachyon(memoryview(buffer_array_to_soa))

    # Verify it's a SoA with field arrays of length 3
    assert upgraded_array_to_soa_copy.points.x == [1.0, 4.0, 7.0]
    assert upgraded_array_to_soa_copy.points.y == [2.0, 5.0, 8.0]
    assert upgraded_array_to_soa_copy.points.z == [3.0, 6.0, 9.0]
