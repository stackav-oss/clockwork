# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for conversion_utils."""

from pathlib import PurePath
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.cpp.context import Header
from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    conversion_utils,
    cpp_target,
    interface,
    representation,
    schema,
    typesys,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def mock_compiler_context() -> MagicMock:
    """Fixture for a mock CompilerContext."""
    return MagicMock()


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def mock_header() -> Header:
    """Fixture for a mock Header."""
    return Header(CLK_REPO, PurePath("path/to/header.h"))


def test_conversion_params_properties(mock_compiler_context: MagicMock) -> None:
    """Test properties of ConversionParams."""
    params = conversion_utils.ConversionParams(
        compiler_context=mock_compiler_context,
        source_name="src",
        destination_name="dest",
        field_type=clkbuiltins.STRING,
        field_name="MyField",
        enclosing_namespace="test::ns",
    )
    assert params.proto_field_name == "myfield"
    assert params.proto_getter == "src.myfield()"


def test_validation_params_properties() -> None:
    """Test properties of ValidationParams."""
    params = conversion_utils.ValidationParams(
        source_name="src",
        field_type=clkbuiltins.STRING,
        field_name="AnotherField",
        enclosing_namespace="test::ns",
    )
    assert params.proto_field_name == "anotherfield"
    assert params.proto_getter == "src.anotherfield()"


def test_converter_registry(
    fs_importer: FilesystemImporter,
    mock_header: Header,
) -> None:
    source = """

        // Doc
        schema TestSchema
        {
          fields
          {
            // Some data
            #1 data: Int32;
          }
        }

        // Doc
        cpp_target test_schema_target
        {
          options
          {
            namespace stack::clockwork::foo;
          }
          schema TestSchema;
          representation tap_test_rep: Tachyon<TestSchema>;
          interface tappy_test_interface: Tappy<TestSchema>;
        }
        proto_target proto
        {
          options
          {
            package stack.clockwork.foo;
          }
          representation proto_test_rep: Protobuf<TestSchema>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    registry = conversion_utils.ConverterRegistry({})
    clk_type = module.inner_scope.lookup("TestSchema")
    assert isinstance(clk_type, schema.Schema)
    cpp_target_ir = module.inner_scope.lookup("test_schema_target")
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    proto_test_rep_ir = module.inner_scope.lookup("proto_test_rep")
    assert isinstance(proto_test_rep_ir, representation.ReprInstantiation)
    tappy_test_interface_ir = module.inner_scope.lookup("tappy_test_interface")
    assert isinstance(tappy_test_interface_ir, interface.InterfaceAlias)
    assert not registry.in_converter_registry(clk_type)

    with pytest.raises(TypeError, match="No converter type registered"):
        registry.get_conversion_registration(clk_type)

    registration_info = conversion_utils.ConversionRegistration(
        parent_target=cpp_target_ir,
        conversion_type=typesys.Instantiation(
            instantiates=clkbuiltins.PROTOBUF_TO_TAP,
            arguments={"source": proto_test_rep_ir.typespec, "destination": tappy_test_interface_ir.interface.typespec},
            type_info=clkbuiltins.TYPE_TYPE,
        ),
        include_location=mock_header,
        namespace="test::converters",
    )

    registry.register_schema_conversion(clk_type, registration_info)

    assert registry.in_converter_registry(clk_type)
    assert registry.get_conversion_registration(clk_type) is registration_info

    with pytest.raises(ValueError, match="already registered"):
        registry.register_schema_conversion(clk_type, registration_info)
