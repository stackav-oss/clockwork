# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for resolved journal scope validation."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.cog_channel import (
    CogChannelMap,
    CogChannelRef,
    DiscoveredSnapshotChannelRef,
)
from clockwork.tools.journal_file_generator.log_index import LogIndex, TopicInfo
from clockwork.tools.journal_file_generator.scope import resolve_scope
from clockwork.tools.topology import topology


@dataclass(frozen=True, kw_only=True)
class _FakeCogInstanceMetadata:
    """Minimal fake cog instance signal metadata."""

    cog_path: str
    cog_instance_path: str


@dataclass(frozen=True, kw_only=True)
class _FakeReportGroupChannelMetadata:
    """Minimal fake report-group channel signal metadata."""

    channel_name: str
    cog_path: str
    cog_instance_path: str
    is_cog_metrics_channel: bool


@dataclass(frozen=True, kw_only=True)
class _FakeSignalMetadata:
    """Minimal fake signal metadata object."""

    cog_instances: tuple[_FakeCogInstanceMetadata, ...]
    report_group_channels: tuple[_FakeReportGroupChannelMetadata, ...]


def test_resolve_scope_uses_signal_metadata_and_topology() -> None:
    """Verify known cogs resolve channel schemas, topology, and metrics channels."""
    log_index = _make_log_index()
    signal_metadata = _make_signal_metadata("target_cog")
    topology_system = _make_topology_system()

    resolved = resolve_scope(
        requested_cog_instance_paths=["target_cog"],
        log_index=log_index,
        signal_metadata=signal_metadata,
        topology_system=topology_system,
    )

    assert resolved.requested_cog_instance_paths == ("target_cog",)
    assert len(resolved.cog_scopes) == 1
    assert resolved.cog_scopes[0].cog_path == "demo.TargetCog"
    assert [schema.channel_name for schema in resolved.cog_scopes[0].input_schemas] == ["input_channel"]
    assert resolved.cog_scopes[0].input_schemas[0].schema_name == "demo.Input"
    assert [schema.channel_name for schema in resolved.cog_scopes[0].output_schemas] == ["output_channel"]
    assert resolved.cog_scopes[0].metrics_channels == ("/_clockwork/journal/execution-metrics/demo.TargetCog/id",)
    assert [channel.channel_name for channel in resolved.channel_topologies] == ["input_channel", "output_channel"]
    assert resolved.channel_topologies[0].producer_cog_instance == "source_cog"
    assert resolved.channel_topologies[0].consumer_cog_instances == ("target_cog",)
    assert resolved.channel_topologies[0].has_logged_messages
    assert not resolved.gaps


def test_unknown_cog_records_readiness_gap() -> None:
    """Verify missing requested cogs become structured readiness gaps."""
    resolved = resolve_scope(
        requested_cog_instance_paths=["missing_cog"],
        log_index=LogIndex(topics=()),
        signal_metadata=_make_signal_metadata("target_cog"),
    )

    assert not resolved.cog_scopes
    assert len(resolved.gaps) == 1
    assert resolved.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_REQUESTED_COG_NOT_FOUND
    assert resolved.gaps[0].cog_instance_path == "missing_cog"


def test_missing_signal_metadata_records_gap_but_topology_can_resolve_channels() -> None:
    """Verify topology still provides channel context when signal metadata is missing."""
    resolved = resolve_scope(
        requested_cog_instance_paths=["target_cog"],
        log_index=_make_log_index(),
        signal_metadata=None,
        topology_system=_make_topology_system(),
    )

    assert len(resolved.cog_scopes) == 1
    assert resolved.cog_scopes[0].cog_path == ""
    assert resolved.cog_scopes[0].input_schemas[0].schema_name == "demo.Input"
    assert len(resolved.gaps) == 1
    assert resolved.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA


