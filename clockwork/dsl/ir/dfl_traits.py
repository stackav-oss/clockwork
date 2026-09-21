# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""DFL Traits Loading.

Ensures std/traits.clk is compiled and trait definitions are available.

This module provides access to the standard DFL traits (Add, Sub, Mul, etc.)
for code that needs to programmatically check or reference trait definitions.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import compiler, dfl_types, importer_registry
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import node


@dataclass
class TraitsEntities:
    """Core trait definitions from std/traits.clk.

    Attributes:
        add_trait: The Add trait for '+' operator.
        sub_trait: The Sub trait for '-' operator.
        mul_trait: The Mul trait for '*' operator.
        div_trait: The Div trait for '/' operator.
        rem_trait: The Rem trait for '%' operator.
        neg_trait: The Neg trait for unary '-'.
        pos_trait: The Pos trait for unary '+'.
        abs_trait: The Abs trait for '|x|'.
        ord_trait: The Ord trait for comparison operators.
        eq_trait: The Eq trait for equality operators.
        and_trait: The And trait for logical 'and'.
        or_trait: The Or trait for logical 'or'.
        not_trait: The Not trait for logical 'not'.
    """

    add_trait: dfl_types.TraitDef
    sub_trait: dfl_types.TraitDef
    mul_trait: dfl_types.TraitDef
    div_trait: dfl_types.TraitDef
    rem_trait: dfl_types.TraitDef
    neg_trait: dfl_types.TraitDef
    pos_trait: dfl_types.TraitDef
    abs_trait: dfl_types.TraitDef
    ord_trait: dfl_types.TraitDef
    eq_trait: dfl_types.TraitDef
    and_trait: dfl_types.TraitDef
    or_trait: dfl_types.TraitDef
    not_trait: dfl_types.TraitDef


@final
class TraitsRegistry(Context):
    """Registry for standard trait entities.

    Stores references to all standard DFL traits from std/traits.clk.
    Use ensure_traits_loaded() to obtain an instance.
    """

    def __init__(self, name: str | None, entities: TraitsEntities, module: node.Module) -> None:
        """Create a new traits registry.

        Args:
            name: The registry name (for debugging).
            entities: The loaded trait entities.
            module: The module containing the traits.
        """
        self.name = name
        self.entities = entities
        self.module = module

    @override
    def import_from(self, other: TraitsRegistry) -> None:
        """Merge another registry into this one.

        Args:
            other: The registry to merge.

        Raises:
            RuntimeError: If the registries have conflicting entities.
        """
        if self.entities != other.entities:
            msg = f"Traits registry has conflicting entities.\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)


def _load_traits_entities(compiler_context: CompilerContext) -> tuple[TraitsEntities, node.Module]:
    """Load trait entities by compiling std/traits.clk.

    Args:
        compiler_context: The compiler context to use.

    Returns:
        A tuple of (TraitsEntities, Module).

    Raises:
        RuntimeError: If no importer is registered or traits cannot be loaded.
    """
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)

    traits_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("std/traits.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(traits_module.context)

    return TraitsEntities(
        add_trait=_extract_trait(traits_module, "Add"),
        sub_trait=_extract_trait(traits_module, "Sub"),
        mul_trait=_extract_trait(traits_module, "Mul"),
        div_trait=_extract_trait(traits_module, "Div"),
        rem_trait=_extract_trait(traits_module, "Rem"),
        neg_trait=_extract_trait(traits_module, "Neg"),
        pos_trait=_extract_trait(traits_module, "Pos"),
        abs_trait=_extract_trait(traits_module, "Abs"),
        ord_trait=_extract_trait(traits_module, "Ord"),
        eq_trait=_extract_trait(traits_module, "Eq"),
        and_trait=_extract_trait(traits_module, "And"),
        or_trait=_extract_trait(traits_module, "Or"),
        not_trait=_extract_trait(traits_module, "Not"),
    ), traits_module


def _extract_trait(module: node.Module, trait_name: str) -> dfl_types.TraitDef:
    """Extract a trait definition from a module.

    Args:
        module: The module to extract from.
        trait_name: The trait name to look up.

    Returns:
        The TraitDef for the specified trait.

    Raises:
        RuntimeError: If the trait is not found or is not a TraitDef.
    """
    trait_def = module.inner_scope.lookup(trait_name)
    if trait_def is None:
        msg = f"Trait '{trait_name}' not found in module {module.module_id}"
        raise RuntimeError(msg)
    if not isinstance(trait_def, dfl_types.TraitDef):
        msg = f"Entity '{trait_name}' in module {module.module_id} is not a TraitDef"
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is correct for compiler internal error)
    return trait_def


class TraitsRegistryKey(ContextKey[TraitsRegistry]):
    """CompilerContext key for the traits registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> TraitsRegistry:
        """Create a default instance of the traits registry.

        Args:
            compiler_context: The compiler context.

        Returns:
            A new TraitsRegistry with all traits loaded.
        """
        entities, module = _load_traits_entities(compiler_context)
        return TraitsRegistry(compiler_context.name, entities, module)


TRAITS_REGISTRY_KEY: Final = TraitsRegistryKey("TraitsRegistryKey")


def ensure_traits_loaded(compiler_context: CompilerContext) -> TraitsRegistry:
    """Ensure std/traits.clk is loaded and return the registry.

    This is idempotent - calling multiple times returns the same registry.
    Use this function when you need access to trait definitions for
    programmatic type checking or trait registration.

    Args:
        compiler_context: The compiler context to use.

    Returns:
        The TraitsRegistry with all standard traits loaded.
    """
    return compiler_context[TRAITS_REGISTRY_KEY]


def get_entities(compiler_context: CompilerContext) -> TraitsEntities:
    """Get all trait entities.

    Args:
        compiler_context: The compiler context.

    Returns:
        The TraitsEntities containing all standard trait definitions.
    """
    return ensure_traits_loaded(compiler_context).entities
