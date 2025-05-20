# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utility functions for generating C++ literals."""

from dataclasses import dataclass
from decimal import Decimal
from typing import Final

from clockwork.dsl.cpp import types
from clockwork.dsl.ir import clkbuiltins, primitive, typesys


def bool_value_to_cpp(value: typesys.Value) -> types.CppValue:
    """Converts a Clockwork bool numeric to its C++ representation.

    This function formats a value from the Clockwork IR as a string
    representing the literal in C++ source.

    Args:
        value: The Clockwork value to be converted.

    Returns:
        A C++ literal representing the value.
    """
    if value == clkbuiltins.FALSE_VALUE:
        return types.CppValue(None, "false")
    if value == clkbuiltins.TRUE_VALUE:
        return types.CppValue(None, "true")
    msg = f"Unsupported literal bool type {value}"
    raise NotImplementedError(msg)


@dataclass
class Factory:
    """A factory function to produce a value."""

    input_type: typesys.TypeVal
    fn: types.CppFn


_CPP_FACTORY_REGISTRY: Final[dict[str, Factory]] = {}


def register_factory_fn(type_info: typesys.TypeDef, input_type: typesys.TypeVal, fn: types.CppFn) -> None:
    """Register a factory function.

    Args:
        type_info: The type that gets produced by the factory.
        input_type: Type of the input argument to the factory.
        fn: A cpp function to invoke as the factory.
    """
    factory = Factory(
        input_type=input_type,
        fn=fn,
    )

    existing = _CPP_FACTORY_REGISTRY.get(type_info.value_key(), None)
    if existing and existing != factory:
        msg = f"Factory already registered for {type_info}"
        raise ValueError(msg)

    _CPP_FACTORY_REGISTRY[type_info.value_key()] = factory


def get_factory_fn(type_info: typesys.Value) -> Factory | None:
    """Get the registered factory or None if not registered."""
    return _CPP_FACTORY_REGISTRY.get(type_info.value_key(), None)


def decimal_value_to_cpp(value: primitive.DecimalValue) -> types.CppValueExpr:  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    """Converts a Clockwork numeric value to its C++ representation.

    This function formats a value from the Clockwork IR as a string
    representing the literal in C++ source.

    The function adds 'U' suffixes for unsigned integers but does not add 'L' or
    'LL' suffixes for long integers due to the complexity of supporting
    different architectures (e.g., aarch64, x86_64). The C++ standard ensures
    that a suitable type will be chosen to fit the literal. This is designed
    with the assumption that the literals are being used in C++ in a context
    where precise type deduction isn't critical, such as template instantiation
    with known types like std::array, rather than arbitrary type deduction with
    auto. This fits our expected use cases.

    For floating-point literals, portability is not a concern, so 'f' will be
    added for float.

    Args:
        value: The Clockwork value to be converted.

    Returns:
        A C++ literal representing the value.

    Raises:
        NotImplementedError: If the values's type is unsupported.
    """
    suffix = ""
    num_type = value.type_info
    if isinstance(num_type, typesys.InferenceVar):
        num_type = num_type.resolution()

        if not isinstance(num_type, typesys.TypeVal):
            msg = f"Resolution of failed to produce a TypeVal.  Input value: {value}"
            raise TypeError(msg)

    factory = get_factory_fn(num_type)
    if factory:
        arg = decimal_value_to_cpp(primitive.DecimalValue(type_info=factory.input_type, value=value.value))
        return factory.fn.invoke((arg,))

    if isinstance(num_type, clkbuiltins.IntegerPrimitiveType):
        int_val = int(value.value)
        if int_val != value.value:
            msg = f"Integer DecimalValue contains non-integer value {value.value}"
            raise ValueError(msg)
        if not num_type.signed:
            if int_val < 0:
                msg = f"Integer DecimalValue of unsigned type {num_type} has negative value {int_val}"
                raise ValueError(msg)
            suffix += "U"
        five_digits: Final = 10000
        int_str = f"{int_val:_}".replace("_", "'") if int_val >= five_digits else str(int_val)

        return types.CppValue(None, f"{int_str}{suffix}")
    if isinstance(num_type, clkbuiltins.FloatingPointPrimitiveType):
        single_precision: Final = 32
        suffix = "f" if num_type.bit_width == single_precision else ""
        return types.CppValue(None, f"{value.value}{suffix}")
    msg = f"Unsupported literal type: {num_type} from {value}"
    raise NotImplementedError(msg)


def int_to_cpp(value: int, type_info: clkbuiltins.IntegerPrimitiveType) -> types.CppValueExpr:  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    """Converts Python numeric integer to its C++ representation.

    See decimal_value_to_cpp for more details.

    Args:
        value: The numeric integer to convert
        type_info: Target IntegerPrimitiveType (see types in clkbuiltins)

    Returns:
        A C++ literal representing the value.

    Raises:
        See decimal_value_to_cpp for details.
    """
    decimal_value = primitive.DecimalValue(type_info=type_info, value=Decimal(value))
    return decimal_value_to_cpp(decimal_value)
