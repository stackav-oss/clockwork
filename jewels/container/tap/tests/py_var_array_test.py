# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test VarArray interface."""

import itertools
import operator
import re
from copy import copy
from functools import reduce
from typing import Final

import pytest
from jewels.container.tap import py_var_array, value_serdes

_UINT32_VAR_ARRAY_CAPACITY: Final = 3
_UINT32_VAR_ARRAY_SIZE: Final = (
    value_serdes.UInt32SerDes.size_bytes() * _UINT32_VAR_ARRAY_CAPACITY
    + py_var_array.var_array_between_padding(value_serdes.UInt32SerDes.size_bytes(), _UINT32_VAR_ARRAY_CAPACITY)
    + py_var_array._SIZE_T_SIZE_BYTES
    + py_var_array.var_array_trailing_padding(value_serdes.UInt32SerDes.alignment())
)


def make_buffer(*args: int, size: int) -> bytes:
    """Helper function to make a buffer in bytes for VarArray<UInt32, N>."""
    elems = (arg.to_bytes(4, "little") for arg in args)
    between_padding = bytes(4)
    return reduce(operator.add, itertools.chain(elems, (between_padding, size.to_bytes(8, "little"))))


def test_init() -> None:
    int_serdes = value_serdes.UInt32SerDes
    with pytest.raises(
        ValueError,
        match=re.escape(
            "For a capacity of 3 and value constraints (size: 4, alignment: 4), the expected buffer size is 24 bytes, but received 25 bytes."
        ),
    ):
        py_var_array.VarArray.from_buffer(bytearray(_UINT32_VAR_ARRAY_SIZE + 1), _UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    var_array = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)
    assert len(var_array) == 0
    assert var_array.capacity == _UINT32_VAR_ARRAY_CAPACITY
    assert var_array._serdes is int_serdes
    assert var_array._buf == bytes(_UINT32_VAR_ARRAY_SIZE)


def test_append() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    assert len(var_array) == 0
    assert not list(var_array)

    var_array.append(1)
    assert len(var_array) == 1
    assert list(var_array) == [1]

    var_array.append(2)
    assert len(var_array) == 2
    assert list(var_array) == [1, 2]

    var_array.append(3)
    assert len(var_array) == 3
    assert list(var_array) == [1, 2, 3]

    with pytest.raises(ValueError, match=re.escape("VarArray has insufficient capacity (3) to add an extra element.")):
        var_array.append(123)


def test_extend() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    assert len(var_array) == 0
    assert not list(var_array)

    var_array.extend(range(3))
    assert len(var_array) == 3
    assert list(var_array) == [0, 1, 2]

    with pytest.raises(ValueError, match=re.escape("VarArray has insufficient capacity (3) to add an extra element.")):
        var_array.extend([123])


def test_pop() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    assert var_array._buf == bytes(_UINT32_VAR_ARRAY_SIZE)

    var_array.append(1)
    var_array.append(2)
    var_array.append(3)
    assert var_array._buf == make_buffer(1, 2, 3, size=3)

    assert var_array.pop() == 3
    assert len(var_array) == 2
    assert var_array._buf == make_buffer(1, 2, 0, size=2)

    assert var_array.pop() == 2
    assert len(var_array) == 1
    assert var_array._buf == make_buffer(1, 0, 0, size=1)

    assert var_array.pop() == 1
    assert len(var_array) == 0
    assert var_array._buf == make_buffer(0, 0, 0, size=0)

    with pytest.raises(IndexError, match="Cannot pop from an empty VarArray."):
        var_array.pop()


def test_clear() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    # Clear when empty is okay.
    var_array.clear()
    assert var_array._buf == make_buffer(0, 0, 0, size=0)

    var_array.append(1)
    assert var_array._buf == make_buffer(1, 0, 0, size=1)
    var_array.clear()
    assert var_array._buf == make_buffer(0, 0, 0, size=0)

    var_array.append(1)
    var_array.append(2)
    assert var_array._buf == make_buffer(1, 2, 0, size=2)
    var_array.clear()
    assert var_array._buf == make_buffer(0, 0, 0, size=0)

    var_array.append(1)
    var_array.append(2)
    var_array.append(3)
    assert var_array._buf == make_buffer(1, 2, 3, size=3)
    var_array.clear()
    assert var_array._buf == make_buffer(0, 0, 0, size=0)


