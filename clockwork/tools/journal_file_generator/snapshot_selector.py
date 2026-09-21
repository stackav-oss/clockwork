# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""State snapshot selection for journal generation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, Protocol, final

from clockwork.journal import journal_pb2
from clockwork.logging.readers.nb_types import LogInterval, LogTimestamp
from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.tools.journal_file_generator.request import JournalRequest, SnapshotSelection
from clockwork.tools.journal_file_generator.scope import (
    ResolvedCogScope,
    ResolvedJournalScope,
    ResolvedSnapshotChannelRef,
    ScopeGap,
)

_MIN_LOG_INTERVAL_TIME_NS: Final = 0
_MAX_LOG_INTERVAL_TIME_NS: Final = 2**63 - 1

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator, Sequence


class LogTimestampLike(Protocol):
    """Timestamp fields used from raw log messages."""

    @property
    def nanoseconds(self) -> int:
        """Timestamp in nanoseconds."""
        ...


class RawSnapshotMessageLike(Protocol):
    """Raw log message fields needed for snapshot selection."""

    @property
    def topic(self) -> str:
        """Logged topic name."""
        ...

    @property
    def sequence_number(self) -> int:
        """Logged sequence number."""
        ...

    @property
    def publish_time(self) -> LogTimestampLike:
        """Message publish sync time."""
        ...

    @property
    def data(self) -> bytes:
        """Serialized snapshot bytes."""
        ...


class RawSnapshotReaderLike(Protocol):
    """Raw log reader API used without snapshot payload deserialization."""

    def raw_messages(self, topic_filter: Callable[[str], bool] | None = None) -> Iterator[RawSnapshotMessageLike]:
        """Iterate raw logged messages."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class SelectedCogSnapshot:
    """Selected serialized snapshot for one cog journal."""

    cog_instance_path: str
    """Cog instance path initialized by the selected snapshot."""

    channel_name: str
    """Snapshot channel used for selection."""

    snapshot_time_ns: int
    """Snapshot publish sync time."""

    state_data: bytes
    """Serialized snapshot payload bytes."""

    state_schema_uuid: str
    """Schema UUID from log topic metadata, when available."""


@final
@dataclass(frozen=True, kw_only=True)
class SnapshotExtraction:
    """Snapshot selection result plus replay-readiness gaps."""

    selected_snapshots: tuple[SelectedCogSnapshot, ...]
    """Selected snapshots in deterministic cog-instance order."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps found while selecting snapshots."""


@final
@dataclass(frozen=True, kw_only=True)
class _SnapshotChannelSelection:
    """Primary snapshot channel selected for one cog under the current schema."""

    cog_instance_path: str
    channel: ResolvedSnapshotChannelRef


@final
@dataclass(frozen=True, kw_only=True)
class _SnapshotCandidate:
    """Current best serialized snapshot observed for one cog."""

    cog_instance_path: str
    channel: ResolvedSnapshotChannelRef
    snapshot_time_ns: int
    sequence_number: int
    selection_key: _SnapshotSelectionKey
    state_data: bytes


@final
@dataclass(frozen=True, order=True, kw_only=True)
class _SnapshotSelectionKey:
    """Comparable snapshot candidate rank, where lower field values are better."""

    distance_ns: int
    """Primary distance from the requested start time."""

    side_priority: int
    """Side-of-start tie-breaker for closest mode, where earlier snapshots sort first."""

    timestamp_rank: int
    """Timestamp tie-breaker with sign chosen so the preferred timestamp sorts first."""

    sequence_number_rank: int
    """Sequence tie-breaker with sign chosen so the preferred sequence sorts first."""


