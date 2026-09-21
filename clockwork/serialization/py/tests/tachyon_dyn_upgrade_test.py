# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for schema upgrade functionality."""

import uuid
from typing import Any, cast

import pytest
from clockwork.dsl.ir import clkenum, compiler, node, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn


def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def compile_schema_v1(importer: FilesystemImporter) -> node.Module:
    """Compile schema version 1."""
    schema_source = """
    // Test schema version 1
    schema TestSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // String field
        #2 string_field: VarString<max_size=64>;
        // Field to be renamed
        #3 old_name: Float32;
        // Field to be removed
        #4 to_be_removed: Byte;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestSchema;
        representation Tachyon<TestSchema>;
        interface Tappy<TestSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "test_schema_v1"), importer=importer)


def compile_schema_v2(importer: FilesystemImporter) -> node.Module:
    """Compile schema version 2."""
    schema_source = """
    // Test schema version 2
    schema TestSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field (upgraded)
        #1 integer_field: Int64;
        // String field
        #2 string_field: VarString<max_size=128>;
        // Renamed field
        #5 new_name: Float64;
        // Newly added field
        #6 added_field: Optional<Bool> = nullopt;
      }
      history
      {
        version: 6;
        legacy_became: [3->5];
        removed: [4];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestSchema;
        representation Tachyon<TestSchema>;
        interface Tappy<TestSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "test_schema_v2"), importer=importer)


def compile_container_schema_v1(importer: FilesystemImporter) -> node.Module:
    """Compile container schema version 1."""
    schema_source = """
    // Container schema version 1
    schema ContainerSchema
    {
      uuid: 335dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Optional field
        #1 optional_field: Optional<Int32>;
        // Var array of bytes
        #2 array_field: VarArray<Byte, max_size=32>;
        // Optional to be converted to array
        #3 opt_to_array: Optional<VarString<max_size=32>>;
        // Array to be converted to optional
        #4 array_to_opt: VarArray<Int32, max_size=5>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema ContainerSchema;
        representation Tachyon<ContainerSchema>;
        interface Tappy<ContainerSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "container_schema_v1"), importer=importer)


def compile_container_schema_v2(importer: FilesystemImporter) -> node.Module:
    """Compile container schema version 2."""
    schema_source = """
    // Container schema version 2
    schema ContainerSchema
    {
      uuid: 335dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Optional field
        #8 optional_field: Optional<Int64>;
        // Changed to string
        #7 array_field: VarString<max_size=32>;
        // Changed from optional to array
        #5 opt_to_array: VarArray<VarString<max_size=32>, max_size=1>;
        // Changed from array to optional
        #6 array_to_opt: Optional<Int32>;
      }
      history
      {
        version: 8;
        legacy_became: [3->5, 4->6, 2->7, 1->8];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema ContainerSchema;
        representation Tachyon<ContainerSchema>;
        interface Tappy<ContainerSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "container_schema_v2"), importer=importer)


def compile_nested_schemas(importer: FilesystemImporter) -> tuple[node.Module, node.Module]:
    """Compile nested schemas (v1 and v2)."""
    # Create schema v1 with nested schemas in a single source text
    schema_v1_source = """
    // Test schema version 1
    schema TestSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // String field
        #2 string_field: VarString<max_size=64>;
        // Field to be renamed
        #3 old_name: Float32;
        // Field to be removed
        #4 to_be_removed: Byte;
      }
    }

    // Outer schema version 1
    schema OuterSchema
    {
      uuid: dbe6ee0b-ec41-40ce-8587-fb3583e5386e;
      fields
      {
        // Name field
        #1 name: VarString<max_size=32>;
        // Inner schema field
        #2 inner_schema: TestSchema;
        // Inner schema to be renamed
        #3 old_inner_schema: TestSchema;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestSchema;
        schema OuterSchema;
        representation Tachyon<TestSchema>;
        representation Tachyon<OuterSchema>;
        interface Tappy<TestSchema>;
        interface Tappy<OuterSchema>;
    }
    """

    module_v1 = compiler.compile_source_text(schema_v1_source, ModuleID(CLK_REPO, "nested_schema"), importer=importer)

    # Create schema v2 with nested schemas in a single source text
    schema_v2_source = """
    // Test schema version 2
    schema TestSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Integer field (upgraded)
        #1 integer_field: Int64;
        // String field
        #2 string_field: VarString<max_size=128>;
        // Renamed field
        #5 new_name: Float64;
        // Newly added field
        #6 added_field: Optional<Bool> = nullopt;
      }
      history
      {
        version:  6;
        legacy_became: [3->5];
        removed: [4];
      }
    }

    // Outer schema version 2
    schema OuterSchema
    {
      uuid: dbe6ee0b-ec41-40ce-8587-fb3583e5386e;
      fields
      {
        // Name field
        #1 name: VarString<max_size=64>;
        // Inner schema field
        #2 inner_schema: TestSchema;
        // Renamed inner schema
        #4 new_inner_schema: TestSchema;
      }
      history
      {
        version: 4;
        legacy_became: [3->4];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestSchema;
        schema OuterSchema;
        representation Tachyon<TestSchema>;
        representation Tachyon<OuterSchema>;
        interface Tappy<TestSchema>;
        interface Tappy<OuterSchema>;
    }
    """

    module_v2 = compiler.compile_source_text(schema_v2_source, ModuleID(CLK_REPO, "nested_schema"), importer=importer)

    return module_v1, module_v2


def compile_enum_schemas(importer: FilesystemImporter) -> tuple[node.Module, node.Module]:
    """Compile enum schemas (v1 and v2)."""
    # Create enum schema v1
    enum_schema_v1_src = """
    // Test enum
    enum TestEnum
    {
      uuid: 02d1a601-5c69-46dd-9922-06fee33b581e;
      options
      {
        underlying_type: Int32;
      }
      values
      {
        // Value 1
        #1 VALUE1 default;
        // Value 2
        #2 VALUE2;
      }
    }

    // Enum schema version 1
    schema EnumSchema
    {
      uuid: 435dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // Enum field
        #2 enum_field: TestEnum;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema EnumSchema;
        representation Tachyon<EnumSchema>;
        interface Tappy<EnumSchema>;
    }
    """
    module_enum_v1 = compiler.compile_source_text(
        enum_schema_v1_src, ModuleID(CLK_REPO, "enum_schema"), importer=importer
    )

    # Create enum schema v2
    enum_schema_v2_src = """
    // Test enum with additional value
    enum TestEnum
    {
      uuid: 02d1a601-5c69-46dd-9922-06fee33b581e;
      options
      {
        underlying_type: Int32;
      }
      values
      {
        // Value 1
        #1 VALUE1 default;
        // Value 2
        #2 VALUE2;
      }
    }

    // Enum schema version 2
    schema EnumSchema
    {
      uuid: 435dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Integer field
        #1 integer_field: Int64;
        // Enum field
        #2 enum_field: TestEnum;
        // New enum field
        #3 new_enum_field: TestEnum = TestEnum::VALUE1;
      }
      history
      {
        version: 3;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema EnumSchema;
        representation Tachyon<EnumSchema>;
        interface Tappy<EnumSchema>;
    }
    """
    module_enum_v2 = compiler.compile_source_text(
        enum_schema_v2_src, ModuleID(CLK_REPO, "enum_schema"), importer=importer
    )

    return module_enum_v1, module_enum_v2


