# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tachyon Structured Type Layout Registry."""

from __future__ import annotations

from typing import TYPE_CHECKING, Final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.serialization import tachyon_reg

if TYPE_CHECKING:
    from clockwork.dsl.ir import typesys
    from clockwork.dsl.serialization import tachyon_layout


class LayoutRegistry(Context):
    """Compiler Context for Tachyon structured type layouts."""

    def __init__(self) -> None:  # pyright: ignore[reportMissingSuperCall] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Create a new, empty Layout registry."""
        self.layout_registry: dict[tachyon_reg.TypeKey, tachyon_layout.Layout] = {}

    def import_from(self, other: LayoutRegistry) -> None:
        """Combine this context with items from another.

        Raises:
            RuntimeError: If a type already exists with a different layout.
        """
        for key, layout in other.layout_registry.items():
            if key in self.layout_registry and self.layout_registry[key] != layout:
                msg = f"Type {key} has conflicting layouts: {self.layout_registry[key]} vs {layout}"
                raise RuntimeError(msg)
            self.layout_registry[key] = layout


class LayoutRegistryKey(ContextKey[LayoutRegistry]):
    """Compiler context key for Tachyon structured type layout registry."""

    def make_default(self, compiler_context: CompilerContext) -> LayoutRegistry:  # noqa: ARG002 (conform to supertype)
        """Create a default instance of the layout registry."""
        return LayoutRegistry()


LAYOUT_REGISTRY_KEY: Final = LayoutRegistryKey("LayoutRegistry")


def layout_for_type(
    compiler_context: CompilerContext,
    typ: typesys.TypeVal,
) -> tachyon_layout.Layout | None:
    """Determine layout for a structured type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to look up.
        _registry: Internal use only - registry to use instead of context.

    Returns:
        The layout if found, else None
    """
    registry = compiler_context[LAYOUT_REGISTRY_KEY]

    return registry.layout_registry.get(typ.value_key())


def register_structured_type(
    compiler_context: CompilerContext, typ: typesys.TypeVal, layout: tachyon_layout.Layout
) -> None:
    """Register layout and size/alignment constraint for a non-generic structured type.

    Args:
        compiler_context: Compiler context containing the registry.
        typ: The type to register; must be fully instantiated.
        layout: The layout for the type.
        _registry: Internal use only - registry to use instead of context.

    Raises:
        RuntimeError if the type is not fully instantiated or is already registered with different constraint
    """
    registry = compiler_context[LAYOUT_REGISTRY_KEY]

    if typ.generic_parameters():
        msg = f"Attempt to register generic type {typ}"
        raise RuntimeError(msg)
    existing = layout_for_type(compiler_context, typ)
    if existing:
        msg = f"Attempt to register layout {layout} for type {typ} that is already registered."
        raise RuntimeError(msg)
    registry.layout_registry[typ.value_key()] = layout
    constraint = tachyon_reg.FieldConstraint(size=layout.size, alignment=layout.alignment)
    tachyon_reg.register_type(compiler_context, typ, constraint)
