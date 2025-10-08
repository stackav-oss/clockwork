# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for compiler context."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from clockwork.dsl.compiler_context import CompilerContext, ContextKey
from typing_extensions import override


@dataclass
class _Context1:
    value: Any = field(default_factory=object)
    import_called: int = 0

    def import_from(self, other: _Context1) -> None:
        """Combine this context with items from another."""
        assert self.value is not other.value
        self.import_called += 1


class _Key1(ContextKey[_Context1]):
    @override
    def make_default(self, compiler_context: CompilerContext) -> _Context1:
        """Create a default (empty) instance of the context."""
        return _Context1()


def test_context() -> None:
    context1 = CompilerContext()
    key1 = _Key1("key1")
    key2 = _Key1("key1")
    assert context1[key1] is not context1[key2]
    assert context1[key1].import_called == 0
    context2 = CompilerContext()
    context2[key1]
    assert len(context2._contexts) == 1
    context2.import_from(context1)
    assert len(context2._contexts) == 2
    assert context2[key1].import_called == 1
    assert context2[key2].import_called == 1
    assert context1[key1].import_called == 0
    assert context1[key2].import_called == 0
