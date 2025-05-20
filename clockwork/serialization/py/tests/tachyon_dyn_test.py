# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
import uuid
from pathlib import Path

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, schema, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tachyon_reg
from clockwork.serialization.py import tachyon_dyn


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.mark.parametrize(
    ("clk_type", "val", "tachyon"),
    [
        (clkbuiltins.UINT8, 0x81, b"\x81"),
        (clkbuiltins.UINT16, 0x81, b"\x81\0"),
        (clkbuiltins.UINT32, 0x81, b"\x81\0\0\0"),
        (clkbuiltins.UINT64, 0x81, b"\x81\0\0\0\0\0\0\0"),
        (clkbuiltins.INT8, -127, b"\x81"),
        (clkbuiltins.INT16, -127, b"\x81\xff"),
        (clkbuiltins.INT32, -127, b"\x81\xff\xff\xff"),
        (clkbuiltins.INT64, -127, b"\x81\xff\xff\xff\xff\xff\xff\xff"),
        (clkbuiltins.FLOAT32, 1.0, b"\0\0\x80\x3f"),
        (clkbuiltins.FLOAT64, 1.0, b"\0\0\0\0\0\0\xf0\x3f"),
    ],
)
def test_primitive(clk_type: clkbuiltins.PrimitiveType, val: float, tachyon: bytes) -> None:
    context = CompilerContext()
    serdes = tachyon_dyn.serdes_for_type(context, clk_type)
    assert serdes is not None
    buffer = bytearray(clk_type.bit_width // 8)
    serdes.serializer(val, memoryview(buffer))
    assert buffer == tachyon
    deser = serdes.deserializer(memoryview(tachyon))
    assert deser == val


def test_time() -> None:
    context = CompilerContext()
    duration = tachyon_dyn.serdes_for_type(context, clkbuiltins.DURATION)
    assert duration is not None
    buffer = bytearray(8)
    py = 0x0102030405
    tachyon = b"\x05\x04\x03\x02\x01\0\0\0"
    duration.serializer(py, memoryview(buffer))
    assert buffer == tachyon
    assert duration.deserializer(memoryview(tachyon)) == py
    synctime = tachyon_dyn.serdes_for_type(context, clkbuiltins.SYNC_TIME)
    assert synctime is not None
    buffer = bytearray(8)
    synctime.serializer(py, memoryview(buffer))
    assert buffer == tachyon
    assert synctime.deserializer(memoryview(tachyon)) == py


def test_hellomsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    hello_msg_ir = module.inner_scope.lookup("HelloMsg")
    assert isinstance(hello_msg_ir, schema.Schema)
    serdes = tachyon_dyn.serdes_for_type(module.context, hello_msg_ir)
    HelloMsg = serdes.type_  # noqa: N806
    hello = HelloMsg(seqno=42, data=[x % 256 for x in range(1024)], greeting_id=uuid.uuid4(), msg_id=uuid.uuid4())
    assert HelloMsg.get_tachyon_metadata_name() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomsg::HelloMsg"
    buffer = bytearray(HelloMsg.get_tachyon_constraint().size)
    serdes.serializer(hello, memoryview(buffer))
    hello2 = serdes.deserializer(memoryview(bytes(buffer)))
    assert hello2 == hello


def test_nested_and_instantiated(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")), fs_importer
    )
    bti_ir = module.inner_scope.lookup("BetterThanInheritance")
    hellomsg_ir = module.inner_scope.lookup("HelloMsg")
    helloenum_ir = module.inner_scope.lookup("HelloEnum")
    assert isinstance(bti_ir, schema.Schema)
    assert isinstance(hellomsg_ir, schema.Schema)
    assert isinstance(helloenum_ir, clkenum.ClkEnum)
    serdes = tachyon_dyn.serdes_for_type(module.context, bti_ir)
    HelloMsg = tachyon_dyn.serdes_for_type(module.context, hellomsg_ir).type_  # noqa: N806
    HelloEnum = tachyon_dyn.serdes_for_type(module.context, helloenum_ir).type_  # noqa: N806
    BetterThanInheritance = serdes.type_  # noqa: N806
    generic_msg_ir = bti_ir.fields[4].type_info
    assert isinstance(generic_msg_ir, typesys.Instantiation)
    GenericMsg_Float32_32 = tachyon_dyn.serdes_for_type(module.context, generic_msg_ir).type_  # noqa: N806
    hello = BetterThanInheritance(
        hello=HelloMsg(seqno=42, data=[x % 256 for x in range(1024)], greeting_id=uuid.uuid4(), msg_id=uuid.uuid4()),
        generic=GenericMsg_Float32_32(data=[1.0, 3.0, 0.5]),
        desc="Description",
        greeting=HelloEnum.namaste,
    )
    assert serdes.constraint == tachyon_reg.FieldConstraint(size=1280, alignment=8)
    buffer = bytearray(serdes.constraint.size)
    serdes.serializer(hello, memoryview(buffer))
    hello2 = serdes.deserializer(memoryview(bytes(buffer)))
    assert hello2 == hello


def test_tapmsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")), fs_importer
    )
    TapMsg, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "TapMsg")  # noqa: N806
    SomeEnum, _ = tachyon_dyn.get_enum(module.context, module, "SomeEnum")  # noqa: N806
    SomeFlags, _ = tachyon_dyn.get_enum(module.context, module, "SomeFlags")  # noqa: N806
    SubMsg, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "SubMsg")  # noqa: N806
    msg = TapMsg(
        integer=1,
        floating_point=2.0,
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["one", "two"],
        uuid=uuid.uuid4(),
        uuid_different_namespace=uuid.uuid4(),
        default_enum=SomeEnum.second_value,
        enum_with_init=SomeEnum.first_value,
        default_flags=SomeFlags.flag12,
        flags_with_init=SomeFlags.flag3 | SomeFlags.flag12,
        nested_schema=SubMsg(field=42),
        array_of_schema=[SubMsg(field=11)],
        duration=1234,
        sync_time=4567,
        optional=None,
        bool_with_init=False,
        strong_type=3,
        external_strong_type=4,
        fixed_array=list(range(2)),
        var_string="a",
    )
    assert TapMsg.get_tachyon_metadata_name() == f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::TapMsg"
    buffer = bytearray(TapMsg.get_tachyon_constraint().size)
    msg.serialize_tachyon(memoryview(buffer))
    msg2 = TapMsg.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg2 == msg
    msg.optional = 13
    msg.array_of_array = []
    msg.array_of_schema.append(SubMsg(field=3))
    msg.serialize_tachyon(memoryview(buffer))
    msg2 = TapMsg.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg2 == msg

    msg.array_of_schema.append(SubMsg(field=3))
    with pytest.raises(ValueError, match=re.escape("Attempt to serialize array of length 3, max 2")):
        msg.serialize_tachyon(memoryview(buffer))
