# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Builtins for multi-dimensional data."""

import itertools
import operator
import uuid
from dataclasses import dataclass
from typing import Final, Generic, TypeVar, cast

from clockwork.dsl.ir import clkbuiltins, clkenum, node, primitive, statement, typesys

_TENSOR_LAYOUT_SCOPE = clkbuiltins.BUILTINS_SCOPE.make_child_scope("TensorLayout")

TENSOR_LAYOUT_ENUM = clkenum.ClkEnum(
    type_info=clkbuiltins.TYPE_TYPE,
    doc=node.Doc(
        module=clkbuiltins.BUILTINS_MODULE, cst_node=None, value="A convenience enum for common tensor layouts."
    ),
    module=clkbuiltins.BUILTINS_MODULE,
    cst_node=None,  # No direct CST node for generated enums
    name="TensorLayout",
    scope=clkbuiltins.BUILTINS_SCOPE,
    inner_scope=_TENSOR_LAYOUT_SCOPE,
    uuid=uuid.uuid5(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, "TensorLayout"),
    values={},
    default_field_num=0,  # row_major
    bit_flags=False,
    underlying_type=clkbuiltins.UINT8,
    has_explicit_underlying_type=False,
    linter_overrides=set(),
    history=None,
    attributes=None,
    resolved=None,
)
TENSOR_LAYOUT_ENUM.values = {
    0: clkenum.ValueDef(
        doc=node.Doc(
            module=clkbuiltins.BUILTINS_MODULE,
            cst_node=None,
            value="Stride[i] is the product of Dimensions[j] for all j in (i, n). Stride[n-1] is always 1.",
        ),
        module=clkbuiltins.BUILTINS_MODULE,
        cst_node=None,
        name="row_major",
        scope=_TENSOR_LAYOUT_SCOPE,
        enum=TENSOR_LAYOUT_ENUM,
        field_num=0,
        is_default=True,
        integer_value=0,
        resolved=None,
    ),
    1: clkenum.ValueDef(
        doc=node.Doc(
            module=clkbuiltins.BUILTINS_MODULE,
            cst_node=None,
            value="Stride[i] is the product of Dimensions[j] for all j in (0, i). Stride[0] is always 1.",
        ),
        module=clkbuiltins.BUILTINS_MODULE,
        cst_node=None,
        name="column_major",
        scope=_TENSOR_LAYOUT_SCOPE,
        enum=TENSOR_LAYOUT_ENUM,
        field_num=1,
        is_default=False,
        integer_value=1,
        resolved=None,
    ),
}
for value in TENSOR_LAYOUT_ENUM.values.values():
    _TENSOR_LAYOUT_SCOPE.define(value.name, clkenum.ValueRef.make(value), None)

TENSOR: Final = clkbuiltins.BuiltinSerializableGenericTypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE,
    name="Tensor",
    parameters=(
        typesys.Parameter(name="type", type_bound=clkbuiltins.TYPE_TYPE, default=None),
        typesys.Parameter(name="dimensions", type_bound=clkbuiltins.LIST, default=None),
        typesys.Parameter(
            name="layout",
            type_bound=typesys.TypeUnion.make(
                types=(TENSOR_LAYOUT_ENUM, clkbuiltins.LIST),
            ),
            default=cast("clkenum.ValueRef", TENSOR_LAYOUT_ENUM.lookup("row_major")),
        ),
    ),
    type_info=clkbuiltins.TYPE_TYPE,
)


T = TypeVar("T")


@dataclass
class TensorData(Generic[T]):
    """A thin wrapper around the buffer for a tensor of T, its shape, and its memory layout."""

    data: list[T]
    shape: list[int]
    strides: list[int]