def compile_complex_schema_v1(importer: FilesystemImporter) -> node.Module:
    """Compile complex schema version 1."""
    schema_source = """
    // Basic schema for nesting
    schema BasicSchema
    {
      uuid: 85e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Integer field
        #1 value: Int32;
        // String field
        #2 name: VarString<max_size=32>;
      }
    }

    // Complex schema version 1
    schema ComplexSchema
    {
      uuid: 75e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Element type will change
        #1 int_array: VarArray<Int32, max_size=10>;

        // Container will change and element type will change
        #2 opt_float: Optional<Float32>;

        // Schema container - will change element schema fields
        #3 basic_array: VarArray<BasicSchema, max_size=5>;

        // Field that will undergo multiple transformations
        #4 multi_step_field: Int8;

        // String to be converted to byte array
        #5 str_to_bytes: VarString<max_size=16>;

        // String field that will change capacity
        #6 growing_string: VarString<max_size=8>;

        // Int to Duration conversion
        #7 int_to_duration: Int64;

        // Int to SyncTime conversion
        #8 int_to_synctime: Int32;

        // Value to Optional<Value> conversion
        #9 direct_to_optional: Int16;

        // String to byte array conversion
        #10 string_to_bytes: VarString<max_size=12>;

        // Array of schemas that will have element conversion
        #11 schema_array: VarArray<BasicSchema, max_size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema BasicSchema;
        schema ComplexSchema;
        representation Tachyon<BasicSchema>;
        representation Tachyon<ComplexSchema>;
        interface Tappy<BasicSchema>;
        interface Tappy<ComplexSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "complex_schema"), importer=importer)


def compile_complex_schema_v2(importer: FilesystemImporter) -> node.Module:
    """Compile complex schema version 2."""
    schema_source = """
    // Basic schema for nesting
    schema BasicSchema
    {
      uuid: 85e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Integer field - upgraded
        #1 value: Int64;
        // String field
        #2 name: VarString<max_size=64>;
        // A new field
        #3 new_field: Optional<Int32> = nullopt;
      }
      history
      {
        version: 3;
      }
    }

    // Complex schema version 2 - intermediate version
    schema ComplexSchema
    {
      uuid: 75e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Element type changed
        #101 int_array: VarArray<Int64, max_size=10>;

        // Container changed, element type changed
        #102 array_double: VarArray<Float64, max_size=1>;

        // Schema container - element schema fields changed
        #3 basic_array: VarArray<BasicSchema, max_size=5>;

        // Renamed and type changed
        #104 multi_step_renamed: Int16;

        // String to bytes
        #105 bytes_array: VarArray<Byte, max_size=16>;

        // Increased capacity
        #106 growing_string: VarString<max_size=16>;

        // Int to Duration conversion
        #107 duration_field: Duration;

        // Int to SyncTime conversion
        #108 synctime_field: SyncTime;

        // Value to Optional<Value> conversion
        #109 optional_value: Optional<Int32>;

        // String to byte array conversion
        #110 byte_array: VarArray<Byte, max_size=12>;

        // Array of schemas with element conversion
        #111 schema_array: VarArray<BasicSchema, max_size=3>;
      }
      history
      {
        version: 111;
        legacy_became: [1->101, 2->102, 4->104, 5->105, 6->106, 7->107, 8->108, 9->109, 10->110, 11->111];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema BasicSchema;
        schema ComplexSchema;
        representation Tachyon<BasicSchema>;
        representation Tachyon<ComplexSchema>;
        interface Tappy<BasicSchema>;
        interface Tappy<ComplexSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "complex_schema"), importer=importer)


def compile_complex_schema_v3(importer: FilesystemImporter) -> node.Module:
    """Compile complex schema version 3."""
    schema_source = """
    // Basic schema for nesting
    schema BasicSchema
    {
      uuid: 85e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Integer field - upgraded
        #1 value: Int64;
        // String field
        #2 name: VarString<max_size=64>;
        // A new field
        #3 new_field: Optional<Int32> = nullopt;
      }
      history
      {
        version: 3;
      }
    }

    // Complex schema version 3 - final version
    schema ComplexSchema
    {
      uuid: 75e6ee0b-ec41-40ce-8587-fb3583e5385e;
      fields
      {
        // Element type changed
        #101 int_array: VarArray<Int64, max_size=10>;

        // Container changed, element type changed
        #102 array_double: VarArray<Float64, max_size=1>;

        // Schema container - element schema fields changed
        #3 basic_array: VarArray<BasicSchema, max_size=5>;

        // Final step in multi-step evolution
        #204 final_multi_step: Int32;

        // Bytes to string
        #205 str_again: VarString<max_size=32>;

        // Final growing string with increased capacity
        #206 growing_string: VarString<max_size=32>;

        // Duration field (converted from int)
        #207 duration_field: Duration;

        // SyncTime field (converted from int)
        #208 synctime_field: SyncTime;

        // Optional value (converted from direct value)
        #209 optional_value: Optional<Int64>;

        // Bytes array (converted from string)
        #210 byte_array: VarArray<Byte, max_size=12>;

        // Array with upgraded schema elements
        #211 schema_array: VarArray<BasicSchema, max_size=3>;
      }
      history
      {
        version: 211;
        legacy_became: [1->101, 2->102, 4->104, 5->105, 6->106, 7->107, 8->108, 9->109, 10->110, 11->111,
                        104->204, 105->205, 106->206, 107->207, 108->208, 109->209, 110->210, 111->211];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema BasicSchema;
        schema ComplexSchema;
        representation Tachyon<BasicSchema>;
        representation Tachyon<ComplexSchema>;
        interface Tappy<BasicSchema>;
        interface Tappy<ComplexSchema>;
    }
    """
    return compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "complex_schema"), importer=importer)


def test_basic_schema_upgrade() -> None:
    """Test upgrading a schema from v1 to v2."""
    # Compile schemas
    module_v1 = compile_schema_v1(fs_importer())
    module_v2 = compile_schema_v2(fs_importer())

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("TestSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("TestSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    # Create an instance of v1
    TestSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = TestSchemaV1(
        integer_field=42,
        string_field="test string",
        old_name=3.14,
        to_be_removed=123,
    )

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1))

    # Verify the upgrade
    instance_v2 = cast("Any", instance_v2)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any to suppress mypy errors
    assert instance_v2.integer_field == 42
    assert instance_v2.string_field == "test string"
    assert instance_v2.new_name == 3.14  # old_name was renamed to new_name
    assert instance_v2.added_field is None  # New field with default value

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))
    instance_v2_copy = cast("Any", instance_v2_copy)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any to suppress mypy errors
    assert instance_v2_copy.integer_field == 42
    assert instance_v2_copy.string_field == "test string"
    assert instance_v2_copy.new_name == 3.14
    assert instance_v2_copy.added_field is None


def test_container_type_conversions() -> None:
    """Test upgrading a schema with container type conversions."""
    # Compile schemas
    module_v1 = compile_container_schema_v1(fs_importer())
    module_v2 = compile_container_schema_v2(fs_importer())

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("ContainerSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("ContainerSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    # Create an instance of v1
    ContainerSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = ContainerSchemaV1(
        optional_field=42,  # Will be converted to INT64
        array_field=[98, 121, 116, 101, 115],  # Will be converted to string "bytes"
        opt_to_array="test",  # Will be converted from Optional to array [test]
        array_to_opt=[123],  # Will be converted from array to Optional
    )

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1))
    instance_v2 = cast("Any", instance_v2)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any to suppress mypy errors

    # Verify the upgrade
    assert instance_v2.optional_field == 42
    assert instance_v2.array_field == "bytes"  # Array of bytes converted to string
    assert instance_v2.opt_to_array == ["test"]  # Optional converted to single-element array
    assert instance_v2.array_to_opt == 123  # Single-element array converted to Optional

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))
    instance_v2_copy = cast("Any", instance_v2_copy)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any to suppress mypy errors
    assert instance_v2_copy.optional_field == 42
    assert instance_v2_copy.array_field == "bytes"
    assert instance_v2_copy.opt_to_array == ["test"]
    assert instance_v2_copy.array_to_opt == 123


