# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for clockwork generated nanobindings."""

import copy
import re
import struct
from enum import Enum

import pytest
from clockwork.dsl.tests.support.nanobindable_messages_clk_nb import (
    AU_METERS32_CONSTANT,
    AU_METERS64_CONSTANT,
    INTEGRAL_STRONG_TYPE_CONSTANT,
    SOME_FALSE_CONSTANT,
    SOME_FLOAT32_CONSTANT,
    SOME_FLOAT64_CONSTANT,
    SOME_INT_CONSTANT,
    SOME_STR_CONSTANT,
    SOME_TRUE_CONSTANT,
    SOME_UINT_CONSTANT,
    UNTYPED_FALSE_CONSTANT,
    UNTYPED_FLOAT_CONSTANT,
    UNTYPED_INT_CONSTANT,
    UNTYPED_STR_CONSTANT,
    UNTYPED_TRUE_CONSTANT,
    DistinctGenericMessageAlias,
    Foo,
    FooEnum,
    GenericMessageFloat32,
    MessageWithOptionals,
    NumericalParamUInt,
    SubMessage,
)
from jewels.nanobind.nb_sync_time import Duration, SyncTime


def test_normal_msg_field() -> None:
    """Basic field containing other schema."""
    foo = Foo()
    assert foo.msg.int_field == 0
    foo.msg.int_field = 22
    assert foo.msg.int_field == 22


def test_normal_prim_field() -> None:
    """Basic field containing primitive."""
    foo = Foo()
    assert foo.prim == 0
    foo.prim = 22
    assert foo.prim == 22


def test_msg_array() -> None:
    """FixedArray of other schema."""
    foo = Foo()
    assert foo.msg_array[0].int_field == 0
    foo.msg_array[0].int_field = 22
    assert foo.msg_array[0].int_field == 22
    assert bool(foo.msg_array)  # non-empty


def test_prim_array() -> None:
    """FixedArray of primitives."""
    foo = Foo()
    assert foo.prim_array[0] == 0
    foo.prim_array[0] = 22
    assert foo.prim_array[0] == 22
    assert bool(foo.msg_array)  # non-empty


def test_msg_var_array() -> None:
    """VarArray of other schema."""
    foo = Foo()
    assert len(foo.msg_var_array) == 0
    assert not foo.msg_var_array
    foo.msg_var_array.append(SubMessage())
    assert foo.msg_var_array[0].int_field == 0
    foo.msg_var_array[0].int_field = 22
    assert foo.msg_var_array[0].int_field == 22

    assert len(list(foo.msg_var_array)) == 1  # test converting to list
    assert foo.msg_array  # non-empty

    # capacity checks
    foo.msg_var_array.from_iter([SubMessage()] * 10)
    assert len(foo.msg_var_array) == 10
    with pytest.raises(ValueError, match=r"push_back: Insufficient capacity"):
        foo.msg_var_array.append(SubMessage())

    assert len(list(foo.msg_var_array)) == 10  # test converting to list

    with pytest.raises(
        ValueError,
        match=re.escape(
            "VarArray<N9clockwork3TapINS_7TachyonIN3foo10SubMessageEEEEE, 10>: push_back: Insufficient capacity [10]."
        ),
    ):
        foo.msg_var_array.from_iter([SubMessage()] * 11)


def test_prim_var_array() -> None:
    """VarArray of primitives."""
    foo = Foo()
    assert len(foo.prim_var_array) == 0
    assert not foo.prim_var_array
    foo.prim_var_array.append(0)
    assert foo.prim_var_array[0] == 0
    foo.prim_var_array[0] = 22
    assert foo.prim_var_array[0] == 22

    # capacity checks
    foo.prim_var_array.from_iter([42] * 10)
    assert len(foo.prim_var_array) == 10
    with pytest.raises(ValueError, match=r"Insufficient capacity"):
        foo.prim_var_array.append(42)

    with pytest.raises(
        ValueError,
        match=re.escape("VarArray<i, 10>: push_back: Insufficient capacity [10]."),
    ):
        foo.prim_var_array.from_iter([42] * 11)

    # representation checks
    foo.prim_var_array.from_iter([1, 2, 3])
    assert str(foo.prim_var_array) == "[1, 2, 3]"


