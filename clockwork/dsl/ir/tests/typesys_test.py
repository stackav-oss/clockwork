# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test the Cog IR module."""

import re
from typing import cast
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.ir import clkbuiltins, node, statement, typesys


class MockType(typesys.TypeVal, typesys.ObjectIdentityValue):
    """Mock type."""


@pytest.fixture()
def typeval() -> typesys.TypeVal:
    # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return MockType(type_info=cast("typesys.TypeVal", None))


@pytest.fixture()
def type_a(typeval: typesys.TypeVal) -> typesys.TypeDef:
    return typesys.TypeDef(
        name="TypeA",
        scope=node.Scope(parent=None, uniq_path="uniq.path", module_id_for_errors=None),
        type_info=typeval,
    )


@pytest.fixture()
def type_b(typeval: typesys.TypeVal) -> typesys.TypeDef:
    return typesys.TypeDef(
        name="TypeB",
        scope=node.Scope(parent=None, uniq_path="uniq.path", module_id_for_errors=None),
        type_info=typeval,
    )


@pytest.fixture()
def type_c(typeval: typesys.TypeVal) -> typesys.TypeDef:
    return typesys.TypeDef(
        name="TypeC",
        scope=node.Scope(parent=None, uniq_path="uniq.path", module_id_for_errors=None),
        type_info=typeval,
    )


@pytest.fixture()
def type_a_or_b(type_a: typesys.TypeDef, type_b: typesys.TypeDef) -> typesys.TypeUnion:
    return typesys.TypeUnion.make(types=(type_a, type_b))


@pytest.fixture()
def type_b_or_c(type_b: typesys.TypeDef, type_c: typesys.TypeDef) -> typesys.TypeUnion:
    return typesys.TypeUnion.make(types=(type_b, type_c))


@pytest.fixture()
def typedef(typeval: typesys.TypeVal) -> typesys.TypeDef:
    return typesys.TypeDef(
        name="SomeType",
        scope=node.Scope(parent=None, uniq_path="uniq.path", module_id_for_errors=None),
        type_info=typeval,
    )


@pytest.fixture()
def context() -> node.Module:
    # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return cast("node.Module", None)


def test_typeval_satisfies(typeval: typesys.TypeVal) -> None:
    assert typeval.satisfies(typesys.NumericType.NONE) is True
    assert typeval.satisfies(typesys.NumericType.INTEGER) is False
    assert typeval.satisfies(typesys.NumericType.FLOAT) is False


def test_numerictype_intersect() -> None:
    assert typesys.NumericType.NONE.intersect_with(typesys.NumericType.NONE) is typesys.NumericType.NONE
    assert typesys.NumericType.NONE.intersect_with(typesys.NumericType.INTEGER) is typesys.NumericType.INTEGER
    assert typesys.NumericType.NONE.intersect_with(typesys.NumericType.FLOAT) is typesys.NumericType.FLOAT
    assert typesys.NumericType.INTEGER.intersect_with(typesys.NumericType.INTEGER) is typesys.NumericType.INTEGER
    assert typesys.NumericType.INTEGER.intersect_with(typesys.NumericType.NONE) is typesys.NumericType.INTEGER
    assert typesys.NumericType.FLOAT.intersect_with(typesys.NumericType.FLOAT) is typesys.NumericType.FLOAT
    assert typesys.NumericType.FLOAT.intersect_with(typesys.NumericType.NONE) is typesys.NumericType.FLOAT
    with pytest.raises(TypeError):
        typesys.NumericType.INTEGER.intersect_with(typesys.NumericType.FLOAT)
    with pytest.raises(TypeError):
        typesys.NumericType.FLOAT.intersect_with(typesys.NumericType.INTEGER)


def test_inferencevar_make(context: node.Module) -> None:
    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    assert v0._id != v1._id
    assert v0 != v1
    assert v0.resolution() is v0
    assert v1.resolution() is v1


def test_inferencevar_resolution(context: node.Module, typeval: typesys.TypeVal) -> None:
    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1._resolution = v0
    assert v0.resolution() is v0
    assert v1.resolution() is v0
    v0._resolution = typeval
    assert v0.resolution() is typeval
    assert v1.resolution() is typeval