def test_array_to_optional_edge_cases() -> None:
    """Test edge cases for array to optional conversion."""
    # Compile schemas
    module_v1 = compile_container_schema_v1(fs_importer())
    module_v2 = compile_container_schema_v2(fs_importer())

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("ContainerSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("ContainerSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Test case 1: Empty array -> None
    ContainerSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    empty_array_instance = ContainerSchemaV1(
        optional_field=None,
        array_field=[],
        opt_to_array=None,
        array_to_opt=[],  # Empty array should become None
    )

    # Upgrade and verify empty array becomes None
    upgraded_empty = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, empty_array_instance))
    assert upgraded_empty.array_to_opt is None

    # Test case 2: Multi-element array -> Exception
    multi_array_instance = ContainerSchemaV1(
        optional_field=None,
        array_field=[],
        opt_to_array=None,
        array_to_opt=[1, 2, 3],  # Multi-element array should raise exception
    )

    # This should raise ValueError about multi-element list
    with pytest.raises(ValueError, match=r"Cannot convert multi-element list to Optional"):
        tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, multi_array_instance)


def test_optional_to_array_edge_cases() -> None:
    """Test edge cases for optional to array conversion."""
    # Compile schemas
    module_v1 = compile_container_schema_v1(fs_importer())
    module_v2 = compile_container_schema_v2(fs_importer())

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("ContainerSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("ContainerSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Test: None -> Empty array
    ContainerSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    none_optional_instance = ContainerSchemaV1(
        optional_field=None,
        array_field=[],
        opt_to_array=None,  # None should become empty array
        array_to_opt=[1],
    )

    # Upgrade and verify None becomes empty array
    upgraded_none = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, none_optional_instance))
    assert upgraded_none.opt_to_array == []


def test_nested_schema_upgrade() -> None:
    """Test upgrading schemas with nested schemas."""
    # Compile schemas
    module_outer_v1, module_outer_v2 = compile_nested_schemas(fs_importer())

    # Get schema objects
    schema_ir_outer_v1 = module_outer_v1.inner_scope.lookup("OuterSchema")
    schema_ir_test_v1 = module_outer_v1.inner_scope.lookup("TestSchema")
    schema_ir_outer_v2 = module_outer_v2.inner_scope.lookup("OuterSchema")
    assert isinstance(schema_ir_outer_v1, schema.Schema)
    assert isinstance(schema_ir_test_v1, schema.Schema)
    assert isinstance(schema_ir_outer_v2, schema.Schema)

    # Create instantiated schemas
    schema_outer_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_outer_v1)
    schema_test_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_test_v1)
    schema_outer_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_outer_v2)

    # Create SerDes for schemas
    serdes_outer_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_outer_v1.context, schema_outer_v1
    )
    serdes_test_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_outer_v1.context, schema_test_v1
    )
    serdes_outer_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_outer_v2.context, schema_outer_v2
    )

    # Create test instances
    TestSchemaV1 = serdes_test_v1.py_class  # noqa: N806 it's a type and should be camel case
    OuterSchemaV1 = serdes_outer_v1.py_class  # noqa: N806 it's a type and should be camel case

    inner_instance = TestSchemaV1(
        integer_field=42,
        string_field="test string",
        old_name=3.14,
        to_be_removed=123,
    )

    outer_instance = OuterSchemaV1(
        name="outer schema",
        inner_schema=inner_instance,
        old_inner_schema=inner_instance,
    )

    # Upgrade the outer instance
    upgraded_outer = cast("Any", tachyon_dyn.upgrade_schema(module_outer_v2.context, schema_outer_v2, outer_instance))

    # Verify basic outer schema upgrade
    assert upgraded_outer.name == "outer schema"
    assert hasattr(upgraded_outer, "new_inner_schema")  # Renamed from old_inner_schema
    assert not hasattr(upgraded_outer, "old_inner_schema")  # Old field name is gone

    # Verify inner schema upgrade worked properly
    upgraded_inner = upgraded_outer.inner_schema
    upgraded_inner = cast("Any", upgraded_inner)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any for mypy
    assert upgraded_inner.integer_field == 42
    assert upgraded_inner.string_field == "test string"
    assert upgraded_inner.new_name == 3.14  # Renamed from old_name
    assert hasattr(upgraded_inner, "added_field")  # New field added
    assert not hasattr(upgraded_inner, "old_name")  # Old field name is gone
    assert not hasattr(upgraded_inner, "to_be_removed")  # Removed field is gone

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_outer_v2.py_class.get_tachyon_constraint().size)
    upgraded_outer.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    roundtrip = serdes_outer_v2.py_class.deserialize_tachyon(memoryview(buffer))
    roundtrip = cast("Any", roundtrip)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any for mypy
    assert roundtrip.name == "outer schema"
    assert roundtrip.inner_schema.integer_field == 42
    assert roundtrip.inner_schema.string_field == "test string"
    assert roundtrip.inner_schema.new_name == 3.14


def test_enum_schema_upgrade() -> None:
    """Test upgrading schema with enum fields."""
    # Compile schemas
    module_enum_v1, module_enum_v2 = compile_enum_schemas(fs_importer())

    # Get schema objects
    schema_ir_enum_v1 = module_enum_v1.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v2 = module_enum_v2.inner_scope.lookup("EnumSchema")
    assert isinstance(schema_ir_enum_v1, schema.Schema)
    assert isinstance(schema_ir_enum_v2, schema.Schema)

    # Create instantiated schemas
    schema_enum_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v1)
    schema_enum_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v2)

    # Create SerDes for both schemas
    serdes_enum_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v1.context, schema_enum_v1
    )
    serdes_enum_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v2.context, schema_enum_v2
    )

    # Get the enum class from SerDes
    enum_class = serdes_enum_v1.field_serdeses[1][2].type_

    # Create an instance of v1
    EnumSchemaV1 = serdes_enum_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = EnumSchemaV1(
        integer_field=42,
        enum_field=enum_class.VALUE1,
    )

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_enum_v2.context, schema_enum_v2, instance_v1))

    # Verify the upgrade
    assert instance_v2.integer_field == 42
    assert instance_v2.enum_field.name == "VALUE1"  # Enum value preserved
    assert instance_v2.new_enum_field.name == "VALUE1"  # New enum field gets default value

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_enum_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_enum_v2.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v2_copy.integer_field == 42
    assert instance_v2_copy.enum_field.name == "VALUE1"
    assert instance_v2_copy.new_enum_field.name == "VALUE1"


def test_no_changes_needed() -> None:
    """Test upgrading when no changes are needed (same version)."""
    # Compile schema
    module_v1 = compile_schema_v1(fs_importer())

    # Get schema object
    schema_ir_v1 = module_v1.inner_scope.lookup("TestSchema")
    assert isinstance(schema_ir_v1, schema.Schema)

    # Create instantiated schema
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)

    # Create SerDes
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create an instance of v1
    TestSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = TestSchemaV1(
        integer_field=42,
        string_field="test string",
        old_name=3.14,
        to_be_removed=123,
    )

    # Upgrade should return the same instance when versions match
    instance_same = tachyon_dyn.upgrade_schema(module_v1.context, schema_v1, instance_v1)
    assert instance_same is instance_v1  # Should be the same instance (no upgrade needed)


