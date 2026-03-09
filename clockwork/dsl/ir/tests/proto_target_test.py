# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for proto_target."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.bazel.proto_targets import (
    ProtoCcLibrary,
    ProtoCompile,
    ProtoGoLibrary,
    ProtoLibrary,
    ProtoPyLibrary,
)
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir import clkbuiltins, compiler, proto_target, schema_reg, typesys
from clockwork.dsl.ir.clkenum import ClkEnum
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.representation import RepresentationReference, ReprInstantiation
from clockwork.dsl.ir.schema import InstantiatedSchema
from clockwork.dsl.proto.proto_typereg import DefinedProtobufType
from clockwork.dsl.serialization.protobuf import ProtobufField, ProtobufMsgLayout


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_proto_target(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/protomsg.clk")), fs_importer
    )
    proto_target_ir = module.inner_scope.lookup("proto_tester_onboard", recursive=False)
    assert proto_target_ir is not None
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    representation = proto_target_ir.representations[0]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("ProtoTester")
    assert isinstance(representation.typespec, typesys.Instantiation)
    assert representation.typespec.instantiates is clkbuiltins.PROTOBUF
    assert representation.typespec.arguments["schema"] is representation.get_resolved().schema_ir.schema.source
    repr_info = RepresentationReference.from_typespec(representation.typespec)
    assert isinstance(repr_info, RepresentationReference)
    repr_lookup = schema_reg.lookup_representation(module.context, repr_info)
    assert repr_lookup is not None
    assert repr_lookup.representation_ir is representation.get_resolved()
    composition_ir = module.inner_scope.lookup("better_than_inheritance", recursive=False)
    assert isinstance(composition_ir, proto_target.ProtoTarget)
    assert len(composition_ir.representations) == 3
    assert len(composition_ir.enums) == 1
    assert isinstance(composition_ir.enums[0].enum_ir, ClkEnum)
    assert len(composition_ir.enums[0].enum_ir.values) == 5


def test_clk_proto_target(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_protomsg.clk")), fs_importer
    )
    proto_target_ir = module.inner_scope.lookup("clk_protomsg_clk_proto", recursive=False)
    assert proto_target_ir is not None
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 5
    assert len(proto_target_ir.enums) == 1
    assert isinstance(proto_target_ir.enums[0].enum_ir, ClkEnum)
    assert len(proto_target_ir.enums[0].enum_ir.values) == 5
    representation = proto_target_ir.representations[0]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("GenericMsg")
    assert isinstance(representation.typespec, typesys.Instantiation)
    representation = proto_target_ir.representations[1]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("GenericMsg")
    assert isinstance(representation.typespec, typesys.Instantiation)
    representation = proto_target_ir.representations[2]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("ProtoTester")
    assert isinstance(representation.typespec, typesys.Instantiation)
    assert representation.typespec.instantiates is clkbuiltins.PROTOBUF
    assert representation.typespec.arguments["schema"] is representation.get_resolved().schema_ir.schema.source
    repr_info = RepresentationReference.from_typespec(representation.typespec)
    assert isinstance(repr_info, RepresentationReference)
    repr_lookup = schema_reg.lookup_representation(module.context, repr_info)
    assert repr_lookup is not None
    assert repr_lookup.representation_ir is representation.get_resolved()
    representation = proto_target_ir.representations[3]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("DependencyTester")
    assert isinstance(representation.typespec, typesys.Instantiation)
    representation = proto_target_ir.representations[4]
    assert not representation.is_generic
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("BetterThanInheritance")
    assert isinstance(representation.typespec, typesys.Instantiation)


