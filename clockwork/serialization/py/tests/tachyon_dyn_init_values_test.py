# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for default value and init_value handling in tachyon_dyn."""

from __future__ import annotations

import re
import uuid
from decimal import Decimal

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, compiler, primitive, schema, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_primitive_default_values() -> None:
    """Test that primitive types get correct default values."""
    # Test primitive type defaults
    context = CompilerContext()
    assert tachyon_dyn._get_default_for_type(context, clkbuiltins.BOOL) == (True, False)
    assert tachyon_dyn._get_default_for_type(context, clkbuiltins.INT8) == (True, 0)
    assert tachyon_dyn._get_default_for_type(context, clkbuiltins.UINT64) == (True, 0)
    assert tachyon_dyn._get_default_for_type(context, clkbuiltins.FLOAT32) == (True, 0.0)
    assert tachyon_dyn._get_default_for_type(context, clkbuiltins.DURATION) == (True, 0)


def test_container_default_values() -> None:
    """Test that container types get correct default values."""
    context = CompilerContext()
    # Fixed array with 3 INT32s
    fixed_array = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.FIXED_ARRAY,
        arguments={
            "type": clkbuiltins.INT32,
            "size": primitive.DecimalValue(clkbuiltins.INT64, Decimal(3)),
        },
    )
    assert tachyon_dyn._get_default_for_type(context, fixed_array) == (True, [0, 0, 0])

    # Var array of FLOAT64s
    var_array = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_ARRAY,
        arguments={
            "type": clkbuiltins.FLOAT64,
            "max_size": primitive.DecimalValue(clkbuiltins.INT64, Decimal(10)),
        },
    )
    assert tachyon_dyn._get_default_for_type(context, var_array) == (True, [])

    # Var string
    var_string = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.VAR_STRING,
        arguments={"max_size": primitive.DecimalValue(clkbuiltins.INT64, Decimal(10))},
    )
    assert tachyon_dyn._get_default_for_type(context, var_string) == (True, "")

    # Optional INT64
    optional = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.OPTIONAL,
        arguments={"type": clkbuiltins.INT64},
    )
    assert tachyon_dyn._get_default_for_type(context, optional) == (True, None)


def test_schema_with_init_values(fs_importer: FilesystemImporter) -> None:
    """Test that schemas with init values work correctly."""
    source_content = """
    // Schema with initialization values
    schema WithInitValues {
        fields {
            // Integer with init value
            #1 integer: Int32 = 42;
            // Float with init value
            #2 floating_point: Float64 = 3.14;
            // Boolean with init value
            #3 boolean: Bool = true;
        }
    }

    cpp_target test_target {
        options { namespace test; }
        representation Tachyon<WithInitValues>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(source_content, ModuleID(CLK_REPO, "init_values_test"), importer=fs_importer)

    # Get the dataclass
    with_init_values_class, _schema_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "WithInitValues")

    # Create a new instance with defaults
    instance = with_init_values_class()

    # Verify defaults were applied
    assert instance.integer == 42
    assert instance.floating_point == 3.14
    assert instance.boolean is True

    # Create with overridden values
    custom = with_init_values_class(integer=100, boolean=False)
    assert custom.integer == 100
    assert custom.floating_point == 3.14  # Default still applies
    assert custom.boolean is False


def test_enum_default_values(fs_importer: FilesystemImporter) -> None:
    """Test that enums get correct default values."""
    source_content = """
    // Enum definition
    enum TestEnum {
        values {
            // First value
            #1 first;
            // Second value (default)
            #2 second default;
            // Third value
            #3 third;
        }
    }

    // Schema with enum fields
    schema WithEnumField {
        fields {
            // Default enum field using default enum value
            #1 default_enum: TestEnum;
            // Enum field with explicit init
            #2 enum_field: TestEnum = TestEnum::second;
            // Enum field with non-default init
            #3 custom_enum: TestEnum = TestEnum::first;
        }
    }

        cpp_target test_target {
        options { namespace test; }
        representation Tachyon<WithEnumField>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(source_content, ModuleID(CLK_REPO, "enum_test"), importer=fs_importer)

    # Get the enum and schema dataclasses
    test_enum_class, _enum_ir = tachyon_dyn.get_enum(module.context, module, "TestEnum")
    with_enum_field_class, _schema_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "WithEnumField")

    # Create instance with defaults
    instance = with_enum_field_class()

    # Check enum default values
    assert instance.default_enum == test_enum_class.second  # Uses enum's default value
    assert instance.enum_field == test_enum_class.second  # Uses init value
    assert instance.custom_enum == test_enum_class.first  # Uses explicit non-default init

    # Override defaults
    custom = with_enum_field_class(
        default_enum=test_enum_class.third, enum_field=test_enum_class.third, custom_enum=test_enum_class.third
    )
    assert custom.default_enum == test_enum_class.third
    assert custom.enum_field == test_enum_class.third
    assert custom.custom_enum == test_enum_class.third


