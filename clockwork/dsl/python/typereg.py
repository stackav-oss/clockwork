# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Provides facilities to convert Clockwork IR types to corresponding Python types."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import typesys


@dataclass
class PySymbol:
    """A python symbol."""

    import_spec: str | None
    class_name: str


@dataclass
class PyType:
    """Defines a python type."""

    repo: str
    import_spec: str
    class_name: str


class PyTypeRegistry(Context):
    """Compiler Context for Python type mappings."""

    def __init__(self) -> None:
        """Create a new, empty type registry."""
        self.py_type_registry: dict[str, PyType] = {}

    @override
    def import_from(self, other: PyTypeRegistry) -> None:
        """Combine this context with items from another.

        Raises:
            ValueError: If a type already exists with a different object identity.
        """
        for key, py_type in other.py_type_registry.items():
            if key in self.py_type_registry and self.py_type_registry[key] is not py_type:
                msg = f"Type {key} has conflicting registrations"
                raise ValueError(msg)
            self.py_type_registry[key] = py_type


class PyTypeRegistryKey(ContextKey[PyTypeRegistry]):
    """Compiler context key for Python type registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> PyTypeRegistry:
        """Create a default instance of the context with built-in types."""
        return PyTypeRegistry()


PY_TYPE_REGISTRY_KEY: Final = PyTypeRegistryKey("PyTypeRegistry")


def register_py_type(
    context: CompilerContext,
    clk_type: typesys.TypeVal,
    py_type: PyType,
) -> None:
    """Registers the Py type information for a given Clockwork type.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The Clockwork type that needs to be mapped to a Py type.
        py_type: The corresponding Py type descriptor.

    Raises:
        ValueError: If the Clockwork type is already registered with a different Python type.
    """
    registry = context[PY_TYPE_REGISTRY_KEY]
    try:
        existing_type = registry.py_type_registry[clk_type.value_key()]
        if existing_type is not py_type:
            msg = f"Type {clk_type} already registered as {existing_type}"
            raise ValueError(msg)
    except KeyError:
        pass
    registry.py_type_registry[clk_type.value_key()] = py_type


def get_py_type(context: CompilerContext, fqn: str) -> PyType:
    """Gets the Py type information for a given Clockwork type value.

    Args:
        context: Compiler context containing the type registry.
        fqn: The fully qualified name of the Clockwork type for which the Py type info is required.

    Returns:
        The corresponding Py type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a Py type.
    """
    registry = context[PY_TYPE_REGISTRY_KEY]
    try:
        return registry.py_type_registry[fqn]
    except KeyError:
        msg = (
            f"No Py type registered for Clockwork type {fqn}.\n"
            "If this is a user defined type, it might be missing from a `py_target`."
        )
        raise TypeError(msg) from None
