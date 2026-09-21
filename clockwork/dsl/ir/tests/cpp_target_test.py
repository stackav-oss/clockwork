# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp_target."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.bazel.cc_targets import CcLibrary
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.cpp import typereg
from clockwork.dsl.ir import (
    box,
    clkbuiltins,
    compiler,
    cpp_executable,
    cpp_target,
    schema,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.interface import InterfaceReference
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.nanobind_type_casters import render_nanobind_casters
from clockwork.dsl.ir.representation import RepresentationReference, ReprInstantiation, ResolvedReprInstantiation


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _compile_seqno_metadata_cpp_target(
    fs_importer: FilesystemImporter, *, module_name: str, emit_sequence_numbers: bool
) -> cpp_target.CppTarget:
    emit_sequence_numbers_text = "true" if emit_sequence_numbers else "false"
    source = f"""\
#![generate(cpp, cpp_cog)]
#![cpp(namespace=clockwork::testing)]

use std::cog_metrics_policy::{{CogEventMetricsPolicy, CogTelemetryMetricsPolicy}};
use clockwork::dsl::tests::support::clk_hellomsg::{{HelloMsg}};

// Cog used to test sequence-number metadata include handling.
cog SeqNoMetadataCog
{{
    inputs
    {{
        sensor: Tappy<HelloMsg>
        {{
            max_msgs: 5;
            connect_optional: true;
        }}
    }}

    execution
    {{
        condition new_data: new_message(sensor);
        execute when: new_data;
    }}
}}

policy CogEventMetricsPolicy for SeqNoMetadataCog
{{
    enabled = true;
    emit_sequence_numbers = {emit_sequence_numbers_text};
}}

policy CogTelemetryMetricsPolicy for SeqNoMetadataCog
{{
    enabled = false;
}}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), importer=fs_importer)
    target = module.inner_scope.lookup(f"{module_name}_clk_cc", recursive=False)
    assert isinstance(target, cpp_target.CppTarget)
    return target


def test_cpp_target(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 (test code)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("hello_msg_onboard", recursive=False)
    assert cpp_target is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.representations) == 3
    assert len(cpp_target_ir.interfaces) == 3
    representation = cpp_target_ir.representations[0]
    assert isinstance(representation, ReprInstantiation)
    assert not representation.is_generic
    assert isinstance(representation, ReprInstantiation)
    assert representation.get_resolved().schema_ir.schema.source is module.inner_scope.lookup("HelloMsg")
    assert isinstance(representation.typespec, typesys.Instantiation)
    assert representation.typespec.instantiates is clkbuiltins.TACHYON
    assert representation.typespec.arguments["schema"] is representation.get_resolved().schema_ir.schema.source
    repr_info = RepresentationReference.from_typespec(representation.typespec)
    assert isinstance(repr_info, RepresentationReference)
    repr_lookup = schema_reg.lookup_representation(module.context, repr_info)
    assert repr_lookup is not None
    assert repr_lookup.representation_ir is representation.get_resolved()

    interface = cpp_target_ir.interfaces[0]
    assert not interface.is_generic
    assert interface.representation is not None
    ref_lookup = schema_reg.lookup_representation(module.context, interface.representation)
    assert ref_lookup is not None
    assert ref_lookup.representation_ir is representation.get_resolved()
    assert isinstance(interface.typespec, typesys.Instantiation)
    assert interface.typespec.instantiates is clkbuiltins.TAP
    assert interface.representation.schema_ir.schema is representation.get_resolved().schema_ir.schema
    iface_info = InterfaceReference.from_typespec(interface.typespec)
    assert isinstance(iface_info, InterfaceReference)
    iface_lookup = schema_reg.lookup_interface(module.context, iface_info)
    assert iface_lookup is not None
    assert iface_lookup.interface_ir is interface

    assert len(cpp_target_ir.schema_tags) == 3
    hello_msg_tag = cpp_target_ir.schema_tags[0]
    assert hello_msg_tag.schema_ir is module.inner_scope.lookup("HelloMsg")
    composition_tag = cpp_target_ir.schema_tags[1]
    assert composition_tag.schema_ir is module.inner_scope.lookup("BetterThanInheritance")
    generic_msg_tag = cpp_target_ir.schema_tags[2]
    assert generic_msg_tag.schema_ir is module.inner_scope.lookup("GenericMsg")

    assert len(cpp_target_ir.tags) == 1
    tag = cpp_target_ir.tags[0]
    assert tag.tag_ir is module.inner_scope.lookup("SampleTag")

    assert len(cpp_target_ir.enums) == 1
    enum = cpp_target_ir.enums[0]
    assert enum.enum_ir is module.inner_scope.lookup("HelloEnum")

    # Schema Cpp type registration
    non_generic_schema = module.inner_scope.lookup("HelloMsg")
    assert isinstance(non_generic_schema, schema.Schema)
    # Will throw if not registered.
    typereg.get_cpp_type(module.context, non_generic_schema)

    generic_schema = module.inner_scope.lookup("GenericMsg")
    assert isinstance(generic_schema, schema.Schema)
    # Will throw if not registered.
    typereg.get_cpp_template(module.context, generic_schema)


def test_clk_cpp_target(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 (test code)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_hellomsg.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("clk_hellomsg_clk_cc", recursive=False)
    assert cpp_target is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert not cpp_target_ir.representations
    assert not cpp_target_ir.interfaces
    assert len(cpp_target_ir.representations_and_interfaces) == 4
    representation, interface = cpp_target_ir.representations_and_interfaces[0]
    assert isinstance(representation, ResolvedReprInstantiation)
    assert not representation.is_generic
    assert representation.schema_ir.schema.source is module.inner_scope.lookup("HelloMsg")
    assert isinstance(representation.typespec, typesys.Instantiation)
    assert representation.typespec.instantiates is clkbuiltins.TACHYON
    assert representation.typespec.arguments["schema"] is representation.schema_ir.schema.source
    repr_info = RepresentationReference.from_typespec(representation.typespec)
    assert isinstance(repr_info, RepresentationReference)
    repr_lookup = schema_reg.lookup_representation(module.context, repr_info)
    assert repr_lookup is not None
    assert repr_lookup.representation_ir is representation

    assert not interface.is_generic
    assert interface.representation is not None
    ref_lookup = schema_reg.lookup_representation(module.context, interface.representation)
    assert ref_lookup is not None
    assert ref_lookup.representation_ir is representation
    assert isinstance(interface.typespec, typesys.Instantiation)
    assert interface.typespec.instantiates is clkbuiltins.TAP
    assert interface.representation.schema_ir.schema is representation.schema_ir.schema
    iface_info = InterfaceReference.from_typespec(interface.typespec)
    assert isinstance(iface_info, InterfaceReference)
    iface_lookup = schema_reg.lookup_interface(module.context, iface_info)
    assert iface_lookup is not None
    assert iface_lookup.interface_ir is interface

    assert len(cpp_target_ir.schema_tags) == 3
    hello_msg_tag = cpp_target_ir.schema_tags[0]
    assert hello_msg_tag.schema_ir is module.inner_scope.lookup("HelloMsg")
    composition_tag = cpp_target_ir.schema_tags[1]
    assert composition_tag.schema_ir is module.inner_scope.lookup("BetterThanInheritance")
    generic_msg_tag = cpp_target_ir.schema_tags[2]
    assert generic_msg_tag.schema_ir is module.inner_scope.lookup("GenericMsg")

    assert len(cpp_target_ir.tags) == 1
    tag = cpp_target_ir.tags[0]
    assert tag.tag_ir is module.inner_scope.lookup("SampleTag")

    assert len(cpp_target_ir.enums) == 2
    enum = cpp_target_ir.enums[0]
    assert enum.enum_ir is module.inner_scope.lookup("HelloEnum")

    # Schema Cpp type registration
    non_generic_schema = module.inner_scope.lookup("HelloMsg")
    assert isinstance(non_generic_schema, schema.Schema)
    # Will throw if not registered.
    typereg.get_cpp_type(module.context, non_generic_schema)

    generic_schema = module.inner_scope.lookup("GenericMsg")
    assert isinstance(generic_schema, schema.Schema)
    # Will throw if not registered.
    typereg.get_cpp_template(module.context, generic_schema)


def test_cpp_target_cogs(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("hellocog", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.cogs) == 6
    assert cpp_target_ir.cogs[0].cog_ir is module.inner_scope.lookup("HelloCog")
    assert cpp_target_ir.cogs[1].cog_ir is module.inner_scope.lookup("HelloCogWithMetrics")
    assert cpp_target_ir.cogs[2].cog_ir is module.inner_scope.lookup("HelloInit")
    assert cpp_target_ir.cogs[3].cog_ir is module.inner_scope.lookup("HelloInit2")
    assert cpp_target_ir.cogs[4].cog_ir is module.inner_scope.lookup("HelloCogMinMessages")
    assert cpp_target_ir.cogs[5].cog_ir is module.inner_scope.lookup("HelloCogMinNewMessages")


def test_clk_cpp_target_cogs(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_hellocog.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("clk_hellocog_clk_cc", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.cogs) == 4
    assert cpp_target_ir.cogs[0].cog_ir is module.inner_scope.lookup("HelloCog")
    assert cpp_target_ir.cogs[1].cog_ir is module.inner_scope.lookup("HelloCogWithMetrics")
    assert cpp_target_ir.cogs[2].cog_ir is module.inner_scope.lookup("HelloInit")
    assert cpp_target_ir.cogs[3].cog_ir is module.inner_scope.lookup("HelloInit2")


def test_cpp_target_dial_includes_types_when_sequence_numbers_enabled(fs_importer: FilesystemImporter) -> None:
    target = _compile_seqno_metadata_cpp_target(
        fs_importer,
        module_name="cpp_target_seqno_metadata_enabled",
        emit_sequence_numbers=True,
    )

    dial_mod = target.render_cpp_dial()
    assert dial_mod is not None
    header = dial_mod.header_chunk.render_str(render_includes=True)

    assert '#include "cpp_target_seqno_metadata_enabled_clk_cc_types.hh"' in header


def test_cpp_target_dial_omits_types_when_sequence_numbers_disabled(fs_importer: FilesystemImporter) -> None:
    target = _compile_seqno_metadata_cpp_target(
        fs_importer,
        module_name="cpp_target_seqno_metadata_disabled",
        emit_sequence_numbers=False,
    )

    dial_mod = target.render_cpp_dial()
    assert dial_mod is not None
    header = dial_mod.header_chunk.render_str(render_includes=True)

    assert '#include "cpp_target_seqno_metadata_disabled_clk_cc_types.hh"' not in header


def test_cpp_target_converters(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/protomsg.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("convert_hello_msg", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.converters) == 2
    for converter in cpp_target_ir.converters:
        assert cpp_target_ir.options is not None
        converter.render(module.context, cpp_target_ir.options.namespace)
        assert converter.source_reference
        assert converter.destination_reference
        assert isinstance(converter.typespec, typesys.Instantiation)
        if converter.typespec.instantiates is clkbuiltins.PROTOBUF_TO_TAP:
            assert converter.source_reference.typespec.instantiates is clkbuiltins.PROTOBUF
            assert converter.destination_reference.typespec.instantiates in (clkbuiltins.TAP, clkbuiltins.TAPPY)
        elif converter.typespec.instantiates is clkbuiltins.TAP_TO_PROTOBUF:
            assert converter.source_reference.typespec.instantiates in (clkbuiltins.TAP, clkbuiltins.TAPPY)
            assert converter.destination_reference.typespec.instantiates is clkbuiltins.PROTOBUF
        else:
            pytest.fail("Unexpected converter type")
        assert converter.namespace == cpp_target_ir.options.namespace


def test_clk_cpp_target_converters(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_protomsg.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("clk_protomsg_clk_proto_conv", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.converters) == 10
    for converter in cpp_target_ir.converters:
        assert cpp_target_ir.options is not None
        converter.render(module.context, cpp_target_ir.options.namespace)
        assert converter.source_reference
        assert converter.destination_reference
        assert isinstance(converter.typespec, typesys.Instantiation)
        if converter.typespec.instantiates is clkbuiltins.PROTOBUF_TO_TAP:
            assert converter.source_reference.typespec.instantiates is clkbuiltins.PROTOBUF
            assert converter.destination_reference.typespec.instantiates in (clkbuiltins.TAP, clkbuiltins.TAPPY)
        elif converter.typespec.instantiates is clkbuiltins.TAP_TO_PROTOBUF:
            assert converter.source_reference.typespec.instantiates in (clkbuiltins.TAP, clkbuiltins.TAPPY)
            assert converter.destination_reference.typespec.instantiates is clkbuiltins.PROTOBUF
        else:
            pytest.fail("Unexpected converter type")
        assert converter.namespace == cpp_target_ir.options.namespace


def test_cpp_cpp_target_generic_converter(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::protomsg;

        // Doc
        schema Holder
        {
          parameters
          {
            // Type of array elements
            #1 data_type: Type;
          }

          fields
          {
            // Some data
            #2 data: VarArray<data_type, max_size=10>;
          }
        }

        cpp_target convert_generic_schema_arg
        {
          options
          {
            namespace clockwork::foo;
          }
          schema Holder;
          representation Tachyon<Holder<protomsg::ProtoTester>>;
          interface Tappy<Holder<protomsg::ProtoTester>>;
          converter ProtobufToTap<Protobuf<Holder<protomsg::ProtoTester>>, Tappy<Holder<protomsg::ProtoTester>>>;
        }
        proto_target proto
        {
          options
          {
            package clockwork.foo;
          }
          representation holder: Protobuf<Holder<protomsg::ProtoTester>>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    cpp_target_ir = module.inner_scope.lookup("convert_generic_schema_arg", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.converters) == 1
    chunks = cpp_target_ir.render_cpp_entities()
    assert (
        chunks.header_chunk.render_str()
        == """namespace clockwork::foo
{
/// Doc
template <class data_type>
struct Holder
{
};
[[nodiscard]] ::jewels::ConversionStatusExpected protobuf_to_tap(::clockwork::Tap<::clockwork::Tachyon<Holder<::clockwork::demo::ProtoTester>>>& output, const holder& input);
[[nodiscard]] bool validate_protobuf([[maybe_unused]] const holder& input);
} // namespace clockwork::foo
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpacked-non-pod"
/// Tachyon layout for Holder.
template <>
struct __attribute__((__packed__)) alignas(8) ::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>>
{
public:
    /// Parameter: data_type
    using data_type = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>;
    /// Class type UUID.
    static constexpr ::jewels::Uuid<::clockwork::RepresentationTag> _clockwork_uuid{::std::array<uint8_t, 16U>{0xc3, 0xcf, 0x82, 0x1e, 0x2b, 0xb3, 0x5f, 0xb9, 0xac, 0x19, 0x14, 0xeb, 0xc2, 0x9d, 0xd5, 0xd2}};
    /// data: data member.
    ::jewels::tap::VarArray<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>, 10U> data{};
    /// Equality operator.
    [[nodiscard]] inline bool operator==(const ::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>>& other) const;
    /// Reverts to default constructed state, but doesn't zero unused space in VarArrays, etc.
    inline void clear();
};
#pragma clang diagnostic pop
// Tachyon logging traits for Holder without UUID.
template <>
struct ::clockwork::LoggingTraits<::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>>>
  : public ::clockwork::TachyonLoggingTraits
{
  static constexpr bool has_metadata = false;
};
/// Tap interface for the Tachyon representation of Holder.
template <>
struct ::clockwork::Tap<::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>>>
{
public:
    /// Parameter: data_type
    using data_type = ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>;
    /// Default constructor.
    Tap() = default;
    /// data: Get a const span.
    [[nodiscard]] inline ::std::span<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>> get_data() const &;
    /// data: Get a span.
    [[nodiscard]] inline ::std::span<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>> get_mutable_data() &;
    /// data: A const get method.
    [[nodiscard]] inline const ::jewels::tap::VarArray<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>, 10U>& get_underlying_data() const &;
    /// data: An explicitly mutable get method.
    [[nodiscard]] inline ::jewels::tap::VarArray<::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>, 10U>& get_underlying_data() &;
    /// data: Try set from a span.
    [[nodiscard]] inline bool try_set_data(::std::span<const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::ProtoTester>>> input_span) &;
    /// Equality operator.
    [[nodiscard]] inline bool operator==(const ::clockwork::Tap<::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>>>& other) const;
    /// Reverts to default constructed state, but doesn't zero unused space in VarArrays, etc.
    inline void clear();
private:
    /// Data member layout struct.
    ::clockwork::Tachyon<::clockwork::foo::Holder<::clockwork::demo::ProtoTester>> fields_{};
};
namespace clockwork::foo
{
// Interface and instantiation aliases
} // namespace clockwork::foo
"""
    )


def test_unsupported_converters(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::protomsg;

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          converter ProtobufToTap<Tappy<protomsg::ProtoTester>, Protobuf<protomsg::ProtoTester>>;
        }
        """
    with pytest.raises(
        TypeError,
        match=re.escape("Attempted to use a ProtobufToTap Converter with incompatible source or destination type"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    tap_to_proto_source = """
        use clockwork::dsl::tests::support::protomsg;

        cpp_target bar
        {
          options
          {
            namespace bar;
          }
          converter TapToProtobuf<Protobuf<protomsg::ProtoTester>, Tappy<protomsg::ProtoTester>>;
        }
        """
    with pytest.raises(
        TypeError,
        match=re.escape("Attempted to use a TapToProtobuf Converter with incompatible source or destination type"),
    ):
        compiler.compile_source_text(tap_to_proto_source, ModuleID(CLK_REPO, "bar"), fs_importer)


def test_invalid_converters(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::protomsg;

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          converter ProtobufToTap<protomsg::ProtoTester, Protobuf<protomsg::ProtoTester>>;
        }
        """
    with pytest.raises(
        TypeError,
        match=re.escape(
            "The source and destination parameters of Converters can only contain representations and interfaces."
        ),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_representation_for_non_generic_schema(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg;

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          representation generic Pod<hellomsg::HelloMsg>;
        }
        """
    with pytest.raises(
        ValueError,
        match=re.escape("Generic representation requested for non-generic schema"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_representation_for_instantiated_schema(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg;

        cpp_target foo
        {
            options
            {
                namespace foo;
            }
            representation generic Pod<hellomsg::GenericMsg<Byte, 2>>;
        }
        """
    with pytest.raises(ValueError, match=re.escape("Generic representation requested for instantiated schema")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_missing_parameters(fs_importer: FilesystemImporter) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg;

        cpp_target foo
        {
            options
            {
                namespace foo;
            }
            representation Pod<hellomsg::GenericMsg>;
        }
        """
    with pytest.raises(
        ValueError,
        match=re.escape("Parameter data_type not found in arguments"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_interface_for_non_generic_schema(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema NonGeneric
        {
            fields
            {
               // Doc
               #1 value: UInt64;
            }
        }

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          interface generic Tap<Tachyon<NonGeneric>>;
        }
        """
    with pytest.raises(
        ValueError,
        match=re.escape("Generic interface requested for non-generic schema"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_interface_for_instantiated_schema(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema Generic
        {
            parameters
            {
               // Doc
               #2 param: UInt64;
            }
            fields
            {
               // Doc
               #1 value: UInt64;
            }
        }

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          interface generic Tap<Tachyon<Generic<0>>>;
        }
        """
    with pytest.raises(
        ValueError,
        match=re.escape("Generic interface requested for instantiated schema"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_interface_missing_parameters(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema Generic
        {
            parameters
            {
               // Doc
               #2 param: UInt64;
            }
            fields
            {
               // Doc
               #1 value: UInt64;
            }
        }

        cpp_target foo
        {
          options
          {
            namespace foo;
          }
          interface Tap<Tachyon<Generic>>;
        }
        """
    with pytest.raises(
        ValueError,
        match=re.escape('Instantiating interface for generic schema requires parameters or "generic" keyword'),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_casing_process(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("hellomod", recursive=False)
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    exe = module.inner_scope.lookup("helloworld_exe", recursive=False)
    assert isinstance(exe, cpp_executable.CppExecutable)
    casing = exe.casing
    assert len(casing.interfaces) == 1
    iface_ir = casing.interfaces[0]
    assert isinstance(iface_ir, schema_reg.InterfaceInfo)
    assert len(casing.cogs) == 1
    cog_ir = casing.cogs[0]
    assert isinstance(cog_ir, cpp_executable.CppCog)
    box_ir = module.inner_scope.lookup("HelloBox", recursive=False)
    assert isinstance(box_ir, box.BoxTemplate)
    assert (
        casing.get_resolved().render().implementation_chunk.render_str().strip()
        == """
namespace clockwork::scaffolding
{
::std::shared_ptr<AbstractCasing> make_casing(::jewels::memory::MemoryResource memory_resource)
{
    using Casing = CasingImpl<::std::tuple<>, ::std::tuple<ProtoSchema<::hello_msg::HelloMsg, ::jewels::Uuid<::clockwork::RepresentationTag>{::std::array<uint8_t, 16U>{0xfa, 0xb4, 0x4a, 0x52, 0x49, 0xd4, 0x57, 0x67, 0x83, 0xff, 0x98, 0xce, 0x7e, 0x3b, 0x63, 0x21}}, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::io::VarPacket<4U>>>>, ::std::tuple<::clockwork::testing::IncomingUdpSocket, ::clockwork::testing::OutgoingUdpSocket>>;
    return ::jewels::memory::make_pmr_shared<Casing>(memory_resource, memory_resource);
}
} // namespace clockwork::scaffolding
""".strip()
    )


def test_clk_casing_process(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_hellomod.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("clk_hellomod_clk_cc", recursive=False)
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    exe = module.inner_scope.lookup("clk_hellomod_clk_exe", recursive=False)
    assert isinstance(exe, cpp_executable.CppExecutable)
    casing = exe.casing
    assert len(casing.boxes) == 5
    box_ir = module.inner_scope.lookup("HelloBox", recursive=False)
    assert isinstance(box_ir, box.BoxTemplate)
    assert (
        casing.get_resolved().render().implementation_chunk.render_str().strip()
        == """
namespace clockwork::scaffolding
{
::std::shared_ptr<AbstractCasing> make_casing(::jewels::memory::MemoryResource memory_resource)
{
    using Casing = CasingImpl<::std::tuple<>, ::std::tuple<ProtoSchema<::clockwork::clockwork::dsl::tests::support::clk_hellomsg_clk_proto::HelloMsg, ::jewels::Uuid<::clockwork::RepresentationTag>{::std::array<uint8_t, 16U>{0x58, 0x2f, 0xbf, 0xa8, 0x21, 0x6c, 0x54, 0xba, 0xbe, 0x96, 0x3c, 0xff, 0x13, 0x22, 0x3b, 0x18}}, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>>, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::demo::HelloMsg>>, ::clockwork::Tap<::clockwork::Tachyon<::clockwork::io::VarPacket<4U>>>>, ::std::tuple<::clockwork::testing::IncomingUdpSocket, ::clockwork::testing::OutgoingUdpSocket>>;
    return ::jewels::memory::make_pmr_shared<Casing>(memory_resource, memory_resource);
}
} // namespace clockwork::scaffolding
""".strip()
    )


def test_executable(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    cpp_exe_ir = module.inner_scope.lookup("helloworld_exe", recursive=False)
    assert isinstance(cpp_exe_ir, cpp_executable.CppExecutable)
    casing = cpp_exe_ir.casing
    assert len(casing.interfaces) == 1
    iface_ir = casing.interfaces[0]
    assert isinstance(iface_ir, schema_reg.InterfaceInfo)
    assert len(casing.cogs) == 1
    cog_ir = casing.cogs[0]
    assert isinstance(cog_ir, cpp_executable.CppCog)
    box_template = module.inner_scope.lookup("HelloProcs", recursive=False)
    assert isinstance(box_template, box.BoxTemplate)
    box_ir = box_template.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    proc1 = box_ir.attribute("hello_proc1")
    assert isinstance(proc1, box.ProcessInstance)
    assert proc1.executable is cpp_exe_ir
    proc2 = box_ir.attribute("hello_proc2")
    assert isinstance(proc2, box.ProcessInstance)
    assert proc2.executable is cpp_exe_ir


def test_clk_executable(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_hellomod.clk")), fs_importer
    )
    cpp_exe_ir = module.inner_scope.lookup("clk_hellomod_clk_exe", recursive=False)
    assert isinstance(cpp_exe_ir, cpp_executable.CppExecutable)
    casing = cpp_exe_ir.casing
    assert len(casing.interfaces) == 0
    assert len(casing.cogs) == 0
    assert len(casing.boxes) == 5
    box_template = module.inner_scope.lookup("HelloProcs", recursive=False)
    assert isinstance(box_template, box.BoxTemplate)
    box_ir = box_template.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    proc1 = box_ir.attribute("hello_proc1")
    assert isinstance(proc1, box.ProcessInstance)
    assert proc1.executable is cpp_exe_ir
    proc2 = box_ir.attribute("hello_proc2")
    assert isinstance(proc2, box.ProcessInstance)
    assert proc2.executable is cpp_exe_ir


def test_target_outputs(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema Base
        {
            fields
            {
               // Doc
               #0 value: UInt64;
            }
        }

        // Doc
        schema Derived
        {
            fields
            {
               // Doc
               #0 value: Base;
            }
        }

        cpp_target base
        {
          options
          {
            namespace base;
          }
          schema Base;
          representation Tachyon<Base>;
          interface Tappy<Base>;
        }

        cpp_target derived
        {
          options
          {
            namespace derived;
          }
          schema Derived;
          representation Tachyon<Derived>;
          interface Tappy<Derived>;
        }
        """

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "a::b::c::cc_target_test"), fs_importer)

    minimal_deps = [
        Label("//clockwork:repr_iface"),
        Label("//clockwork:tags"),
        Label("//jewels/meta:concepts"),
        Label("//jewels/uuid:uuid"),
    ]

    base_ir = module.inner_scope.lookup("base", recursive=False)
    assert isinstance(base_ir, cpp_target.CppTarget)
    base_targets = base_ir.output_targets()
    # cpp_target emits an umbrella ``:base`` plus a ``:base_types``
    # library carrying the actual schema content.
    base_umbrella = next(t for t in base_targets if t.name == "base")
    base_types_target = next(t for t in base_targets if t.name == "base_types")
    assert {t.name for t in base_targets} == {"base", "base_types"}

    assert base_types_target == CcLibrary(
        name="base_types",
        hdrs=[Path("base_types.hh")],
        srcs=[Path("base_types.inl"), Path("base_types.cc")],
        deps=minimal_deps,
        data=[Label("//a/b/c:cc_target_test_clk")],
        testonly=False,
    )

    # Umbrella re-exports ``:base_types``; no schema content of its own.
    assert base_umbrella.name == "base"
    assert base_umbrella.hdrs == [Path("base.hh")]
    assert sorted(base_umbrella.srcs) == sorted([Path("base.inl"), Path("base.cc")])
    assert Label("//a/b/c:base_types") in base_umbrella.deps

    derived_ir = module.inner_scope.lookup("derived", recursive=False)
    assert isinstance(derived_ir, cpp_target.CppTarget)

    derived_targets = derived_ir.output_targets()
    derived_types_target = next(t for t in derived_targets if t.name == "derived_types")
    assert {t.name for t in derived_targets} == {"derived", "derived_types"}
    assert derived_types_target == CcLibrary(
        name="derived_types",
        hdrs=[Path("derived_types.hh")],
        srcs=[Path("derived_types.inl"), Path("derived_types.cc")],
        deps=[Label("//a/b/c:base"), *minimal_deps],
        data=[Label("//a/b/c:cc_target_test_clk")],
        testonly=False,
    )


def test_cpp_target_nanobind_type_casters(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/nanobind_type_casters.clk")),
        fs_importer,
    )
    cpp_target_ir = module.inner_scope.lookup("foo", recursive=False)
    assert cpp_target_ir is not None
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    assert len(cpp_target_ir.nanobind_casters) == 3
    for caster in cpp_target_ir.nanobind_casters:
        assert caster.python_target
        assert caster.namespace
        assert cpp_target_ir.options is not None
        assert caster.namespace == cpp_target_ir.options.namespace

    chunks = render_nanobind_casters(cpp_target_ir.nanobind_casters, set({}))

    assert (
        chunks.header_chunk.render_str()
        == """#ifdef CLK_ENABLE_NANOBIND_TYPE_CASTER
#include <nanobind/nanobind.h>
NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)
template <> struct type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>>; // IWYU pragma: keep
template <> struct type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>>; // IWYU pragma: keep
template <> struct type_caster<::foo::FooEnum>; // IWYU pragma: keep
NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
#endif // CLK_ENABLE_NANOBIND_TYPE_CASTER
"""
    )

    assert (
        chunks.inline_chunk.render_str()
        == """#ifdef CLK_ENABLE_NANOBIND_TYPE_CASTER
#include "jewels/nanobind/nanobind_tappy_convert.hh"
#include <string>
#include <nanobind/nanobind.h>
NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)
/// nanobind caster for Tap<Tachyon<::foo::Message>>
template <>
struct type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>>
{
NB_TYPE_CASTER(::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>, ::nanobind::detail::const_name("clockwork.dsl.tests.support.foo_py.Message"))
public:
    /// Cast the python representation to the Tappy type.
    [[nodiscard]] inline bool from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/);
    /// Cast the Tappy type to the python representation.
    [[nodiscard]] static inline ::nanobind::handle from_cpp(const ::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>& message, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/);
};
inline auto type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>>::from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/) -> bool
{
    value = ::jewels::deserialize_tappy_from_py<::foo::Message>(src);
    return true;
}
inline auto type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>>::from_cpp(const ::clockwork::Tap<::clockwork::Tachyon<::foo::Message>>& message, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/) -> ::nanobind::handle
{
    const std::string class_name{"Message"};
    const std::string module_name{"clockwork.dsl.tests.support.foo_py"};
    return ::jewels::serialize_tappy_to_py<::foo::Message>(message, module_name, class_name);
}
/// nanobind caster for Tap<Tachyon<::foo::GenericMessage<float>>>
template <>
struct type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>>
{
NB_TYPE_CASTER(::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>, ::nanobind::detail::const_name("clockwork.dsl.tests.support.foo_py.GenericMessageFloat32"))
public:
    /// Cast the python representation to the Tappy type.
    [[nodiscard]] inline bool from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/);
    /// Cast the Tappy type to the python representation.
    [[nodiscard]] static inline ::nanobind::handle from_cpp(const ::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>& message, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/);
};
inline auto type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>>::from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/) -> bool
{
    value = ::jewels::deserialize_tappy_from_py<::foo::GenericMessage<float>>(src);
    return true;
}
inline auto type_caster<::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>>::from_cpp(const ::clockwork::Tap<::clockwork::Tachyon<::foo::GenericMessage<float>>>& message, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/) -> ::nanobind::handle
{
    const std::string class_name{"GenericMessageFloat32"};
    const std::string module_name{"clockwork.dsl.tests.support.foo_py"};
    return ::jewels::serialize_tappy_to_py<::foo::GenericMessage<float>>(message, module_name, class_name);
}
/// nanobind caster for ::foo::FooEnum
template <>
struct type_caster<::foo::FooEnum>
{
NB_TYPE_CASTER(::foo::FooEnum, ::nanobind::detail::const_name("clockwork.dsl.tests.support.foo_py.FooEnum"))
public:
    /// Cast the python representation to the enum type.
    [[nodiscard]] inline bool from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/);
    /// Cast the enum type to the python representation.
    [[nodiscard]] static inline ::nanobind::handle from_cpp(::foo::FooEnum cpp_value, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/);
};
inline auto type_caster<::foo::FooEnum>::from_python(::nanobind::handle src, uint8_t /*policy*/, cleanup_list* /*cleanup*/) -> bool
{
    return ::jewels::deserialize_enum_from_py<::foo::FooEnum>(src, value);
}
inline auto type_caster<::foo::FooEnum>::from_cpp(::foo::FooEnum cpp_value, ::nanobind::rv_policy /*policy*/, cleanup_list* /*cleanup*/) -> ::nanobind::handle
{
    const std::string enum_name{"FooEnum"};
    const std::string module_name{"clockwork.dsl.tests.support.foo_py"};
    return ::jewels::serialize_enum_to_py<::foo::FooEnum>(cpp_value, module_name, enum_name);
}
NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
#endif // CLK_ENABLE_NANOBIND_TYPE_CASTER
"""
    )


def test_nanobind_casters_invalid_types(fs_importer: FilesystemImporter) -> None:
    source = """
// Doc
schema Message
{
  fields
  {
    // Doc
    #0 value: UInt64;
  }
}

cpp_target foo
{
  options
  {
    namespace foo;
  }
  nanobind_type_caster for Tachyon<Message> from foo_py;
}

py_target foo_py
{
  interface Tappy<Message>;
}
"""
    with pytest.raises(TypeError, match=re.escape("Nanobind casters over can only be generated for Tap interfraces.")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc
schema Message
{
  fields
  {
    // Doc
    #0 value: UInt64;
  }
}

cpp_target foo
{
  options
  {
    namespace foo;
  }
  nanobind_type_caster for Message from foo_py;
}

py_target foo_py
{
  interface Tappy<Message>;
}
"""
    with pytest.raises(
        TypeError,
        match=re.escape(
            "For identifier Message: Expected entity of type clockwork.dsl.ir.interface.InterfaceAlias | clockwork.dsl.ir.clkenum.ClkEnum, got <class 'clockwork.dsl.ir.schema.Schema'>"
        ),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_nanobind_invalid_target(fs_importer: FilesystemImporter) -> None:
    source = """
// Doc
schema Message
{
  fields
  {
    // Doc
    #0 value: UInt64;
  }
}

cpp_target foo
{
  options
  {
    namespace foo;
  }
  nanobind_type_caster for Tachyon<Message> from foo2;
}

cpp_target foo2
{
  options
  {
    namespace foo2;
  }
  interface Tappy<Message>;
}
"""
    with pytest.raises(
        TypeError,
        match=re.escape(
            "For identifier foo2: Expected entity of type <class 'clockwork.dsl.ir.py_target.PyTarget'>, got <class 'clockwork.dsl.ir.cpp_target.CppTarget'>"
        ),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)

    source = """
// Doc
schema Message
{
  fields
  {
    // Doc
    #0 value: UInt64;
  }
}

cpp_target foo
{
  options
  {
    namespace foo;
  }
  nanobind_type_caster for Tachyon<Message> from idontexist;
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Undefined identifier idontexist"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