def test_generic_with_no_alias(fs_importer: FilesystemImporter) -> None:
    source = """
        // Array size
        array_size: UInt64 = 10;

        // Hello templates
        schema GenericMsg
        {
            parameters
            {
                // Type of array elements
                #1 data_type: Type;

                // Number of elements
                #2 data_size = 3;
            }

            fields
            {
                // Some data
                #3 data: VarArray<data_type, max_size=data_size>;
            }
        }

        // Doc
        schema GenericUser
        {
            fields
            {
                // Hi
                #3 hello: Int64;

                // yet another field
                #4 a_field: Int64;

                // A generic
                #5 my_generic: GenericMsg<Float32, array_size>;
            }
        }
        proto_target foo
        {
          options
          {
            package foo;
          }


          representation Protobuf<GenericMsg<Float32, array_size>>;
          representation Protobuf<GenericUser>;
        }
        """
    with pytest.raises(
        ValueError, match=re.escape("Protobuf representations require an alias for instantiated generics:")
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


# test cases for validate_proto option
@pytest.mark.parametrize(
    ("validate_proto_text", "validate_proto_expected_value"),
    [
        ("validate_proto true;", True),
        ("validate_proto false;", False),
        # Default value when omitted
        ("", True),
    ],
)
# test cases for validate_proto option
@pytest.mark.parametrize(
    ("package_text", "package_expected_value"),
    [
        ("package foo;", "foo"),
        ("package foo.bar;", "foo.bar"),
        ("package foo.bar.baz;", "foo.bar.baz"),
        # Default value when omitted
        ("", f"{CLK_REPO}.package_test.package_test_target"),
    ],
)
def test_options(
    fs_importer: FilesystemImporter,
    validate_proto_text: str,
    validate_proto_expected_value: bool,
    package_text: str,
    package_expected_value: str,
) -> None:
    source = f"""

        // Doc
        schema NotGeneric
        {{
            fields
            {{
                // Hi
                #3 hello: Int64;

                // yet another field
                #4 a_field: Int64;
            }}
        }}
        proto_target package_test_target
        {{
            options
            {{
                {package_text}
                go_package foo_bar;
                {validate_proto_text}
            }}

            representation another_alias: Protobuf<NotGeneric>;
        }}
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "package_test"), fs_importer)
    package_test_ir = module.inner_scope.lookup("package_test_target", recursive=False)
    assert isinstance(package_test_ir, proto_target.ProtoTarget)
    assert package_test_ir.options.package == package_expected_value
    assert package_test_ir.options.go_package == "foo_bar"
    assert package_test_ir.options.validate_proto == validate_proto_expected_value


def test_empty_options(fs_importer: FilesystemImporter) -> None:
    source = """

        // Doc
        schema NotGeneric
        {
            fields
            {
                // Hi
                #3 hello: Int64;

                // yet another field
                #4 a_field: Int64;
            }
        }
        proto_target package_test_target
        {
            options
            {
            }

            representation another_alias: Protobuf<NotGeneric>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "package_test"), fs_importer)
    package_test_ir = module.inner_scope.lookup("package_test_target", recursive=False)
    assert isinstance(package_test_ir, proto_target.ProtoTarget)
    assert package_test_ir.options.package == "clockwork.package_test.package_test_target"
    assert package_test_ir.options.go_package is None
    assert package_test_ir.options.validate_proto is True


