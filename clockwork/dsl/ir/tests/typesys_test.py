# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Test the Cog IR module."""

from typing import cast

import pytest
from clockwork.dsl.ir import node, typesys


class MockType(typesys.TypeVal, typesys.ObjectIdentityValue):
    """Mock type."""


@pytest.fixture()
def typeval() -> typesys.TypeVal:
    return MockType(type_info=cast("typesys.TypeVal", None))


@pytest.fixture()
def typedef(typeval: typesys.TypeVal) -> typesys.TypeDef:
    return typesys.TypeDef(
        name="SomeType",
        scope=node.Scope(parent=None, uniq_path="uniq.path", module_id_for_errors=None),
        type_info=typeval,
    )


@pytest.fixture()
def context() -> node.Module:
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
    assert v0._id != v1._id  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert v0 != v1
    assert v0.resolution() is v0
    assert v1.resolution() is v1


def test_inferencevar_resolution(context: node.Module, typeval: typesys.TypeVal) -> None:
    v0 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1 = typesys.InferenceVar.make(context, None, typesys.NumericType.NONE)
    v1._resolution = v0  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert v0.resolution() is v0
    assert v1.resolution() is v0
    v0._resolution = typeval  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
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
        v0.unify(MockType(type_info=cast("typesys.TypeVal", None)))
    with pytest.raises(TypeError):
        v0.conform(MockType(type_info=cast("typesys.TypeVal", None)))
    with pytest.raises(TypeError):
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