def test_get_set() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    # Test errors when empty
    with pytest.raises(IndexError, match="Tried to get element 0 of VarArray with size 0."):
        var_array[0]

    with pytest.raises(IndexError, match="Tried to set element 0 of VarArray with size 0."):
        var_array[0] = 0

    with pytest.raises(IndexError, match="Tried to get element -1 of VarArray with size 0."):
        var_array[-1]

    with pytest.raises(IndexError, match="Tried to set element -1 of VarArray with size 0."):
        var_array[-1] = 0

    var_array.append(1)
    var_array.append(2)

    # Test errors when not full
    with pytest.raises(IndexError, match="Tried to get element 2 of VarArray with size 2."):
        var_array[2]

    with pytest.raises(IndexError, match="Tried to set element 2 of VarArray with size 2."):
        var_array[2] = 0

    var_array.append(3)

    # Test errors when full
    with pytest.raises(IndexError, match="Tried to get element 3 of VarArray with size 3."):
        var_array[3]

    with pytest.raises(IndexError, match="Tried to set element 3 of VarArray with size 3."):
        var_array[3] = 0

    assert var_array[0] == 1
    assert var_array[1] == 2
    assert var_array[2] == 3

    assert var_array[-3] == 1
    assert var_array[-2] == 2
    assert var_array[-1] == 3

    var_array[1] = 7

    assert var_array[0] == 1
    assert var_array[1] == 7
    assert var_array[2] == 3

    assert var_array[-3] == 1
    assert var_array[-2] == 7
    assert var_array[-1] == 3


def test_iter() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    count = 0
    for value in var_array:
        assert value == var_array[count]
        count += 1
    assert count == 0

    var_array.append(1)
    count = 0
    for value in var_array:
        assert value == var_array[count]
        count += 1
    assert len(var_array) == count
    assert count == 1

    var_array.append(2)
    count = 0
    for value in var_array:
        assert value == var_array[count]
        count += 1
    assert len(var_array) == count
    assert count == 2


def test_eq() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array_a: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    var_array_b: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    # empty
    assert var_array_a == var_array_b

    # different lenths
    var_array_a.append(0)
    assert var_array_a != var_array_b

    # same length but different values
    var_array_b.append(1)
    assert var_array_a != var_array_b

    # non-empty and same data
    var_array_b[0] = 0
    assert var_array_a == var_array_b

    # some other type
    assert var_array_a != 123


def test_copy() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array_a: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    var_array_a.append(0)
    var_array_a.append(1)

    var_array_b = copy(var_array_a)
    assert var_array_a is not var_array_b
    assert var_array_a == var_array_b

    var_array_b.append(2)
    assert var_array_a != var_array_b
    # It's possible to have a bug in __copy__ that causes the compare
    # to fail, but some internal members aren't correctly copied, so
    # we validate that explicitly here.
    assert len(var_array_a) != len(var_array_b)
    assert var_array_a._buf != var_array_b._buf
    assert len(var_array_a) != len(var_array_b)
    assert var_array_a._capacity == var_array_b._capacity
    assert var_array_a._serdes is var_array_b._serdes


def nested_var_array() -> None:
    outer_cap = 3
    inner_cap = 2
    var_array = py_var_array.VarArray(outer_cap, py_var_array.VarArraySerDes(inner_cap, value_serdes.UInt32SerDes))
    for outer in range(outer_cap):
        var_array.append(py_var_array.VarArray(inner_cap, value_serdes.UInt32SerDes))
        for inner in range(inner_cap):
            var_array[outer].append(outer * inner_cap + inner)

    assert list(var_array[0]) == [0, 1]
    assert list(var_array[1]) == [2, 3]
    assert list(var_array[2]) == [4, 5]
    assert var_array._buf == make_buffer(0, 1, size=2) + make_buffer(2, 3, size=2) + make_buffer(4, 5, size=2) + (
        3
    ).to_bytes(8, "little")

    var_array[1].clear()
    assert var_array._buf == make_buffer(0, 1, size=2) + make_buffer(0, 0, size=0) + make_buffer(4, 5, size=2) + (
        3
    ).to_bytes(8, "little")
    var_array.clear()
    assert var_array._buf == 3 * make_buffer(0, 0, size=0) + (3).to_bytes(8, "little")


def test_mutability_semantics() -> None:
    int_serdes = value_serdes.UInt32SerDes
    var_array: py_var_array.VarArray[int] = py_var_array.VarArray(_UINT32_VAR_ARRAY_CAPACITY, int_serdes)

    var_array.append(0)
    assert var_array[0] == 0
    var_array[0] += 22
    assert var_array[0] == 22
    elem = var_array[0]
    elem += 1
    assert var_array[0] == 22  # unaffected

    # TODO(OI-3450): Add tests here for schema value types.
    # TODO(OI-3450): Bring tests here to parity with nanobinding_var_array_test.py.