def test_unification(context: node.Module, typeval: typesys.TypeVal) -> None:
    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    assert v0.unify(v1) is v0
    assert v1.resolution() is v0
    assert v0.resolution() is v0

    # Doing it again changes nothing
    assert v0.unify(v1) is v0
    assert v1.resolution() is v0
    assert v0.resolution() is v0

    # Doing it in reverse changes nothing (does not create cycle)
    assert v1.unify(v0) is v0
    assert v1.resolution() is v0
    assert v0.resolution() is v0

    # Unifying one with a TypeVal unifies them both
    assert typesys.unify(v1, typeval) is typeval
    assert v1.resolution() is typeval
    assert v0.resolution() is typeval

    # And doing that over again in different directions doesn't change anything
    assert typesys.unify(typeval, v0) is typeval
    assert v1.resolution() is typeval
    assert v0.resolution() is typeval
    assert v0.unify(typeval) is typeval
    assert v1.resolution() is typeval
    assert v0.resolution() is typeval
    assert v1.unify(typeval) is typeval
    assert v1.resolution() is typeval
    assert v0.resolution() is typeval

    assert v0.conform(typeval) is typeval
    assert typesys.unify(typeval, typeval) is typeval

    # Unifying with a different type raises TypeError
    with pytest.raises(TypeError):
        # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        v0.unify(MockType(type_info=cast("typesys.TypeVal", None)))
    with pytest.raises(TypeError):
        # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        v0.conform(MockType(type_info=cast("typesys.TypeVal", None)))
    with pytest.raises(TypeError):
        # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        typesys.unify(typeval, MockType(type_info=cast("typesys.TypeVal", None)))

    # Introducing a third var and unifying
    v2 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    assert v0.unify(v2) is typeval


def test_numeric_unification(context: node.Module, typeval: typesys.TypeVal) -> None:
    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.INTEGER)
    v2 = typesys.InferenceVar.make(context, None, typesys.NumericType.FLOAT)
    v3 = typesys.InferenceVar.make(context, None, typesys.NumericType.SIGNED_INTEGER)

    with pytest.raises(TypeError):
        v1.unify(v2)

    with pytest.raises(TypeError):
        v3.unify(v2)

    v0.unify(v1)
    resolution = v0.resolution()
    assert isinstance(resolution, typesys.InferenceVar)
    assert resolution.numeric_type == typesys.NumericType.INTEGER

    with pytest.raises(TypeError):
        v0.unify(v2)
    with pytest.raises(TypeError):
        v2.unify(v0)
    with pytest.raises(TypeError):
        v0.unify(typeval)

    v4 = typesys.InferenceVar.make(context, None, typesys.NumericType.INTEGER)
    v4.unify(v3)
    assert v4.numeric_type == typesys.NumericType.SIGNED_INTEGER


def test_bind_args(typeval: typesys.TypeVal) -> None:
    values = [MockType(type_info=typeval) for i in range(5)]  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    params = (
        typesys.Parameter(name="a", type_bound=typeval, default=None),
        typesys.Parameter(name="b", type_bound=typeval, default=None),
        typesys.Parameter(name="c", type_bound=typeval, default=values[4]),
        typesys.Parameter(name="d", type_bound=typeval, default=None),
    )

    with pytest.raises(ValueError, match="No value specified for parameter a"):
        typesys.bind_args(params, ())

    with pytest.raises(ValueError, match="No value specified for parameter d"):
        typesys.bind_args(
            params,
            (
                (None, values[0]),
                (None, values[1]),
                (None, values[2]),
            ),
        )

    with pytest.raises(ValueError, match="Non-keyword argument at 3 follows keyword argument"):
        typesys.bind_args(
            params,
            (
                (None, values[0]),
                (None, values[1]),
                ("c", values[2]),
                (None, values[3]),
            ),
        )

    with pytest.raises(ValueError, match="Keyword argument d specified multiple times"):
        typesys.bind_args(
            params,
            (
                (None, values[0]),
                (None, values[1]),
                ("d", values[2]),
                ("d", values[3]),
            ),
        )

    result = typesys.bind_args(
        params,
        (
            (None, values[0]),
            (None, values[1]),
            ("d", values[2]),
        ),
    )
    assert result == {"a": values[0], "b": values[1], "c": values[4], "d": values[2]}


def test_bind_arg_uses_concrete_type_info() -> None:
    """Regression test: bind_arg uses concrete_type_info() not type_info.

    A channel alias has type_info=TYPE_TYPE but concrete_type_info() returns CHANNEL_TYPE.
    Using type_info in bind_arg would cause unification to fail with a TypeError.
    """
    argument = MagicMock()
    argument.concrete_type_info.return_value = clkbuiltins.CHANNEL_TYPE
    parameter = typesys.Parameter(name="ch", type_bound=clkbuiltins.CHANNEL_TYPE, default=None)

    result = typesys.bind_arg(parameter, argument)
    assert result is argument


def test_bind_args_optional_parameter_absent(typeval: typesys.TypeVal) -> None:
    """bind_args produces AbsentOptionalValue when an optional parameter is not supplied."""
    optional_type = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.OPTIONAL,
        arguments={"type": typeval},
    )
    param = typesys.Parameter(name="opt", type_bound=optional_type, default=None, is_optional=True)
    result = typesys.bind_args([param], [])
    assert isinstance(result["opt"], typesys.AbsentOptionalValue)


def test_bind_args_optional_parameter_present(typeval: typesys.TypeVal) -> None:
    """bind_args binds an optional parameter normally when it is supplied."""
    optional_type = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.OPTIONAL,
        arguments={"type": typeval},
    )
    value = MockType(type_info=typeval)
    param = typesys.Parameter(name="opt", type_bound=optional_type, default=None, is_optional=True)
    result = typesys.bind_args([param], [(None, value)])
    assert result["opt"] is value