def test_complex_schema_upgrade() -> None:  # noqa: PLR0915 For testing only
    """Test complex schema upgrades with multiple transformations."""
    # Compile schemas
    module_v1 = compile_complex_schema_v1(fs_importer())
    module_v3 = compile_complex_schema_v3(fs_importer())

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("ComplexSchema")
    schema_ir_basic_v1 = module_v1.inner_scope.lookup("BasicSchema")
    schema_ir_v3 = module_v3.inner_scope.lookup("ComplexSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_basic_v1, schema.Schema)
    assert isinstance(schema_ir_v3, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_basic_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_basic_v1)
    schema_v3 = schema.InstantiatedSchema.from_typespec(schema_ir_v3)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_basic_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_basic_v1)
    serdes_v3: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v3.context, schema_v3)

    # Create instances of BasicSchema v1
    BasicSchemaV1 = serdes_basic_v1.py_class  # noqa: N806 it's a type and should be camel case
    basic1 = BasicSchemaV1(value=10, name="Basic1")
    basic2 = BasicSchemaV1(value=20, name="Basic2")
    basic3 = BasicSchemaV1(value=30, name="Basic3")

    # Create an instance of ComplexSchema v1
    ComplexSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = ComplexSchemaV1(
        int_array=[1, 2, 3],  # Element type will change
        opt_float=3.14,  # Container and element type will change
        basic_array=[basic1, basic2],  # Container of schema types
        multi_step_field=42,  # Will undergo multiple transformations
        str_to_bytes="hello",  # Will convert to bytes and back
        growing_string="small",  # Will increase capacity
        # New test cases
        int_to_duration=1000000,  # Int64 -> Duration
        int_to_synctime=500000,  # Int32 -> SyncTime
        direct_to_optional=25,  # Int16 -> Optional<Int32> -> Optional<Int64>
        string_to_bytes="convert me",  # VarString -> VarArray<Byte>
        schema_array=[basic1, basic2, basic3],  # Array of schema elements that need conversion
    )

    # Upgrade the instance to v3 - directly from v1 to v3
    instance_v3 = cast("Any", tachyon_dyn.upgrade_schema(module_v3.context, schema_v3, instance_v1))

    # Verify the upgrade
    assert instance_v3.int_array == [1, 2, 3]  # Same array, element type changed to Int64
    assert instance_v3.array_double == [3.14]  # Optional<Float32> → VarArray<Float64>

    # Verify nested schema fields were upgraded
    assert len(instance_v3.basic_array) == 2
    assert instance_v3.basic_array[0].value == 10  # Int32 → Int64
    assert instance_v3.basic_array[0].name == "Basic1"
    assert instance_v3.basic_array[1].value == 20
    assert instance_v3.basic_array[1].name == "Basic2"

    # Verify multi-step field transformation
    assert instance_v3.final_multi_step == 42  # Int8 → Int16 → Int32

    # Verify string ↔ bytes conversion
    assert instance_v3.str_again == "hello"  # String → Bytes → String

    # Verify capacity changes
    assert instance_v3.growing_string == "small"  # Capacity: 8 → 16 → 32

    # Verify new test cases

    # Int to Duration/SyncTime conversions
    assert instance_v3.duration_field == 1000000  # Int64 -> Duration
    assert instance_v3.synctime_field == 500000  # Int32 -> SyncTime

    # Direct value to Optional conversion
    assert instance_v3.optional_value == 25  # Int16 -> Optional<Int64>

    # String to byte array conversion
    assert bytes(instance_v3.byte_array).decode("utf-8") == "convert me"

    # Array with schema element conversion
    assert len(instance_v3.schema_array) == 3
    assert instance_v3.schema_array[0].value == 10  # Int32 -> Int64
    assert instance_v3.schema_array[0].name == "Basic1"
    assert instance_v3.schema_array[1].value == 20  # Int32 -> Int64
    assert instance_v3.schema_array[1].name == "Basic2"
    assert instance_v3.schema_array[2].value == 30  # Int32 -> Int64
    assert instance_v3.schema_array[2].name == "Basic3"

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_v3.py_class.get_tachyon_constraint().size)
    instance_v3.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    roundtrip = serdes_v3.py_class.deserialize_tachyon(memoryview(buffer))
    assert roundtrip.int_array == [1, 2, 3]
    assert roundtrip.array_double == [3.14]
    assert len(roundtrip.basic_array) == 2
    assert roundtrip.basic_array[0].value == 10
    assert roundtrip.final_multi_step == 42
    assert roundtrip.str_again == "hello"
    assert roundtrip.growing_string == "small"

    # Verify new test cases after serialization
    assert roundtrip.duration_field == 1000000
    assert roundtrip.synctime_field == 500000
    assert roundtrip.optional_value == 25
    assert bytes(roundtrip.byte_array).decode("utf-8") == "convert me"
    assert len(roundtrip.schema_array) == 3
    assert roundtrip.schema_array[0].value == 10
    assert roundtrip.schema_array[0].name == "Basic1"
    assert roundtrip.schema_array[2].value == 30
    assert roundtrip.schema_array[2].name == "Basic3"


