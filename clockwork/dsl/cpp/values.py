# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utility functions for generating C++ literals."""

import uuid
from decimal import Decimal

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import typereg, types
from clockwork.dsl.ir import clkbuiltins, primitive, typesys


def uuid_to_byte_array(context: CompilerContext, uuid_value: uuid.UUID) -> types.CppValue:
    """Convert a UUID to a std::array of bytes.

    Args:
        context: The compiler context.
        uuid_value: The UUID to convert.

    Returns:
        C++ value representing the UUID as a byte array.
    """
    array_type = typereg.get_cpp_type(
        context,
        typesys.Instantiation(
            instantiates=clkbuiltins.FIXED_ARRAY,
            arguments={
                "type": clkbuiltins.UINT8,
                "size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(len(uuid_value.bytes))),
            },
            type_info=clkbuiltins.TYPE_TYPE,
        ),
    )
    return types.CppValue(
        value_type=array_type,
        value=", ".join(f"{byte:#04x}" for byte in uuid_value.bytes),
    )


def _uuid_type(context: CompilerContext, tag: typesys.TypeDef) -> types.CppType | types.CppTemplateType:
    return typereg.get_cpp_type(
        context,
        typesys.Instantiation(
            instantiates=clkbuiltins.UUID,
            arguments={"tag": tag},
            type_info=clkbuiltins.TYPE_TYPE,
        ),
    )


def uuid_to_named_value(
    context: CompilerContext, uuid_value: uuid.UUID, name: str, tag: typesys.TypeDef
) -> types.CppNamedValue:
    """Convert a UUID instance to a Cpp UUID instance.

    Args:
        context: The compiler context.
        uuid_value: The UUID to convert.
        name: The name for the C++ variable.
        tag: The type definition to tag the UUID with.

    Returns:
        C++ named value representing the UUID.
    """
    return types.CppNamedValue(
        named_type=types.CppNamedType(
            argument_type=_uuid_type(context, tag),
            argument_name=name,
        ),
        value=uuid_to_byte_array(context, uuid_value),
        doc="Class type UUID.",
        qualifiers=["static", "constexpr"],
    )


def uuid_to_value(context: CompilerContext, uuid_value: uuid.UUID, tag: typesys.TypeDef) -> types.CppValue:
    """Convert a UUID instance to a Cpp UUID instance.

    Args:
        context: The compiler context.
        uuid_value: The UUID to convert.
        tag: The type definition to tag the UUID with.

    Returns:
        C++ value representing the UUID.
    """
    return types.CppValue(
        value_type=_uuid_type(context, tag),
        value=uuid_to_byte_array(context, uuid_value),
    )
