# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pod module."""

from decimal import Decimal
from pathlib import Path
from textwrap import dedent
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import typereg, types
from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    cpp_target,
    importer,
    interface,
    node,
    primitive,
    schema,
    strongtypes,
    typesys,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.representation import RepresentationReference
from clockwork.dsl.ir.statement import ImmutableBinding
from clockwork.dsl.serialization import tachyon_layout, tap


@pytest.fixture()
def fs_importer() -> importer.FilesystemImporter:
    return importer.FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def tapmsg_module(fs_importer: importer.FilesystemImporter) -> node.Module:
    tapmsg_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")),
        fs_importer,
    )
    assert tapmsg_module.cst_node is not None
    return tapmsg_module


@pytest.fixture()
def tapmsg(tapmsg_module: node.Module) -> schema.Schema:
    result = tapmsg_module.inner_scope.lookup("TapMsg")
    assert isinstance(result, schema.Schema)
    return result


@pytest.fixture()
def generictapmsg(tapmsg_module: node.Module) -> schema.Schema:
    result = tapmsg_module.inner_scope.lookup("GenericTapMsg")
    assert isinstance(result, schema.Schema)
    return result


@pytest.fixture()
def tap_msg_cpp_target(tapmsg_module: node.Module) -> cpp_target.CppTarget:
    tapmsg_cpp_target = tapmsg_module.inner_scope.lookup("tapmsg")
    assert isinstance(tapmsg_cpp_target, cpp_target.CppTarget)
    return tapmsg_cpp_target


def test_default_comparsion_operator() -> None:
    cpp_type = types.CppType([], "SomeType", "")
    op = tap.define_default_comparison_operator(cpp_type)
    cpp_mod = op.render(cpp_type, "")
    expected = (
        "    /// Equality operator.\n    [[nodiscard]] inline bool operator==(const SomeType& other) const = default;\n"
    )
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected


