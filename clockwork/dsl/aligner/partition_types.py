# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Partition metadata types shared by aligner analysis and codegen."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class PartitionComponent:
    """One connected component in a partitioned residual suffix."""

    component_id: int
    level_indices: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class PartitionPoint:
    """A residual suffix split starting at a flat join-plan level."""

    partition_id: int
    start_level_index: int
    scope_level_indices: tuple[int, ...]
    components: tuple[PartitionComponent, ...]


@dataclass(frozen=True, slots=True)
class PartitionPlan:
    """All profitable residual partitions for a codegen plan."""

    partitions: tuple[PartitionPoint, ...]

    def partition_starting_at(self, start_level_index: int) -> PartitionPoint | None:
        """Return the partition beginning at ``start_level_index``, if any."""
        for partition in self.partitions:
            if partition.start_level_index == start_level_index:
                return partition
        return None

    def partition_for_scope(self, scope_level_indices: tuple[int, ...]) -> PartitionPoint | None:
        """Return the partition for an exact residual scope, if any."""
        for partition in self.partitions:
            if partition.scope_level_indices == scope_level_indices:
                return partition
        return None