def test_schema_to_container_upgrade() -> None:
    """Test case for upgrading a schema field to a container of the same schema type."""
    schema_source_v1 = """
    // Test schema version 1
    schema NestedSchema
    {
      uuid: abe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Value field
        #1 value: Int32;
      }
    }

    // Schema that transitions from T to [T]
    schema SchemaToContainerSchema
    {
      uuid: bbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Single schema field
        #1 nested: NestedSchema;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema NestedSchema;
        schema SchemaToContainerSchema;
        representation Tachyon<NestedSchema>;
        representation Tachyon<SchemaToContainerSchema>;
        interface Tappy<NestedSchema>;
        interface Tappy<SchemaToContainerSchema>;
    }
    """

    schema_source_v2 = """
    // Test schema version 2
    schema NestedSchema
    {
      uuid: abe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Value field
        #1 value: Int32;
        // Added field
        #2 name: VarString<max_size=32>;
      }
      history
      {
        version: 2;
      }
    }

    // Schema that transitions from T to [T]
    schema SchemaToContainerSchema
    {
      uuid: bbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Array of schema field
        #2 nested_array: VarArray<NestedSchema, max_size=1>;
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
        schema NestedSchema;
        schema SchemaToContainerSchema;
        representation Tachyon<NestedSchema>;
        representation Tachyon<SchemaToContainerSchema>;
        interface Tappy<NestedSchema>;
        interface Tappy<SchemaToContainerSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_source_v1, ModuleID(CLK_REPO, "schema_to_container"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_source_v2, ModuleID(CLK_REPO, "schema_to_container"), importer=fs_importer()
    )

    # Get schema objects
    nested_schema_ir_v1 = module_v1.inner_scope.lookup("NestedSchema")
    schema_ir_v1 = module_v1.inner_scope.lookup("SchemaToContainerSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("SchemaToContainerSchema")

    assert isinstance(nested_schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    nested_schema_v1 = schema.InstantiatedSchema.from_typespec(nested_schema_ir_v1)
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes
    nested_serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, nested_schema_v1)
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create instances
    NestedSchemaV1 = nested_serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    SchemaToContainerSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case

    nested_instance = NestedSchemaV1(value=42)
    instance_v1 = SchemaToContainerSchemaV1(nested=nested_instance)

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1))

    # Verify the upgrade - schema field converted to array of schema
    assert isinstance(instance_v2.nested_array, list)
    assert len(instance_v2.nested_array) == 1
    assert instance_v2.nested_array[0].value == 42
    assert instance_v2.nested_array[0].name == ""  # Default value for new field


def test_static_parent_with_upgraded_child() -> None:  # noqa: PLR0915 For testing only
    """Test upgrading when a parent schema doesn't change but a child schema does."""
    # Create schema sources for version 1
    schema_v1_source = """
    // Child schema version 1
    schema SubSchema
    {
      uuid: abe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Value field
        #1 value: Int32;
        // String field
        #2 name: VarString<max_size=32>;
      }
    }

    // Parent schema - will remain at version 1
    schema ParentSchema
    {
      uuid: bbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Parent field
        #1 parent_field: Int32;
        // Reference to child schema
        #2 child: SubSchema;
      }
    }

    // Parent schema with container - will remain at version 1
    schema ContainerParentSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Parent field
        #1 parent_field: Int32;
        // Array of child schemas
        #2 children: VarArray<SubSchema, max_size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SubSchema;
        schema ParentSchema;
        schema ContainerParentSchema;
        representation Tachyon<SubSchema>;
        representation Tachyon<ParentSchema>;
        representation Tachyon<ContainerParentSchema>;
        interface Tappy<SubSchema>;
        interface Tappy<ParentSchema>;
        interface Tappy<ContainerParentSchema>;
    }
    """

    # Create schema sources for version 2 - where the child schema changes but parent stays the same
    schema_v2_source = """
    // Child schema version 2
    schema SubSchema
    {
      uuid: abe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Value field upgraded to Int64
        #1 value: Int64;
        // String field - capacity increased
        #2 name: VarString<max_size=64>;
        // Added field
        #3 added_field: Optional<Bool> = nullopt;
      }
      history
      {
        version: 3;
      }
    }

    // Parent schema - unchanged at version 1
    schema ParentSchema
    {
      uuid: bbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Parent field
        #1 parent_field: Int32;
        // Reference to child schema
        #2 child: SubSchema;
      }
    }

    // Parent schema with container - unchanged at version 1
    schema ContainerParentSchema
    {
      uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
      fields
      {
        // Parent field
        #1 parent_field: Int32;
        // Array of child schemas
        #2 children: VarArray<SubSchema, max_size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SubSchema;
        schema ParentSchema;
        schema ContainerParentSchema;
        representation Tachyon<SubSchema>;
        representation Tachyon<ParentSchema>;
        representation Tachyon<ContainerParentSchema>;
        interface Tappy<SubSchema>;
        interface Tappy<ParentSchema>;
        interface Tappy<ContainerParentSchema>;
    }
    """

    # Compile the schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "nested_update_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "nested_update_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_sub_v1 = module_v1.inner_scope.lookup("SubSchema")
    schema_ir_parent_v1 = module_v1.inner_scope.lookup("ParentSchema")
    schema_ir_container_parent_v1 = module_v1.inner_scope.lookup("ContainerParentSchema")

    schema_ir_sub_v2 = module_v2.inner_scope.lookup("SubSchema")
    schema_ir_parent_v2 = module_v2.inner_scope.lookup("ParentSchema")
    schema_ir_container_parent_v2 = module_v2.inner_scope.lookup("ContainerParentSchema")

    assert isinstance(schema_ir_sub_v1, schema.Schema)
    assert isinstance(schema_ir_parent_v1, schema.Schema)
    assert isinstance(schema_ir_container_parent_v1, schema.Schema)
    assert isinstance(schema_ir_sub_v2, schema.Schema)
    assert isinstance(schema_ir_parent_v2, schema.Schema)
    assert isinstance(schema_ir_container_parent_v2, schema.Schema)

    # Create instantiated schemas
    schema_sub_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_sub_v1)
    schema_parent_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_parent_v1)
    schema_container_parent_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_container_parent_v1)

    schema_parent_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_parent_v2)
    schema_container_parent_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_container_parent_v2)

    # Create SerDes for v1 schemas
    serdes_sub_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_sub_v1)
    serdes_parent_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_parent_v1)
    serdes_container_parent_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v1.context, schema_container_parent_v1
    )

    # Test case 1: Direct field of child schema
    # Create instances for v1
    SubSchemaV1 = serdes_sub_v1.py_class  # noqa: N806 it's a type and should be camel case
    ParentSchemaV1 = serdes_parent_v1.py_class  # noqa: N806 it's a type and should be camel case

    sub_instance = SubSchemaV1(value=42, name="test child")
    parent_instance = ParentSchemaV1(parent_field=100, child=sub_instance)

    # Upgrade parent to v2
    upgraded_parent = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_parent_v2, parent_instance))

    # Verify parent fields remain the same
    assert upgraded_parent.parent_field == 100

    # Verify child was upgraded
    assert upgraded_parent.child.value == 42  # Value preserved, even though type changed
    assert upgraded_parent.child.name == "test child"
    assert upgraded_parent.child.added_field is None  # New field with default value

    # Test case 2: Container of child schema
    # Create instances for v1
    ContainerParentSchemaV1 = serdes_container_parent_v1.py_class  # noqa: N806 it's a type and should be camel case

    sub_instance1 = SubSchemaV1(value=10, name="child 1")
    sub_instance2 = SubSchemaV1(value=20, name="child 2")
    container_parent_instance = ContainerParentSchemaV1(parent_field=200, children=[sub_instance1, sub_instance2])

    # Upgrade container parent to v2
    upgraded_container = cast(
        "Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_container_parent_v2, container_parent_instance)
    )

    # Verify parent fields remain the same
    assert upgraded_container.parent_field == 200

    # Verify children were upgraded
    assert len(upgraded_container.children) == 2
    assert upgraded_container.children[0].value == 10  # Value preserved, even though type changed
    assert upgraded_container.children[0].name == "child 1"
    assert upgraded_container.children[0].added_field is None  # New field with default value

    assert upgraded_container.children[1].value == 20
    assert upgraded_container.children[1].name == "child 2"
    assert upgraded_container.children[1].added_field is None

    # Verify we can serialize the upgraded instances
    serdes_parent_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_parent_v2)
    serdes_container_parent_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_v2.context, schema_container_parent_v2
    )

    buffer1 = bytearray(serdes_parent_v2.py_class.get_tachyon_constraint().size)
    upgraded_parent.serialize_tachyon(memoryview(buffer1))

    buffer2 = bytearray(serdes_container_parent_v2.py_class.get_tachyon_constraint().size)
    upgraded_container.serialize_tachyon(memoryview(buffer2))


def compile_enum_upgrade_schemas(importer: FilesystemImporter) -> tuple[node.Module, node.Module, node.Module]:
    """Compile enum upgrade test schemas (v1, v2, and v3)."""
    # Create enum schema v1
    enum_v1_src = """
    // Test enum v1
    enum TestEnum
    {
      uuid: 02d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default;
        // Value 2
        #2 value2;
        // Value 3
        #3 value3;
        // To be removed in v2
        #4 to_be_removed;
        // To be renamed in v2
        #5 to_be_renamed;
        // Will go through multiple steps
        #6 multi_step1;
      }
    }

    // Enum with explicit values v1
    enum ExplicitEnum
    {
      uuid: 12d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default { underlying_value: 0; }
        // Value with explicit value
        #2 value2 { underlying_value: 10; }
        // Another value
        #3 value3 { underlying_value: 20; }
        // Value that will change
        #4 explicit_change { underlying_value: 30; }
      }
    }

    // Enum schema version 1
    schema EnumSchema
    {
      uuid: 435dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // Enum field
        #2 enum_field: TestEnum;
        // Explicit enum field
        #3 explicit_enum_field: ExplicitEnum;
        // Array of enums field
        #4 enum_array: VarArray<TestEnum, max_size=5>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema EnumSchema;
        representation Tachyon<EnumSchema>;
        interface Tappy<EnumSchema>;
    }
    """
    module_enum_v1 = compiler.compile_source_text(
        enum_v1_src, ModuleID(CLK_REPO, "enum_upgrade_schema"), importer=importer
    )

    # Create enum schema v2
    enum_v2_src = """
    // Test enum v2
    enum TestEnum
    {
      uuid: 02d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default;
        // Value 2
        #2 value2;
        // Value 3
        #3 value3;
        // Renamed value
        #7 renamed_value;
        // Will go through multiple steps
        #8 multi_step2;
        // New value added in v2
        #9 new_in_v2;
      }
      history
      {
        version: 9;
        legacy_became: [5->7, 6->8];
        removed: [4];
      }
    }

    // Enum with explicit values v2
    enum ExplicitEnum
    {
      uuid: 12d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default { underlying_value: 0; }
        // Value with explicit value
        #2 value2 { underlying_value: 10; }
        // Another value
        #3 value3 { underlying_value: 20; }
        // Value that will change
        #5 explicit_change { underlying_value: 40; }
      }
      history
      {
        version: 5;
        legacy_became: [4->5];
      }
    }

    // Enum schema version 2
    schema EnumSchema
    {
      uuid: 435dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Integer field
        #101 integer_field: Int64;
        // Enum field
        #2 enum_field: TestEnum;
        // Explicit enum field
        #3 explicit_enum_field: ExplicitEnum;
        // Array of enums field
        #4 enum_array: VarArray<TestEnum, max_size=5>;
      }
      history
      {
        version: 101;
        legacy_became: [1->101];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema EnumSchema;
        representation Tachyon<EnumSchema>;
        interface Tappy<EnumSchema>;
    }
    """
    module_enum_v2 = compiler.compile_source_text(
        enum_v2_src, ModuleID(CLK_REPO, "enum_upgrade_schema"), importer=importer
    )

    # Create enum schema v3
    enum_v3_src = """
    // Test enum v3
    enum TestEnum
    {
      uuid: 02d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default;
        // Value 2
        #2 value2;
        // Value 3
        #3 value3;
        // Renamed value
        #7 renamed_value;
        // Final step
        #10 multi_step_final;
        // New value added in v2
        #9 new_in_v2;
      }
      history
      {
        version: 10;
        legacy_became: [5->7, 6->8, 8->10];
        removed: [4];
      }
    }

    // Enum with explicit values v3
    enum ExplicitEnum
    {
      uuid: 12d1a601-5c69-46dd-9922-06fee33b581e;
      values
      {
        // Default value
        #1 default_value default { underlying_value: 0; }
        // Value with explicit value
        #2 value2 { underlying_value: 10; }
        // Another value
        #3 value3 { underlying_value: 20; }
        // Value that will change
        #6 explicit_change { underlying_value: 50; }
      }
      history
      {
        version: 6;
        legacy_became: [4->5, 5->6];
      }
    }

    // Enum schema version 3
    schema EnumSchema
    {
      uuid: 435dd4d4-7753-4aa6-a5a9-f39f60556aae;
      fields
      {
        // Integer field
        #101 integer_field: Int64;
        // Enum field
        #2 enum_field: TestEnum;
        // Explicit enum field
        #3 explicit_enum_field: ExplicitEnum;
        // Array of enums field
        #4 enum_array: VarArray<TestEnum, max_size=5>;
      }
      history
      {
        version: 101;
        legacy_became: [1->101];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema EnumSchema;
        representation Tachyon<EnumSchema>;
        interface Tappy<EnumSchema>;
    }
    """
    module_enum_v3 = compiler.compile_source_text(
        enum_v3_src, ModuleID(CLK_REPO, "enum_upgrade_schema"), importer=importer
    )

    return module_enum_v1, module_enum_v2, module_enum_v3