def test_nested_schema_defaults(fs_importer: FilesystemImporter) -> None:
    """Test that nested schemas with defaults work correctly."""
    source_content = """
    // Inner schema with default values
    schema Inner {
        fields {
            // Value with init
            #1 value: Int32 = 100;
            // Flag with init
            #2 flag: Bool = true;
        }
    }

    // Outer schema containing an Inner schema
    schema Outer {
        fields {
            // Nested schema field
            #1 inner: Inner;
            // Value with init
            #2 outer_value: Int32 = 200;
        }
    }

        cpp_target test_target {
        options { namespace test; }
        representation Tachyon<Inner>;
        representation Tachyon<Outer>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(
        source_content, ModuleID(CLK_REPO, "nested_schema_test"), importer=fs_importer
    )

    # Get the dataclasses
    inner_class, _inner_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "Inner")
    outer_class, _outer_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "Outer")

    # Create a nested instance with defaults
    instance = outer_class()

    # Verify defaults were applied to both levels
    assert instance.outer_value == 200
    assert instance.inner.value == 100
    assert instance.inner.flag is True

    # Test customization
    inner_custom = inner_class(value=999, flag=False)
    outer_custom = outer_class(inner=inner_custom, outer_value=888)
    assert outer_custom.outer_value == 888
    assert outer_custom.inner.value == 999
    assert outer_custom.inner.flag is False


def test_container_with_defaults(fs_importer: FilesystemImporter) -> None:
    """Test containers with default values."""
    source_content = """
    // Schema with various container types
    schema WithContainers {
        fields {
            // Fixed array
            #1 fixed_array: FixedArray<Int32, 3>;
            // Variable array
            #2 var_array: VarArray<Float32, max_size=10>;
            // String
            #3 var_string: VarString<20>;
            // Optional int
            #4 maybe_int: Optional<Int32>;
        }
    }

        cpp_target test_target {
        options { namespace test; }
        representation Tachyon<WithContainers>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(source_content, ModuleID(CLK_REPO, "containers_test"), importer=fs_importer)

    # Get the dataclass
    with_containers_class, _schema_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "WithContainers")

    # Create instance with defaults
    instance = with_containers_class()

    # Verify container defaults
    assert instance.fixed_array == [0, 0, 0]
    assert instance.var_array == []
    assert instance.var_string == ""
    assert instance.maybe_int is None

    # Test with custom values
    custom = with_containers_class(
        fixed_array=[1, 2, 3],
        var_array=[1.1, 2.2],
        var_string="hello",
        maybe_int=42,
    )
    assert custom.fixed_array == [1, 2, 3]
    assert custom.var_array == [1.1, 2.2]
    assert custom.var_string == "hello"
    assert custom.maybe_int == 42