def extract_state_snapshots(
    request: JournalRequest,
    *,
    resolved_scope: ResolvedJournalScope,
) -> SnapshotExtraction:
    """Select snapshots for the resolved journal scope."""
    selected_channels, gaps = _selected_snapshot_channels(resolved_scope)
    logged_channels = tuple(selection for selection in selected_channels if selection.channel.has_logged_messages)
    if not logged_channels:
        return SnapshotExtraction(selected_snapshots=(), gaps=tuple(gaps))

    gap_list = list(gaps)
    selected_candidates_by_cog = _select_snapshot_candidates_by_cog(
        _make_snapshot_log_reader(request),
        selected_channels=logged_channels,
        start_time_ns=request.start_time_ns,
        snapshot_selection=request.snapshot_selection,
    )
    selected_snapshots: list[SelectedCogSnapshot] = []
    for selection in logged_channels:
        candidate = selected_candidates_by_cog.get(selection.cog_instance_path)
        if candidate is None:
            gap_list.append(_missing_matching_snapshot_gap(selection, request.snapshot_selection))
            continue
        selected_snapshots.append(
            SelectedCogSnapshot(
                cog_instance_path=candidate.cog_instance_path,
                channel_name=candidate.channel.channel_name,
                snapshot_time_ns=candidate.snapshot_time_ns,
                state_data=candidate.state_data,
                state_schema_uuid=candidate.channel.schema_uuid,
            )
        )

    return SnapshotExtraction(
        selected_snapshots=tuple(sorted(selected_snapshots, key=lambda snapshot: snapshot.cog_instance_path)),
        gaps=tuple(gap_list),
    )


def _make_snapshot_log_reader(request: JournalRequest) -> RawSnapshotReaderLike:
    log_interval = _snapshot_log_interval(
        start_time_ns=request.start_time_ns,
        snapshot_selection=request.snapshot_selection,
    )
    if log_interval is None:
        return LogReader(request.log_uri)
    return LogReader(request.log_uri, log_interval)


def _snapshot_log_interval(
    *,
    start_time_ns: int,
    snapshot_selection: SnapshotSelection,
) -> LogInterval | None:
    match snapshot_selection:
        case SnapshotSelection.BEFORE:
            if start_time_ns < _MIN_LOG_INTERVAL_TIME_NS or start_time_ns >= _MAX_LOG_INTERVAL_TIME_NS:
                return None
            return LogInterval(LogTimestamp(_MIN_LOG_INTERVAL_TIME_NS), LogTimestamp(start_time_ns + 1))
        case SnapshotSelection.AFTER:
            if start_time_ns < _MIN_LOG_INTERVAL_TIME_NS or start_time_ns >= _MAX_LOG_INTERVAL_TIME_NS:
                return None
            return LogInterval(LogTimestamp(start_time_ns), LogTimestamp(_MAX_LOG_INTERVAL_TIME_NS))
        case SnapshotSelection.CLOSEST:
            return None


def _selected_snapshot_channels(
    resolved_scope: ResolvedJournalScope,
) -> tuple[tuple[_SnapshotChannelSelection, ...], list[ScopeGap]]:
    selections: list[_SnapshotChannelSelection] = []
    gaps: list[ScopeGap] = []
    for cog_scope in resolved_scope.cog_scopes:
        selected_channel = _primary_snapshot_channel(cog_scope)
        if selected_channel is None:
            if cog_scope.has_clockwork_state:
                gaps.append(_missing_configured_snapshot_gap(cog_scope.cog_instance_path))
            continue
        selection = _SnapshotChannelSelection(cog_instance_path=cog_scope.cog_instance_path, channel=selected_channel)
        if selected_channel.has_logged_messages:
            selections.append(selection)
        else:
            gaps.append(_missing_logged_snapshot_gap(selection))
    return tuple(selections), gaps


def _primary_snapshot_channel(cog_scope: ResolvedCogScope) -> ResolvedSnapshotChannelRef | None:
    if not cog_scope.snapshot_channels:
        return None
    return sorted(cog_scope.snapshot_channels, key=lambda channel: (channel.cog_member_name, channel.channel_name))[0]