def test_enum_upgrade() -> None:
    """Test upgrading schemas with enum fields through multiple versions."""
    # Compile schemas
    module_enum_v1, module_enum_v2, module_enum_v3 = compile_enum_upgrade_schemas(fs_importer())

    # Get schema objects
    schema_ir_enum_v1 = module_enum_v1.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v2 = module_enum_v2.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v3 = module_enum_v3.inner_scope.lookup("EnumSchema")
    assert isinstance(schema_ir_enum_v1, schema.Schema)
    assert isinstance(schema_ir_enum_v2, schema.Schema)
    assert isinstance(schema_ir_enum_v3, schema.Schema)

    # Look up the enum types directly from the module
    test_enum_ir_v1 = module_enum_v1.inner_scope.lookup("TestEnum")
    explicit_enum_ir_v1 = module_enum_v1.inner_scope.lookup("ExplicitEnum")
    assert isinstance(test_enum_ir_v1, clkenum.ClkEnum)
    assert isinstance(explicit_enum_ir_v1, clkenum.ClkEnum)

    # Create instantiated schemas
    schema_enum_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v1)
    schema_enum_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v2)
    schema_enum_v3 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v3)

    # Create SerDes for all schemas
    serdes_enum_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v1.context, schema_enum_v1
    )
    serdes_enum_v3: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v3.context, schema_enum_v3
    )

    # Get the enum SerDes and classes
    test_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, test_enum_ir_v1.get_resolved())
    explicit_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, explicit_enum_ir_v1.get_resolved())

    test_enum_class = test_enum_serdes_v1.type_
    explicit_enum_class = explicit_enum_serdes_v1.type_

    # Create an instance of v1
    EnumSchemaV1 = serdes_enum_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = EnumSchemaV1(
        integer_field=42,
        enum_field=test_enum_class.to_be_renamed,
        explicit_enum_field=explicit_enum_class.explicit_change,
        enum_array=[test_enum_class.multi_step1, test_enum_class.value2],
    )

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_enum_v2.context, schema_enum_v2, instance_v1))

    # Verify the upgrade to v2
    assert instance_v2.integer_field == 42
    assert instance_v2.enum_field.name == "renamed_value"  # Enum value renamed (#5 -> #7)
    assert instance_v2.explicit_enum_field.name == "explicit_change"
    assert instance_v2.explicit_enum_field.value == 40  # Explicit value changed (30 -> 40)

    # Verify array elements were upgraded
    assert len(instance_v2.enum_array) == 2
    assert instance_v2.enum_array[0].name == "multi_step2"  # First element transformed (#6 -> #8)
    assert instance_v2.enum_array[1].name == "value2"  # Second element unchanged

    # Upgrade the instance to v3
    instance_v3 = cast("Any", tachyon_dyn.upgrade_schema(module_enum_v3.context, schema_enum_v3, instance_v2))

    # Verify the upgrade to v3
    assert instance_v3.integer_field == 42
    assert instance_v3.enum_field.name == "renamed_value"  # Enum value preserved
    assert instance_v3.explicit_enum_field.name == "explicit_change"
    assert instance_v3.explicit_enum_field.value == 50  # Explicit value changed again (40 -> 50)

    # Verify array elements were upgraded through multiple steps
    assert len(instance_v3.enum_array) == 2
    assert instance_v3.enum_array[0].name == "multi_step_final"  # First element transformed again (#8 -> #10)
    assert instance_v3.enum_array[1].name == "value2"  # Second element still unchanged

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_enum_v3.py_class.get_tachyon_constraint().size)
    instance_v3.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v3_copy = serdes_enum_v3.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v3_copy.integer_field == 42
    assert instance_v3_copy.enum_field.name == "renamed_value"
    assert instance_v3_copy.explicit_enum_field.name == "explicit_change"
    assert instance_v3_copy.explicit_enum_field.value == 50
    assert len(instance_v3_copy.enum_array) == 2
    assert instance_v3_copy.enum_array[0].name == "multi_step_final"


def test_removed_enum_value() -> None:
    """Test that using a removed enum value raises an error during upgrade."""
    # Compile schemas
    module_enum_v1, module_enum_v2, _ = compile_enum_upgrade_schemas(fs_importer())

    # Get schema objects and create instantiated schemas
    schema_ir_enum_v1 = module_enum_v1.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v2 = module_enum_v2.inner_scope.lookup("EnumSchema")
    assert isinstance(schema_ir_enum_v1, schema.Schema)
    assert isinstance(schema_ir_enum_v2, schema.Schema)

    # Look up the enum types directly
    test_enum_ir_v1 = module_enum_v1.inner_scope.lookup("TestEnum")
    explicit_enum_ir_v1 = module_enum_v1.inner_scope.lookup("ExplicitEnum")
    assert isinstance(test_enum_ir_v1, clkenum.ClkEnum)
    assert isinstance(explicit_enum_ir_v1, clkenum.ClkEnum)

    schema_enum_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v1)
    schema_enum_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v2)

    # Create SerDes for both schemas
    serdes_enum_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v1.context, schema_enum_v1
    )

    # Get the enum classes directly
    test_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, test_enum_ir_v1.get_resolved())
    explicit_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, explicit_enum_ir_v1.get_resolved())

    test_enum_class = test_enum_serdes_v1.type_
    explicit_enum_class = explicit_enum_serdes_v1.type_

    # Create an instance of v1 with a to-be-removed enum value
    EnumSchemaV1 = serdes_enum_v1.py_class  # noqa: N806 it's a type and should be camel case
    instance_v1 = EnumSchemaV1(
        integer_field=42,
        enum_field=test_enum_class.to_be_removed,  # This value was removed in v2
        explicit_enum_field=explicit_enum_class.explicit_change,
        enum_array=[test_enum_class.value2],
    )

    # This should raise ValueError about the removed enum value
    with pytest.raises(ValueError, match=r"Cannot upgrade enum value TestEnum\.to_be_removed.*"):
        tachyon_dyn.upgrade_schema(module_enum_v2.context, schema_enum_v2, instance_v1)


