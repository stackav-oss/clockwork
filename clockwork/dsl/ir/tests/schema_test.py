# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Schema IR module."""

import re
import uuid
from decimal import Decimal
from pathlib import Path
from textwrap import dedent
from typing import Final

import pytest
from clockwork.dsl import cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    compiler,
    expr,
    importer,
    node,
    parse,
    primitive,
    schema,
    strongtypes,
    typesys,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path


@pytest.fixture()
def hellomsg_cst() -> parse.CstParseContext[cst.Module]:
    return parse.clk_source_to_cst(str(fix_clockwork_path(Path("clockwork/dsl/tests/support/hellomsg.clk"))))


@pytest.fixture()
def hellomsg_module(hellomsg_cst: parse.CstParseContext[cst.Module]) -> node.Module:
    return module_from_cst(hellomsg_cst)


def module_from_cst(parse_context: parse.CstParseContext[cst.Module]) -> node.Module:
    return node.Module.from_cst(
        module_id=ModuleID(CLK_REPO, "test"),
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_context.cst,
        terminals=parse_context.terminals,
    )


@pytest.fixture()
def hellomsg_schema_firstpass(hellomsg_module: node.Module) -> schema.Schema:
    return schema_firstpass(hellomsg_module)


def schema_firstpass(module: node.Module) -> schema.Schema:
    assert module.cst_node is not None
    return schema.Schema.from_cst(
        module=module,
        scope=module.inner_scope,
        cst_schema=next(module.cst_node.children_entity()).child_schema(),
    )


def test_schema_cst_to_ir() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, fix_clockwork_path(Path("clockwork/dsl/tests/support/hellomsg.clk"))), fs_importer
    )
    schema_ir: Final = module.inner_scope.lookup("HelloMsg")
    assert isinstance(schema_ir, schema.Schema)
    assert schema_ir.module is module
    assert schema_ir.name == "HelloMsg"
    assert schema_ir.uuid == uuid.UUID("cbe6ee0b-ec41-40ce-8587-fb3583e5385d")
    assert len(schema_ir.fields) == 4
    f1 = schema_ir.fields[1]
    f2 = schema_ir.fields[2]
    f3 = schema_ir.fields[3]
    f4 = schema_ir.fields[4]
    assert isinstance(f1, schema.FieldDef)
    assert isinstance(f2, schema.FieldDef)
    assert isinstance(f3, schema.FieldDef)
    assert isinstance(f4, schema.FieldDef)
    assert f1.num == 1
    assert f1.cur_name == "seqno"
    assert f1.doc
    assert f1.doc.value == "Sequence number"
    assert f2.num == 2
    assert f2.cur_name == "data"
    assert f2.doc
    assert f2.doc.value == "Some data"

    assert f1.type_info is clkbuiltins.INT64
    assert isinstance(f2.type_info, typesys.Instantiation)
    assert f2.type_info.instantiates is clkbuiltins.FIXED_ARRAY
    assert set(f2.type_info.arguments.keys()) == {"type", "size"}
    arg_size = f2.type_info.arguments["size"]
    assert isinstance(arg_size, primitive.DecimalLiteral)
    assert isinstance(arg_size.type_info, typesys.InferenceVar)
    assert arg_size.type_info.resolution() is clkbuiltins.UINT64
    assert arg_size.value == 1024
    arg_type = f2.type_info.arguments["type"]
    assert isinstance(arg_type, typesys.TypeDef)
    assert arg_type is clkbuiltins.BYTE

    assert isinstance(f3.type_info, typesys.Instantiation)
    assert f3.type_info.instantiates is clkbuiltins.UUID
    sample_tag = module.inner_scope.lookup("SampleTag")
    assert isinstance(sample_tag, strongtypes.Tag)
    assert f3.type_info.arguments["tag"] is sample_tag

    assert isinstance(f4.type_info, typesys.Instantiation)
    assert f4.type_info.instantiates is clkbuiltins.UUID
    assert f4.type_info.arguments["tag"] is schema_ir


