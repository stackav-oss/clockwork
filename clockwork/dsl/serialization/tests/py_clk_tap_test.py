# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pod module."""

import struct
import uuid
from pathlib import Path
from typing import cast

from clockwork.dsl.tests.support import clk_tapmsg_clk_py
from clockwork.tests.support.py_test_utils import fix_clockwork_path


def to_single_precision(value: float) -> float:
    """Convert from double to single precision."""
    return cast("float", struct.unpack("f", struct.pack("f", value))[0])


def test_clk_tapmsg_clk_py_default() -> None:
    msg_from_py = clk_tapmsg_clk_py.TapMsg(
        integer=0,
        floating_point=to_single_precision(1.234),
        boolean=False,
        array_of_primitives=[],
        array_of_array=[],
        uuid=uuid.UUID("00000000-0000-0000-0000-000000000000"),
        uuid_different_namespace=uuid.UUID("00000000-0000-0000-0000-000000000000"),
        default_enum=clk_tapmsg_clk_py.SomeEnum.first_value,
        enum_with_init=clk_tapmsg_clk_py.SomeEnum.second_value,
        default_flags=clk_tapmsg_clk_py.SomeFlags(0),
        flags_with_init=clk_tapmsg_clk_py.SomeFlags.flag2,
        nested_schema=clk_tapmsg_clk_py.SubMsg(field=0),
        array_of_schema=[],
        duration=0,
        sync_time=0,
        optional=None,
        bool_with_init=True,
        strong_type=0,
        external_strong_type=123,
        fixed_array=[0, 0],
        var_string="",
        integer_with_init=234,
    )
    bytes_from_py = bytearray(clk_tapmsg_clk_py.TapMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/tapmsg_default.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = clk_tapmsg_clk_py.TapMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_clk_tapmsg_clk_py_full() -> None:
    msg_from_py = clk_tapmsg_clk_py.TapMsg(
        integer=123,
        floating_point=to_single_precision(9.87),
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["ab", "cd"],
        uuid=uuid.UUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
        uuid_different_namespace=uuid.UUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"),
        default_enum=clk_tapmsg_clk_py.SomeEnum.second_value,
        enum_with_init=clk_tapmsg_clk_py.SomeEnum.first_value,
        default_flags=clk_tapmsg_clk_py.SomeFlags.flag1 | clk_tapmsg_clk_py.SomeFlags.flag3,
        flags_with_init=clk_tapmsg_clk_py.SomeFlags.flag1
        | clk_tapmsg_clk_py.SomeFlags.flag2
        | clk_tapmsg_clk_py.SomeFlags.flag3,
        nested_schema=clk_tapmsg_clk_py.SubMsg(field=456),
        array_of_schema=[clk_tapmsg_clk_py.SubMsg(field=1), clk_tapmsg_clk_py.SubMsg(field=2)],
        duration=1212,
        sync_time=3434,
        optional=789,
        bool_with_init=False,
        strong_type=9876543210,
        external_strong_type=10101,
        fixed_array=[1, 9],
        var_string="+",
        integer_with_init=234,
    )
    bytes_from_py = bytearray(clk_tapmsg_clk_py.TapMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/tapmsg_full.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = clk_tapmsg_clk_py.TapMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_values_only() -> None:
    msg_from_py = clk_tapmsg_clk_py.GenericValuesOnly3(
        field=[1, 2, 3],
    )
    bytes_from_py = bytearray(clk_tapmsg_clk_py.GenericValuesOnly3.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_values_only.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = clk_tapmsg_clk_py.GenericValuesOnly3.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_primitives() -> None:
    msg_from_py = clk_tapmsg_clk_py.GenericSubMsg(
        field=[True, False, True],
    )
    bytes_from_py = bytearray(clk_tapmsg_clk_py.GenericSubMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_primitives.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = clk_tapmsg_clk_py.GenericSubMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_with_schema() -> None:
    msg_from_py = clk_tapmsg_clk_py.ParamAsField(
        value=clk_tapmsg_clk_py.PaddedMsg(large=1234, small=False),
    )
    bytes_from_py = bytearray(clk_tapmsg_clk_py.ParamAsField.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_with_schema.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = clk_tapmsg_clk_py.ParamAsField.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_sub_tap_msg() -> None:
    msg_from_py = clk_tapmsg_clk_py.GenericSubTapMsg()
    msg_from_py.field.append(clk_tapmsg_clk_py.TapMsg())
    bytes_from_py = bytearray(clk_tapmsg_clk_py.GenericSubTapMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    msg_from_bytes = clk_tapmsg_clk_py.GenericSubTapMsg.deserialize_tachyon(memoryview(bytes_from_py))
    assert msg_from_py == msg_from_bytes


def test_generic_sub_tap_msg345() -> None:
    msg_from_py = clk_tapmsg_clk_py.GenericSubTapMsg345()
    msg_from_py.field.append(clk_tapmsg_clk_py.TapMsg345())
    bytes_from_py = bytearray(clk_tapmsg_clk_py.GenericSubTapMsg345.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    msg_from_bytes = clk_tapmsg_clk_py.GenericSubTapMsg345.deserialize_tachyon(memoryview(bytes_from_py))
    assert msg_from_py == msg_from_bytes


def test_generic_sub_sub_msg() -> None:
    msg_from_py = clk_tapmsg_clk_py.GenericSubSubMsg()
    sub_msg = clk_tapmsg_clk_py.GenericSubMsg()
    sub_msg.field.append(True)
    sub_msg.field.append(False)
    msg_from_py.field.append(sub_msg)
    bytes_from_py = bytearray(clk_tapmsg_clk_py.GenericSubSubMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    msg_from_bytes = clk_tapmsg_clk_py.GenericSubSubMsg.deserialize_tachyon(memoryview(bytes_from_py))
    assert msg_from_py == msg_from_bytes
