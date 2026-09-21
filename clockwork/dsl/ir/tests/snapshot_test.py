# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for snapshot policies."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.composition import pdf
from clockwork.dsl.ir import compiler, policy, pubsub
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_snapshot_policies(fs_importer: FilesystemImporter) -> None:
    """Test that snapshot policies can be instantiated and applied to cog endpoints."""
    system_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/test_snapshot_policy.clk")), fs_importer
    )
    snapshot_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("std/snapshot.clk")), fs_importer
    )

    # Get policy definitions
    take_snapshots_def = snapshot_policy_module.inner_scope.lookup("TakeSnapshots")
    assert isinstance(take_snapshots_def, policy.PolicyDef)
    take_snapshots_class = take_snapshots_def.get_resolved()

    snapshot_once_def = snapshot_policy_module.inner_scope.lookup("SnapshotOnce")
    assert isinstance(snapshot_once_def, policy.PolicyDef)
    snapshot_once_class = snapshot_once_def.get_resolved()

    # Test that we can lookup all TakeSnapshots policies (should be 2 in test_snapshot_policy.clk)
    all_take_snapshots = list(policy.lookup_all_policies(system_module, take_snapshots_class))
    assert len(all_take_snapshots) == 2

    # Test that we can lookup all SnapshotOnce policies (should be 1 in test_snapshot_policy.clk)
    all_snapshot_once = list(policy.lookup_all_policies(system_module, snapshot_once_class))
    assert len(all_snapshot_once) == 1

    # Verify TakeSnapshots policies have expected structure
    for snapshot_policy in all_take_snapshots:
        assert isinstance(snapshot_policy, policy.PolicyData)
        assert snapshot_policy.policy_class is take_snapshots_class
        policy_data = snapshot_policy.data.data
        # All TakeSnapshots policies must have a channel
        assert "channel" in policy_data
        assert isinstance(policy_data["channel"], pubsub.Channel)
        # interval and cycles are optional
        assert "interval" in policy_data
        assert "cycles" in policy_data

    # Verify SnapshotOnce policy has expected structure
    for snapshot_policy in all_snapshot_once:
        assert isinstance(snapshot_policy, policy.PolicyData)
        assert snapshot_policy.policy_class is snapshot_once_class
        policy_data = snapshot_policy.data.data
        # All SnapshotOnce policies must have a channel
        assert "channel" in policy_data
        assert isinstance(policy_data["channel"], pubsub.Channel)


def test_snapshot_policy_process_description() -> None:
    """Test that snapshot policies are correctly processed into ProcessDescription.

    This test verifies that:
    1. TakeSnapshots policies with interval+cycles generate correct snapshot_configs
    2. TakeSnapshots policies with only interval generate correct snapshot_configs
    3. SnapshotOnce policies generate correct snapshot_configs
    4. The snapshot_publisher_id correctly references the synthetic buffer
    """
    # Load the generated ProcessDescription
    proc_file = (
        Path(__file__).parent.parent.parent
        / "tests"
        / "support"
        / "clockwork.clockwork.dsl.tests.support.test_snapshot_policy.test_snapshot_system.proc.tachyon"
    )

    with proc_file.open("rb") as f:
        buffer = f.read()
        proc_desc = pdf.ProcessDescription.deserialize_tachyon(memoryview(buffer))

    # We should have 3 snapshot configs (one for each policy in test_snapshot_policy.clk)
    assert len(proc_desc.snapshot_configs) == 3, f"Expected 3 snapshot configs, got {len(proc_desc.snapshot_configs)}"

    # Sort by endpoint_id for deterministic checking
    configs = sorted(proc_desc.snapshot_configs, key=lambda c: str(c.endpoint_id))

    # Find configs by checking their properties
    config_with_interval_and_cycles = None
    config_with_interval_only = None
    config_snapshot_once = None

    for config in configs:
        if config.interval is not None and config.cycles is not None:
            config_with_interval_and_cycles = config
        elif config.interval is not None and config.cycles is None:
            # TakeSnapshots with interval but no cycles
            config_with_interval_only = config
        elif config.interval is None and config.cycles is None:
            # This should be SnapshotOnce
            config_snapshot_once = config

    # Verify config with interval=100ms and cycles=10
    assert config_with_interval_and_cycles is not None, "Missing snapshot config with interval and cycles"
    assert config_with_interval_and_cycles.interval == 100_000_000, (
        f"Expected interval=100000000 (100ms), got {config_with_interval_and_cycles.interval}"
    )
    assert config_with_interval_and_cycles.cycles == 10, (
        f"Expected cycles=10, got {config_with_interval_and_cycles.cycles}"
    )
    assert config_with_interval_and_cycles.snapshot_publisher_id == config_with_interval_and_cycles.endpoint_id, (
        "snapshot_publisher_id should the endpoint UUID"
    )

    # Verify config with interval=10ms but no cycles
    assert config_with_interval_only is not None, "Missing snapshot config with interval only"
    assert config_with_interval_only.interval == 10_000_000, (
        f"Expected interval=10000000 (10ms), got {config_with_interval_only.interval}"
    )
    assert config_with_interval_only.cycles is None, "Expected cycles=None for interval-only config"
    assert config_with_interval_only.snapshot_publisher_id == config_with_interval_only.endpoint_id, (
        "snapshot_publisher_id should be the endpoint UUID"
    )

    # Verify SnapshotOnce config
    assert config_snapshot_once is not None, "Missing SnapshotOnce config"
    assert config_snapshot_once.interval is None, "SnapshotOnce should have no interval"
    assert config_snapshot_once.cycles is None, "SnapshotOnce should have no cycles"
    assert config_snapshot_once.snapshot_publisher_id == config_snapshot_once.endpoint_id, (
        "snapshot_publisher_id should be the endpoint UUID"
    )

    # Verify all endpoint_ids are valid UUIDs (non-zero)
    for config in configs:
        assert config.endpoint_id.int != 0, f"endpoint_id should be a valid UUID, got {config.endpoint_id}"
        assert config.snapshot_publisher_id.int != 0, (
            f"snapshot_publisher_id should be a valid UUID, got {config.snapshot_publisher_id}"
        )
