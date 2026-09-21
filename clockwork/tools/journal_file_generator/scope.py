# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Resolved cog scope validation for journal generation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, cast, final

from clockwork.journal import journal_pb2
from clockwork.tools.signal_extractor.signal_extractor import SignalExtractor

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from clockwork.tools.journal_file_generator.cog_channel import (
        CogChannelMap,
        CogChannelRef,
        DiscoveredSnapshotChannelRef,
    )
    from clockwork.tools.journal_file_generator.log_index import LogIndex
    from clockwork.tools.topology import topology


class ReportGroupChannelMetadataLike(Protocol):
    """Signal metadata entry for one report-group channel."""

    @property
    def channel_name(self) -> str:
        """Report-group channel name."""
        ...

    @property
    def cog_path(self) -> str:
        """Cog class path."""
        ...

    @property
    def cog_instance_path(self) -> str:
        """Cog instance path."""
        ...

    @property
    def is_cog_metrics_channel(self) -> bool:
        """Whether this report-group channel contains cog metrics."""
        ...


class CogInstanceMetadataLike(Protocol):
    """Signal metadata entry for one cog instance."""

    @property
    def cog_path(self) -> str:
        """Cog class path."""
        ...

    @property
    def cog_instance_path(self) -> str:
        """Cog instance path."""
        ...


class SignalMetadataLike(Protocol):
    """Signal metadata fields used for resolved-scope validation."""

    @property
    def cog_instances(self) -> Sequence[CogInstanceMetadataLike]:
        """Cog instances described by signal metadata."""
        ...

    @property
    def report_group_channels(self) -> Sequence[ReportGroupChannelMetadataLike]:
        """Report-group channels described by signal metadata."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class ChannelSchemaRef:
    """Schema reference for one scoped channel."""

    channel_name: str
    """Channel name."""

    schema_name: str
    """Schema name from log metadata, when the channel is logged."""

    schema_uuid: str
    """Schema UUID from log metadata, when available."""


@final
@dataclass(frozen=True, kw_only=True)
class ResolvedSnapshotChannelRef:
    """Schema-aware snapshot channel candidate for one cog state endpoint."""

    cog_member_name: str
    """Cog state member initialized by the snapshot."""

    channel_name: str
    """Snapshot channel name."""

    schema_name: str
    """Schema name from log metadata, when the snapshot channel is logged."""

    schema_uuid: str
    """Schema UUID from log metadata, when available."""

    has_logged_messages: bool
    """Whether the snapshot channel is present in log topic metadata."""


@final
@dataclass(frozen=True, kw_only=True)
class ChannelTopology:
    """Producer and consumer context for one scoped channel."""

    channel_name: str
    """Channel name."""

    producer_cog_instance: str
    """Single producer cog instance, or empty when unavailable or ambiguous."""

    consumer_cog_instances: tuple[str, ...]
    """Consumer cog instance paths."""

    has_logged_messages: bool
    """Whether the channel is present in log topic metadata."""


@final
@dataclass(frozen=True, kw_only=True)
class ScopeGap:
    """Replay-readiness gap found while resolving journal scope."""

    reason: journal_pb2.ReplayReadinessGapReason
    """Journal protobuf replay-readiness gap reason."""

    message: str
    """Human-readable diagnostic message."""

    cog_instance_path: str = ""
    """Cog instance associated with the gap, when applicable."""

    channel_name: str = ""
    """Channel associated with the gap, when applicable."""

    execution_index: int = 0
    """Execution index associated with the gap, when applicable."""


@final
@dataclass(frozen=True, kw_only=True)
class ResolvedCogScope:
    """Resolved metadata for one requested cog instance."""

    cog_instance_path: str
    """Fully resolved runtime cog instance path."""

    cog_path: str
    """Cog class path, when signal metadata is available."""

    input_schemas: tuple[ChannelSchemaRef, ...]
    """Input channel schema references."""

    output_schemas: tuple[ChannelSchemaRef, ...]
    """Output channel schema references."""

    metrics_channels: tuple[str, ...]
    """Cog metrics report-group channels from signal metadata."""

    input_channel_refs: tuple[CogChannelRef, ...] = ()
    """Input member-to-channel refs from compiled system context."""

    output_channel_refs: tuple[CogChannelRef, ...] = ()
    """Output member-to-channel refs from compiled system context."""

    has_clockwork_state: bool = False
    """Whether the cog has at least one Clockwork state endpoint."""

    snapshot_channels: tuple[ResolvedSnapshotChannelRef, ...] = ()
    """State snapshot channels discovered from compiled system context."""


@final
@dataclass(frozen=True, kw_only=True)
class ResolvedJournalScope:
    """Resolved metadata and readiness gaps for a journal request scope."""

    requested_cog_instance_paths: tuple[str, ...]
    """Requested cog instance paths in deterministic order."""

    cog_scopes: tuple[ResolvedCogScope, ...]
    """Resolved cog scopes in deterministic order."""

    channel_topologies: tuple[ChannelTopology, ...]
    """Scoped channel topology in deterministic order."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps detected during scope resolution."""


