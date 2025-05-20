# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit test for function calls on clockwork-generated nanobindings."""

from clockwork.dsl.tests.support.nanobindable_messages_clk_nb import Foo
from clockwork.dsl.tests.support.nb_function_calls import make_foo, mutate_foo


def test_return_value() -> None:
    """Basic test - function returns a C++ value and it shows up in python with the correct values."""
    foo = make_foo()
    assert foo.msg.int_field == 100
    assert foo.prim == 101


def test_return_by_reference() -> None:
    """Test that return-by-reference works."""
    foo = Foo()
    assert foo.msg.int_field == 0
    assert foo.prim == 0

    mutate_foo(foo)
    assert foo.msg.int_field == 22
    assert foo.prim == 42
