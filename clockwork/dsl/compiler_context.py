# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Pseudo-global compiler context."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Generic, Protocol, TypeVar, cast

from typing_extensions import Self, override


class Context(Protocol):
    """Type protocol for compiler context values."""

    def import_from(self, other: Self) -> None:
        """Combine this context with items from another."""


ContextType = TypeVar("ContextType", bound=Context)


class ContextKey(Generic[ContextType], ABC):
    """Sentinel values for CPU context.

    These are meant to serve as unique keys in the CPU context.  The name is for
    display purposes only; two keys with the same name are nevertheless
    different keys.
    """

    def __init__(self, name: str) -> None:
        """Construct a new ContextKey."""
        self.name = name

    @override
    def __str__(self) -> str:
        """Human-readable string."""
        return self.name

    @abstractmethod
    def make_default(self, compiler_context: CompilerContext) -> ContextType:
        """Create a default (empty) instance of the context."""


class CompilerContext:
    """General-purpose context mechanism.

    This class implements a way to accumulate pseudo-global context information
    within the compiler.  It is pseudo-global in that different pieces of
    context don't need to be explicitly and individually passed through the call
    stack, but it's not truly global because everything is cleanly encapsulated
    in an instance of this class.

    It's also designed to allow context to be associated with particular scopes
    and then hierarchically merged, while retaining a distinct version of the
    context for each scope.  This class doesn't dictate what those scopes are or
    how they're associated, but the intention is for each Clockwork module to
    have its own context, which is the merged context of every module it imports
    plus any context it adds.

    This overall mechanism ensures that each module gets its own consistent view
    of context which is self-contained: It only depends on itself and the
    modules it imports, transitively.  It's own context cannot be affected by
    modules that import it, but modules that import it can combine its context
    with context of other modules.

    This makes context particularly useful for accumulating information about
    the overall system, such as connections between entities defined in
    different modules, where those connections are made in higher-level system
    definition modules that import a large graph of modules that don't
    necessarily import each other.

    Context is associated with unique ContextKey, which define both a key type
    and a value type, and which implement merge logic specific to the type of
    context but using a uniform API.  This allows this whole thing to operate as
    if it were somewhat type-safe, even though under the hood it's a
    loosely-typed dictionary.  No type casting is required in user code when
    accessing context; mypy can infer types based on the key used to retrieve
    the values.
    """

    def __init__(self, name: str | None = None) -> None:
        """Create new, empty context."""
        self._contexts: dict[ContextKey[Any], Any] = {}
        self.name = name

    def __getitem__(self, key: ContextKey[ContextType]) -> ContextType:
        """Retrieve context by key.

        If the key is not currently present, a default instance will be created.
        """
        try:
            return cast("ContextType", self._contexts[key])
        except KeyError:
            pass
        result = key.make_default(self)
        self._contexts[key] = result
        return result

    def import_from(self, other: CompilerContext) -> None:
        """Merge another context into this one.

        This does not affect the other context.

        Raises:
            Any errors raised by the relevant context type's import_from.
        """
        for key, value in other._contexts.items():
            ours = self[key]
            ours.import_from(value)
