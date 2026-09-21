# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Shared utilities for loading and extracting metrics policy entities from CLK modules."""

from __future__ import annotations

from typing import TYPE_CHECKING

from clockwork.dsl.ir import importer_registry, policy
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from pathlib import Path

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir import node


def load_policy_module(compiler_context: CompilerContext, clk_module_path: Path) -> node.Module:
    """Load a CLK policy module via the filesystem importer.

    Args:
        compiler_context: The active compiler context.
        clk_module_path: Path to the .clk file relative to the CLK repo root.

    Returns:
        The compiled module.

    Raises:
        RuntimeError: If no importer is registered in the compiler context.
        TypeError: If the registered importer is not a FilesystemImporter.
    """
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)
    importer = importer_reg.importer
    if not isinstance(importer, FilesystemImporter):
        msg = f"Expected FilesystemImporter, got {type(importer).__name__}"
        raise TypeError(msg)
    module_id = ModuleID.from_path(CLK_REPO, clk_module_path)
    # We do not import and use the compiler directly to avoid circular dependencies.
    return importer.compile_fn(module_id, importer)


def extract_policy_class(module: node.Module, policy_name: str) -> policy.PolicyClass:
    """Extract a named PolicyClass from a compiled CLK module.

    Args:
        module: The compiled CLK module.
        policy_name: The name of the policy definition to extract.

    Returns:
        The resolved PolicyClass.

    Raises:
        RuntimeError: If the named entity is not found or is not a PolicyDef.
    """
    policy_def = module.inner_scope.lookup(policy_name)
    if policy_def is None:
        msg = f"Policy '{policy_name}' not found in module {module.module_id}"
        raise RuntimeError(msg)
    if not isinstance(policy_def, policy.PolicyDef):
        msg = f"Entity '{policy_name}' in module {module.module_id} is not a PolicyDef"
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is correct for a compiler internal error)
    return policy_def.get_resolved()