def test_error_no_terminals(hellomsg_module: node.Module) -> None:
    hellomsg_module.terminals = None
    assert hellomsg_module.cst_node is not None
    schema_cst = next(hellomsg_module.cst_node.children_entity()).child_schema()
    with pytest.raises(ValueError, match="Cannot construct IR nodes from CST without a TerminalSource"):
        schema.Schema.from_cst(
            module=hellomsg_module,
            scope=hellomsg_module.inner_scope,
            cst_schema=schema_cst,
        )
    with pytest.raises(ValueError, match="Cannot construct IR nodes from CST without a TerminalSource"):
        schema.FieldDef.from_cst(
            module=hellomsg_module,
            cst_node=next(
                schema_cst.child_schema_fields_block().children_schema_field(),
            ),
        )


def test_error_bad_field_type_expression_firstpass() -> None:
    parse_result = parse.clk_string_to_cst(
        """
// Hello!
schema HelloMsg
{
  fields
  {
    // Sequence number
    #1 seqno: 1;
    // Some data
    #2 data: FixedArray<Byte, 1'024>;
  }
}
""",
    )
    with pytest.raises(TypeError, match=r"Attempt to unify NumericType.INTEGER type with TypeDef\(name='Type'"):
        schema_firstpass(module_from_cst(parse_result))


def test_error_bad_field_type_expression_during_resolution(hellomsg_schema_firstpass: schema.Schema) -> None:
    schema_ir = hellomsg_schema_firstpass
    one: Final = primitive.DecimalLiteral(
        module=schema_ir.module,
        cst_node=None,
        type_info=clkbuiltins.INT16,
        value=Decimal(1),
    )
    with pytest.raises(
        TypeError,
        match=re.escape(
            f"Type inference failed: {clkbuiltins.INT16.value_key()} != {clkbuiltins.TYPE_TYPE.value_key()}"
        ),
    ):
        schema_ir.fields[1].type_info = expr.TypeExpression.make(
            expr.SimpleExpr(
                module=schema_ir.module,
                cst_node=None,
                type_info=one.type_info,
                resolved_value=one,
                value=one,
            ),
        )