def test_container_of_enums_upgrade() -> None:
    """Test upgrading a container of enum values."""
    # Compile schemas
    module_enum_v1, _module_enum_v2, module_enum_v3 = compile_enum_upgrade_schemas(fs_importer())

    # Get schema objects and create instantiated schemas
    schema_ir_enum_v1 = module_enum_v1.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v3 = module_enum_v3.inner_scope.lookup("EnumSchema")
    assert isinstance(schema_ir_enum_v1, schema.Schema)
    assert isinstance(schema_ir_enum_v3, schema.Schema)

    # Look up the enum types directly
    test_enum_ir_v1 = module_enum_v1.inner_scope.lookup("TestEnum")
    assert isinstance(test_enum_ir_v1, clkenum.ClkEnum)

    schema_enum_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v1)
    schema_enum_v3 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v3)

    # Create SerDes
    serdes_enum_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(
        module_enum_v1.context, schema_enum_v1
    )

    # Get the enum class directly
    test_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, test_enum_ir_v1.get_resolved())
    test_enum_class = test_enum_serdes_v1.type_

    # Create an instance with all enum values in the array
    EnumSchemaV1 = serdes_enum_v1.py_class  # noqa: N806 it's a type and should be camel case
    explicit_enum_v1 = module_enum_v1.inner_scope.lookup("ExplicitEnum")
    assert isinstance(explicit_enum_v1, clkenum.ClkEnum)
    instance_v1 = EnumSchemaV1(
        integer_field=42,
        enum_field=test_enum_class.default_value,
        explicit_enum_field=tachyon_dyn.serdes_for_type(
            module_enum_v1.context, explicit_enum_v1.get_resolved()
        ).type_.default_value,
        # Include all enum values that will be transformed:
        enum_array=[
            test_enum_class.default_value,  # Unchanged
            test_enum_class.value2,  # Unchanged
            test_enum_class.value3,  # Unchanged
            test_enum_class.multi_step1,  # Will go through multi-step transformation
            test_enum_class.to_be_renamed,  # Will be renamed
        ],
    )

    # Upgrade directly from v1 to v3
    instance_v3 = cast("Any", tachyon_dyn.upgrade_schema(module_enum_v3.context, schema_enum_v3, instance_v1))

    # Verify the upgrade worked for all enum array elements
    assert len(instance_v3.enum_array) == 5
    assert instance_v3.enum_array[0].name == "default_value"  # Unchanged
    assert instance_v3.enum_array[1].name == "value2"  # Unchanged
    assert instance_v3.enum_array[2].name == "value3"  # Unchanged
    assert instance_v3.enum_array[3].name == "multi_step_final"  # Transformed through multiple steps
    assert instance_v3.enum_array[4].name == "renamed_value"  # Renamed


def test_multi_step_enum_value_evolution() -> None:
    """Test enum value that goes through multiple evolution steps."""
    # Compile schemas
    module_enum_v1, _, module_enum_v3 = compile_enum_upgrade_schemas(fs_importer())

    # Get schema objects and create instantiated schemas
    schema_ir_enum_v1 = module_enum_v1.inner_scope.lookup("EnumSchema")
    schema_ir_enum_v3 = module_enum_v3.inner_scope.lookup("EnumSchema")
    assert isinstance(schema_ir_enum_v1, schema.Schema)
    assert isinstance(schema_ir_enum_v3, schema.Schema)

    # Look up the enum types directly
    test_enum_ir_v1 = module_enum_v1.inner_scope.lookup("TestEnum")
    explicit_enum_ir_v1 = module_enum_v1.inner_scope.lookup("ExplicitEnum")
    test_enum_ir_v3 = module_enum_v3.inner_scope.lookup("TestEnum")
    explicit_enum_ir_v3 = module_enum_v3.inner_scope.lookup("ExplicitEnum")

    assert isinstance(test_enum_ir_v1, clkenum.ClkEnum)
    assert isinstance(explicit_enum_ir_v1, clkenum.ClkEnum)
    assert isinstance(test_enum_ir_v3, clkenum.ClkEnum)
    assert isinstance(explicit_enum_ir_v3, clkenum.ClkEnum)

    schema_enum_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v1)
    schema_enum_v3 = schema.InstantiatedSchema.from_typespec(schema_ir_enum_v3)

    # Create SerDes
    serdes_enum_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, schema_enum_v1)

    # Get the enum classes directly
    test_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, test_enum_ir_v1.get_resolved())
    explicit_enum_serdes_v1 = tachyon_dyn.serdes_for_type(module_enum_v1.context, explicit_enum_ir_v1.get_resolved())

    test_enum_serdes_v3 = tachyon_dyn.serdes_for_type(module_enum_v3.context, test_enum_ir_v3.get_resolved())
    explicit_enum_serdes_v3 = tachyon_dyn.serdes_for_type(module_enum_v3.context, explicit_enum_ir_v3.get_resolved())

    test_enum_class_v1 = test_enum_serdes_v1.type_
    explicit_enum_class_v1 = explicit_enum_serdes_v1.type_

    test_enum_class_v3 = test_enum_serdes_v3.type_
    explicit_enum_class_v3 = explicit_enum_serdes_v3.type_

    # Create an instance focusing on multi-step values
    EnumSchemaV1 = serdes_enum_v1.type_  # noqa: N806 it's a type and should be camel case
    instance_v1 = EnumSchemaV1(
        integer_field=42,
        enum_field=test_enum_class_v1.multi_step1,  # Will become multi_step_final
        explicit_enum_field=explicit_enum_class_v1.explicit_change,  # Will have value change twice
        enum_array=[test_enum_class_v1.default_value],
    )

    # Upgrade directly from v1 to v3 (skipping v2)
    instance_v3 = cast("Any", tachyon_dyn.upgrade_schema(module_enum_v3.context, schema_enum_v3, instance_v1))

    # Verify multi-step transformations worked correctly
    assert instance_v3.enum_field == test_enum_class_v3.multi_step_final
    assert instance_v3.explicit_enum_field == explicit_enum_class_v3.explicit_change


