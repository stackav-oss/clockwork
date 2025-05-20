# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for pod module."""

import struct
import uuid
from pathlib import Path
from typing import cast

from clockwork.dsl.tests.support import msg_with_au_py, pytapmsg
from clockwork.tests.support.py_test_utils import fix_clockwork_path


def to_single_precision(value: float) -> float:
    """Convert from double to single precision."""
    return cast("float", struct.unpack("f", struct.pack("f", value))[0])


def test_pytapmsg_default() -> None:
    msg_from_py = pytapmsg.TapMsg(
        integer=0,
        floating_point=to_single_precision(1.234),
        boolean=False,
        array_of_primitives=[],
        array_of_array=[],
        uuid=uuid.UUID("00000000-0000-0000-0000-000000000000"),
        uuid_different_namespace=uuid.UUID("00000000-0000-0000-0000-000000000000"),
        default_enum=pytapmsg.SomeEnum.first_value,
        enum_with_init=pytapmsg.SomeEnum.second_value,
        default_flags=pytapmsg.SomeFlags(0),
        flags_with_init=pytapmsg.SomeFlags.flag2,
        nested_schema=pytapmsg.SubMsg(field=0),
        array_of_schema=[],
        duration=0,
        sync_time=0,
        optional=None,
        bool_with_init=True,
        strong_type=0,
        external_strong_type=123,
        fixed_array=[0, 0],
        var_string="",
    )
    bytes_from_py = bytearray(pytapmsg.TapMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/tapmsg_default.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = pytapmsg.TapMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_pytapmsg_full() -> None:
    msg_from_py = pytapmsg.TapMsg(
        integer=123,
        floating_point=to_single_precision(9.87),
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["ab", "cd"],
        uuid=uuid.UUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
        uuid_different_namespace=uuid.UUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"),
        default_enum=pytapmsg.SomeEnum.second_value,
        enum_with_init=pytapmsg.SomeEnum.first_value,
        default_flags=pytapmsg.SomeFlags.flag1 | pytapmsg.SomeFlags.flag3,
        flags_with_init=pytapmsg.SomeFlags.flag1 | pytapmsg.SomeFlags.flag2 | pytapmsg.SomeFlags.flag3,
        nested_schema=pytapmsg.SubMsg(field=456),
        array_of_schema=[pytapmsg.SubMsg(field=1), pytapmsg.SubMsg(field=2)],
        duration=1212,
        sync_time=3434,
        optional=789,
        bool_with_init=False,
        strong_type=9876543210,
        external_strong_type=10101,
        fixed_array=[1, 9],
        var_string="+",
    )
    bytes_from_py = bytearray(pytapmsg.TapMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/tapmsg_full.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = pytapmsg.TapMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_values_only() -> None:
    msg_from_py = pytapmsg.GenericValuesOnly3(
        field=[1, 2, 3],
    )
    bytes_from_py = bytearray(pytapmsg.GenericValuesOnly3.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_values_only.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = pytapmsg.GenericValuesOnly3.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_primitives() -> None:
    msg_from_py = pytapmsg.GenericSubMsg3Bool(
        field=[True, False, True],
    )
    bytes_from_py = bytearray(pytapmsg.GenericSubMsg3Bool.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_primitives.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = pytapmsg.GenericSubMsg3Bool.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_with_au() -> None:
    msg_from_py = msg_with_au_py.GenericWithAuMetersF(
        field=to_single_precision(1.234),
    )
    bytes_from_py = bytearray(msg_with_au_py.GenericWithAuMetersF.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_with_au.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = msg_with_au_py.GenericWithAuMetersF.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file


def test_generic_with_schema() -> None:
    msg_from_py = pytapmsg.ParamAsFieldPaddedMsg(
        value=pytapmsg.PaddedMsg(large=1234, small=False),
    )
    bytes_from_py = bytearray(pytapmsg.ParamAsFieldPaddedMsg.get_tachyon_constraint().size)
    msg_from_py.serialize_tachyon(memoryview(bytes_from_py))

    path = fix_clockwork_path(Path("clockwork/dsl/serialization/tests/resources/generic_with_schema.bin"))
    with path.open("rb") as f:
        bytes_from_file = bytearray(f.read())
        msg_from_file = pytapmsg.ParamAsFieldPaddedMsg.deserialize_tachyon(memoryview(bytes_from_file))

    assert msg_from_py == msg_from_file
    assert bytes_from_py == bytes_from_file