def test_prim_fixed_array_reference_semantics() -> None:
    """Reference semantics for primitive FixedArray."""
    foo = Foo()
    field = foo.prim_array
    assert field == [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    foo.prim_array[1] = 10
    foo.prim_array[7] = 11
    assert field == [0, 10, 0, 0, 0, 0, 0, 11, 0, 0]


def test_prim_var_array_reference_semantics() -> None:
    """Reference semantics for primitive VarArray."""
    foo = Foo()
    field = foo.prim_var_array
    assert not field
    foo.prim_var_array.append(22)
    foo.prim_var_array.append(1)
    foo.prim_var_array.append(23)
    assert field == [22, 1, 23]


def test_msg_fixed_array_reference_semantics() -> None:
    """Reference semantics for non-primitive FixedArray."""
    foo = Foo()
    field = foo.msg_array
    assert field == [SubMessage(k) for k in [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]]
    foo.msg_array[1] = SubMessage(10)
    foo.msg_array[7] = SubMessage(11)
    assert field == [SubMessage(k) for k in [0, 10, 0, 0, 0, 0, 0, 11, 0, 0]]

    # test the opposite direction
    new_submsg = SubMessage(42)
    foo.msg_array[8] = new_submsg
    assert foo.msg_array[8].int_field == 42
    # change it and check that it does not propagate back, because it was copied
    foo.msg_array[8].int_field = 43
    assert new_submsg.int_field != 43
    # same test for slices
    foo.msg_array[0:2] = [new_submsg, new_submsg]
    foo.msg_array[0].int_field = 100
    assert new_submsg.int_field != 100
    foo.msg_array[1].int_field = 101
    assert new_submsg.int_field != 101


def test_msg_var_array_reference_semantics() -> None:
    """Reference semantics for non-primitive VarArray."""
    foo = Foo()
    field = foo.msg_var_array
    assert not field
    foo.msg_var_array.append(SubMessage(22))
    foo.msg_var_array.append(SubMessage(1))
    foo.msg_var_array.append(SubMessage(23))
    assert field == [SubMessage(k) for k in [22, 1, 23]]

    # test the opposite direction
    new_submsg = SubMessage(42)
    foo.msg_var_array[0] = new_submsg
    assert foo.msg_var_array[0].int_field == 42
    # change it and check that it does not propagate back, because it was copied
    foo.msg_var_array[0].int_field = 43
    assert new_submsg.int_field != 43
    # same test for slices
    foo.msg_var_array[1:3] = [new_submsg, new_submsg]
    foo.msg_var_array[1].int_field = 100
    assert new_submsg.int_field != 100
    foo.msg_var_array[2].int_field = 101
    assert new_submsg.int_field != 101


def test_slice_var_array_prim() -> None:
    """Test slicing with __getitem__ / __setitem__ for primitive VarArrays.

    Complete coverage for bind_var_array.hh is in nanobindings_bind_var_array_test.py
    """
    foo = Foo()
    foo.prim_var_array.extend(range(6))
    assert foo.prim_var_array == [0, 1, 2, 3, 4, 5]
    foo.prim_var_array[2:5] = [12, 13, 14]
    assert foo.prim_var_array == [0, 1, 12, 13, 14, 5]
    assert foo.prim_var_array[3:5] == [13, 14]


def test_slice_var_array_msg() -> None:
    """Test slicing with __getitem__ / __setitem__ for non-primitive VarArrays.

    Complete coverage for bind_var_array.hh is in nanobindings_bind_var_array_test.py
    """
    foo = Foo()
    foo.msg_var_array.extend([SubMessage(k) for k in range(6)])
    assert foo.msg_var_array == [SubMessage(k) for k in [0, 1, 2, 3, 4, 5]]
    foo.msg_var_array[2:5] = [SubMessage(k) for k in [12, 13, 14]]
    assert foo.msg_var_array == [SubMessage(k) for k in [0, 1, 12, 13, 14, 5]]
    assert foo.msg_var_array[3:5] == [SubMessage(k) for k in [13, 14]]


def test_optional_msg() -> None:
    """Basic functionality for optional fields containing schemas."""
    # sub message
    foo = Foo()
    # default is None
    assert foo.optional_msg is None
    # set to value, check that it took
    foo.optional_msg = SubMessage(22)
    assert foo.optional_msg is not None
    assert foo.optional_msg.int_field == 22
    # update value and check that it took
    foo.optional_msg.int_field = 42
    assert foo.optional_msg.int_field == 42
    # mutate value and check that it took
    foo.optional_msg.int_field += 1
    assert foo.optional_msg.int_field == 43
    # unset and check that it took
    foo.optional_msg = None
    assert foo.optional_msg is None
    # try to mutate value that's None and check that it fails and doesn't change state
    with pytest.raises(AttributeError, match="'NoneType' object has no attribute 'int_field'"):
        foo.optional_msg.int_field = 2  # pyright: ignore[reportAttributeAccessIssue]
    assert foo.optional_msg is None


def test_invalid_optional_mutation() -> None:
    """Make sure that mutating the memory underlying an optional doesn't swap None to a value."""
    # mutate underlying field
    foo = Foo()
    foo.optional_msg = SubMessage(22)
    sub_message = foo.optional_msg
    foo.optional_msg = None
    sub_message.int_field = 42
    assert foo.optional_msg is None


def test_optional_prim() -> None:
    """Basic functionality for optional primitive fields."""
    # primitive
    foo = Foo()
    # check default is None
    assert foo.optional_prim is None
    # set to value, check that it took
    foo.optional_prim = 22
    assert foo.optional_prim is not None
    assert foo.optional_prim == 22
    # update value, check that it took
    foo.optional_prim = 42
    assert foo.optional_prim is not None
    assert foo.optional_prim == 42
    # mutate value, check that it took
    foo.optional_prim += 1
    assert foo.optional_prim == 43
    # unset and check that it takes
    foo.optional_prim = None
    assert foo.optional_prim is None


def test_optional_msg_reference_semantics() -> None:
    """Make sure that accessing the optional message field gives a reference."""
    # mutate underlying field
    foo = Foo()
    foo.optional_msg = SubMessage(22)

    val = foo.optional_msg
    assert val == SubMessage(22)

    foo.optional_msg = SubMessage(23)
    assert val == SubMessage(23)


def test_optional_prim_reference_semantics() -> None:
    """Make sure that accessing an optional primitive field DOESN'T give a reference."""
    # mutate underlying field
    foo = Foo()
    foo.optional_prim = 22

    val = foo.optional_prim
    assert val == 22

    foo.optional_prim = 23
    assert val == 22


def test_var_string() -> None:
    """Basic checks on VarString fields."""
    foo = Foo()

    # check default is empty
    assert foo.var_string == ""

    foo.var_string = "hi there!"
    assert foo.var_string == "hi there!"

    # setting to one under capacity works
    foo.var_string = "a" * 19

    # setting to capacity fails (because you need null termination)
    with pytest.raises(
        ValueError,
        match="Capacity is insufficient to convert a python string with length 20 to a VarString<20>.",
    ):
        foo.var_string = "b" * 20

    # check that pre-exception value is preserved
    assert foo.var_string == "a" * 19


def test_var_string_immutability() -> None:
    """Make sure var_string is immutable."""
    foo = Foo()

    # check default is empty
    vs1 = foo.var_string
    assert vs1 == ""
    foo.var_string = "yo"
    assert vs1 == ""


def test_au_meters() -> None:
    """Aurora units."""
    foo = Foo()
    # normal
    assert foo.au_meters == 0
    foo.au_meters = 2.2
    assert foo.au_meters == 2.2

    # optional
    assert foo.optional_au_meters is None
    foo.optional_au_meters = 2.2
    assert foo.optional_au_meters == 2.2
    foo.optional_au_meters = None
    assert foo.optional_au_meters is None

    # var array
    assert not foo.au_meters_var_array
    foo.au_meters_var_array.append(2.2)
    assert foo.au_meters_var_array[0] == 2.2
    foo.au_meters_var_array[0] = 3.3
    assert foo.au_meters_var_array[0] == 3.3

    # fixed array
    assert foo.au_meters_array[0] == 0
    foo.au_meters_array[0] = 2.2
    assert foo.au_meters_array[0] == 2.2


def test_str_and_repr() -> None:
    """Test __repr__ and __str__."""
    foo = Foo()
    foo.prim_array[1] = 1
    foo.prim_array[2] = 2
    foo.prim_array[9] = 9
    # simple array
    assert str(foo.prim_array) == "[0, 1, 2, 0, 0, 0, 0, 0, 0, 9]"
    assert repr(foo.prim_array) == "[0, 1, 2, 0, 0, 0, 0, 0, 0, 9]"

    # big message
    assert (
        str(foo)
        == "Foo(msg=SubMessage(int_field=0), prim=0, msg_var_array=[], prim_var_array=[], msg_array=[SubMessage(int_field=0), SubMessage(int_field=0), ...], prim_array=[0, 1, 2, 0, 0, 0, 0, 0, 0, 9], optional_msg=None, optional_prim=None, optional_enum=None, var_string=, au_meters=0.0, optional_au_meters=None, au_meters_var_array=[], au_meters_array=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, ...], dep_msg=Bar(some_val=0.0), dep_enum=OtherEnum.foo, dep_array_size=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, ...], array_of_generic_message=[GenericMessage(value=0), GenericMessage(value=0), ...], sync_time=SyncTime(0ns), duration=Duration(0ns))"
    )
    assert (
        repr(foo)
        == "Foo(msg=SubMessage(int_field=0), prim=0, msg_var_array=[], prim_var_array=[], msg_array=[SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0), SubMessage(int_field=0)], prim_array=[0, 1, 2, 0, 0, 0, 0, 0, 0, 9], optional_msg=None, optional_prim=None, optional_enum=None, var_string='', au_meters=0.0, optional_au_meters=None, au_meters_var_array=[], au_meters_array=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0], dep_msg=Bar(some_val=0.0), dep_enum=OtherEnum.foo, dep_array_size=[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0], array_of_generic_message=[GenericMessage(value=0), GenericMessage(value=0), GenericMessage(value=0), GenericMessage(value=0), GenericMessage(value=0), GenericMessage(value=0), GenericMessage(value=0)], sync_time=SyncTime(0ns), duration=Duration(0ns))"
    )

    # optionals
    opts = MessageWithOptionals()
    assert str(opts) == "MessageWithOptionals(int_field=None, msg_field=None)"
    assert repr(opts) == "MessageWithOptionals(int_field=None, msg_field=None)"

    opts.int_field = 22
    opts.msg_field = SubMessage(42)

    assert str(opts) == "MessageWithOptionals(int_field=22, msg_field=SubMessage(int_field=42))"
    assert repr(opts) == "MessageWithOptionals(int_field=22, msg_field=SubMessage(int_field=42))"


def test_eq() -> None:
    """Equality comparisons."""
    foo1 = Foo()
    foo2 = Foo()

    assert foo1 == foo2
    assert not (foo1 != foo2)  # noqa: SIM202, intentionally checking !=

    foo1.msg.int_field = 2
    assert foo1 != foo2

    foo1 = Foo()  # reset
    foo2 = Foo()  # reset
    foo1.var_string = "hi"
    assert foo1 != foo2
    foo2.var_string = "hi"
    assert foo1 == foo2

    foo1 = Foo()  # reset
    foo2 = Foo()  # reset
    foo1.msg_var_array.append(SubMessage(int_field=2))
    assert foo1 != foo2
    foo2.msg_var_array.append(SubMessage(int_field=3))
    assert foo1 != foo2
    foo2.msg_var_array[0].int_field = 2
    assert foo1 == foo2

    # generic message
    gen1 = GenericMessageFloat32(value=0.0)
    gen2 = GenericMessageFloat32(value=0.0)
    assert gen1 == gen2
    assert not (gen1 != gen2)  # noqa: SIM202, intentionally checking !=

    gen2.value = 22
    assert gen1 != gen2
    assert not (gen1 == gen2)  # noqa: SIM201, intentionally checking ==
    gen1.value = 22
    assert gen1 == gen2

    # Distinct instantiations of the same templated type always compare false
    assert GenericMessageFloat32(value=0.0) != DistinctGenericMessageAlias(value=0)
    assert GenericMessageFloat32(value=0.0) != DistinctGenericMessageAlias(value=1)
    assert GenericMessageFloat32(value=1.0) != DistinctGenericMessageAlias(value=0)

    assert not (GenericMessageFloat32(value=0.0) == DistinctGenericMessageAlias(value=0))  # noqa: SIM201, intentionally checking ==
    assert not (GenericMessageFloat32(value=0.0) == DistinctGenericMessageAlias(value=1))  # noqa: SIM201, intentionally checking ==
    assert not (GenericMessageFloat32(value=1.0) == DistinctGenericMessageAlias(value=0))  # noqa: SIM201, intentionally checking ==


def test_eq_wrong_types() -> None:
    """Equality comparisons with type mismatches.

    By default, nanobind's __eq__ throws exceptions when types are different.
    We've re-defined it to first try to cast, and returns False if a cast isn't possible.
    These are all type mismatches that should simply fail without throwing.
    """
    foo = Foo()
    twenty_two: object = 22
    assert foo != twenty_two
    assert foo.var_string != twenty_two
    assert foo.msg_var_array != twenty_two
    assert foo.prim_var_array != twenty_two
    assert foo.msg_array != twenty_two
    assert foo.prim_array != twenty_two


def test_dep() -> None:
    """Makes sure that the transitive imports work.

    These types have bindings in scope without (manually) importing the module.
    """
    foo = Foo()
    assert str(foo.dep_msg).startswith("Bar(")
    assert str(foo.dep_enum) == "OtherEnum.foo"


def test_generic() -> None:
    """Basic generic schema instantiation."""
    msg = GenericMessageFloat32()
    assert msg.value == 0
    msg.value = 22.2
    # Check that the message is stored internally as a 32 bit floating point
    assert msg.value != 22.2
    assert msg.value == struct.unpack("f", struct.pack("f", 22.2))[0]


def test_array_of_generic_alias() -> None:
    """The Array of generic messages should have picked up the generic alias."""
    foo = Foo()

    # DistinctGenericMessageAlias is the user-assigned alias.
    # If this feature were broken, this name would be something like 'FixedArray_GenericMessageFloat64_7
    assert type(foo.array_of_generic_message).__name__ == "FixedArray_DistinctGenericMessageAlias_7"


def test_constant() -> None:
    """Check that constants are correct."""
    assert SOME_INT_CONSTANT == 42
    assert SOME_UINT_CONSTANT == 43
    assert SOME_TRUE_CONSTANT is True
    assert SOME_FALSE_CONSTANT is False
    assert SOME_STR_CONSTANT == "im a string constant"

    assert SOME_FLOAT32_CONSTANT == 2.2
    assert SOME_FLOAT64_CONSTANT == 2.2

    assert AU_METERS32_CONSTANT == 22.2
    assert AU_METERS64_CONSTANT == 22.2

    assert INTEGRAL_STRONG_TYPE_CONSTANT == 22

    assert UNTYPED_INT_CONSTANT == 22
    assert UNTYPED_FLOAT_CONSTANT == 2.2
    assert UNTYPED_TRUE_CONSTANT is True
    assert UNTYPED_FALSE_CONSTANT is False
    assert UNTYPED_STR_CONSTANT == "hello"


def test_enum() -> None:
    """Basic enum properties."""
    assert list(FooEnum) == [FooEnum.foo, FooEnum.bar]

    assert FooEnum["foo"] == FooEnum.foo
    assert FooEnum["bar"] == FooEnum.bar

    assert FooEnum.foo.name == "foo"
    assert FooEnum.bar.name == "bar"

    assert isinstance(FooEnum.foo, Enum)
    assert isinstance(FooEnum.bar, Enum)


def test_constructor_with_optionals() -> None:
    """Test passing None in constructors."""
    msg = MessageWithOptionals(int_field=None, msg_field=None)
    assert msg.int_field is None
    assert msg.msg_field is None

    msg = MessageWithOptionals(int_field=22, msg_field=None)
    assert msg.int_field == 22
    assert msg.msg_field is None

    msg = MessageWithOptionals(int_field=None, msg_field=SubMessage(22))
    assert msg.int_field is None
    assert msg.msg_field == SubMessage(22)

    msg = MessageWithOptionals(int_field=22, msg_field=SubMessage(22))
    assert msg.int_field == 22
    assert msg.msg_field == SubMessage(22)


def test_sync_time() -> None:
    """Test a sync time field."""
    foo = Foo()
    assert foo.sync_time == SyncTime()


def test_duration() -> None:
    """Test a sync time field."""
    foo = Foo()
    assert foo.duration == Duration()


def test_param_from_constant() -> None:
    """Test param from constant."""
    foo = NumericalParamUInt()
    assert foo.field == SOME_UINT_CONSTANT


def test_copy_constructor() -> None:
    """Test copy constructor."""
    foo = Foo()
    foo.prim = 22
    foo.msg.int_field = 42
    foo_copy = Foo(foo)
    assert foo_copy.prim == foo.prim
    assert foo_copy.msg.int_field == foo.msg.int_field
    assert foo_copy is not foo
    foo.prim = 87
    assert foo_copy.prim != foo.prim


@pytest.mark.parametrize(
    "test_deepcopy",
    [False, True],
)
def test_copy_and_deepcopy(test_deepcopy: bool) -> None:
    """Test __copy__/__deepcopy__."""
    foo = Foo()
    foo.prim = 22
    foo.msg.int_field = 42
    foo_copy: Foo
    foo_copy = copy.deepcopy(foo) if test_deepcopy else copy.copy(foo)
    assert foo_copy.prim == foo.prim
    assert foo_copy.msg.int_field == foo.msg.int_field
    assert foo_copy is not foo
    foo.prim = 87
    assert foo_copy.prim != foo.prim


@pytest.mark.parametrize(
    "use_default_constructor",
    [False, True],
)
def test_default(use_default_constructor: bool) -> None:
    """Test default values."""
    foo: Foo = Foo() if use_default_constructor else Foo.default()
    assert foo.msg.int_field == 0
    assert foo.prim == 0
    assert not foo.msg_var_array
    assert list(foo.prim_var_array) == []
    assert foo.msg_array == 10 * [SubMessage(0)]
    assert foo.prim_array == 10 * [0]
    assert foo.optional_msg is None
    assert foo.optional_prim is None
    assert foo.optional_enum is None
    assert foo.var_string == ""
    assert foo.au_meters == 0.0
    assert foo.optional_au_meters is None
    assert not foo.au_meters_var_array
    assert foo.au_meters_array == 10 * [0.0]
    assert foo.dep_msg.some_val == 0.0
    assert str(foo.dep_enum) == "OtherEnum.foo"  # intentionally don't import OtherEnum
    assert foo.dep_array_size == 23 * [0.0]
    assert foo.array_of_generic_message == 7 * [DistinctGenericMessageAlias(value=0)]
    assert foo.sync_time == SyncTime()
    assert foo.duration == Duration()