def test_bind_arg_absent_optional_propagates(typeval: typesys.TypeVal) -> None:
    """bind_arg propagates AbsentOptionalValue for optional parameters without type checking."""
    optional_type = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.OPTIONAL,
        arguments={"type": typeval},
    )
    param = typesys.Parameter(name="opt", type_bound=optional_type, default=None, is_optional=True)
    absent = typesys.AbsentOptionalValue(type_info=optional_type)
    result = typesys.bind_arg(param, absent)
    assert result is absent


def test_immutable_binding_concrete_type_info_delegates_to_value() -> None:
    """Regression test: ImmutableBinding.concrete_type_info() delegates to bound value.

    A resolved channel alias binding has type_info=TYPE_TYPE but concrete_type_info()
    should return CHANNEL_TYPE, derived from the bound value's concrete type.
    """
    resolved_value = MagicMock()
    resolved_value.concrete_type_info.return_value = clkbuiltins.CHANNEL_TYPE

    binding = statement.ImmutableBinding(
        module=MagicMock(),
        cst_node=None,
        doc=None,
        type_info=clkbuiltins.TYPE_TYPE,
        name="TestAlias",
        scope=MagicMock(),
        value=resolved_value,
        typespec=None,
        attributes=None,
    )

    assert binding.concrete_type_info() is clkbuiltins.CHANNEL_TYPE


def test_type_union(  # noqa: PLR0913 test code.
    context: node.Module,
    type_a: typesys.TypeDef,
    type_b: typesys.TypeDef,
    type_c: typesys.TypeDef,
    type_a_or_b: typesys.TypeUnion,
    type_b_or_c: typesys.TypeUnion,
) -> None:
    assert type_a.alternatives() == (type_a,)
    assert type_b.alternatives() == (type_b,)
    assert type_c.alternatives() == (type_c,)
    assert type_a_or_b.alternatives() == (type_a, type_b)
    assert type_b_or_c.alternatives() == (type_b, type_c)

    with pytest.raises(TypeError):
        typesys.unify(type_a_or_b, type_c)

    with pytest.raises(TypeError):
        typesys.unify(type_c, type_a_or_b)

    # Unification with a union should always return the most specific result.
    assert typesys.unify(type_a, type_a_or_b).alternatives() == (type_a,)
    assert typesys.unify(type_a_or_b, type_a).alternatives() == (type_a,)
    assert typesys.unify(type_b, type_a_or_b).alternatives() == (type_b,)
    assert typesys.unify(type_a_or_b, type_b).alternatives() == (type_b,)
    assert typesys.unify(type_a_or_b, type_a_or_b).alternatives() == (type_a, type_b)
    assert typesys.unify(type_a_or_b, type_b_or_c).alternatives() == (type_b,)
    assert typesys.unify(type_b_or_c, type_a_or_b).alternatives() == (type_b,)

    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    # A and B should both unify with A|B
    assert v0.unify(type_a_or_b).alternatives() == (type_a, type_b)
    assert v0.resolution().alternatives() == (type_a, type_b)
    assert v0.unify(type_a).alternatives() == (type_a,)
    assert v0.resolution().alternatives() == (type_a, type_b)
    assert v0.unify(type_b).alternatives() == (type_b,)
    assert v0.resolution().alternatives() == (type_a, type_b)
    # ... but C obviously shouldn't
    with pytest.raises(TypeError):
        v0.unify(type_c)

    # Make a new var and unify with A. Afterward, the var should also unify
    # indirectly with A|B through v0.
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    assert v1.unify(type_a).alternatives() == (type_a,)
    assert v1.unify(type_a_or_b).alternatives() == (type_a,)
    assert v1.unify(v0).alternatives() == (type_a,)
    # ... but it should not inherit compatability with B after unifying with v0.
    with pytest.raises(TypeError):
        v1.unify(type_b)


def test_bind_union_param(
    type_a: typesys.TypeDef,
    type_b: typesys.TypeDef,
    type_c: typesys.TypeDef,
    type_a_or_b: typesys.TypeUnion,
) -> None:
    params = (typesys.Parameter(name="either", type_bound=type_a_or_b, default=None),)

    with pytest.raises(
        TypeError,
        match=re.escape("Type inference failed: uniq.path::TypeA|uniq.path::TypeB and uniq.path::TypeC are disjoint"),
    ):
        typesys.bind_args(params, ((None, MockType(type_info=type_c)),))

    value_a = MockType(type_info=type_a)
    result = typesys.bind_args(params, ((None, value_a),))
    assert result == {"either": value_a}

    value_b = MockType(type_info=type_b)
    result = typesys.bind_args(params, ((None, value_b),))
    assert result == {"either": value_b}


def test_invalid_union(
    type_c: typesys.TypeDef,
    type_a_or_b: typesys.TypeUnion,
) -> None:
    # We shouldn't be able to construct a TypeUnion that includes another
    # TypeUnion as a member.
    with pytest.raises(TypeError):
        typesys.TypeUnion.make(types=(type_c, type_a_or_b))
