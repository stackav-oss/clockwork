# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for cpp.literal."""

import re
from dataclasses import dataclass
from decimal import Decimal
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.cpp import types
from clockwork.dsl.cpp.literal import (
    bool_value_to_cpp,
    decimal_value_to_cpp,
    get_factory_fn,
    register_factory_fn,
)
from clockwork.dsl.ir import clkbuiltins, primitive, typesys


@dataclass
class BoolTestCase:
    """A test case."""

    value: typesys.NamedValue
    expect: str


def bool_false() -> BoolTestCase:
    return BoolTestCase(
        value=clkbuiltins.FALSE_VALUE,
        expect="false",
    )


def bool_true() -> BoolTestCase:
    return BoolTestCase(
        value=clkbuiltins.TRUE_VALUE,
        expect="true",
    )


@pytest.mark.parametrize(
    "test_val",
    [
        bool_false(),
        bool_true(),
    ],
)
def test_bool_to_cpp(test_val: BoolTestCase) -> None:
    got = bool_value_to_cpp(test_val.value)
    assert got.render("") == test_val.expect, f"Expected {test_val.expect}, got {got} instead."


@dataclass
class LiteralTestCase:
    """A test case."""

    value: primitive.DecimalValue
    expect: str


def int_signed_positive() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalLiteral(
            value=Decimal(42),
            type_info=clkbuiltins.INT16,
            module=MagicMock(),
            cst_node=None,
        ),
        expect="42",
    )


def int_signed_negative() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalLiteral(
            value=Decimal(-42),
            type_info=clkbuiltins.INT16,
            module=MagicMock(),
            cst_node=None,
        ),
        expect="-42",
    )


def int_unsigned() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalLiteral(
            value=Decimal(42),
            type_info=clkbuiltins.UINT64,
            module=MagicMock(),
            cst_node=None,
        ),
        expect="42U",
    )


def float_single_precision() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalLiteral(
            value=Decimal("3.14"),
            type_info=clkbuiltins.FLOAT32,
            module=MagicMock(),
            cst_node=None,
        ),
        expect="3.14f",
    )


def float_double_precision() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalLiteral(
            value=Decimal("3.14159e4211"),
            type_info=clkbuiltins.FLOAT64,
            module=MagicMock(),
            cst_node=None,
        ),
        expect="3.14159E+4211",
    )


def big_unsigned_int() -> LiteralTestCase:
    return LiteralTestCase(
        value=primitive.DecimalValue(
            value=Decimal("5e8"),
            type_info=clkbuiltins.UINT64,
        ),
        expect="500'000'000U",
    )


@pytest.mark.parametrize(
    "test_val",
    [
        int_signed_positive(),
        int_signed_negative(),
        int_unsigned(),
        big_unsigned_int(),
        float_single_precision(),
        float_double_precision(),
    ],
)
def test_literal_to_cpp(test_val: LiteralTestCase) -> None:
    got = decimal_value_to_cpp(test_val.value)
    assert got.render("") == test_val.expect, f"Expected {test_val.expect}, got {got} instead."


def test_literal_to_cpp_unsupported_type() -> None:
    # Simulating an unsupported type with a mock object or a type not included in the handling logic.
    literal = primitive.DecimalLiteral(
        value=Decimal(0),
        type_info=clkbuiltins.FIXED_ARRAY,
        module=MagicMock(),
        cst_node=None,
    )
    with pytest.raises(NotImplementedError):
        decimal_value_to_cpp(literal)


def test_decimal_to_cpp_negative_unsigned() -> None:
    with pytest.raises(
        ValueError,
        match=re.escape(f"Integer DecimalValue of unsigned type {clkbuiltins.UINT16} has negative value -5"),
    ):
        decimal_value_to_cpp(value=primitive.DecimalValue(type_info=clkbuiltins.UINT16, value=Decimal(-5)))


def test_decimal_to_cpp_noninteger_int() -> None:
    with pytest.raises(ValueError, match=re.escape("Integer DecimalValue contains non-integer value 4.3")):
        decimal_value_to_cpp(value=primitive.DecimalValue(type_info=clkbuiltins.UINT16, value=Decimal("4.3")))


def test_factory_fn_registry_no_registered() -> None:
    assert get_factory_fn(MagicMock()) is None


def test_factory_fn_registry() -> None:
    type_info = typesys.TypeDef(type_info=MagicMock(), scope=MagicMock(), name="some_type")
    input_type = typesys.TypeDef(type_info=MagicMock(), scope=MagicMock(), name="some_input")
    fn = types.CppFn([], "a", "b")
    register_factory_fn(type_info, input_type, fn)

    factory = get_factory_fn(type_info)
    assert factory is not None
    assert factory.input_type == input_type
    assert factory.fn == fn