def resolve_tensor_parameters(tensor: typesys.TypeVal) -> None:
    """Unify a tensor's parameters with the appropriate types.

    Raises:
        TypeError: If the input type is not a tensor or any arguments have invalid types.
        KeyError: If any arguments are missing.
    """
    if not isinstance(tensor, typesys.Instantiation) or tensor.instantiates is not TENSOR:
        msg = f"Attempting to compute strides for non-tensor type {tensor}"
        raise TypeError(msg)

    dimensions = tensor.arguments["dimensions"]
    assert isinstance(dimensions, typesys.Values)
    for dim in dimensions.elements:
        typesys.unify(dim.type_info, clkbuiltins.UINT64)

    layout = tensor.arguments["layout"]
    if isinstance(layout, typesys.Values):
        for stride in layout.elements:
            typesys.unify(stride.type_info, clkbuiltins.UINT64)


def get_tensor_element_type(type_val: typesys.TypeVal) -> typesys.TypeVal:
    """Tensor<T, shape, layout> -> T."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != TENSOR:
        msg = f"Expected a Tensor field type. Received: {type_val}"
        raise TypeError(msg)

    element_type = type_val.arguments["type"]
    if not isinstance(element_type, typesys.TypeVal):
        msg = f"Expected a TypeVal. Received: {element_type}."
        raise TypeError(msg)

    return element_type


def get_tensor_shape(type_val: typesys.TypeVal) -> list[int]:
    """Compute the stride list for a tensor type.

    Raises:
        TypeError: If the input type is not a tensor or any arguments have invalid types.
        KeyError: If any arguments are missing.
    """
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates is not TENSOR:
        msg = f"Attempting to compute strides for non-tensor type {type_val}"
        raise TypeError(msg)

    raw_dimensions = type_val.arguments["dimensions"]
    # guaranteed by type definition
    assert isinstance(raw_dimensions, typesys.Values)

    dimensions: list[int] = []
    for dim in raw_dimensions.elements:
        dim_value = dim.bound_value() if isinstance(dim, statement.ImmutableBinding) else dim

        # ensured by resolve_tensor_parameters()
        assert isinstance(dim_value, primitive.DecimalValue)
        dimensions.append(primitive.unsigned_decimal_to_int(dim_value))

    # the list literal syntax makes it impossible to represent an empty list
    assert len(dimensions) > 0

    return dimensions


def compute_tensor_strides(dimensions: list[int], layout: clkenum.ValueRef) -> list[int]:
    """Multiply tensor dimensions to produce the stride list.

    Raises:
        TypeError: If the layout parameter is not a TensorLayout value.
    """
    if layout.value_def.enum is not TENSOR_LAYOUT_ENUM:
        msg = f"Stride computation expectes TensorLayout, but got {layout.value_def.enum.name}"
        raise TypeError(msg)

    if layout == TENSOR_LAYOUT_ENUM.lookup("column_major"):
        # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return [1, *list(itertools.accumulate(dimensions, operator.mul))[:-1]]

    # row_major
    # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return [*list(itertools.accumulate(reversed(dimensions), operator.mul))[::-1][1:], 1]


def get_tensor_strides(type_val: typesys.TypeVal) -> list[int]:
    """Compute the stride list for a tensor type.

    Raises:
        TypeError: If the input type is not a tensor or any arguments have invalid types.
        KeyError: If any arguments are missing.
    """
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates is not TENSOR:
        msg = f"Attempting to compute strides for non-tensor type {type_val}"
        raise TypeError(msg)

    dimensions = get_tensor_shape(type_val)

    layout = type_val.arguments["layout"]
    if isinstance(layout, clkenum.ValueRef):
        return compute_tensor_strides(dimensions, layout)

    assert isinstance(layout, typesys.Values)
    strides = []
    for element in layout.elements:
        element_value = element.bound_value() if isinstance(element, statement.ImmutableBinding) else element
        typesys.unify(clkbuiltins.UINT64, element_value.type_info)
        # ensured by the unification above
        assert isinstance(element_value, primitive.DecimalValue)
        strides.append(primitive.unsigned_decimal_to_int(element_value))

    return strides


for typ in (TENSOR, TENSOR_LAYOUT_ENUM):
    clkbuiltins.BUILTINS_SCOPE.define(typ.name, typ, None)