def load_signal_metadata(log_uri: str) -> SignalMetadataLike | None:
    """Load signal metadata from a Clockwork log when present."""
    try:
        return cast("SignalMetadataLike", SignalExtractor(log_uri).get_metadata())
    except ValueError:
        return None


def resolve_scope(
    *,
    requested_cog_instance_paths: Sequence[str],
    log_index: LogIndex,
    signal_metadata: SignalMetadataLike | None,
    topology_system: topology.System | None = None,
    cog_channel_maps: Mapping[str, CogChannelMap] | None = None,
) -> ResolvedJournalScope:
    """Validate and resolve requested cog instance paths."""
    requested_paths = tuple(sorted(set(requested_cog_instance_paths)))
    cog_metadata_by_path = _cog_metadata_by_instance_path(signal_metadata)
    metric_channels_by_path = _metric_channels_by_instance_path(signal_metadata)
    gaps: list[ScopeGap] = []

    if signal_metadata is None:
        gaps.append(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA,
                message="Signal metadata was not found in the log.",
            )
        )

    cog_scopes: list[ResolvedCogScope] = []
    channel_topologies: dict[str, ChannelTopology] = {}
    for cog_instance_path in requested_paths:
        cog_metadata = cog_metadata_by_path.get(cog_instance_path)
        topology_entity = _topology_entity(topology_system, cog_instance_path)
        channel_map = _cog_channel_map(cog_channel_maps, cog_instance_path)

        if cog_metadata is None and topology_entity is None and channel_map is None:
            if signal_metadata is not None or topology_system is not None or cog_channel_maps is not None:
                gaps.append(_requested_cog_not_found_gap(cog_instance_path))
            continue

        input_channel_refs = _input_channel_refs(channel_map, topology_entity)
        output_channel_refs = _output_channel_refs(channel_map, topology_entity)
        input_channel_names = (
            tuple(topology_entity.inputs) if topology_entity is not None else _channel_names(input_channel_refs)
        )
        output_channel_names = (
            tuple(topology_entity.outputs) if topology_entity is not None else _channel_names(output_channel_refs)
        )
        snapshot_channels = _snapshot_channel_refs(channel_map, log_index)
        # fmt: off
        cog_scopes.append(
            ResolvedCogScope(
                cog_instance_path=cog_instance_path,
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                cog_path=str(cog_metadata.cog_path) if cog_metadata is not None else "",
                input_schemas=_schema_refs(input_channel_names, log_index),
                output_schemas=_schema_refs(output_channel_names, log_index),
                metrics_channels=metric_channels_by_path.get(cog_instance_path, ()),
                input_channel_refs=input_channel_refs,
                output_channel_refs=output_channel_refs,
                has_clockwork_state=_has_clockwork_state(channel_map, topology_entity),
                snapshot_channels=snapshot_channels,
            )
        )
        # fmt: on

        if topology_system is not None:
            for channel_name in (*input_channel_names, *output_channel_names):
                channel_topologies[channel_name] = _channel_topology(channel_name, log_index, topology_system)

    return ResolvedJournalScope(
        requested_cog_instance_paths=requested_paths,
        cog_scopes=tuple(sorted(cog_scopes, key=lambda scope: scope.cog_instance_path)),
        channel_topologies=tuple(channel_topologies[name] for name in sorted(channel_topologies)),
        gaps=tuple(gaps),
    )


def _cog_metadata_by_instance_path(
    signal_metadata: SignalMetadataLike | None,
) -> dict[str, CogInstanceMetadataLike]:
    if signal_metadata is None:
        return {}
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return {str(cog_instance.cog_instance_path): cog_instance for cog_instance in signal_metadata.cog_instances}


def _metric_channels_by_instance_path(signal_metadata: SignalMetadataLike | None) -> dict[str, tuple[str, ...]]:
    if signal_metadata is None:
        return {}

    channels_by_path: dict[str, list[str]] = {}
    for channel in signal_metadata.report_group_channels:
        if channel.is_cog_metrics_channel:
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            channels_by_path.setdefault(str(channel.cog_instance_path), []).append(str(channel.channel_name))
    return {path: tuple(sorted(channels)) for path, channels in channels_by_path.items()}