def _select_snapshot_candidates_by_cog(
    reader: RawSnapshotReaderLike,
    *,
    selected_channels: Sequence[_SnapshotChannelSelection],
    start_time_ns: int,
    snapshot_selection: SnapshotSelection,
) -> dict[str, _SnapshotCandidate]:
    selections_by_channel: dict[str, list[_SnapshotChannelSelection]] = {}
    for selection in selected_channels:
        selections_by_channel.setdefault(selection.channel.channel_name, []).append(selection)

    def topic_filter(topic: str) -> bool:
        return topic in selections_by_channel

    selected_candidates: dict[str, _SnapshotCandidate] = {}
    for message in reader.raw_messages(topic_filter):
        message_data: bytes | None = None
        snapshot_time_ns = message.publish_time.nanoseconds
        sequence_number = message.sequence_number
        selection_key = _candidate_selection_key(
            snapshot_time_ns=snapshot_time_ns,
            sequence_number=sequence_number,
            start_time_ns=start_time_ns,
            snapshot_selection=snapshot_selection,
        )
        if selection_key is None:
            continue

        for selection in selections_by_channel[message.topic]:
            selected_candidate = selected_candidates.get(selection.cog_instance_path)
            if selected_candidate is not None and selected_candidate.selection_key <= selection_key:
                continue
            if message_data is None:
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                message_data = bytes(message.data)
            selected_candidates[selection.cog_instance_path] = _SnapshotCandidate(
                cog_instance_path=selection.cog_instance_path,
                channel=selection.channel,
                snapshot_time_ns=snapshot_time_ns,
                sequence_number=sequence_number,
                selection_key=selection_key,
                state_data=message_data,
            )
    return selected_candidates


def _candidate_selection_key(
    *,
    snapshot_time_ns: int,
    sequence_number: int,
    start_time_ns: int,
    snapshot_selection: SnapshotSelection,
) -> _SnapshotSelectionKey | None:
    match snapshot_selection:
        case SnapshotSelection.BEFORE:
            if snapshot_time_ns > start_time_ns:
                return None
            return _SnapshotSelectionKey(
                distance_ns=start_time_ns - snapshot_time_ns,
                side_priority=0,
                timestamp_rank=-snapshot_time_ns,
                sequence_number_rank=-sequence_number,
            )
        case SnapshotSelection.AFTER:
            if snapshot_time_ns < start_time_ns:
                return None
            return _SnapshotSelectionKey(
                distance_ns=snapshot_time_ns - start_time_ns,
                side_priority=0,
                timestamp_rank=snapshot_time_ns,
                sequence_number_rank=sequence_number,
            )
        case SnapshotSelection.CLOSEST:
            distance = abs(snapshot_time_ns - start_time_ns)
            if snapshot_time_ns <= start_time_ns:
                return _SnapshotSelectionKey(
                    distance_ns=distance,
                    side_priority=0,
                    timestamp_rank=-snapshot_time_ns,
                    sequence_number_rank=-sequence_number,
                )
            return _SnapshotSelectionKey(
                distance_ns=distance,
                side_priority=1,
                timestamp_rank=snapshot_time_ns,
                sequence_number_rank=sequence_number,
            )
        case _:
            msg = f"Unsupported snapshot selection mode: {snapshot_selection}"
            raise ValueError(msg)


def _missing_configured_snapshot_gap(cog_instance_path: str) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
        message=(f"No configured state snapshot channel was discovered for cog instance path: {cog_instance_path}"),
        cog_instance_path=cog_instance_path,
    )


def _missing_logged_snapshot_gap(selection: _SnapshotChannelSelection) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
        message=(
            f"Configured state snapshot channel was not found in the log: {selection.channel.channel_name} "
            f"for cog instance path: {selection.cog_instance_path}"
        ),
        cog_instance_path=selection.cog_instance_path,
        channel_name=selection.channel.channel_name,
    )


def _missing_matching_snapshot_gap(
    selection: _SnapshotChannelSelection,
    snapshot_selection: SnapshotSelection,
) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
        message=(
            f"No state snapshot matching selection mode '{snapshot_selection.value}' was found on channel: "
            f"{selection.channel.channel_name} for cog instance path: {selection.cog_instance_path}"
        ),
        cog_instance_path=selection.cog_instance_path,
        channel_name=selection.channel.channel_name,
    )
