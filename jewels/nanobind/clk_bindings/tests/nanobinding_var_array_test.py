# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for 'bind_var_array.hh'.

There is one test for each of the bound methods in bind_var_array.hh, in order.
Some tests must use methods from other tests (for instance append() and __init__ are used commonly),
but it's still nice to just have an explicit test for each method, so we don't accidentally miss one.
"""

import re
from typing import TypeAlias

import numpy as np
import pytest
from jewels.nanobind.clk_bindings.tests.support.bind_array_msg_clk_nb import (
    Foo,
    LongContainers,
    SubMessage,
    VarArray_clockwork_jewels_nanobind_clk_bindings_tests_support_bind_array_msg_SubMessage_10,
    VarArray_Int32_10,
    WithVarBytes,
)

# the name is a mouthful right now...
VarArray_SubMessage_10: TypeAlias = (
    VarArray_clockwork_jewels_nanobind_clk_bindings_tests_support_bind_array_msg_SubMessage_10
)


def test_len() -> None:
    """Test __len__."""
    # sanity checks
    arr = VarArray_Int32_10()
    assert len(arr) == 0
    arr.append(2)
    assert len(arr) == 1
    arr.append(2)
    assert len(arr) == 2

    # exhaustive
    arr = VarArray_Int32_10()
    for k in range(arr.capacity()):
        arr.append(42 * k)
        assert len(arr) == k + 1


def test_bool() -> None:
    """Test __bool__."""
    arr = VarArray_Int32_10()
    assert not arr
    arr.append(1)
    assert arr
    arr.append(1)
    assert arr


def test_str_and_repr() -> None:
    """Test __repr__."""
    arr = VarArray_Int32_10()
    assert str(arr) == "[]"
    assert repr(arr) == "[]"
    arr.append(22)
    assert str(arr) == "[22]"
    assert repr(arr) == "[22]"
    arr.append(42)
    assert str(arr) == "[22, 42]"
    assert repr(arr) == "[22, 42]"

    # truncation
    long_containers = LongContainers()
    long_containers.long_var_array = [2] * 60
    assert str(long_containers.long_var_array) == "[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, ...]"
    assert repr(long_containers.long_var_array) == "[" + ", ".join(["2"] * 60) + "]"


def test_iter() -> None:
    """Test __iter__."""
    arr = VarArray_Int32_10()
    expected = [1, 2, 3, 4]
    arr.extend(expected)
    assert expected == list(arr)
    assert expected == list(element for element in arr)  # noqa: C400 be explicit about the iterator


def test_getitem() -> None:
    """Test __getitem__."""
    expected = [20, 30, 40, 50]
    arr = VarArray_Int32_10(expected)
    for k, expected_value in enumerate(expected):
        assert arr[k] == expected_value
    with pytest.raises(IndexError, match=""):
        arr[4]


def test_getitem_reference_semantics() -> None:
    """Test __getitem__ reference semantics."""
    arr = VarArray_SubMessage_10([SubMessage(k) for k in range(10)])
    elem = arr[4]
    assert elem == arr[4]
    elem.int_field = 22
    assert arr[4].int_field == 22


def test_clear() -> None:
    """Test clear."""
    arr = VarArray_Int32_10()

    # check that nothing weird happens when it's empty
    assert arr == []
    arr.clear()
    assert arr == []

    arr.append(200)
    assert arr == [200]
    assert len(arr) == 1
    arr.clear()
    assert len(arr) == 0
    assert arr == []


def test_init() -> None:
    """Test __init__."""
    # test with explicit construction
    assert list(VarArray_Int32_10([20, 30, 40])) == [20, 30, 40]

    # test with property assignment
    foo = Foo()
    foo.prim_var_array = [10, 11, 12]
    assert list(foo.prim_var_array) == [10, 11, 12]

    # test with explicit iterator
    assert list(VarArray_Int32_10(range(3))) == [0, 1, 2]

    # test at capacity
    arr = VarArray_Int32_10([0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
    assert len(arr) == 10

    # test above capacity
    with pytest.raises(ValueError, match=r"Insufficient capacity."):
        VarArray_Int32_10([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10])

    # test error message when using an iterator
    with pytest.raises(ValueError, match=r"Insufficient capacity."):
        VarArray_Int32_10(range(100))


def test_append() -> None:
    """Test append."""
    arr = VarArray_Int32_10()
    expected = [10, 20, 30, 40, 50]
    for k, val in enumerate(expected):
        arr.append(val)
        assert arr == expected[: k + 1]

    # test out of memory
    arr = VarArray_Int32_10(range(VarArray_Int32_10().capacity()))
    with pytest.raises(ValueError, match=r"Insufficient capacity."):
        arr.append(666)


def test_insert() -> None:
    """Test insert."""
    # at front
    arr = VarArray_Int32_10([0, 1, 2, 3, 4, 5])
    arr.insert(0, 100)
    assert arr == [100, 0, 1, 2, 3, 4, 5]

    # last possible, one before the end
    arr = VarArray_Int32_10([0, 1, 2, 3, 4, 5])
    arr.insert(5, 100)
    assert arr == [0, 1, 2, 3, 4, 100, 5]

    # in middle
    arr = VarArray_Int32_10([0, 1, 2, 3, 4, 5])
    arr.insert(3, 100)
    assert arr == [0, 1, 2, 100, 3, 4, 5]


def test_pop_prim() -> None:
    """Test pop on primitives."""
    arr = VarArray_Int32_10([100, 200, 300, 400, 500, 600])
    assert len(arr) == 6

    # pop last (default arg is -1)
    assert arr.pop() == 600
    assert len(arr) == 5
    assert arr == [100, 200, 300, 400, 500]

    # pop last (explicit -1)
    assert arr.pop(-1) == 500
    assert len(arr) == 4
    assert arr == [100, 200, 300, 400]

    # pop middle
    assert arr.pop(2) == 300
    assert len(arr) == 3
    assert arr == [100, 200, 400]

    # pop first
    assert arr.pop(0) == 100
    assert len(arr) == 2
    assert arr == [200, 400]

    # keep popping first until there none left
    assert arr.pop(0) == 200
    assert len(arr) == 1
    assert arr == [400]

    assert arr.pop(0) == 400
    assert len(arr) == 0
    assert arr == []

    with pytest.raises(IndexError, match=""):
        arr.pop(0)

    assert len(arr) == 0


def test_pop_msg() -> None:
    """Test pop on submessages."""
    arr = VarArray_SubMessage_10([SubMessage(k) for k in [100, 200, 300, 400, 500, 600]])
    assert len(arr) == 6

    # pop last (default arg is -1)
    assert arr.pop() == SubMessage(600)
    assert len(arr) == 5
    assert arr == [SubMessage(k) for k in [100, 200, 300, 400, 500]]

    # pop last (explicit -1)
    assert arr.pop(-1) == SubMessage(500)
    assert len(arr) == 4
    assert arr == [SubMessage(k) for k in [100, 200, 300, 400]]

    # pop middle
    assert arr.pop(2) == SubMessage(300)
    assert len(arr) == 3
    assert arr == [SubMessage(k) for k in [100, 200, 400]]

    # pop first
    assert arr.pop(0) == SubMessage(100)
    assert len(arr) == 2
    assert arr == [SubMessage(k) for k in [200, 400]]

    # keep popping first until there none left
    assert arr.pop(0) == SubMessage(200)
    assert len(arr) == 1
    assert arr == [SubMessage(k) for k in [400]]

    assert arr.pop(0) == SubMessage(400)
    assert len(arr) == 0
    assert arr == []

    with pytest.raises(IndexError, match=""):
        arr.pop(0)

    assert len(arr) == 0


def test_extend() -> None:
    """Test extend."""
    arr = VarArray_Int32_10([])
    arr.extend([])
    assert arr == []

    arr.extend([20])
    assert arr == [20]

    arr.extend([1, 2, 3, 4])
    assert arr == [20, 1, 2, 3, 4]

    with pytest.raises(ValueError, match="VarArray: extend: Insufficient capacity."):
        arr.extend([1, 2, 3, 4, 5, 6, 7, 8, 9])

    # didn't change contents
    assert arr == [20, 1, 2, 3, 4]

    # Extend too far (iterator version)
    with pytest.raises(ValueError, match="VarArray: extend: Insufficient capacity."):
        arr.extend(range(50))

    # Extend too far (list verision)
    with pytest.raises(ValueError, match="VarArray: extend: Insufficient capacity."):
        arr.extend(list(range(50)))


def test_setitem() -> None:
    """Test __setitem__ (individual)."""
    arr = VarArray_Int32_10([0, 1, 2, 3])
    arr[0] = 100
    assert arr == [100, 1, 2, 3]

    arr[-1] = 100
    assert arr == [100, 1, 2, 100]

    arr[1] = 100
    assert arr == [100, 100, 2, 100]

    arr[2] = 100
    assert arr == [100, 100, 100, 100]


def test_delitem() -> None:
    """Test __delitem__ (individual)."""
    arr = VarArray_Int32_10([10, 20, 30, 40])
    del arr[1]
    assert arr == [10, 30, 40]
    del arr[-1]
    assert arr == [10, 30]
    del arr[0]
    assert arr == [30]
    del arr[0]
    assert arr == []

    # index error
    with pytest.raises(IndexError, match=""):
        del arr[0]


def test_getitem_slice() -> None:
    """Test __getitem__ (slice)."""
    max_len = 5
    span = range(-max_len - 2, max_len + 2)
    for start in [None, *span]:
        for end in [None, *span]:
            for step in [None, *span]:
                if step == 0:
                    continue
                values = [42 * k for k in range(max_len)]
                arr = VarArray_Int32_10(values)
                assert arr[start:end:step] == values[start:end:step]


def test_getitem_slice_reference_semantics() -> None:
    """Test __getitem__ (slice) reference semantics."""
    arr = VarArray_SubMessage_10([SubMessage(k) for k in range(10)])
    elem4, elem5 = arr[4:6]
    assert elem4 == arr[4]
    assert elem5 == arr[5]
    elem4.int_field = 22
    assert arr[4].int_field == 22
    elem5.int_field = 42
    assert arr[5].int_field == 42


def test_setitem_slice() -> None:
    """Test __setitem__ (slice)."""
    max_len = 5
    span = range(-max_len - 2, max_len + 2)
    for start in [None, *span]:
        for end in [None, *span]:
            for step in [None, *span]:
                if step == 0:
                    continue
                values = [42 * k for k in range(max_len)]
                arr = VarArray_Int32_10(values)

                new_length = len(range(*slice(start, end, step).indices(len(values))))
                new_values = [100 + k for k in range(new_length)]

                arr[start:end:step] = new_values
                values[start:end:step] = new_values

    arr = VarArray_Int32_10()
    with pytest.raises(
        TypeError,
        match=re.escape("""\
