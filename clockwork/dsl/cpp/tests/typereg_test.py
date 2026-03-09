# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for typereg."""

from decimal import Decimal
from typing import Final
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import types
from clockwork.dsl.cpp.context import Header, SystemHeader
from clockwork.dsl.cpp.typereg import (
    _CPP_INT64_TYPE,
    CPP_TYPE_REGISTRY_KEY,
    get_cpp_template,
    get_cpp_type,
    register_cpp_template,
    register_cpp_type,
)
from clockwork.dsl.ir import clkbuiltins, primitive, typesys


@pytest.fixture()
def context() -> CompilerContext:
    """Create a fresh compiler context for tests."""
    return CompilerContext()


MOCK_TYPE: Final = typesys.TypeDef(name="MockType", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
MOCK_TYPE_CPP: Final = types.CppType(
    includes=[Header("repo", "mock.hh")],
    cpp_namespace="MockNamespace",
    type_name="MockType",
)


MOCK_GENERIC_TYPE: Final = typesys.GenericTypeDef(
    name="MockGeneric",
    scope=MagicMock(),
    type_info=clkbuiltins.TYPE_TYPE,
    parameters=[typesys.Parameter(name="T", type_bound=clkbuiltins.TYPE_TYPE, default=None)],
)

MOCK_GENERIC_TYPE_CPP: Final = types.CppTemplate(
    includes=[SystemHeader("mockgeneric")],
    cpp_namespace="MockNamespace",
    template_name="MockGenericType",
)


@pytest.fixture()
def mock_instantiated_type(context: CompilerContext) -> typesys.Instantiation:
    """Create a mock instantiated type for testing."""
    register_cpp_template(context, MOCK_GENERIC_TYPE, MOCK_GENERIC_TYPE_CPP)
    return typesys.Instantiation(
        instantiates=MOCK_GENERIC_TYPE,
        arguments={"T": clkbuiltins.INT64},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def test_register_cpp_type(context: CompilerContext) -> None:
    """Test registering a C++ type."""
    register_cpp_type(context, MOCK_TYPE, MOCK_TYPE_CPP)
    result = get_cpp_type(context, MOCK_TYPE)
    assert result == MOCK_TYPE_CPP


def test_register_cpp_type_existing_error(context: CompilerContext) -> None:
    """Registering an existing type should raise ValueError."""
    register_cpp_type(context, MOCK_TYPE, MOCK_TYPE_CPP)

    different_cpp_type = types.CppType(
        includes=[Header("repo", "different.hh")],
        cpp_namespace="DifferentNamespace",
        type_name="DifferentType",
    )

    with pytest.raises(ValueError, match=r"already registered as"):
        register_cpp_type(context, MOCK_TYPE, different_cpp_type)


def test_register_same_type_object_identity(context: CompilerContext) -> None:
    """Test registering the same type object twice succeeds."""
    register_cpp_type(context, MOCK_TYPE, MOCK_TYPE_CPP)
    # This should not raise since we're using the same object
    register_cpp_type(context, MOCK_TYPE, MOCK_TYPE_CPP)
    assert get_cpp_type(context, MOCK_TYPE) is MOCK_TYPE_CPP


def test_get_cpp_type_not_registered(context: CompilerContext) -> None:
    """Test getting C++ type info for an unregistered type should raise TypeError."""
    unregistered_clk_type = typesys.TypeDef(name="Unregistered", scope=MagicMock(), type_info=clkbuiltins.TYPE_TYPE)
    with pytest.raises(TypeError):
        get_cpp_type(context, unregistered_clk_type)


def test_register_cpp_template(context: CompilerContext) -> None:
    """Test registering a C++ template."""
    register_cpp_template(context, MOCK_GENERIC_TYPE, MOCK_GENERIC_TYPE_CPP)
    result = get_cpp_template(context, MOCK_GENERIC_TYPE)
    assert result == MOCK_GENERIC_TYPE_CPP


def test_register_cpp_template_existing_error(context: CompilerContext) -> None:
    """Registering an existing type should raise ValueError."""
    register_cpp_template(context, MOCK_GENERIC_TYPE, MOCK_GENERIC_TYPE_CPP)

    different_cpp_template = types.CppTemplate(
        includes=[SystemHeader("different")],
        cpp_namespace="DifferentNamespace",
        template_name="DifferentTemplate",
    )

    with pytest.raises(ValueError, match=r"already registered as"):
        register_cpp_template(context, MOCK_GENERIC_TYPE, different_cpp_template)


def test_register_same_template_object_identity(context: CompilerContext) -> None:
    """Test registering the same template object twice succeeds."""
    register_cpp_template(context, MOCK_GENERIC_TYPE, MOCK_GENERIC_TYPE_CPP)
    # This should not raise since we're using the same object
    register_cpp_template(context, MOCK_GENERIC_TYPE, MOCK_GENERIC_TYPE_CPP)
    assert get_cpp_template(context, MOCK_GENERIC_TYPE) is MOCK_GENERIC_TYPE_CPP


def test_get_cpp_template_not_registered(context: CompilerContext) -> None:
    """Test getting C++ type info for an unregistered type should raise TypeError."""
    unregistered_clk_template = typesys.GenericTypeDef(
        name="Unregistered",
        scope=MagicMock(),
        type_info=clkbuiltins.TYPE_TYPE,
        parameters=[typesys.Parameter(name="T", type_bound=clkbuiltins.TYPE_TYPE, default=None)],
    )
    with pytest.raises(TypeError):
        get_cpp_template(context, unregistered_clk_template)


def test_get_cpp_type_template_instantiation(
    context: CompilerContext, mock_instantiated_type: typesys.Instantiation
) -> None:
    """Test getting C++ type for template instantiation."""
    result = get_cpp_type(context, mock_instantiated_type)
    assert isinstance(result, types.CppTemplateType)
    assert result.template_name == "MockGenericType"
    assert result.arguments
    assert len(result.arguments) == 1
    # The registry already has INT64 registered by default
    assert result.arguments[0] is get_cpp_type(context, clkbuiltins.INT64)


def test_builtin_types_in_default_context(context: CompilerContext) -> None:
    """Test that built-in types are available in a fresh context."""
    # Test primitive types
    int64_type = get_cpp_type(context, clkbuiltins.INT64)
    assert int64_type is _CPP_INT64_TYPE

    bool_type = get_cpp_type(context, clkbuiltins.BOOL)
    assert isinstance(bool_type, types.CppType)
    assert bool_type.type_name == "bool"

    # Test template types
    array_template = get_cpp_template(context, clkbuiltins.FIXED_ARRAY)
    assert array_template.template_name == "array"
    assert array_template.cpp_namespace == "std"


def test_context_import_from(context: CompilerContext) -> None:
    """Test that import_from correctly merges registries."""
    # Create a second context with a custom type
    other_context = CompilerContext()
    register_cpp_type(other_context, MOCK_TYPE, MOCK_TYPE_CPP)

    # Import from other context
    context.import_from(other_context)

    # Verify type was imported
    assert get_cpp_type(context, MOCK_TYPE) is MOCK_TYPE_CPP

    # Verify built-ins are maintained
    assert get_cpp_type(context, clkbuiltins.INT64) is _CPP_INT64_TYPE


def test_context_import_from_with_conflict() -> None:
    """Test that import_from raises error on object identity conflicts."""
    context1 = CompilerContext()
    context2 = CompilerContext()

    # Create a conflicting type with different identity but same key
    different_cpp_type = types.CppType(
        includes=[Header("repo", "different.hh")],
        cpp_namespace="DifferentNamespace",
        type_name="DifferentType",
    )

    # Register in second context
    registry2 = context2[CPP_TYPE_REGISTRY_KEY]
    registry2.cpp_type_registry[MOCK_TYPE.value_key()] = different_cpp_type

    # Register in first context
    register_cpp_type(context1, MOCK_TYPE, MOCK_TYPE_CPP)

    # Import should fail with conflict error
    with pytest.raises(ValueError, match=r"has conflicting registrations"):
        context1.import_from(context2)


def test_get_cpp_type_unbound_instantiation(context: CompilerContext) -> None:
    """Test that attempting to get C++ type for unbound instantiation raises error."""
    # Prepare an incompletely instantiated mock Clockwork type
    incomplete_generic_type = typesys.GenericTypeDef(
        name="AType",
        parameters=[
            typesys.Parameter("T1", clkbuiltins.TYPE_TYPE, default=None),
            typesys.Parameter("T2", clkbuiltins.TYPE_TYPE, default=None),
        ],
        scope=MagicMock(),
        type_info=clkbuiltins.TYPE_TYPE,
    )

    incomplete_instantiated_type = typesys.Instantiation(
        instantiates=incomplete_generic_type,
        arguments={"T1": clkbuiltins.INT64},
        type_info=clkbuiltins.TYPE_TYPE,
    )

    register_cpp_template(context, incomplete_generic_type, MOCK_GENERIC_TYPE_CPP)
    with pytest.raises(TypeError, match=r"Attempt to instantiate without fully-bound parameters: .*? missing T2"):
        get_cpp_type(context, incomplete_instantiated_type)


def test_register_cpp_template_with_non_generic(context: CompilerContext) -> None:
    """Test that attempting to register non-generic as template raises error."""
    with pytest.raises(ValueError, match=r"Type being registered as a template does not have generic parameters\."):
        register_cpp_template(context, MOCK_TYPE, MOCK_GENERIC_TYPE_CPP)


def test_get_cpp_template_with_non_generic(context: CompilerContext) -> None:
    """Test that attempting to get template for non-generic raises error."""
    with pytest.raises(ValueError, match=r"Cannot get a Cpp template for a type that is not generic\."):
        get_cpp_template(context, MOCK_TYPE)


def test_cpp_type_info_fqn_with_namespace_and_template_args(context: CompilerContext) -> None:
    """Test template type rendering with namespace and arguments."""
    int32 = get_cpp_type(context, clkbuiltins.INT32)
    float32 = get_cpp_type(context, clkbuiltins.FLOAT32)
    cpp_type_info_with_args = types.CppTemplate(
        includes=[Header("repo", "header")],
        cpp_namespace="SomeNamespace",
        template_name="SomeType",
    ).instantiate([int32, float32])
    expected_fqn = "::SomeNamespace::SomeType<int32_t, float>"
    assert cpp_type_info_with_args.render("") == expected_fqn


def test_cpp_type_info_fqn_without_namespace() -> None:
    """Test type rendering without namespace."""
    cpp_type_info_without_ns = types.CppType(
        includes=[],
        cpp_namespace=None,
        type_name="NoNamespaceType",
    )
    expected_fqn = "NoNamespaceType"
    assert cpp_type_info_without_ns.render("") == expected_fqn


def test_get_cpp_type_invalid_type(context: CompilerContext) -> None:
    """Test that passing an object that's not a TypeDef or Instantiation raises TypeError."""
    not_a_clk_type = MagicMock(name="NotATypeDefOrInstantiation")
    with pytest.raises(TypeError, match=r"Cannot construct C\+\+ type corresponding to"):
        get_cpp_type(context, not_a_clk_type)


def test_get_cpp_type_decimal_literal_template_argument(
    context: CompilerContext, mock_instantiated_type: typesys.Instantiation
) -> None:
    """Test the instantiation with a DecimalLiteral as an argument."""
    # Prepare a mock DecimalLiteral
    decimal_literal = primitive.DecimalLiteral(
        value=Decimal(42),
        module=MagicMock(),
        cst_node=None,
        type_info=clkbuiltins.INT32,
    )

    # Mock the argument to include a DecimalLiteral
    mock_instantiated_type.arguments["T"] = decimal_literal  # pyright: ignore[reportIndexIssue]

    # Use a patch to control the return value of literal_to_cpp
    result = get_cpp_type(context, mock_instantiated_type)
    assert isinstance(result, types.CppTemplateType)

    assert result.arguments == [types.CppValue(None, "42")]


def test_get_cpp_type_unhandled_template_argument_error(
    context: CompilerContext, mock_instantiated_type: typesys.Instantiation
) -> None:
    """Test an unhandled argument type when instantiating raises NotImplementedError."""
    # Prepare an unhandled type
    mock_unhandled_type = MagicMock(name="UnhandledArgumentType")

    # Mock the argument to include the unhandled type
    mock_instantiated_type.arguments["T"] = mock_unhandled_type  # pyright: ignore[reportIndexIssue]

    with pytest.raises(NotImplementedError, match=r"Cannot construct C\+\+ template argument for"):
        get_cpp_type(context, mock_instantiated_type)
