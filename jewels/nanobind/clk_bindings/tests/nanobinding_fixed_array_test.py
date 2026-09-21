# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for 'bind_fixed_array.hh'.

There is one test for each of the bound methods in bind_fixed_array.hh, in order.
Some tests must use methods from other tests (for instance append() and __init__ are used commonly),
but it's still nice to just have an explicit test for each method, so we don't accidentally miss one.
"""

import re
from typing import TypeAlias

import numpy as np
import pytest
from jewels.nanobind.clk_bindings.tests.support.bind_array_msg_clk_nb import (
    FixedArray_clockwork_jewels_nanobind_clk_bindings_tests_support_bind_array_msg_SubMessage_10,
    FixedArray_Int32_10,
    Foo,
    LongContainers,
    SubMessage,
    WithFixedBytes,
)

# the name is a mouthful right now...
FixedArray_SubMessage_10: TypeAlias = (
    FixedArray_clockwork_jewels_nanobind_clk_bindings_tests_support_bind_array_msg_SubMessage_10
)


def test_bool() -> None:
    """Test __bool__."""
    assert FixedArray_Int32_10()
    # can't have 0-length arrays, so this is never false


def test_str_and_repr() -> None:
    """Test __repr__."""
    arr = FixedArray_Int32_10(range(10))
    assert str(arr) == "[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]"
    assert repr(arr) == "[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]"

    # truncation
    long_containers = LongContainers()
    long_containers.long_fixed_array.from_iter([2] * 100)
    assert str(long_containers.long_fixed_array) == "[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, ...]"
    assert repr(long_containers.long_fixed_array) == "[" + ", ".join(["2"] * 100) + "]"


def test_iter() -> None:
    """Test __iter__."""
    arr = FixedArray_Int32_10(range(10, 20))
    for k, val in enumerate(arr):
        assert val == 10 + k
    assert list(range(10, 20)) == list(arr)


def test_from_iter() -> None:
    """Test .from_iter()."""
    arr = FixedArray_Int32_10()
    arr.from_iter(range(10, 20))
    assert list(arr) == list(range(10, 20))


def test_getitem() -> None:
    """Test __getitem__."""
    expected = [20, 30, 40, 50, 20, 10, 30, 4, 4, 5]
    arr = FixedArray_Int32_10(expected)
    for k in range(-10, 10):
        assert arr[k] == expected[k]
    with pytest.raises(IndexError):
        arr[10]
    with pytest.raises(IndexError):
        arr[-11]


def test_getitem_reference_semantics() -> None:
    """Test __getitem__ reference semantics."""
    # reference semantics double check - checked more thoroughly in nanobinding_message_test.py
    arr = FixedArray_SubMessage_10([SubMessage(k) for k in range(10)])
    elem = arr[4]
    assert elem == arr[4]
    elem.int_field = 22
    assert arr[4].int_field == 22


def test_init() -> None:
    """Test __init__."""
    expected = [20, 30, 40, 50, 20, 10, 30, 4, 4, 5]

    # test with explicit construction
    assert list(FixedArray_Int32_10(expected)) == [20, 30, 40, 50, 20, 10, 30, 4, 4, 5]

    # test with property assignment
    foo = Foo()
    foo.prim_array.from_iter(expected)
    assert foo.prim_array == expected

    foo = Foo()
    foo.msg_array.from_iter([SubMessage(k) for k in expected])
    assert list(foo.msg_array) == [SubMessage(k) for k in expected]

    # test with explicit iterator
    assert list(FixedArray_Int32_10(range(10))) == list(range(10))

    # test above capacity
    with pytest.raises(ValueError, match=re.escape("Array of length 10 set from too-large iterator.")):
        FixedArray_Int32_10([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])

    # test error message when using an iterator
    with pytest.raises(ValueError, match=re.escape("Array of length 10 set from too-large iterator.")):
        FixedArray_Int32_10(range(11))

    # test error message when init from empty
    with pytest.raises(ValueError, match=re.escape("Array of length 10 set from iterator with too few elements (0).")):
        FixedArray_Int32_10([])


def test_setitem() -> None:
    """Test __setitem__ (individual)."""
    arr = FixedArray_Int32_10(range(10))
    expected = list(range(10))

    for index in (0, -1, 1, 2, 5, 7):
        arr[index] = 100
        expected[index] = 100
        assert arr == expected


def test_getitem_slice() -> None:
    """Test __getitem__ (slice)."""
    n = len(FixedArray_Int32_10())
    span = range(-n - 2, n + 2)
    for start in [None, *span]:
        for end in [None, *span]:
            for step in [None, *span]:
                if step == 0:
                    continue
                values = [42 * k for k in range(n)]
                arr = FixedArray_Int32_10(values)
                assert arr[start:end:step] == values[start:end:step]


def test_getitem_slice_reference_semantics() -> None:
    """Test __getitem__ (slice) reference semantics."""
    arr = FixedArray_SubMessage_10([SubMessage(k) for k in range(10)])
    elem4, elem5 = arr[4:6]
    assert elem4 == arr[4]
    assert elem5 == arr[5]
    elem4.int_field = 22
    assert arr[4].int_field == 22
    elem5.int_field = 42
    assert arr[5].int_field == 42


def test_setitem_slice() -> None:
    """Test __setitem__ (slice)."""
    n = len(FixedArray_Int32_10())
    span = range(-n - 2, n + 2)
    for start in [None, *span]:
        for end in [None, *span]:
            for step in [None, *span]:
                if step == 0:
                    continue
                values = [42 * k for k in range(n)]
                arr = FixedArray_Int32_10(values)

                new_length = len(range(*slice(start, end, step).indices(len(values))))
                new_values = [100 + k for k in range(new_length)]

                arr[start:end:step] = new_values
                values[start:end:step] = new_values

    arr = FixedArray_Int32_10()
    with pytest.raises(
        TypeError,
        match=re.escape("""\