def test_missing_parameters() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::dsl::tests::support::hellomsg;
        // Bad
        schema Bad
        {
            fields
            {
            // Missing parameters
            #1 bad: hellomsg::GenericMsg;
            }
        }
        """,
    )
    with pytest.raises(TypeError, match=re.escape("Generic type must be instantiated when used as a field type")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_sign_type_mismatch() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          fields
          {
            // Data
            #1 data: VarArray<Int8, max_size=-3>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Attempt to unify NumericType.SIGNED_INTEGER type with {clkbuiltins.UINT64}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_parameter_type_mismatch() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          parameters
          {
            // Not gonna work; should be UInt64 given how it's used later
            #1 bad_size: UInt32;
          }
          fields
          {
            // Data
            #2 data: VarArray<Int8, max_size=bad_size>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Type inference failed: {clkbuiltins.UINT64} != {clkbuiltins.UINT32}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_instantiation_type_mismatch() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::dsl::tests::support::hellomsg;
        // Bad
        schema Bad
        {
          parameters
          {
            // Not gonna work; should be UInt64 given how it's used later
            #1 bad_size: UInt32;
          }
          fields
          {
            // Data
            #2 data: hellomsg::GenericMsg<Int8, bad_size>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Type inference failed: {clkbuiltins.UINT64} != {clkbuiltins.UINT32}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)

    source = dedent(
        """
        use clockwork::dsl::tests::support::hellomsg;
        // Bad
        schema Bad
        {
          fields
          {
            // Data
            #1 data: hellomsg::GenericMsg<3, 2>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Attempt to unify NumericType.INTEGER type with {clkbuiltins.TYPE_TYPE}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_bad_parameter_default() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          parameters
          {
            // A parameter with bad default value
            #1 max_size: UInt64 = 3.14;
          }
          fields
          {
            // Data
            #2 data: VarArray<Int8, max_size=max_size>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Attempt to unify NumericType.FLOAT type with {clkbuiltins.UINT64}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)

    source = dedent(
        """
        // Bad
        schema Bad
        {
          parameters
          {
            // A parameter with bad default value
            #1 max_size: UInt64 = 3ms;
          }
          fields
          {
            // Data
            #2 data: VarArray<Int8, max_size=max_size>;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Type inference failed: {clkbuiltins.DURATION} != {clkbuiltins.UINT64}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_bad_field_init() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          fields
          {
            // Data
            #1 data: Int8 = 4ms;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(f"Type inference failed: {clkbuiltins.DURATION} != {clkbuiltins.INT8}"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_missing_field_type() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          fields
          {
            // Data
            #1 data;
          }
        }
        """,
    )
    with pytest.raises(TypeError, match=re.escape("Fields must have explicit types, not inferred types.")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_failed_parameter_type_inference() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Bad
        schema Bad
        {
          parameters
          {
            // This won't work because we don't use it anywhere, so there's no type information for inference.
            #1 max_size = 4;
          }
          fields
          {
            // Data
            #2 data: Int8;
          }
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape(
            "Type inference failed; not enough information to infer type",
        ),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_composition() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, fix_clockwork_path(Path("clockwork/dsl/tests/support/hellomsg.clk"))), fs_importer
    )
    schema_ir = module.inner_scope.lookup("BetterThanInheritance", recursive=False)
    assert isinstance(schema_ir, schema.Schema)
    assert list(schema_ir.fields.keys()) == [3, 4, 5, 6]
    f3 = schema_ir.fields[3]
    f4 = schema_ir.fields[4]
    f5 = schema_ir.fields[5]
    f6 = schema_ir.fields[6]
    assert f3.cur_name == "hello"
    hello_msg = module.inner_scope.lookup("HelloMsg")
    assert isinstance(hello_msg, schema.Schema)
    assert f3.type_info is hello_msg
    assert f4.cur_name == "generic"
    assert isinstance(f4.type_info, typesys.Instantiation)
    generic_msg = module.inner_scope.lookup("GenericMsg")
    assert isinstance(generic_msg, schema.Schema)
    assert f4.type_info.instantiates is generic_msg
    assert f4.type_info.arguments["data_type"] is clkbuiltins.FLOAT32
    assert isinstance(f4.type_info.arguments["data_size"], primitive.DecimalLiteral)
    assert f4.type_info.arguments["data_size"].value == 32
    assert f5.cur_name == "desc"
    assert isinstance(f5.type_info, typesys.Instantiation)
    assert f5.type_info.instantiates is clkbuiltins.VAR_STRING
    assert isinstance(f5.type_info.arguments["max_size"], primitive.DecimalLiteral)
    assert f5.type_info.arguments["max_size"].value == 64
    hello_enum = module.inner_scope.lookup("HelloEnum")
    assert isinstance(hello_enum, clkenum.ClkEnum)
    hola = hello_enum.lookup("hola")
    assert isinstance(hola, clkenum.ValueRef)
    assert f6.type_info is hello_enum
    assert f6.init_value is hola


def test_field_source_ordering() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Doc
        schema OutOfOrder
        {
          fields
          {
            // Doc
            #3 three: Int8;
            // Doc
            #0 zero: Int8;
            // Doc
            #1 one: Int8;
          }
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "out_of_order"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("OutOfOrder", recursive=False)
    assert isinstance(schema_ir, schema.Schema)
    assert schema_ir.field_src_order == {3: 0, 0: 1, 1: 2}


def test_empty_options() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Doc
        schema EmptyOptions
        {
          options
          {
          }
          fields
          {
            // Doc
            #1 field: Int8;
          }
        }
        """,
    )
    # Just test that an empty options compiles.
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "empty_options"), importer=fs_importer)


def test_constructor_invalid() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Doc
        schema InvalidConstructor
        {
          options
          {
            constructor: invalid;
          }
          fields
          {
            // Doc
            #1 field: Int8;
          }
        }
        """,
    )
    with pytest.raises(ValueError, match="Unsupported constructor type.  Only `source_code_order` is supported."):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "bad"), importer=fs_importer)


def test_constructor_source_code_order() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Doc
        schema SourceCodeOrderConstructor
        {
          options
          {
            constructor: source_code_order;
          }
          fields
          {
            // Doc
            #1 field: Int8;
          }
        }
        """,
    )
    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, "source_code_order_constructor"), importer=fs_importer
    )
    schema_ir = module.inner_scope.lookup("SourceCodeOrderConstructor", recursive=False)
    assert isinstance(schema_ir, schema.Schema)
    assert schema_ir.options
    assert schema_ir.options.provide_constructor


def test_param_as_field_type() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Param as field type
        schema Test
        {
            parameters
            {
              // Param
              #0 param: Type;
            }
            fields
            {
              // Parameterized field
              #1 fld: param;
            }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test<Byte>>;
          interface Tappy<Test<Byte>>;
        }
        """,
    )
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_param_as_field_type_non_type() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        // Param as field type
        schema Test
        {
            parameters
            {
              // Param
              #0 param: UInt8;
            }
            fields
            {
              // Parameterized field
              #1 fld: param;
            }
        }
      """,
    )
    with pytest.raises(TypeError, match=re.escape("Type inference failed: ")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_schema_history() -> None:
    """Test schema history parsing and resolution."""
    source = dedent(
        """
        // Test field ordering
        schema SomeSchema
        {
          fields
          {
            // Bool
            #5 boolean: Bool = true;
            // Integer
            #3 integer: Int64;
            // Floating point
            #6 floating_point: Float64;
          }
          history
          {
            versions: [2, 3, 6];
            fields
            {
              // test removal
              #2 obsolete: Int32 -> removed #3;
              #0 fp: Float32 -> became #4;
              #1 boolean: Bool -> became #5;
              #4 floating_point: Float32 -> became #6;
            }

            schema
            {
              name
              {
                #1 OldSchema;
              }
            }
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "history_test"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("SomeSchema")
    assert isinstance(schema_ir, schema.Schema)

    # Test history parsing
    assert schema_ir.history is not None
    assert schema_ir.history.versions == [2, 3, 6]
    assert len(schema_ir.history.fields) == 4
    assert schema_ir.history.old_names == {1: "OldSchema"}

    # Test historical field details
    hist_field = schema_ir.history.fields[2]
    assert hist_field.num == 2
    assert hist_field.name == "obsolete"
    assert hist_field.removed_in_version == 3

    hist_field = schema_ir.history.fields[1]
    assert hist_field.num == 1
    assert hist_field.name == "boolean"
    assert hist_field.became_field_num == 5

    # Test resolution
    resolved = schema_ir.resolve()
    assert resolved.history is not None
    assert resolved.history.versions == [2, 3, 6]

    resolved_field = resolved.history.fields[2]
    assert isinstance(resolved_field.original_type, typesys.TypeVal)
    assert resolved_field.original_type is clkbuiltins.INT32
    assert resolved_field.removed_in_version == 3

    # Test instantiation
    instantiated = schema.InstantiatedSchema.from_typespec(schema_ir)
    assert instantiated.history is not None
    assert instantiated.history.versions == [2, 3, 6]

    inst_field = instantiated.history.fields[1]
    assert isinstance(inst_field.original_type, typesys.TypeVal)
    assert inst_field.original_type is clkbuiltins.BOOL
    assert inst_field.became_field_num == 5


def test_schema_history_with_parameters() -> None:
    """Test schema history with parameterized types."""
    source = dedent(
        """
        // Test parameterized history
        schema ParameterizedHistory
        {
          parameters
          {
            // Type parameter
            #1 T: Type;
          }
          fields
          {
            // Current field
            #3 current: T;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              // Historical parameterized field
              #2 old: T -> became #3;
            }
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "param_history_test"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("ParameterizedHistory")
    assert isinstance(schema_ir, schema.Schema)

    # Test resolution
    resolved = schema_ir.resolve()
    assert resolved.history is not None
    hist_field = resolved.history.fields[2]
    assert isinstance(hist_field.original_type, schema.ParameterRef)
    assert hist_field.original_type.name == "T"

    # Test instantiation with concrete type
    instantiated = schema.InstantiatedSchema.from_typespec(
        typesys.Instantiation(
            type_info=clkbuiltins.TYPE_TYPE,
            instantiates=schema_ir,
            arguments={"T": clkbuiltins.INT64},
        )
    )
    assert instantiated.history is not None
    inst_field = instantiated.history.fields[2]
    assert isinstance(inst_field.original_type, typesys.TypeVal)
    assert inst_field.original_type is clkbuiltins.INT64


def test_schema_history_errors() -> None:
    """Test error cases in schema history."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    # Test duplicate field numbers
    source = dedent(
        """
        // Duplicate field numbers in history
        schema DuplicateHistory
        {
          fields
          {
            // field
            #1 field: Int32;
          }
          history
          {
            versions: [1];
            fields
            {
              #1 old1: Int32 -> removed #2;
              #1 old2: Int32 -> removed #2;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Duplicate field number"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "duplicate_history"), importer=fs_importer)


def test_schema_history_validation_errors() -> None:
    """Test that schema history validation catches invalid histories."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)

    # Test overlapping field numbers
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #1 field: Int32;
          }
          history
          {
            versions: [1];
            fields
            {
              #1 old: Int32 -> removed #2;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match=re.escape("Field numbers {1} are used in both current and historical fields")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test change version <= field number
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #3 field: Int32;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Int32 -> removed #2;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Historical field 2 cannot be changed in version 2"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test non-existent change version
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #2 field: Int32;
          }
          history
          {
            versions: [2];
            fields
            {
              #1 old: Int32 -> removed #3;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Historical field 1 references non-existent version 3"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test multiple fields becoming same field
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #3 field: Int32;
          }
          history
          {
            versions: [3];
            fields
            {
              #1 old1: Int32 -> became #3;
              #2 old2: Int32 -> became #3;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Historical fields 2 and 1 cannot both become field 3"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test version list contains non-existent version
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #3 field: Int32;
          }
          history
          {
            versions: [1, 2, 3];
            fields
            {
              #2 old: Int32 -> removed #3;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Version 1 is listed in version history but not defined by any field"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test current version not in version list
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // field
            #2 field: Int32;
          }
          history
          {
            versions: [1];
            fields
            {
              #1 old: Int32 -> removed #2;
            }
          }
        }
        """
    )
    with pytest.raises(ValueError, match="Historical version does not include current version 2"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_schema_history_type_changes() -> None:
    """Test type compatibility checks in schema history."""
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)

    # Test compatible integer to integer field change
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: Int64;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Int32 -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    # This should compile without error - Int32 to Int64 is allowed because they map to same Python type
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "test1"), importer=fs_importer)

    # Test incompatible integer to float field change
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: Float64;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Int32 -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    with pytest.raises(ValueError, match="Incompatible type change.*Int32.*became.*Float64"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test2"), importer=fs_importer)

    # Test container type changes (Optional -> VarArray -> FixedArray)
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: VarArray<Int32, max_size=10>;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Optional<Int32> -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    # This should compile - container types with compatible inner types are allowed
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)

    # Test incompatible container inner types
    source = dedent(
        """
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: Optional<Float32>;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Optional<Int32> -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    with pytest.raises(ValueError, match="Incompatible type change.*Optional.*Int32.*became.*Optional.*Float32"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test3"), importer=fs_importer)

    # Test integer to enum with explicit values
    source = dedent(
        """
        // Test enum with explicit values
        enum TestEnum
        {
          values
          {
            // 0
            #0 zero default { underlying_value: 0; }
            // 1
            #1 one { underlying_value: 1; }
            // 2
            #2 two { underlying_value : 2; }
          }
        }
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: TestEnum;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Int32 -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    # This should compile - integer to enum with explicit values is allowed
    compiler.compile_source_text(source, ModuleID(CLK_REPO, "test4"), importer=fs_importer)

    # Test integer to enum without explicit values
    source = dedent(
        """
        // Test enum without explicit values
        enum TestEnum
        {
          values
          {
            // 0
            #0 zero default;
            // 1
            #1 one;
            // 2
            #2 two;
          }
        }
        // TEst
        schema Test
        {
          fields
          {
            // Current
            #3 field: TestEnum;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Int32 -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    with pytest.raises(ValueError, match="Incompatible type change.*Int32.*became.*TestEnum"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test5"), importer=fs_importer)

    # Test enum to enum (always incompatible)
    source = dedent(
        """
        // Test enum to enum
        enum Enum1
        {
          values
          {
            // 0
            #0 zero default { underlying_value: 0; }
            // 1
            #1 one { underlying_value: 1; }
          }
        }
        // Test
        enum Enum2
        {
          values
          {
            // 0
            #0 zero default { underlying_value: 0; }
            // 1
            #1 one { underlying_value: 1; }
          }
        }
        // Test
        schema Test
        {
          fields
          {
            // Current
            #3 field: Enum2;
          }
          history
          {
            versions: [2, 3];
            fields
            {
              #2 old: Enum1 -> became #3;
            }
          }
        }

        cpp_target test
        {
          options { namespace test; }
          schema Test;
          representation Tachyon<Test>;
          interface Tappy<Test>;
        }
        """
    )
    with pytest.raises(ValueError, match="Incompatible type change.*Enum1.*became.*Enum2"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "test7"), importer=fs_importer)


def test_schema_pseudoversions() -> None:
    """Test pseudoversions in schema history."""
    source = dedent(
        """
        // Test pseudoversions
        schema SchemaWithPseudoversions
        {
          fields
          {
            // Bool
            #6 boolean: Bool = true;
            // Integer
            #4 integer: Int64;
            // Floating point
            #7 floating_point: Float64;
          }
          history
          {
            versions: [2, 3, 7];
            version_pseudofields: [3, 5];
            fields
            {
              // test removal
              #2 obsolete: Int32 -> removed #3;
              #1 boolean: Bool -> became #6;
            }
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "pseudover_test"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("SchemaWithPseudoversions")
    assert isinstance(schema_ir, schema.Schema)

    # Test history parsing
    assert schema_ir.history is not None
    assert schema_ir.history.versions == [2, 3, 7]
    assert schema_ir.history.pseudoversions == [3, 5]

    # Test resolution
    resolved = schema_ir.resolve()
    assert resolved.history is not None
    assert resolved.history.versions == [2, 3, 7]
    assert resolved.history.pseudoversions == [3, 5]

    # Test instantiation
    instantiated = schema.InstantiatedSchema.from_typespec(schema_ir)
    assert instantiated.history is not None
    assert instantiated.history.versions == [2, 3, 7]
    assert instantiated.history.pseudoversions == [3, 5]

    # Test current version includes pseudoversions
    assert instantiated.cur_version() == 7


def test_schema_pseudoversion_conflict() -> None:
    """Test that pseudoversions can't conflict with field numbers."""
    source = dedent(
        """
        // Test pseudoversion conflicts
        schema ConflictingPseudoversions
        {
          fields
          {
            // Bool
            #5 boolean: Bool = true;
          }
          history
          {
            versions: [5];
            version_pseudofields: [5];
            fields {}
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    with pytest.raises(ValueError, match=r"Pseudoversions \{5\} conflict with field numbers"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "conflict_test"), importer=fs_importer)


def test_empty_pseudoversions() -> None:
    """Test schema with empty pseudoversions list."""
    source = dedent(
        """
        // Test empty pseudoversions
        schema EmptyPseudoversions
        {
          fields
          {
            // Bool
            #3 boolean: Bool = true;
          }
          history
          {
            versions: [3];
            version_pseudofields: [];
            fields {}
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "empty_pseudover_test"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("EmptyPseudoversions")
    assert isinstance(schema_ir, schema.Schema)
    assert schema_ir.history is not None
    assert schema_ir.history.pseudoversions == []


def test_schema_pseudoversion_in_current_version() -> None:
    """Test that pseudoversions are included in current version calculation."""
    source = dedent(
        """
        // Test pseudoversion in current version
        schema PseudoCurrentVersion
        {
          fields
          {
            // Bool
            #3 boolean: Bool = true;
          }
          history
          {
            versions: [8];
            version_pseudofields: [8];
            fields {}
          }
        }
        """
    )
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "pseudover_current_test"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("PseudoCurrentVersion")
    assert isinstance(schema_ir, schema.Schema)

    # Test current version calculation
    resolved = schema_ir.resolve()
    assert resolved is not None
    # current_version should be 8 (the pseudoversion), not 3 (the field number)
    assert schema_ir.current_version() == 8

    # Test instantiated schema
    instantiated = schema.InstantiatedSchema.from_typespec(schema_ir)
    assert instantiated.cur_version() == 8
