# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test serdes."""

import struct

import pytest
from jewels.container.tap import value_serdes


@pytest.mark.parametrize(
    ("width", "serdes", "min_val", "max_val"),
    [
        (1, value_serdes.UInt8SerDes, 0, 2**8 - 1),
        (2, value_serdes.UInt16SerDes, 0, 2**16 - 1),
        (4, value_serdes.UInt32SerDes, 0, 2**32 - 1),
        (8, value_serdes.UInt64SerDes, 0, 2**64 - 1),
        (1, value_serdes.Int8SerDes, -(2**7), 2**7 - 1),
        (2, value_serdes.Int16SerDes, -(2**15), 2**15 - 1),
        (4, value_serdes.Int32SerDes, -(2**31), 2**31 - 1),
        (8, value_serdes.Int64SerDes, -(2**63), 2**63 - 1),
    ],
)
def test_integral(width: int, serdes: value_serdes.PrimitiveSerDes[int], min_val: int, max_val: int) -> None:
    # For integral types, the struct module throws for both invalid
    # byte counts or out-of-bounds values.  Leveraging this to sanity
    # check that the SerDes each have the right format specifiers.
    assert width == serdes.size_bytes()
    assert width == serdes.alignment()
    buf = bytearray(width)
    serdes.serialize(min_val, buf)
    assert serdes.deserialize(buf) == min_val
    serdes.serialize(max_val, buf)
    assert serdes.deserialize(buf) == max_val

    with pytest.raises(struct.error):
        serdes.serialize(min_val - 1, buf)

    with pytest.raises(struct.error):
        serdes.serialize(max_val + 1, buf)


@pytest.mark.parametrize(
    ("width", "serdes", "value", "expected"),
    [
        (4, value_serdes.Float32SerDes, 0.01, 0.009999999776482582),
        (8, value_serdes.Float64SerDes, 0.01, 0.01),
    ],
)
def test_floating_point(width: int, serdes: value_serdes.PrimitiveSerDes[float], value: float, expected: float) -> None:
    # For numeric types, the struct module will throw for invalid byte
    # count and will round the input to the specified precision.
    # Value 0.01 is not representable in 32bit.  Using this we can
    # sanity check each serdes has the right format specifier.
    assert width == serdes.size_bytes()
    assert width == serdes.alignment()
    buf = bytearray(width)

    serdes.serialize(value, buf)
    assert serdes.deserialize(buf) == expected


def test_boolean() -> None:
    assert value_serdes.BoolSerDes.size_bytes() == 1
    buf = bytearray(1)
    value_serdes.BoolSerDes.serialize(True, buf)
    assert value_serdes.BoolSerDes.deserialize(buf)
    value_serdes.BoolSerDes.serialize(False, buf)
    assert not value_serdes.BoolSerDes.deserialize(buf)