def test_resolve_scope_uses_channel_maps_without_topology() -> None:
    """Verify compiled-system channel maps can provide channel schemas without topology summaries."""
    resolved = resolve_scope(
        requested_cog_instance_paths=["target_cog"],
        log_index=_make_log_index(),
        signal_metadata=_make_signal_metadata("target_cog"),
        cog_channel_maps={
            "target_cog": CogChannelMap(
                cog_instance_path="target_cog",
                input_channels=(CogChannelRef(cog_member_name="input_member", channel_name="input_channel"),),
                output_channels=(CogChannelRef(cog_member_name="output_member", channel_name="output_channel"),),
                has_clockwork_state=True,
            )
        },
    )

    assert len(resolved.cog_scopes) == 1
    assert [schema.channel_name for schema in resolved.cog_scopes[0].input_schemas] == ["input_channel"]
    assert [schema.channel_name for schema in resolved.cog_scopes[0].output_schemas] == ["output_channel"]
    assert resolved.cog_scopes[0].input_channel_refs == (
        CogChannelRef(cog_member_name="input_member", channel_name="input_channel"),
    )
    assert resolved.cog_scopes[0].output_channel_refs == (
        CogChannelRef(cog_member_name="output_member", channel_name="output_channel"),
    )
    assert resolved.cog_scopes[0].has_clockwork_state


def test_resolve_scope_threads_snapshot_channels_with_log_schema_metadata() -> None:
    """Verify snapshot candidates are resolved with schema metadata when topics are logged."""
    resolved = resolve_scope(
        requested_cog_instance_paths=["target_cog"],
        log_index=_make_snapshot_log_index(),
        signal_metadata=_make_signal_metadata("target_cog"),
        cog_channel_maps={
            "target_cog": CogChannelMap(
                cog_instance_path="target_cog",
                input_channels=(),
                output_channels=(),
                has_clockwork_state=True,
                snapshot_channels=(
                    DiscoveredSnapshotChannelRef(
                        cog_member_name="state",
                        channel_name="StateSnapshotChannel",
                    ),
                    DiscoveredSnapshotChannelRef(
                        cog_member_name="unlogged_state",
                        channel_name="UnloggedSnapshotChannel",
                    ),
                ),
            )
        },
    )

    assert len(resolved.cog_scopes) == 1
    snapshot_refs = resolved.cog_scopes[0].snapshot_channels
    assert [(ref.cog_member_name, ref.channel_name) for ref in snapshot_refs] == [
        ("state", "StateSnapshotChannel"),
        ("unlogged_state", "UnloggedSnapshotChannel"),
    ]
    assert snapshot_refs[0].schema_name == "demo.State"
    assert snapshot_refs[0].schema_uuid == "33333333-3333-3333-3333-333333333333"
    assert snapshot_refs[0].has_logged_messages
    assert snapshot_refs[1].schema_name == ""
    assert snapshot_refs[1].schema_uuid == ""
    assert not snapshot_refs[1].has_logged_messages
    assert resolved.cog_scopes[0].has_clockwork_state
    assert not resolved.gaps


def test_resolve_scope_without_snapshot_policy_data_preserves_stateful_cog() -> None:
    """Verify missing snapshot-policy discovery data still records stateful cogs."""
    resolved = resolve_scope(
        requested_cog_instance_paths=["target_cog"],
        log_index=LogIndex(topics=()),
        signal_metadata=_make_signal_metadata("target_cog"),
        cog_channel_maps={
            "target_cog": CogChannelMap(
                cog_instance_path="target_cog",
                input_channels=(),
                output_channels=(),
                has_clockwork_state=True,
            )
        },
    )

    assert len(resolved.cog_scopes) == 1
    assert resolved.cog_scopes[0].has_clockwork_state
    assert resolved.cog_scopes[0].snapshot_channels == ()
    assert not resolved.gaps