def test_unmodified_synctime_field() -> None:
    """Test upgrading a schema where a SyncTime field remains unchanged but other fields change.

    This catches a regression in primitive field upgrades where strong primitives like SyncTime could not be converted
    to themselves.
    """
    # Schema with SyncTime field that won't change
    schema_v1_source = """
    // Schema version 1 with SyncTime field
    schema SyncTimeSchema
    {
      uuid: 53c6ee0b-ec41-40ce-8587-fb3583e5385f;
      fields
      {
        // An integer field that will change
        #1 integer_field: Int32;
        // A SyncTime field that will remain unchanged
        #2 time_field: SyncTime;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SyncTimeSchema;
        representation Tachyon<SyncTimeSchema>;
        interface Tappy<SyncTimeSchema>;
    }
    """

    # Schema with modified fields but unchanged SyncTime field
    schema_v2_source = """
    // Schema version 2 with unchanged SyncTime field
    schema SyncTimeSchema
    {
      uuid: 53c6ee0b-ec41-40ce-8587-fb3583e5385f;
      fields
      {
        // Integer field upgraded to Int64
        #3 integer_field: Int64;
        // SyncTime field remains unchanged
        #2 time_field: SyncTime;
      }
      history
      {
        version: 3;
        legacy_became: [1->3];
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SyncTimeSchema;
        representation Tachyon<SyncTimeSchema>;
        interface Tappy<SyncTimeSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "synctime_schema_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "synctime_schema_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("SyncTimeSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("SyncTimeSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    # Create an instance of v1
    SyncTimeSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    time_value = 12345678  # Example SyncTime value
    instance_v1 = SyncTimeSchemaV1(
        integer_field=42,
        time_field=time_value,
    )

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1))

    # Verify the upgrade
    assert instance_v2.integer_field == 42  # Value preserved but type changed to Int64
    assert instance_v2.time_field == time_value  # SyncTime field should be unchanged

    # Verify we can serialize the upgraded instance
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))
    instance_v2_copy = cast("Any", instance_v2_copy)  # pyright: ignore[reportUnnecessaryCast] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # Cast to Any to suppress mypy errors
    assert instance_v2_copy.integer_field == 42
    assert instance_v2_copy.time_field == time_value


def test_child_parameter_change() -> None:
    """Test upgrading a schema where a child schema has a parameter used to set a container size."""
    schema_v1_source = """
    // Container
    schema Container
    {
      uuid: 6995014b-b265-44be-be58-4777fedbbfa3;
      parameters
      {
        // Value type
        #1 value_type: Type;
        // Size of the container
        #2 size: UInt64;
      }
      fields
      {
        // Array of given type
        #3 integers: VarArray<value_type, max_size=size>;
      }
    }

    // Schema version 1
    schema Parent
    {
      uuid: 53c6ee0b-ec41-40ce-8587-fb3583e5385f;
      fields
      {
        // Container
        #1 container: Container<Int32, size=3>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema Container;
        schema Parent;
        representation Tachyon<Container<Int32, 3>>;
        representation Tachyon<Parent>;
    }
    """

    # Schema with modified fields but unchanged SyncTime field
    schema_v2_source = """
    // Container
    schema Container
    {
      uuid: 6995014b-b265-44be-be58-4777fedbbfa3;
      parameters
      {
        // Value type
        #1 value_type: Type;
        // Size of the container
        #2 size: UInt64;
      }
      fields
      {
        // Array of given type
        #3 integers: VarArray<value_type, max_size=size>;
      }
    }
    // Schema version 2
    schema Parent
    {
      uuid: 53c6ee0b-ec41-40ce-8587-fb3583e5385f;
      fields
      {
        // Container
        #2 container: Container<UInt8, size=5>;
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
        schema Container;
        schema Parent;
        representation Tachyon<Container<UInt8, 5>>;
        representation Tachyon<Parent>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "container_param_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "container_param_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("Parent")
    schema_ir_v2 = module_v2.inner_scope.lookup("Parent")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)

    # Create an instance of v1
    ParentV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    ContainerV1 = serdes_v1.field_serdeses[0][2].type_  # noqa: N806 it's a type and should be camel case

    # Create a container with data
    container_instance = ContainerV1(integers=[1, 2, 3])
    instance_v1 = ParentV1(container=container_instance)

    # Serialize and deserialize the instance to ensure it works
    buffer = bytearray(serdes_v1.py_class.get_tachyon_constraint().size)
    instance_v1.serialize_tachyon(memoryview(buffer))
    instance_v1_copy = serdes_v1.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v1_copy.container.integers == [1, 2, 3]

    # Upgrade the instance to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1_copy))

    # Verify the upgrade
    assert instance_v2.container.integers == [1, 2, 3]

    # Verify we can add more elements now that the capacity has increased
    instance_v2.container.integers = [1, 2, 3, 4, 5]
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))

    # Deserialize and verify
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v2_copy.container.integers == [1, 2, 3, 4, 5]


def test_uuid_to_varstring_upgrade() -> None:
    """Test upgrading a schema field from UUID to VarString<max_size=37>."""
    schema_v1_source = """
    // Schema version 1 with a UUID field
    schema IdSchema
    {
      uuid: 1a2b3c4d-0000-0000-0000-000000000001;
      fields
      {
        // Unique identifier
        #1 id_field: Uuid<Int8>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema IdSchema;
        representation Tachyon<IdSchema>;
        interface Tappy<IdSchema>;
    }
    """

    schema_v2_source = """
    // Schema version 2 with UUID field upgraded to VarString
    schema IdSchema
    {
      uuid: 1a2b3c4d-0000-0000-0000-000000000001;
      fields
      {
        // Unique identifier as string
        #2 id_field: VarString<max_size=37>;
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
        schema IdSchema;
        representation Tachyon<IdSchema>;
        interface Tappy<IdSchema>;
    }
    """

    # Compile schemas
    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "uuid_varstring_upgrade_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "uuid_varstring_upgrade_test"), importer=fs_importer()
    )

    # Get schema objects
    schema_ir_v1 = module_v1.inner_scope.lookup("IdSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("IdSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    # Create instantiated schemas
    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    # Create SerDes for both schemas
    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    serdes_v2: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v2.context, schema_v2)

    # Create a v1 instance with a known UUID
    IdSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    test_uuid = uuid.UUID("550e8400-e29b-41d4-a716-446655440000")
    instance_v1 = IdSchemaV1(id_field=test_uuid)

    # Upgrade to v2
    instance_v2 = cast("Any", tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1))

    # The UUID should have been converted to its canonical string representation
    assert instance_v2.id_field == str(test_uuid)

    # Verify round-trip through serialization
    buffer = bytearray(serdes_v2.py_class.get_tachyon_constraint().size)
    instance_v2.serialize_tachyon(memoryview(buffer))
    instance_v2_copy = serdes_v2.py_class.deserialize_tachyon(memoryview(buffer))
    assert instance_v2_copy.id_field == str(test_uuid)


def test_uuid_to_varstring_too_small_raises() -> None:
    """Test that upgrading a UUID field to a VarString with max_size <= 36 raises an error."""
    schema_v1_source = """
    // Schema for VarString max_size validation
    schema SmallIdSchema
    {
      uuid: 1a2b3c4d-0000-0000-0000-000000000002;
      fields
      {
        // Unique identifier
        #1 id_field: Uuid<Int8>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema SmallIdSchema;
        representation Tachyon<SmallIdSchema>;
        interface Tappy<SmallIdSchema>;
    }
    """

    schema_v2_source = """
    // Schema for VarString max_size validation v2
    schema SmallIdSchema
    {
      uuid: 1a2b3c4d-0000-0000-0000-000000000002;
      fields
      {
        // max_size=10 is too small to hold a 36-character UUID string
        #2 id_field: VarString<max_size=10>;
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
        schema SmallIdSchema;
        representation Tachyon<SmallIdSchema>;
        interface Tappy<SmallIdSchema>;
    }
    """

    module_v1 = compiler.compile_source_text(
        schema_v1_source, ModuleID(CLK_REPO, "uuid_too_small_test"), importer=fs_importer()
    )
    module_v2 = compiler.compile_source_text(
        schema_v2_source, ModuleID(CLK_REPO, "uuid_too_small_test"), importer=fs_importer()
    )

    schema_ir_v1 = module_v1.inner_scope.lookup("SmallIdSchema")
    schema_ir_v2 = module_v2.inner_scope.lookup("SmallIdSchema")
    assert isinstance(schema_ir_v1, schema.Schema)
    assert isinstance(schema_ir_v2, schema.Schema)

    schema_v1 = schema.InstantiatedSchema.from_typespec(schema_ir_v1)
    schema_v2 = schema.InstantiatedSchema.from_typespec(schema_ir_v2)

    serdes_v1: tachyon_dyn.SchemaSerDes[Any] = tachyon_dyn.SchemaSerDes.make(module_v1.context, schema_v1)
    SmallIdSchemaV1 = serdes_v1.py_class  # noqa: N806 it's a type and should be camel case
    test_uuid = uuid.UUID("550e8400-e29b-41d4-a716-446655440000")
    instance_v1 = SmallIdSchemaV1(id_field=test_uuid)

    with pytest.raises(ValueError, match="max_size must be greater than 36"):
        tachyon_dyn.upgrade_schema(module_v2.context, schema_v2, instance_v1)