def _topology_entity(topology_system: topology.System | None, cog_instance_path: str) -> topology.Entity | None:
    if topology_system is None:
        return None
    return topology_system.entities.get(cog_instance_path)


def _cog_channel_map(
    cog_channel_maps: Mapping[str, CogChannelMap] | None,
    cog_instance_path: str,
) -> CogChannelMap | None:
    if cog_channel_maps is None:
        return None
    return cog_channel_maps.get(cog_instance_path)


def _input_channel_refs(
    channel_map: CogChannelMap | None,
    topology_entity: topology.Entity | None,
) -> tuple[CogChannelRef, ...]:
    if channel_map is None:
        return ()
    return _filter_channel_refs(channel_map.input_channels, tuple(topology_entity.inputs) if topology_entity else ())


def _output_channel_refs(
    channel_map: CogChannelMap | None,
    topology_entity: topology.Entity | None,
) -> tuple[CogChannelRef, ...]:
    if channel_map is None:
        return ()
    return _filter_channel_refs(channel_map.output_channels, tuple(topology_entity.outputs) if topology_entity else ())


def _filter_channel_refs(
    channel_refs: tuple[CogChannelRef, ...],
    allowed_channel_names: tuple[str, ...],
) -> tuple[CogChannelRef, ...]:
    if not allowed_channel_names:
        return channel_refs
    allowed = set(allowed_channel_names)
    return tuple(channel_ref for channel_ref in channel_refs if channel_ref.channel_name in allowed)


def _channel_names(channel_refs: tuple[CogChannelRef, ...]) -> tuple[str, ...]:
    return tuple(sorted({channel_ref.channel_name for channel_ref in channel_refs}))


def _has_clockwork_state(
    channel_map: CogChannelMap | None,
    topology_entity: topology.Entity | None,
) -> bool:
    if channel_map is not None:
        return channel_map.has_clockwork_state
    if topology_entity is not None:
        return bool(topology_entity.states)
    return False


def _snapshot_channel_refs(
    channel_map: CogChannelMap | None,
    log_index: LogIndex,
) -> tuple[ResolvedSnapshotChannelRef, ...]:
    if channel_map is None:
        return ()
    return tuple(_snapshot_channel_ref(channel_ref, log_index) for channel_ref in channel_map.snapshot_channels)


def _snapshot_channel_ref(
    channel_ref: DiscoveredSnapshotChannelRef,
    log_index: LogIndex,
) -> ResolvedSnapshotChannelRef:
    topic = log_index.topic(channel_ref.channel_name)
    return ResolvedSnapshotChannelRef(
        cog_member_name=channel_ref.cog_member_name,
        channel_name=channel_ref.channel_name,
        schema_name=topic.schema_name if topic is not None else "",
        schema_uuid=topic.schema_uuid if topic is not None else "",
        has_logged_messages=topic is not None,
    )


def _schema_refs(channel_names: Sequence[str], log_index: LogIndex) -> tuple[ChannelSchemaRef, ...]:
    return tuple(_schema_ref(channel_name, log_index) for channel_name in sorted(channel_names))


def _schema_ref(channel_name: str, log_index: LogIndex) -> ChannelSchemaRef:
    topic = log_index.topic(channel_name)
    if topic is None:
        return ChannelSchemaRef(channel_name=channel_name, schema_name="", schema_uuid="")
    return ChannelSchemaRef(channel_name=channel_name, schema_name=topic.schema_name, schema_uuid=topic.schema_uuid)


def _channel_topology(channel_name: str, log_index: LogIndex, topology_system: topology.System) -> ChannelTopology:
    channel = topology_system.channels.get(channel_name)
    if channel is None:
        return ChannelTopology(
            channel_name=channel_name,
            producer_cog_instance="",
            consumer_cog_instances=(),
            has_logged_messages=log_index.has_topic(channel_name),
        )

    publishers = tuple(sorted(channel.publishers))
    return ChannelTopology(
        channel_name=channel_name,
        producer_cog_instance=publishers[0] if len(publishers) == 1 else "",
        consumer_cog_instances=tuple(sorted(channel.subscribers)),
        has_logged_messages=log_index.has_topic(channel_name),
    )


def _requested_cog_not_found_gap(cog_instance_path: str) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_REQUESTED_COG_NOT_FOUND,
        message=f"Requested cog instance path was not found: {cog_instance_path}",
        cog_instance_path=cog_instance_path,
    )