def test_omitted_options(fs_importer: FilesystemImporter) -> None:
    source = """

        // Doc
        schema NotGeneric
        {
            fields
            {
                // Hi
                #3 hello: Int64;

                // yet another field
                #4 a_field: Int64;
            }
        }
        proto_target package_test_target
        {
            representation another_alias: Protobuf<NotGeneric>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "package_test"), fs_importer)
    package_test_ir = module.inner_scope.lookup("package_test_target", recursive=False)
    assert isinstance(package_test_ir, proto_target.ProtoTarget)
    assert package_test_ir.options.package == "clockwork.package_test.package_test_target"
    assert package_test_ir.options.go_package is None
    assert package_test_ir.options.validate_proto is True


def test_non_generic_with_alias(fs_importer: FilesystemImporter) -> None:
    source = """

        // Doc
        schema NotGeneric
        {
            fields
            {
                // Hi
                #3 hello: Int64;

                // yet another field
                #4 a_field: Int64;
            }
        }
        proto_target foo
        {
          options
          {
            package foo;
          }


          representation another_alias: Protobuf<NotGeneric>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    representation = module.inner_scope.lookup("another_alias", recursive=False)
    assert representation is not None
    assert isinstance(representation, ReprInstantiation)
    assert isinstance(representation.schema_ir, InstantiatedSchema)
    assert len(representation.schema_ir.fields) == 2


def test_adding_messages() -> None:
    proto_module = proto_target.ProtoModule(
        package="test.package",
        go_package="go_package.com",
        prefix_enum_value_names=True,
        messages=[],
        enums=[],
        imports=set(),
        module_name="my/module",
    )

    message_one = ProtobufMsgLayout(
        fields=[
            ProtobufField(
                var_name="field1",
                type_info=DefinedProtobufType(
                    module_id=None,
                    import_location="test/test1",
                    package_name="",
                    type_name="type_one",
                    go_dep_label=None,
                    validate_fields=False,
                ),
            )
        ],
        type_name="message_one",
    )
    message_two = ProtobufMsgLayout(
        fields=[
            ProtobufField(
                var_name="field1",
                type_info=DefinedProtobufType(
                    module_id=None,
                    import_location="test/test2",
                    package_name="",
                    type_name="type_two",
                    go_dep_label=None,
                    validate_fields=False,
                ),
            ),
            ProtobufField(
                var_name="field2",
                type_info=DefinedProtobufType(
                    module_id=None,
                    import_location="test/test3",
                    package_name="",
                    type_name="type_three",
                    go_dep_label=None,
                    validate_fields=False,
                ),
            ),
        ],
        type_name="message_two",
    )

    proto_module.add_message(message_one)
    assert len(proto_module.imports) == 1
    assert len(proto_module.messages) == 1

    proto_module.add_message(message_two)
    assert len(proto_module.imports) == 3
    assert len(proto_module.messages) == 2


def test_output_targets(fs_importer: FilesystemImporter) -> None:
    source = """
        // Array size
        array_size: UInt64 = 10;

        // Doc
        schema Base
        {
            fields
            {
                // Doc
                #0 value: VarArray<Int64, max_size=array_size>;
            }
        }

        // Doc
        schema Derived
        {
            fields
            {
                // Doc
                #0 value: Base;

                // Doc
                #1 duration: Duration;
            }
        }

        proto_target base_target
        {
          options
          {
            package foo;
            go_package foo.com;
          }

          representation Protobuf<Base>;
        }

        proto_target derived_target
        {
          options
          {
            package bar;
            go_package foo_bar.com;
          }

          representation Protobuf<Derived>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "a::b::c::foo"), fs_importer)

    plugins = [
        Label(value="@build_stack_rules_proto//plugin/builtin:cpp"),
        Label(value="@build_stack_rules_proto//plugin/builtin:pyi"),
        Label(value="@build_stack_rules_proto//plugin/builtin:python"),
        Label(value="@clockwork//tools/gazelle:protoc-gen-nolint"),
        Label(value="@build_stack_rules_proto//plugin/golang/protobuf:protoc-gen-go"),
    ]

    base_target = module.inner_scope.lookup("base_target", recursive=False)
    assert isinstance(base_target, proto_target.ProtoTarget)
    base_outputs = base_target.output_targets()
    assert base_outputs == [
        ProtoLibrary(
            name="base_target",
            srcs=[Path("base_target.proto")],
            deps=[],
        ),
        ProtoCompile(
            name="base_target_compile",
            output_mappings=[Label(value="base_target.pb.go=foo.com/base_target.pb.go")],
            outputs=[
                Label(value="base_target.pb.h"),
                Label(value="base_target.pb.cc"),
                Label(value="base_target_pb2.py"),
                Label(value="base_target_pb2.pyi"),
                Label(value="base_target.pb.go"),
            ],
            plugins=plugins,
            proto=Label("base_target"),
        ),
        ProtoCcLibrary(
            name="base_target_cc_library",
            hdrs=[Label(value="base_target.pb.h")],
            srcs=[Label(value="base_target.pb.cc")],
            deps=[Label("@protobuf")],
        ),
        ProtoPyLibrary(
            name="base_target_py_library",
            srcs=[Label(value="base_target_pb2.py")],
            data=[Label(value="base_target_pb2.pyi")],
            deps=[Label("@protobuf//:protobuf_python")],
        ),
        ProtoGoLibrary(
            name="base_target_go_library",
            srcs=[Label(value="base_target.pb.go")],
            deps=[
                Label("@org_golang_google_protobuf//reflect/protoreflect"),
                Label("@org_golang_google_protobuf//runtime/protoimpl"),
            ],
            importpath=Label(value="foo.com"),
        ),
    ]

    derived_target = module.inner_scope.lookup("derived_target", recursive=False)
    assert isinstance(derived_target, proto_target.ProtoTarget)
    derived_outputs = derived_target.output_targets()
    assert derived_outputs == [
        ProtoLibrary(
            name="derived_target",
            srcs=[Path("derived_target.proto")],
            deps=[Label("//a/b/c:base_target"), Label("@protobuf//:duration_proto")],
        ),
        ProtoCompile(
            name="derived_target_compile",
            output_mappings=[Label(value="derived_target.pb.go=foo_bar.com/derived_target.pb.go")],
            outputs=[
                Label(value="derived_target.pb.h"),
                Label(value="derived_target.pb.cc"),
                Label(value="derived_target_pb2.py"),
                Label(value="derived_target_pb2.pyi"),
                Label(value="derived_target.pb.go"),
            ],
            plugins=plugins,
            proto=Label("derived_target"),
        ),
        ProtoCcLibrary(
            name="derived_target_cc_library",
            hdrs=[Label(value="derived_target.pb.h")],
            srcs=[Label(value="derived_target.pb.cc")],
            deps=[Label("//a/b/c:base_target_cc_library"), Label("@protobuf")],
        ),
        ProtoPyLibrary(
            name="derived_target_py_library",
            srcs=[Label(value="derived_target_pb2.py")],
            data=[Label(value="derived_target_pb2.pyi")],
            deps=[Label("//a/b/c:base_target_py_library"), Label("@protobuf//:protobuf_python")],
        ),
        ProtoGoLibrary(
            name="derived_target_go_library",
            srcs=[Label(value="derived_target.pb.go")],
            deps=[
                Label("//a/b/c:base_target_go_library"),
                Label("@org_golang_google_protobuf//reflect/protoreflect"),
                Label("@org_golang_google_protobuf//runtime/protoimpl"),
                Label("@org_golang_google_protobuf//types/known/durationpb"),
            ],
            importpath=Label(value="foo_bar.com"),
        ),
    ]


def test_output_targets_no_go_package(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema BaseB
        {
            fields
            {
                // Doc
                #0 value: Int64;
            }
        }

        // Doc
        schema DerivedB
        {
            fields
            {
                // Doc
                #0 value: BaseB;

                // Doc
                #1 duration: Duration;
            }
        }

        proto_target base_target
        {
          options
          {
            package foo;
          }

          representation Protobuf<BaseB>;
        }

        proto_target derived_target
        {
          options
          {
            package bar;
          }

          representation Protobuf<DerivedB>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "a::b::c::foo"), fs_importer)

    plugins = [
        Label(value="@build_stack_rules_proto//plugin/builtin:cpp"),
        Label(value="@build_stack_rules_proto//plugin/builtin:pyi"),
        Label(value="@build_stack_rules_proto//plugin/builtin:python"),
        Label(value="@clockwork//tools/gazelle:protoc-gen-nolint"),
    ]

    base_target = module.inner_scope.lookup("base_target", recursive=False)
    assert isinstance(base_target, proto_target.ProtoTarget)
    base_outputs = base_target.output_targets()
    assert base_outputs == [
        ProtoLibrary(
            name="base_target",
            srcs=[Path("base_target.proto")],
            deps=[],
        ),
        ProtoCompile(
            name="base_target_compile",
            output_mappings=[],
            outputs=[
                Label(value="base_target.pb.h"),
                Label(value="base_target.pb.cc"),
                Label(value="base_target_pb2.py"),
                Label(value="base_target_pb2.pyi"),
            ],
            plugins=plugins,
            proto=Label("base_target"),
        ),
        ProtoCcLibrary(
            name="base_target_cc_library",
            hdrs=[Label(value="base_target.pb.h")],
            srcs=[Label(value="base_target.pb.cc")],
            deps=[Label("@protobuf")],
        ),
        ProtoPyLibrary(
            name="base_target_py_library",
            srcs=[Label(value="base_target_pb2.py")],
            data=[Label(value="base_target_pb2.pyi")],
            deps=[Label("@protobuf//:protobuf_python")],
        ),
    ]

    derived_target = module.inner_scope.lookup("derived_target", recursive=False)
    assert isinstance(derived_target, proto_target.ProtoTarget)
    derived_outputs = derived_target.output_targets()
    assert derived_outputs == [
        ProtoLibrary(
            name="derived_target",
            srcs=[Path("derived_target.proto")],
            deps=[Label("//a/b/c:base_target"), Label("@protobuf//:duration_proto")],
        ),
        ProtoCompile(
            name="derived_target_compile",
            output_mappings=[],
            outputs=[
                Label(value="derived_target.pb.h"),
                Label(value="derived_target.pb.cc"),
                Label(value="derived_target_pb2.py"),
                Label(value="derived_target_pb2.pyi"),
            ],
            plugins=plugins,
            proto=Label("derived_target"),
        ),
        ProtoCcLibrary(
            name="derived_target_cc_library",
            hdrs=[Label(value="derived_target.pb.h")],
            srcs=[Label(value="derived_target.pb.cc")],
            deps=[Label("//a/b/c:base_target_cc_library"), Label("@protobuf")],
        ),
        ProtoPyLibrary(
            name="derived_target_py_library",
            srcs=[Label(value="derived_target_pb2.py")],
            data=[Label(value="derived_target_pb2.pyi")],
            deps=[Label("//a/b/c:base_target_py_library"), Label("@protobuf//:protobuf_python")],
        ),
    ]


def test_output_targets_cross_repo(fs_importer: FilesystemImporter) -> None:
    source = """
        use @clockwork::clockwork::dsl::tests::support::hellomsg::{HelloMsg};

        // Doc
        schema Base
        {
            fields
            {
                // Doc
                #0 value: HelloMsg;
            }
        }

        proto_target base_target
        {
          options
          {
            package foo;
            go_package foo.com;
          }

          representation Protobuf<Base>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID("other", "a::b::c::foo"), fs_importer)

    plugins = [
        Label(value="@build_stack_rules_proto//plugin/builtin:cpp"),
        Label(value="@build_stack_rules_proto//plugin/builtin:pyi"),
        Label(value="@build_stack_rules_proto//plugin/builtin:python"),
        Label(value="@clockwork//tools/gazelle:protoc-gen-nolint"),
        Label(value="@build_stack_rules_proto//plugin/golang/protobuf:protoc-gen-go"),
    ]

    base_target = module.inner_scope.lookup("base_target", recursive=False)
    assert isinstance(base_target, proto_target.ProtoTarget)
    base_outputs = base_target.output_targets()
    assert base_outputs == [
        ProtoLibrary(
            name="base_target",
            srcs=[Path("base_target.proto")],
            deps=[
                Label(
                    value="@clockwork//clockwork/dsl/tests/support:hello_msg_proto",
                )
            ],
        ),
        ProtoCompile(
            name="base_target_compile",
            output_mappings=[Label(value="base_target.pb.go=foo.com/base_target.pb.go")],
            outputs=[
                Label(value="base_target.pb.h"),
                Label(value="base_target.pb.cc"),
                Label(value="base_target_pb2.py"),
                Label(value="base_target_pb2.pyi"),
                Label(value="base_target.pb.go"),
            ],
            plugins=plugins,
            proto=Label("base_target"),
        ),
        ProtoCcLibrary(
            name="base_target_cc_library",
            hdrs=[Label(value="base_target.pb.h")],
            srcs=[Label(value="base_target.pb.cc")],
            deps=[
                Label(
                    value="@clockwork//clockwork/dsl/tests/support:hello_msg_proto_cc_library",
                ),
                Label("@protobuf"),
            ],
        ),
        ProtoPyLibrary(
            name="base_target_py_library",
            srcs=[Label(value="base_target_pb2.py")],
            data=[Label(value="base_target_pb2.pyi")],
            deps=[
                Label(
                    value="@clockwork//clockwork/dsl/tests/support:hello_msg_proto_py_library",
                ),
                Label("@protobuf//:protobuf_python"),
            ],
        ),
        ProtoGoLibrary(
            name="base_target_go_library",
            srcs=[Label(value="base_target.pb.go")],
            deps=[
                Label(
                    value="@clockwork//clockwork/dsl/tests/support:hello_msg_proto_go_library",
                ),
                Label("@org_golang_google_protobuf//reflect/protoreflect"),
                Label("@org_golang_google_protobuf//runtime/protoimpl"),
            ],
            importpath=Label(value="foo.com"),
        ),
    ]