def test_comparsion_operator_no_fields() -> None:
    cpp_type = types.CppType([], "SomeType", "")
    op = tap.define_comparison_operator(cpp_type, [])
    cpp_mod = op.render(cpp_type, "")
    expected_header = (
        "    /// Equality operator.\n    [[nodiscard]] inline bool operator==(const SomeType& other) const;\n"
    )
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """inline auto SomeType::operator==(const SomeType& other) const -> bool
{
    return true;
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == expected_inline


def test_comparsion_operator_one_field() -> None:
    cpp_type = types.CppType([], "SomeType", "")
    op = tap.define_comparison_operator(cpp_type, ["some_field"])
    cpp_mod = op.render(cpp_type, "")
    expected_header = (
        "    /// Equality operator.\n    [[nodiscard]] inline bool operator==(const SomeType& other) const;\n"
    )
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """inline auto SomeType::operator==(const SomeType& other) const -> bool
{
    return some_field == other.some_field;
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == expected_inline


def test_comparsion_operator_multiple_fields() -> None:
    cpp_type = types.CppType([], "SomeType", "")
    op = tap.define_comparison_operator(cpp_type, ["a", "b", "c"])
    cpp_mod = op.render(cpp_type, "")
    expected_header = (
        "    /// Equality operator.\n    [[nodiscard]] inline bool operator==(const SomeType& other) const;\n"
    )
    assert cpp_mod.header_chunk.render_str(render_includes=False) == expected_header
    expected_inline = """inline auto SomeType::operator==(const SomeType& other) const -> bool
{
    return a == other.a
        && b == other.b
        && c == other.c;
}
"""
    assert cpp_mod.inline_chunk.render_str(render_includes=False) == expected_inline


def test_create_padding_array() -> None:
    assert (
        tap.create_padding_array("padding", 7).render("").render_str()
        == "/// Padding array.\n::std::array<::std::byte, 7U> padding{};\n"
    )


def test_pod(tap_msg_cpp_target: cpp_target.CppTarget) -> None:
    cpp_mod = tap_msg_cpp_target.render_cpp_entities()
    dial_cpp_mod = tap_msg_cpp_target.render_cpp_dial()
    assert dial_cpp_mod is None
    implementation_str = cpp_mod.implementation_chunk.render_str(render_includes=False)
    expected_tachyon_checks = """
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), array_of_primitives) == 0);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().array_of_primitives) == 48);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), array_of_array) == 48);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().array_of_array) == 40);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), array_of_schema) == 88);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().array_of_schema) == 24);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), uuid) == 112);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().uuid) == 16);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), uuid_different_namespace) == 128);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().uuid_different_namespace) == 16);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), var_string) == 144);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().var_string) == 16);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), integer) == 160);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().integer) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), nested_schema) == 168);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().nested_schema) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), duration) == 176);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().duration) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), sync_time) == 184);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().sync_time) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), strong_type) == 192);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().strong_type) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), optional) == 200);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().optional) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), fixed_array) == 208);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().fixed_array) == 8);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), floating_point) == 216);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().floating_point) == 4);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), external_strong_type) == 220);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().external_strong_type) == 4);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), integer_with_init) == 224);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().integer_with_init) == 4);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), boolean) == 228);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().boolean) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), default_enum) == 229);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().default_enum) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), enum_with_init) == 230);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().enum_with_init) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), bool_with_init) == 231);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().bool_with_init) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), default_flags) == 232);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().default_flags) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), flags_with_init) == 233);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().flags_with_init) == 1);
static_assert(offsetof(decltype(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>{}), padding_0_) == 234);
static_assert(sizeof(::std::declval<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>().padding_0_) == 6);
static_assert(sizeof(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>) == 240);
static_assert(alignof(::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>) == 8);
static_assert(::jewels::meta::ImplicitLifetimeType<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>);
""".strip()
    assert expected_tachyon_checks in implementation_str

    expected_tap_checks = """
static_assert(sizeof(::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>) == 240);
static_assert(alignof(::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>) == 8);
static_assert(::jewels::meta::ImplicitLifetimeType<::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>>);
static_assert(::std::is_same<::clockwork::Tappy<::clockwork::testing::TapMsg<234>>,::clockwork::Tap<::clockwork::Tachyon<::clockwork::testing::TapMsg<234>>>>::value);
    """.strip()
    assert expected_tap_checks in implementation_str


def test_to_schema_from_representation(tap_msg_cpp_target: cpp_target.CppTarget) -> None:
    expected_schema_names = {
        "NoConstructor",
        "OutOfOrderFields",
        "ConstructorContainer",
        "PaddedMsg",
        "TapMsg",
        "TapMsg2",
        "SubMsg",
        "GenericSubMsg",
        "GenericMsg",
        "ParamAsField",
        "GenericValuesOnly",
        "FromConstant",
    }
    removed = set()
    for representation in tap_msg_cpp_target.representations:
        assert isinstance(representation.typespec, typesys.Instantiation)
        schema = tap.to_schema_instantiation(representation.typespec)
        if schema.schema_name in expected_schema_names:
            expected_schema_names.remove(schema.schema_name)
            removed.add(schema.schema_name)
        else:
            assert schema.schema_name in removed
    assert not expected_schema_names


def test_to_schema_from_interface(tap_msg_cpp_target: cpp_target.CppTarget) -> None:
    expected_schema_names = {
        "NoConstructor",
        "OutOfOrderFields",
        "ConstructorContainer",
        "PaddedMsg",
        "TapMsg",
        "TapMsg2",
        "SubMsg",
        "GenericSubMsg",
        "GenericMsg",
        "ParamAsField",
        "GenericValuesOnly",
        "FromConstant",
    }
    removed = set()
    for interface_ir in tap_msg_cpp_target.interfaces:
        assert isinstance(interface_ir.typespec, typesys.Instantiation)
        schema = tap.to_schema_instantiation(interface_ir.typespec)
        if schema.schema_name in expected_schema_names:
            expected_schema_names.remove(schema.schema_name)
            removed.add(schema.schema_name)
        else:
            assert schema.schema_name in removed
    assert not expected_schema_names


def test_to_schema_instantiation_invalid() -> None:
    instantiation = typesys.Instantiation(
        instantiates=clkbuiltins.TAP,
        arguments={"representation": MagicMock()},
        type_info=MagicMock(),
    )
    with pytest.raises(
        TypeError, match=f"Expected an instantiation of Tachyon.  Received: {instantiation.arguments['representation']}"
    ):
        tap.to_schema_instantiation(instantiation)

    instantiation = typesys.Instantiation(
        instantiates=MagicMock(),
        arguments=MagicMock(),
        type_info=MagicMock(),
    )
    with pytest.raises(
        TypeError, match=f"Expected an instantiation of either Tap or Tachyon.  Received: {type(instantiation)}"
    ):
        tap.to_schema_instantiation(instantiation)


def test_to_cpp_struct_tag(tapmsg_module: node.Module) -> None:
    result = tapmsg_module.inner_scope.lookup("SampleTag")
    assert isinstance(result, strongtypes.Tag)
    struct = tap.to_cpp_struct(tapmsg_module.context, result)
    assert isinstance(struct.name, types.CppType)
    assert struct.name.type_name == "SampleTag"
    assert struct.doc == "A tag."
    assert not struct.attributes
    assert not struct.static_data_members
    assert not struct.members
    assert not struct.template_param


def test_to_cpp_struct_non_generic(tapmsg_module: node.Module) -> None:
    result = tapmsg_module.inner_scope.lookup("TapMsg")
    assert isinstance(result, schema.Schema)
    struct = tap.to_cpp_struct(tapmsg_module.context, result)
    assert isinstance(struct.name, types.CppType)
    assert struct.name.type_name == "TapMsg"
    assert struct.doc == "Message to test Tap."
    assert not struct.attributes
    assert not struct.static_data_members
    assert not struct.protected
    assert not struct.private
    assert len(struct.public) == 1
    uuid = struct.public[0]
    assert isinstance(uuid, types.CppNamedValue)
    assert isinstance(uuid.named_type.argument_type, types.CppTemplateType)
    assert uuid.named_type.argument_type.template_name == "Uuid"
    assert uuid.named_type.argument_type.arguments == [
        typereg.get_cpp_type(tapmsg_module.context, clkbuiltins.SCHEMA_TAG_TYPE)
    ]
    assert len(struct.template_param) == 1
    assert struct.template_param[0].named_type.argument_name == "signed_value"
    arg_type = struct.template_param[0].named_type.argument_type
    assert isinstance(arg_type, types.CppType)
    assert arg_type.type_name == "int32_t"
    assert isinstance(struct.template_param[0].default, types.CppValue)
    arg_value = struct.template_param[0].default.value
    assert isinstance(arg_value, str)
    assert arg_value == "234"


def test_to_cpp_fields_non_generic(fs_importer: importer.FilesystemImporter) -> None:
    source = dedent(
        """
        // TestSchema.
        schema GetFieldsNonGeneric
        {
          fields
          {
            // Some field.
            #1 integer: UInt64;
          }
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("GetFieldsNonGeneric")
    assert isinstance(schema_ir, schema.Schema)
    instantiated = schema.InstantiatedSchema.from_typespec(schema_ir)
    layout = tachyon_layout.layout_schema(module.context, instantiated)
    field_defs, field_gaps = tap.to_cpp_fields(module.context, instantiated, layout)
    assert len(field_defs) == 1
    integer_field = field_defs[0]
    assert integer_field.offset == 0
    assert integer_field.size == 8
    assert integer_field.field_num == 1
    assert integer_field.field_name == "integer"

    assert not field_gaps


def test_to_cpp_fields_non_generic_with_padding(fs_importer: importer.FilesystemImporter) -> None:
    source = dedent(
        """
        // Test schema.
        schema GetFieldsNonGenericWithPadding
        {
          fields
          {
            // Some field.
            #1 integer: UInt64;

            // Some smaller field to cause a gap.
            #2 smaller_integer: UInt8;
          }
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("GetFieldsNonGenericWithPadding")
    assert isinstance(schema_ir, schema.Schema)
    instantiated = schema.InstantiatedSchema.from_typespec(schema_ir)
    layout = tachyon_layout.layout_schema(module.context, instantiated)
    field_defs, field_gaps = tap.to_cpp_fields(module.context, instantiated, layout)

    assert len(field_defs) == 2
    integer_field = field_defs[0]
    assert integer_field.offset == 0
    assert integer_field.size == 8
    assert integer_field.field_num == 1
    assert integer_field.field_name == "integer"
    smaller_integer_field = field_defs[1]
    assert smaller_integer_field.offset == 8
    assert smaller_integer_field.size == 1
    assert smaller_integer_field.field_num == 2
    assert smaller_integer_field.field_name == "smaller_integer"

    assert len(field_gaps) == 1
    padding = field_gaps[0]
    assert padding.offset == 9
    assert padding.size == 7
    assert padding.field_name == "padding_0_"


def test_to_cpp_fields_generic(fs_importer: importer.FilesystemImporter) -> None:
    source = dedent(
        """
        // Test schema.
        schema GetFieldsGeneric
        {
          parameters
          {
            // Some type.
            #1 some_type: Type;

            // Balh
            #3 some_value: UInt64;
          }
          fields
          {
            // Some field.
            #2 integer_array: VarArray<some_type, 3>;
          }
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), importer=fs_importer)
    schema_ir = module.inner_scope.lookup("GetFieldsGeneric")
    assert isinstance(schema_ir, schema.Schema)
    assert schema_ir.generic_parameters()
    instantiation = typesys.Instantiation(
        instantiates=schema_ir,
        arguments={"some_type": clkbuiltins.UINT8},
        type_info=clkbuiltins.TYPE_TYPE,
    )
    instantiated_schema = schema.InstantiatedSchema.from_typespec(instantiation)
    layout = tachyon_layout.layout_schema(module.context, instantiated_schema)
    field_defs, field_gaps = tap.to_cpp_fields(module.context, instantiated_schema, layout)

    assert len(field_defs) == 1
    integer_field = field_defs[0]
    assert integer_field.offset == 0
    assert integer_field.size == 16
    assert integer_field.field_num == 2
    assert integer_field.field_name == "integer_array"

    assert not field_gaps


def test_to_class_local_defs_empty() -> None:
    assert not tap.to_class_local_defs(CompilerContext(), [], {})


def test_to_class_local_defs() -> None:
    params = [
        typesys.Parameter(name="some_type", type_bound=clkbuiltins.TYPE_TYPE, default=None),
        typesys.Parameter(name="some_value", type_bound=clkbuiltins.UINT64, default=None),
    ]
    args = {
        "some_type": clkbuiltins.UINT8,
        "some_value": primitive.DecimalLiteral(
            value=Decimal(123),
            type_info=clkbuiltins.INT64,
            module=MagicMock(),
            cst_node=None,
        ),
    }
    defs = tap.to_class_local_defs(CompilerContext(), params, args)
    assert len(defs) == 2
    some_type_alias = defs[0]
    assert isinstance(some_type_alias, types.CppTypeAliasDef)
    expected_some_type = "/// Parameter: some_type\nusing some_type = uint8_t;\n"
    assert some_type_alias.render(types.GLOBAL_NAMESPACE).render_str() == expected_some_type

    some_value_alias = defs[1]
    assert isinstance(some_value_alias, types.CppNamedValue)
    expected_some_value = "/// Parameter: some_value\nstatic constexpr uint64_t some_value{123};\n"
    assert some_value_alias.render(types.GLOBAL_NAMESPACE).render_str() == expected_some_value


def test_alias_syntax(fs_importer: importer.FilesystemImporter) -> None:
    source = dedent(
        """
        // Test schema.
        schema Schema1
        {
          fields
          {
            // Some field
            #1 some_field: UInt64;
          }
        }
        // Test schema.
        schema Schema2
        {
          fields
          {
            // Some field
            #1 some_field: UInt64;
          }
        }
        cpp_target cpp_target
        {
          options
          {
            namespace clockwork::testing;
          }
          representation Tachyon<Schema1>;
          representation Tachyon<Schema2>;
          interface Alias1: Tap<Tachyon<Schema1>>;
          interface Alias2 : Tap<Tachyon<Schema2>>;
        }
        """,
    )

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "AliasTestModule"), importer=fs_importer)
    interface1_ir = module.inner_scope.lookup("Alias1")
    assert isinstance(interface1_ir, interface.InterfaceAlias)
    interface2_ir = module.inner_scope.lookup("Alias2")
    assert isinstance(interface2_ir, interface.InterfaceAlias)


def test_instantiation_with_constant(fs_importer: importer.FilesystemImporter) -> None:
    source = dedent(
        """
        // A constant.
        some_constant: UInt64 = 3;

        // Test schema.
        schema SchemaWithValueParam
        {
          parameters
          {
            // Some value
            #1 some_value: UInt64;
          }
          fields
          {
            // Some field.
            #2 some_field: UInt64 = some_value;
          }
        }
        cpp_target cpp_target
        {
          options
          {
            namespace clockwork::testing;
          }
          representation Tachyon<SchemaWithValueParam<some_constant>>;
          interface SchemaFromSomeConstant: Tap<Tachyon<SchemaWithValueParam<some_constant>>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "some_mod"), importer=fs_importer)
    some_constant = module.inner_scope.lookup("some_constant")
    assert isinstance(some_constant, ImmutableBinding)
    alias_ir = module.inner_scope.lookup("SchemaFromSomeConstant")
    assert isinstance(alias_ir, interface.InterfaceAlias)
    interface_ir = alias_ir.interface
    assert isinstance(interface_ir, interface.InterfaceInstantiation)
    repr_ref_ir = interface_ir.representation
    assert isinstance(repr_ref_ir, RepresentationReference)
    schema_instantiation_ir = repr_ref_ir.typespec.arguments["schema"]
    assert isinstance(schema_instantiation_ir, typesys.Instantiation)
    instantiated_schema = schema.InstantiatedSchema.from_typespec(schema_instantiation_ir)
    arguments = instantiated_schema.arguments
    assert arguments is not None
    assert isinstance(arguments["some_value"], primitive.DecimalValue)
    assert arguments["some_value"].value == 3