__setitem__(): incompatible function arguments. The following argument types are supported:
    1. __setitem__(self, arg0: int, arg1: int, /) -> None
    2. __setitem__(self, arg0: slice, arg1: list[int], /) -> None

Invoked with types: jewels.nanobind.clk_bindings.tests.support.bind_array_msg_clk_nb.FixedArray_Int32_10, slice, int"""),
    ):
        # this is an intentional incorrect type, so we have to type: ignore it
        arr[1::2] = 42  # pyright: ignore[reportArgumentType, reportCallIssue] # Intentionally incorrect for testing

    arr = FixedArray_Int32_10()
    with pytest.raises(
        IndexError,
        match=re.escape("The left and right hand side of the slice assignment have mismatched sizes!"),
    ):
        arr[1::2] = [9]


def test_contains() -> None:
    """Test __contains__ (same type)."""
    arr = FixedArray_Int32_10(range(10, 20))
    for k in range(-10, 10):
        assert k not in arr
    for k in range(10, 20):
        assert k in arr
    for k in range(20, 30):
        assert k not in arr


def test_contains_different_type() -> None:
    """Test __contains__ (different type)."""
    arr = FixedArray_Int32_10(range(10))
    assert "yo" not in arr
    assert Foo() not in arr
    assert Foo not in arr
    assert None not in arr


def test_count() -> None:
    """Test count."""
    values = [12, 15, 12, 15, 15, 16, 100, 200, 300, 400]
    arr = FixedArray_Int32_10(values)

    # good values
    for value in values:
        assert values.count(value) == arr.count(value)

    # bad values
    for value in [0, 99, -1, 22, 42]:
        assert arr.count(value) == 0


def test_eq() -> None:
    """Test __eq__ (individual)."""
    assert FixedArray_Int32_10() == [0] * 10
    assert not (FixedArray_Int32_10() != [0] * 10)  # noqa: SIM202 (deliberate test)

    assert FixedArray_Int32_10(range(10)) == list(range(10))
    assert not (FixedArray_Int32_10(range(10)) != list(range(10)))  # noqa: SIM202 (deliberate test)

    assert list(FixedArray_Int32_10(range(10))) == list(range(10))
    assert not (list(FixedArray_Int32_10(range(10))) != list(range(10)))  # noqa: SIM202 (deliberate test)

    assert FixedArray_Int32_10(range(10)) == FixedArray_Int32_10(range(10))
    assert not (FixedArray_Int32_10(range(10)) != FixedArray_Int32_10(range(10)))  # noqa: SIM202 (deliberate test)

    assert FixedArray_Int32_10(range(10)) != FixedArray_Int32_10()
    assert not (FixedArray_Int32_10(range(10)) == FixedArray_Int32_10())  # noqa: SIM201 (deliberate test)

    # doesn't raise an error, just is not equal
    assert FixedArray_Int32_10() != range(11)
    assert not (FixedArray_Int32_10() == range(11))  # noqa: SIM201 (deliberate test)


def test_np_array() -> None:
    """Test conversion to numpy array."""
    arr = np.array(FixedArray_Int32_10(range(10)))
    assert np.all(arr == np.array(range(10)))


def test_np_array_copy_semantics() -> None:
    """Test conversion to numpy array copy semantics."""
    arr = FixedArray_Int32_10(range(10))
    np_arr = np.array(arr)

    # mutate the np.array and check that the original FixedArray is unaffected
    np_arr[1] = 100
    assert arr[1] == 1

    # mutate the FixedArray and check that the np.array is unaffected
    arr[2] = 200
    assert np_arr[2] == 2


def test_fixed_byte_array() -> None:
    obj = WithFixedBytes()
    orig: bytes = obj.fixed_byte_array
    assert isinstance(orig, bytes)
    assert orig == b"\0\0\0"
    obj.fixed_byte_array = b"abc"
    assert obj.fixed_byte_array == b"abc"
    assert obj.fixed_byte_array != orig

    # Too small
    with pytest.raises(
        TypeError,
        match=re.escape(
            "fixed_byte_array(): incompatible function arguments. The following argument types are supported:"
        ),
    ):
        obj.fixed_byte_array = b"ab"

    # Too large
    with pytest.raises(
        TypeError,
        match=re.escape(
            "fixed_byte_array(): incompatible function arguments. The following argument types are supported:"
        ),
    ):
        obj.fixed_byte_array = b"abcd"


def test_nested_fixed_byte_array() -> None:
    obj = WithFixedBytes()
    orig = obj.nested_fixed_byte_array
    assert isinstance(orig[0], bytes)
    assert isinstance(orig[1], bytes)
    assert orig == [b"\0\0\0", b"\0\0\0"]
    obj.nested_fixed_byte_array.from_iter([b"abc", b"xyz"])
    assert list(obj.nested_fixed_byte_array) == [b"abc", b"xyz"]
    # Arrays allow modification in place so we expect the original to change.
    assert obj.nested_fixed_byte_array == orig

    # Too small
    with pytest.raises(
        RuntimeError,
        match=re.escape("std::bad_cast"),
    ):
        obj.nested_fixed_byte_array.from_iter([b"ab", b"xyz"])

    with pytest.raises(
        RuntimeError,
        match=re.escape("std::bad_cast"),
    ):
        obj.nested_fixed_byte_array.from_iter([b"abc", b"xy"])

    # Too large
    with pytest.raises(
        RuntimeError,
        match=re.escape("std::bad_cast"),
    ):
        obj.nested_fixed_byte_array.from_iter([b"abcd", b"xyz"])

    with pytest.raises(
        RuntimeError,
        match=re.escape("std::bad_cast"),
    ):
        obj.nested_fixed_byte_array.from_iter([b"abc", b"wxyz"])
