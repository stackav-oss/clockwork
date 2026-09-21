# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to snapshot policy configurations.

Provides access to snapshot policies (TakeSnapshots, SnapshotOnce) defined
in the Clockwork standard library.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import compiler, importer_registry, policy
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import node


@dataclass
class SnapshotEntities:
    """Snapshot policy classes."""

    take_snapshots_policy: policy.PolicyClass
    snapshot_once_policy: policy.PolicyClass


@final
class SnapshotRegistry(Context):
    """Registry for snapshot policy entities."""

    def __init__(self, name: str | None, entities: SnapshotEntities) -> None:
        """Create a new snapshot config registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: SnapshotRegistry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = f"Snapshot registry has conflicting entities: {self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            raise RuntimeError(msg)

    def get_entities(self) -> SnapshotEntities:
        """Get all snapshot policy entities, compiling modules if needed."""
        return self.entities


def _load_snapshot_entities(compiler_context: CompilerContext) -> SnapshotEntities:
    """Load snapshot policy entities by compiling std::snapshot module."""
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)

    snapshot_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("std/snapshot.clk")),
        importer_reg.importer,
    )
    compiler_context.import_from(snapshot_module.context)

    take_snapshots_policy = _extract_policy(snapshot_module, "TakeSnapshots")
    snapshot_once_policy = _extract_policy(snapshot_module, "SnapshotOnce")

    return SnapshotEntities(
        take_snapshots_policy=take_snapshots_policy,
        snapshot_once_policy=snapshot_once_policy,
    )


def _extract_policy(module: node.Module, policy_name: str) -> policy.PolicyClass:
    """Extract a policy definition from a module."""
    policy_def = module.inner_scope.lookup(policy_name)
    if policy_def is None:
        msg = f"Snapshot policy '{policy_name}' not found in module {module.module_id}"
        raise RuntimeError(msg)
    if not isinstance(policy_def, policy.PolicyDef):
        msg = f"Entity '{policy_name}' in module {module.module_id} is not a PolicyDef"
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is correct for a compiler internal error)
    return policy_def.get_resolved()


class SnapshotRegistryKey(ContextKey[SnapshotRegistry]):
    """CompilerContext Key for Snapshot Config Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> SnapshotRegistry:
        """Create a default instance of a Snapshot Config Registry."""
        return SnapshotRegistry(compiler_context.name, _load_snapshot_entities(compiler_context))


SNAPSHOT_REGISTRY_KEY: Final = SnapshotRegistryKey("SnapshotConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> SnapshotEntities:
    """Get all snapshot policy entities."""
    registry = compiler_context[SNAPSHOT_REGISTRY_KEY]
    return registry.get_entities()


def get_take_snapshots_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the TakeSnapshots policy definition."""
    return get_entities(compiler_context).take_snapshots_policy


def get_snapshot_once_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the SnapshotOnce policy definition."""
    return get_entities(compiler_context).snapshot_once_policy