__setitem__(): incompatible function arguments. The following argument types are supported:
    1. __setitem__(self, arg0: int, arg1: int, /) -> None
    2. __setitem__(self, arg0: slice, arg1: list[int], /) -> None

Invoked with types: jewels.nanobind.clk_bindings.tests.support.bind_array_msg_clk_nb.VarArray_Int32_10, slice, int"""),
    ):
        # this is an intentional incorrect type, so we have to type: ignore it
        arr[1::2] = 42  # type: ignore[call-overload]

    arr = VarArray_Int32_10()
    with pytest.raises(
        IndexError,
        match=re.escape("The left and right hand side of the slice assignment have mismatched sizes!"),
    ):
        arr[1::2] = [9]


def test_delitem_slice() -> None:
    """Test __delitem__ (slice).

    Just exhaustively test against a list
    """
    max_len = 5
    span = range(-max_len - 2, max_len + 2)
    for start in [None, *span]:
        for end in [None, *span]:
            for step in [None, *span]:
                if step == 0:
                    continue
                values = [42 * k for k in range(max_len)]
                arr = VarArray_Int32_10(values)
                del arr[start:end:step]
                del values[start:end:step]
                assert arr == values


def test_contains() -> None:
    """Test __contains__ (same type)."""
    arr = VarArray_Int32_10([0, 1, 2, 3])
    for k in [0, 1, 2, 3]:
        assert k in arr
    assert -1 not in arr
    assert 4 not in arr


def test_contains_different_type() -> None:
    """Test __contains__ (different type)."""
    arr = VarArray_Int32_10([0, 1, 2, 3])
    assert "yo" not in arr
    assert Foo() not in arr
    assert Foo not in arr
    assert None not in arr


def test_count() -> None:
    """Test count."""
    values = [12, 15, 12, 15, 15, 16]
    arr = VarArray_Int32_10(values)

    # good values
    for value in values:
        assert values.count(value) == arr.count(value)

    # bad values
    for value in [0, 100, -1, 22, 42]:
        assert arr.count(value) == 0


def test_remove() -> None:
    """Test remove."""
    orig_values = [12, 15, 12, 15, 15, 16]
    arr = VarArray_Int32_10(orig_values)

    # remove values not in the array
    for bad_val in [0, 100, -1, 22, 42]:
        with pytest.raises(ValueError, match=re.escape("VarArray.remove(x): x not in list.")):
            arr.remove(bad_val)
        assert arr == orig_values

    # remove actual values
    arr.remove(12)
    assert arr == [15, 12, 15, 15, 16]
    arr.remove(12)
    assert arr == [15, 15, 15, 16]
    arr.remove(15)
    assert arr == [15, 15, 16]
    arr.remove(15)
    assert arr == [15, 16]
    arr.remove(16)
    assert arr == [15]
    arr.remove(15)
    assert arr == []

    # nothing weird happens when you remove once it's already empty
    with pytest.raises(ValueError, match=re.escape("VarArray.remove(x): x not in list.")):
        arr.remove(22)
    assert arr == []


def test_capacity() -> None:
    """Test capacity."""
    assert VarArray_Int32_10().capacity() == 10


def test_eq() -> None:
    """Test __eq__ and __ne__."""
    assert VarArray_Int32_10([]) == []
    assert not (VarArray_Int32_10([]) != [])  # noqa: SIM202 (deliberate test)

    assert VarArray_Int32_10([2]) == [2]
    assert not (VarArray_Int32_10([2]) != [2])  # noqa: SIM202 (deliberate test)

    assert VarArray_Int32_10([2]) == [2]
    assert not (VarArray_Int32_10([2]) != [2])  # noqa: SIM202 (deliberate test)

    assert VarArray_Int32_10([2, 3]) == [2, 3]
    assert not (VarArray_Int32_10([2, 3]) != [2, 3])  # noqa: SIM202 (deliberate test)

    # doesn't raise an error, just is not equal
    assert VarArray_Int32_10() != range(11)
    assert not (VarArray_Int32_10() == range(11))  # noqa: SIM201 (deliberate test)


def test_np_array() -> None:
    """Test conversion to numpy array."""
    arr = np.array(VarArray_Int32_10([2, 3, 4, 5]))
    assert np.all(arr == np.array([2, 3, 4, 5]))


def test_np_array_copy_semantics() -> None:
    """Test conversion to numpy array copy semantics."""
    arr = VarArray_Int32_10(range(5))
    np_arr = np.array(arr)

    # mutate the np.array and check that the original VarArray is unaffected
    np_arr[1] = 100
    assert arr[1] == 1

    # mutate the VarArray and check that the np.array is unaffected
    arr[2] = 200
    assert np_arr[2] == 2


def test_var_byte_array() -> None:
    obj = WithVarBytes()
    orig: bytes = obj.var_byte_array
    assert isinstance(orig, bytes)
    assert not orig

    # Empty
    obj.var_byte_array = b""
    assert obj.var_byte_array == b""
    assert obj.var_byte_array == orig

    # Less than capacity
    obj.var_byte_array = b"ab"
    assert obj.var_byte_array == b"ab"
    assert obj.var_byte_array != orig

    # Exactly at capacity
    obj.var_byte_array = b"abc"
    assert obj.var_byte_array == b"abc"
    assert obj.var_byte_array != orig

    # Exceeds capacity
    with pytest.raises(
        TypeError,
        match=re.escape(
            "var_byte_array(): incompatible function arguments. The following argument types are supported:"
        ),
    ):
        obj.var_byte_array = b"abcd"


def test_nested_var_array_bytes() -> None:
    obj = WithVarBytes()
    orig = obj.nested_var_byte_array
    assert not orig

    # Different sizes
    obj.nested_var_byte_array = [b"ab", b"xyz"]
    assert obj.nested_var_byte_array == [b"ab", b"xyz"]
    obj.nested_var_byte_array = [b"ab"]
    assert obj.nested_var_byte_array == [b"ab"]
    obj.nested_var_byte_array = []
    assert obj.nested_var_byte_array == []

    # Exceeds capacity
    with pytest.raises(
        TypeError,
        match=re.escape(
            "nested_var_byte_array(): incompatible function arguments. The following argument types are supported:"
        ),
    ):
        obj.nested_var_byte_array = [b"abcd", b"xyz"]

    with pytest.raises(
        TypeError,
        match=re.escape(
            "nested_var_byte_array(): incompatible function arguments. The following argument types are supported:"
        ),
    ):
        obj.nested_var_byte_array = [b"abc", b"wxyz"]