def test_conversion_errors(fs_importer: FilesystemImporter) -> None:
    """Test that appropriate errors are raised for invalid conversions."""
    source_content = """
    // Simple schema
    schema SimpleSchema {
        fields {
            // Integer
            #1 integer: Int32;
        }
    }

    cpp_target test_target {
        options { namespace test; }
        representation Tachyon<SimpleSchema>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(source_content, ModuleID(CLK_REPO, "containers_test"), importer=fs_importer)

    # Get the dataclass
    _, schema_ir = tachyon_dyn.get_schema_dataclass(module.context, module, "SimpleSchema")

    # Try to convert string to int
    string_val = primitive.StringValue(clkbuiltins.STRING, "test")
    with pytest.raises(
        TypeError,
        match=re.escape(
            "Unsupported init_value type: <class 'clockwork.dsl.ir.primitive.StringValue'> for Python type <class 'int'>"
        ),
    ):
        tachyon_dyn._convert_init_value_to_python(
            CompilerContext(), schema.InstantiatedSchema.from_typespec(schema_ir), string_val, int
        )


def test_complex_schema_with_all_types(fs_importer: FilesystemImporter) -> None:
    """Test a complex schema with all types of fields and default values."""
    source_content = """
        use jewels::units::clk::au::{MetersF};

    // Status enum definition
    enum Status {
        values {
            // Inactive status
            #1 inactive { underlying_value: 2; }
            // Pending status
            #2 pending { underlying_value: 1; }
            // Active status (default)
            #3 active default { underlying_value: 0; }
            // Complete status
            #4 complete { underlying_value: 3; }
        }
    }

    // Sub-schema definition
    schema SubItem {
        fields {
            // Sub-item name field
            #1 name: VarString<20>;
            // Sub-item value field
            #2 value: Int32 = 42;
        }
    }

    // Complex schema with various field types
    schema ComplexSchema {
        fields {
            // Basic primitive types with defaults
            #1 integer: Int64 = 1000;
            // Unsigned integer field
            #2 unsigned: UInt32 = 2000;
            // Floating point field
            #3 floating: Float64 = 3.14159;
            // Boolean field
            #4 boolean: Bool = true;

            // Distance field with strong type
            #6 distance: MetersF = 100.0;

            // Status enum field
            #7 status: Status = Status::pending;

            // String container
            #9 string_field: VarString<30>;
            // Fixed array container
            #10 fixed_array: VarArray<Int32, 3>;
            // Variable array container
            #11 var_array: VarArray<Float32, max_size=5>;
            // Optional with value
            #12 optional_int: Optional<Int32>;
            // Optional without value
            #13 optional_empty: Optional<Float64>;

            // Nested schema field
            #14 item: SubItem;
            // Array of nested schemas
            #15 items: VarArray<SubItem, max_size=2>;

            // UUID field
            #16 id: Uuid<SubItem>;
        }
    }

    cpp_target testtarget
    {
        options { namespace test; }
        representation Tachyon<SubItem>;
        interface Tappy<SubItem>;

        representation Tachyon<ComplexSchema>;
        interface Tappy<ComplexSchema>;
    }
    """

    # Compile the test schema directly from source
    module = compiler.compile_source_text(
        source_content, ModuleID(CLK_REPO, "complex_schema_test"), importer=fs_importer
    )

    # Get the dataclasses
    status_class, _ = tachyon_dyn.get_enum(module.context, module, "Status")
    sub_item_class, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "SubItem")
    complex_schema_class, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "ComplexSchema")

    # Create instance with defaults
    instance = complex_schema_class(id=uuid.uuid4())

    # Verify primitive defaults
    assert instance.integer == 1000
    assert instance.unsigned == 2000
    assert instance.floating == 3.14159
    assert instance.boolean is True

    # Verify strong type defaults
    assert instance.distance == 100.0

    # Verify enum and flags defaults
    assert instance.status == status_class.pending

    # Verify container defaults
    assert instance.string_field == ""
    assert instance.fixed_array == []
    assert instance.var_array == []
    assert instance.optional_int is None
    assert instance.optional_empty is None

    # Verify nested schema defaults
    assert instance.item.name == ""
    assert instance.item.value == 42
    assert len(instance.items) == 0

    # Verify UUID field (should be generated fresh for each instance)
    assert isinstance(instance.id, uuid.UUID)

    # Test with custom values
    custom_subitem = sub_item_class(name="Custom SubItem", value=99)
    custom = complex_schema_class(
        id=uuid.uuid4(),
        integer=500,
        status=status_class.complete,
        item=custom_subitem,
        items=[custom_subitem, sub_item_class(name="Second item", value=77)],
    )

    assert custom.integer == 500
    assert custom.status == status_class.complete
    assert custom.item.name == "Custom SubItem"
    assert custom.item.value == 99
    assert len(custom.items) == 2
    assert custom.items[0].name == "Custom SubItem"
    assert custom.items[1].name == "Second item"