def test_multiple_requested_paths_are_sorted_deterministically() -> None:
    """Verify group scopes resolve in sorted path order."""
    signal_metadata = _FakeSignalMetadata(
        cog_instances=(
            _FakeCogInstanceMetadata(cog_path="demo.ZedCog", cog_instance_path="zed_cog"),
            _FakeCogInstanceMetadata(cog_path="demo.AlphaCog", cog_instance_path="alpha_cog"),
        ),
        report_group_channels=(),
    )

    resolved = resolve_scope(
        requested_cog_instance_paths=["zed_cog", "alpha_cog"],
        log_index=LogIndex(topics=()),
        signal_metadata=signal_metadata,
    )

    assert resolved.requested_cog_instance_paths == ("alpha_cog", "zed_cog")
    assert [scope.cog_instance_path for scope in resolved.cog_scopes] == ["alpha_cog", "zed_cog"]


def _make_log_index() -> LogIndex:
    return LogIndex(
        topics=(
            TopicInfo(
                name="input_channel",
                schema_name="demo.Input",
                schema_uuid="11111111-1111-1111-1111-111111111111",
                message_encoding="tachyon",
                channel_type="regular",
                schema_encoding="clockwork_tachyon",
            ),
            TopicInfo(
                name="output_channel",
                schema_name="demo.Output",
                schema_uuid="22222222-2222-2222-2222-222222222222",
                message_encoding="tachyon",
                channel_type="regular",
                schema_encoding="clockwork_tachyon",
            ),
        )
    )


def _make_snapshot_log_index() -> LogIndex:
    return LogIndex(
        topics=(
            TopicInfo(
                name="StateSnapshotChannel",
                schema_name="demo.State",
                schema_uuid="33333333-3333-3333-3333-333333333333",
                message_encoding="tachyon",
                channel_type="persistent",
                schema_encoding="clockwork_tachyon",
            ),
        )
    )


def _make_signal_metadata(cog_instance_path: str) -> _FakeSignalMetadata:
    return _FakeSignalMetadata(
        cog_instances=(_FakeCogInstanceMetadata(cog_path="demo.TargetCog", cog_instance_path=cog_instance_path),),
        report_group_channels=(
            _FakeReportGroupChannelMetadata(
                channel_name="/_clockwork/journal/execution-metrics/demo.TargetCog/id",
                cog_path="demo.TargetCog",
                cog_instance_path=cog_instance_path,
                is_cog_metrics_channel=True,
            ),
            _FakeReportGroupChannelMetadata(
                channel_name="/_clockwork/report-groups/demo.TargetCog/user/id",
                cog_path="demo.TargetCog",
                cog_instance_path=cog_instance_path,
                is_cog_metrics_channel=False,
            ),
        ),
    )


def _make_topology_system() -> topology.System:
    source = topology.Entity(
        name="source_cog",
        uuid="e9f84c8a-be76-4b7b-920c-ff50099e06e0",
        process="proc",
        outputs=["input_channel"],
        inputs=[],
        states=[],
        memory_resources=[],
    )
    target = topology.Entity(
        name="target_cog",
        uuid="16ad71b1-48d7-4e80-b3d4-e6d85e384736",
        process="proc",
        outputs=["output_channel"],
        inputs=["input_channel"],
        states=[],
        memory_resources=[],
    )
    sink = topology.Entity(
        name="sink_cog",
        uuid="beb7f376-63f0-405c-82de-1f4948f7fefc",
        process="proc",
        outputs=[],
        inputs=["output_channel"],
        states=[],
        memory_resources=[],
    )
    input_channel = topology.Channel(
        name="input_channel",
        size=1,
        publishers=["source_cog"],
        subscribers=["target_cog"],
        message_type="demo.Input",
        message_size=1,
    )
    output_channel = topology.Channel(
        name="output_channel",
        size=1,
        publishers=["target_cog"],
        subscribers=["sink_cog"],
        message_type="demo.Output",
        message_size=1,
    )
    return topology.System(
        cpus={},
        entities={entity.name: entity for entity in (source, target, sink)},
        channels={channel.name: channel for channel in (input_channel, output_channel)},
        processes={},
        memory_resources={},
        states={},
    )
