# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp_target."""

from __future__ import annotations

from textwrap import dedent
from typing import Final

import pytest
from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.python import typereg as py_typereg
from clockwork.dsl.serialization import pytap
from clockwork.dsl.tests.support import py_bindings_v2_clk_py, py_bindings_v2_ext_clk_py
from jewels.container.tap import py_var_array, value_serdes

MY_SCHEMA_SIZE: Final = 88
NESTED_SCHEMA_SIZE: Final = 1
EXT_NESTED_SCHEMA_SIZE: Final = 1


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_bytes_constructor() -> None:
    py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    wrong_size = MY_SCHEMA_SIZE + 1
    with pytest.raises(ValueError, match=f"Expected buffer of size {MY_SCHEMA_SIZE} but received {wrong_size}"):
        py_bindings_v2_clk_py.MySchema(bytearray(wrong_size))


def test_primitives() -> None:
    schema = py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    assert schema.some_integer == 0
    schema.some_integer = 123
    assert schema.some_integer == 123
    assert not schema.some_bool
    schema.some_bool = True
    assert schema.some_bool


def test_type_mappings() -> None:
    # Schemas access primitive fields by generating the pack/unpack
    # function call directly instead of using the SerDes
    # implementations.  This saves some overhead.  But we can use them
    # to validate we have the right specifiers given the SerDes are
    # already tested.
    for key, value in pytap.STRUCT_FORMAT_MAP.items():
        serdes_name = f"{key[1:]}SerDes"
        assert getattr(value_serdes, serdes_name).format_spec() == f"{pytap.ENDIAN}{value}"


def test_py_type_registry(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        // Doc
        schema Schema
        {
          fields
          {
            // Doc
            #1 value: UInt64;
          }
        }

        py_target baz
        {
          representation Tachyon<Schema>;
          interface Tap<Tachyon<Schema>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo::bar"), fs_importer)
    schema_py_type = py_typereg.get_py_type(module.context, f"@{CLK_REPO}::foo::bar::Schema")
    assert schema_py_type == py_typereg.PyType(
        repo=CLK_REPO,
        # The clk file would have been at foo/bar.clk.  However, the
        # py target name is baz.
        import_spec="foo.baz",
        class_name="Schema",
    )


def test_nested_schemas() -> None:
    schema = py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    assert schema.nested_schema.some_integer == 0
    schema.nested_schema.some_integer = 123
    # Accessing a nested schema returns a reference type.  Thus,
    # modifying the reference should modify the original object too.
    assert schema.nested_schema.some_integer == 123

    nested = py_bindings_v2_clk_py.NestedSchema(bytearray(NESTED_SCHEMA_SIZE))
    nested.some_integer = 111
    assert nested.some_integer == 111

    schema.nested_schema = nested
    assert schema.nested_schema


def test_external_nested_schemas() -> None:
    schema = py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    assert schema.ext_nested_schema.some_integer == 0
    schema.ext_nested_schema.some_integer = 123
    # Accessing a nested schema returns a reference type.  Thus,
    # modifying the reference should modify the original object too.
    assert schema.ext_nested_schema.some_integer == 123

    nested = py_bindings_v2_ext_clk_py.NestedSchema(bytearray(EXT_NESTED_SCHEMA_SIZE))
    nested.some_integer = 111
    assert nested.some_integer == 111

    schema.ext_nested_schema = nested
    assert schema.ext_nested_schema


def test_var_array_fields() -> None:
    schema = py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    var_array = schema.var_array_of_primitives
    assert not var_array
    var_array.append(0)
    schema.var_array_of_primitives.append(1)
    var_array.append(2)
    assert list(var_array) == [0, 1, 2]
    assert list(schema.var_array_of_primitives) == [0, 1, 2]
    var_array.clear()
    assert not schema.var_array_of_primitives


def test_nested_var_array_fields() -> None:
    schema = py_bindings_v2_clk_py.MySchema(bytearray(MY_SCHEMA_SIZE))
    var_array = schema.nested_var_array_of_primitives
    assert not var_array
    outer_cap = 3
    inner_cap = 2
    for outer in range(outer_cap):
        var_array.append(py_var_array.VarArray(inner_cap, value_serdes.UInt32SerDes))  # pyright: ignore[reportArgumentType] TODO(DX-2691) False positive
        for inner in range(inner_cap):
            var_array[outer].append(outer * inner_cap + inner)

    assert list(var_array[0]) == [0, 1]
    assert list(var_array[1]) == [2, 3]
    assert list(var_array[2]) == [4, 5]
