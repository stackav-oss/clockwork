# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for protobuf representations of IR."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.ir import compiler, proto_target, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import protobuf


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_proto_message_layout(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema TestSchema
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;
            fields
            {
                // Test stuff
                #1 seqno: Int64;

                // More of it
                #2 other_number: Int32;

                // VarArray with string
                #3 string_array: VarArray<VarString<100>,100>;
            }
        }

        proto_target foo
        {
          options
          {
            package foo;
          }
          representation Protobuf<TestSchema>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    proto_target_ir = module.inner_scope.lookup("foo", recursive=False)
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)
    message_layout = protobuf.render("", proto_target_ir.representations[0].typespec, module.context)

    assert len(message_layout.fields) == 3


def test_proto_message_has_nested_vararray(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema TestSchema2
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385e;
            fields
            {
                // Test stuff
                #1 seqno: Int64;

                // More of it
                #2 data: VarArray<VarArray<Int64,42>, 33>;
            }
        }

        proto_target foo
        {
          options
          {
            package foo;
          }
          representation Protobuf<TestSchema2>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    proto_target_ir = module.inner_scope.lookup("foo", recursive=False)
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)
    with pytest.raises(
        TypeError,
        match=re.escape(
            "Cannot resolve the contained type in a VarArray. Note that protobuf generation does not support nested Arrays. Contained type was:"
        ),
    ):
        protobuf.render("", proto_target_ir.representations[0].typespec, module.context)


def test_proto_message_layout_generic(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/protomsg.clk")), fs_importer
    )
    proto_target_ir = module.inner_scope.lookup("better_than_inheritance", recursive=False)

    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 3
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)

    found = False
    for representation in proto_target_ir.representations:
        if representation.name == "my_alias":
            found = True
            # For mypy
            assert isinstance(representation.typespec, typesys.Instantiation)
            message_layout = protobuf.render("", representation.typespec, module.context)
            assert len(message_layout.fields) == 2

    assert found


def test_proto_message_has_optional_vararray(fs_importer: FilesystemImporter) -> None:
    source = """
        // Doc
        schema TestSchema3
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385f;
            fields
            {
                // Test stuff
                #1 seqno: Int64;

                // More of it
                #2 data: Optional<VarArray<Int64,42>>;
            }
        }

        proto_target foo
        {
          options
          {
            package foo;
          }
          representation Protobuf<TestSchema3>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    proto_target_ir = module.inner_scope.lookup("foo", recursive=False)
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)
    with pytest.raises(
        TypeError,
        match=re.escape(
            "Cannot resolve the contained type in an Optional note that protobuf generation does not support optional VarArrays. Contained type was:"
        ),
    ):
        protobuf.render("", proto_target_ir.representations[0].typespec, module.context)


def test_proto_message_layout_with_soa(fs_importer: FilesystemImporter) -> None:
    """Verify SoA fields render as repeated message fields in protobuf."""
    source = """
        // Element schema
        schema PointSchema
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e53870;
            options
            {
                soa_enabled: true;
                constructor: source_code_order;
            }
            fields
            {
                // X coordinate
                #1 x: Float32;
                // Y coordinate
                #2 y: Float32;
            }
        }

        // Container schema
        schema ContainerSchema
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e53871;
            fields
            {
                // Variable-size SoA
                #1 var_points: VarSoa<PointSchema, 100>;
                // Fixed-size SoA
                #2 fixed_points: FixedSoa<PointSchema, 4>;
            }
        }

        proto_target point_proto
        {
          options
          {
            package test;
          }
          representation Protobuf<PointSchema>;
        }

        proto_target container_proto
        {
          options
          {
            package test;
          }
          representation Protobuf<ContainerSchema>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    proto_target_ir = module.inner_scope.lookup("container_proto", recursive=False)
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)
    message_layout = protobuf.render("", proto_target_ir.representations[0].typespec, module.context)
    assert len(message_layout.fields) == 2
    for field in message_layout.fields:
        rendered = field.render()
        assert rendered.startswith("repeated "), f"SoA field should be repeated: {rendered}"


def test_proto_message_has_nested_varsoa(fs_importer: FilesystemImporter) -> None:
    """Verify that VarSoa nested inside a VarArray raises TypeError."""
    source = """
        // Inner schema
        schema InnerSchema
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5386a;
            options
            {
                soa_enabled: true;
                constructor: source_code_order;
            }
            fields
            {
                // X coordinate
                #1 x: Float32;
            }
        }

        // Doc
        schema TestSchema4
        {
            uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5386b;
            fields
            {
                // Test nested SoA
                #1 data: VarArray<VarSoa<InnerSchema, 10>, 5>;
            }
        }

        proto_target inner_proto
        {
          options
          {
            package foo;
          }
          representation Protobuf<InnerSchema>;
        }

        proto_target foo
        {
          options
          {
            package foo;
          }
          representation Protobuf<TestSchema4>;
        }
        """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    proto_target_ir = module.inner_scope.lookup("foo", recursive=False)
    assert isinstance(proto_target_ir, proto_target.ProtoTarget)
    assert len(proto_target_ir.representations) == 1
    assert isinstance(proto_target_ir.representations[0].typespec, typesys.Instantiation)
    with pytest.raises(
        TypeError,
        match=re.escape("Cannot resolve the contained type in a VarArray"),
    ):
        protobuf.render("", proto_target_ir.representations[0].typespec, module.context)
