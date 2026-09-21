# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Importer registry for compiler context."""

from __future__ import annotations

from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import node


@final
class ImporterRegistry(Context):
    """Importer Registry.

    Stores a singleton Importer instance to avoid having different parts of the code
    instantiate redundant importers, which would bypass the module cache that's inside
    the importer.
    """

    def __init__(self, name: str | None) -> None:
        """Create a new importer registry."""
        self.name = name
        self.importer: node.Importer | None = None

    @override
    def import_from(self, other: ImporterRegistry) -> None:
        """Combine this registry with an importer from another registry.

        Raises:
            RuntimeError: If both registries have importers and they are different instances.
        """
        if other.importer is not None:
            if self.importer is not None and self.importer is not other.importer:
                msg = f"Importer registry has conflicting importers: {self.importer} vs {other.importer}\nWhen merging {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.importer = other.importer


class ImporterRegistryKey(ContextKey[ImporterRegistry]):
    """CompilerContext Key for Importer Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ImporterRegistry:
        """Create a default instance of an Importer Registry."""
        return ImporterRegistry(compiler_context.name)


IMPORTER_REGISTRY_KEY: Final = ImporterRegistryKey("ImporterRegistryKey")
