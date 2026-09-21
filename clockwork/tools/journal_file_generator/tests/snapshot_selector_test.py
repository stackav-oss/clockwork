# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal state snapshot selection."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.request import SnapshotSelection, create_journal_request
from clockwork.tools.journal_file_generator.scope import (
    ResolvedCogScope,
    ResolvedJournalScope,
    ResolvedSnapshotChannelRef,
)
from clockwork.tools.journal_file_generator.snapshot_selector import SnapshotExtraction, extract_state_snapshots

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator, Sequence

    from clockwork.logging.readers.nb_types import LogInterval, RelativeInterval


@dataclass(frozen=True, kw_only=True)
class _FakeTimestamp:
    """Minimal timestamp with nanoseconds."""

    nanoseconds: int


@dataclass(frozen=True, kw_only=True)
class _FakeRawSnapshotMessage:
    """Minimal raw logged snapshot message."""

    topic: str
    sequence_number: int
    publish_time: _FakeTimestamp
    data: bytes


@dataclass(kw_only=True)
class _TrackedRawSnapshotMessage:
    """Minimal raw logged snapshot message with payload access tracking."""

    topic: str
    sequence_number: int
    publish_time: _FakeTimestamp
    payload: bytes
    data_access_count: int = 0

    @property
    def data(self) -> bytes:
        """Return serialized snapshot bytes and track payload access."""
        self.data_access_count += 1
        return self.payload


@dataclass(frozen=True, kw_only=True)
class _FakeRawReader:
    """Raw reader backed by in-memory snapshot messages."""

    messages: tuple[_FakeRawSnapshotMessage | _TrackedRawSnapshotMessage, ...]
    maybe_log_interval: LogInterval | None = None

    def raw_messages(
        self,
        topic_filter: Callable[[str], bool] | None = None,
    ) -> Iterator[_FakeRawSnapshotMessage | _TrackedRawSnapshotMessage]:
        """Iterate fake messages accepted by the topic filter."""
        for message in self.messages:
            if self._interval_contains(message) and (topic_filter is None or topic_filter(message.topic)):
                yield message

    def with_interval(self, maybe_log_interval: LogInterval | None) -> _FakeRawReader:
        """Return a fake reader constrained by the supplied log interval."""
        return _FakeRawReader(messages=self.messages, maybe_log_interval=maybe_log_interval)

    def _interval_contains(self, message: _FakeRawSnapshotMessage | _TrackedRawSnapshotMessage) -> bool:
        if self.maybe_log_interval is None:
            return True
        message_time_ns = message.publish_time.nanoseconds
        return (
            self.maybe_log_interval.start_timestamp.nanoseconds
            <= message_time_ns
            < self.maybe_log_interval.end_timestamp.nanoseconds
        )


@pytest.mark.parametrize(
    ("snapshot_selection", "message_specs", "expected_snapshot_time_ns", "expected_state_data", "expected_intervals"),
    [
        (
            SnapshotSelection.BEFORE,
            (
                ("state_snapshot", 1, 80, b"too-old"),
                ("state_snapshot", 2, 95, b"older"),
                ("state_snapshot", 5, 100, b"selected"),
                ("state_snapshot", 3, 120, b"future"),
                ("unscoped_snapshot", 4, 100, b"ignored"),
            ),
            100,
            b"selected",
            ((0, 101),),
        ),
        (
            SnapshotSelection.AFTER,
            (
                ("state_snapshot", 1, 95, b"past"),
                ("state_snapshot", 2, 100, b"selected"),
                ("state_snapshot", 3, 130, b"later"),
            ),
            100,
            b"selected",
            ((100, 2**63 - 1),),
        ),
        (
            SnapshotSelection.CLOSEST,
            (
                ("state_snapshot", 1, 90, b"selected"),
                ("state_snapshot", 2, 110, b"later"),
            ),
            90,
            b"selected",
            (),
        ),
    ],
    ids=("before", "after", "closest"),
)
def test_selection_mode_uses_expected_snapshot_and_log_interval(
    snapshot_selection: SnapshotSelection,
    message_specs: tuple[tuple[str, int, int, bytes], ...],
    expected_snapshot_time_ns: int,
    expected_state_data: bytes,
    expected_intervals: tuple[tuple[int, int], ...],
) -> None:
    """Verify each selection mode selects the expected snapshot and reader interval."""
    constructed_log_intervals: list[LogInterval | None] = []

    result = _extract_from_reader(
        _FakeRawReader(
            messages=tuple(
                _message(topic, sequence_number=sequence_number, publish_time_ns=publish_time_ns, data=data)
                for topic, sequence_number, publish_time_ns, data in message_specs
            )
        ),
        resolved_scope=_make_scope(),
        start_time_ns=100,
        snapshot_selection=snapshot_selection,
        constructed_log_intervals=constructed_log_intervals,
    )

    assert len(result.selected_snapshots) == 1
    assert result.selected_snapshots[0].snapshot_time_ns == expected_snapshot_time_ns
    assert result.selected_snapshots[0].state_data == expected_state_data
    assert result.selected_snapshots[0].state_schema_uuid == "33333333-3333-3333-3333-333333333333"
    assert _bounded_interval_values(constructed_log_intervals) == expected_intervals
    assert not result.gaps


def test_selection_keeps_only_current_best_payload() -> None:
    """Verify payload bytes are read only for candidates that become the current best."""
    selected = _tracked_message("state_snapshot", sequence_number=2, publish_time_ns=100, data=b"selected")
    older = _tracked_message("state_snapshot", sequence_number=1, publish_time_ns=90, data=b"older")
    future = _tracked_message("state_snapshot", sequence_number=3, publish_time_ns=110, data=b"future")

    result = _extract_from_reader(
        _FakeRawReader(messages=(selected, older, future)),
        resolved_scope=_make_scope(),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    assert len(result.selected_snapshots) == 1
    assert result.selected_snapshots[0].state_data == b"selected"
    assert selected.data_access_count == 1
    assert older.data_access_count == 0
    assert future.data_access_count == 0


def test_after_selection_after_last_snapshot_records_gap() -> None:
    """Verify after mode records a gap when every logged snapshot is earlier than the start time."""
    result = _extract_from_reader(
        _FakeRawReader(
            messages=(
                _message("state_snapshot", sequence_number=1, publish_time_ns=80, data=b"old"),
                _message("state_snapshot", sequence_number=2, publish_time_ns=95, data=b"newer"),
            )
        ),
        resolved_scope=_make_scope(),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.AFTER,
    )

    _assert_single_missing_snapshot_gap(
        result,
        channel_name="state_snapshot",
        message_substring="matching selection mode 'after'",
    )


def test_missing_configured_snapshot_channel_records_gap() -> None:
    """Verify stateful cogs without discovered snapshot channels get a readiness gap."""
    result = _extract_from_reader(
        _FakeRawReader(messages=()),
        resolved_scope=_make_scope(include_state=False),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    _assert_single_missing_snapshot_gap(
        result,
        channel_name="",
        message_substring="No configured state snapshot channel",
    )


def test_stateless_cog_without_snapshot_channel_does_not_record_gap() -> None:
    """Verify stateless cogs do not require discovered snapshot channels."""
    result = _extract_from_reader(
        _FakeRawReader(messages=()),
        resolved_scope=_make_scope(include_state=False, has_clockwork_state=False),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    assert result == SnapshotExtraction(selected_snapshots=(), gaps=())


def test_missing_logged_snapshot_channel_records_gap() -> None:
    """Verify configured snapshot channels absent from log metadata get a readiness gap."""
    result = _extract_from_reader(
        _FakeRawReader(messages=()),
        resolved_scope=_make_scope(snapshot_logged=False),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    _assert_single_missing_snapshot_gap(
        result,
        channel_name="state_snapshot",
        message_substring="was not found in the log",
    )


def test_missing_matching_snapshot_records_gap() -> None:
    """Verify logged channels without a snapshot matching the selection mode get a readiness gap."""
    result = _extract_from_reader(
        _FakeRawReader(messages=(_message("state_snapshot", sequence_number=1, publish_time_ns=110, data=b"future"),)),
        resolved_scope=_make_scope(),
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    _assert_single_missing_snapshot_gap(
        result,
        channel_name="state_snapshot",
        message_substring="matching selection mode 'before'",
    )


def test_log_backed_extraction_does_not_duplicate_pre_scan_gaps() -> None:
    """Verify configured-channel gaps are not duplicated after raw log scanning."""
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("logged_cog", "missing_cog"),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="logged_cog",
                cog_path="demo.LoggedCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=True,
                snapshot_channels=(
                    ResolvedSnapshotChannelRef(
                        cog_member_name="state",
                        channel_name="state_snapshot",
                        schema_name="demo.State",
                        schema_uuid="33333333-3333-3333-3333-333333333333",
                        has_logged_messages=True,
                    ),
                ),
            ),
            ResolvedCogScope(
                cog_instance_path="missing_cog",
                cog_path="demo.MissingCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=True,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    fake_reader = _FakeRawReader(
        messages=(_message("state_snapshot", sequence_number=1, publish_time_ns=90, data=b"state"),)
    )

    result = _extract_from_reader(
        fake_reader,
        resolved_scope=resolved_scope,
        start_time_ns=100,
        snapshot_selection=SnapshotSelection.BEFORE,
    )

    assert [snapshot.cog_instance_path for snapshot in result.selected_snapshots] == ["logged_cog"]
    assert len(result.gaps) == 1
    assert result.gaps[0].cog_instance_path == "missing_cog"
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT


def _extract_from_reader(
    reader: _FakeRawReader,
    *,
    resolved_scope: ResolvedJournalScope,
    start_time_ns: int,
    snapshot_selection: SnapshotSelection,
    constructed_log_intervals: list[LogInterval | None] | None = None,
) -> SnapshotExtraction:
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=Path("snapshot_selector_test.journal.pb"),
        start_time_ns=start_time_ns,
        end_time_ns=start_time_ns,
        cog_instance_paths=resolved_scope.requested_cog_instance_paths,
        snapshot_selection=snapshot_selection,
    )

    def fake_log_reader(
        log_uri: str,
        maybe_log_interval: LogInterval | None = None,
        maybe_relative_interval: RelativeInterval | None = None,
    ) -> _FakeRawReader:
        assert log_uri == "/logs/demo.clog"
        assert maybe_relative_interval is None
        if constructed_log_intervals is not None:
            constructed_log_intervals.append(maybe_log_interval)
        return reader.with_interval(maybe_log_interval)

    with pytest.MonkeyPatch.context() as monkeypatch:
        monkeypatch.setattr("clockwork.tools.journal_file_generator.snapshot_selector.LogReader", fake_log_reader)
        return extract_state_snapshots(request, resolved_scope=resolved_scope)


def _bounded_interval_values(intervals: Sequence[LogInterval | None]) -> tuple[tuple[int, int], ...]:
    return tuple(
        (interval.start_timestamp.nanoseconds, interval.end_timestamp.nanoseconds)
        for interval in intervals
        if interval is not None
    )


def _assert_single_missing_snapshot_gap(
    result: SnapshotExtraction,
    *,
    channel_name: str,
    message_substring: str,
    cog_instance_path: str = "target_cog",
) -> None:
    assert not result.selected_snapshots
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT
    assert result.gaps[0].cog_instance_path == cog_instance_path
    assert result.gaps[0].channel_name == channel_name
    assert message_substring in result.gaps[0].message


def _message(
    topic: str,
    *,
    sequence_number: int,
    publish_time_ns: int,
    data: bytes,
) -> _FakeRawSnapshotMessage:
    return _FakeRawSnapshotMessage(
        topic=topic,
        sequence_number=sequence_number,
        publish_time=_FakeTimestamp(nanoseconds=publish_time_ns),
        data=data,
    )


def _tracked_message(
    topic: str,
    *,
    sequence_number: int,
    publish_time_ns: int,
    data: bytes,
) -> _TrackedRawSnapshotMessage:
    return _TrackedRawSnapshotMessage(
        topic=topic,
        sequence_number=sequence_number,
        publish_time=_FakeTimestamp(nanoseconds=publish_time_ns),
        payload=data,
    )


def _make_scope(
    *,
    include_state: bool = True,
    has_clockwork_state: bool = True,
    snapshot_logged: bool = True,
) -> ResolvedJournalScope:
    snapshot_channels: list[ResolvedSnapshotChannelRef] = []
    if include_state:
        snapshot_channels.append(
            ResolvedSnapshotChannelRef(
                cog_member_name="state",
                channel_name="state_snapshot",
                schema_name="demo.State",
                schema_uuid="33333333-3333-3333-3333-333333333333",
                has_logged_messages=snapshot_logged,
            )
        )
    return ResolvedJournalScope(
        requested_cog_instance_paths=("target_cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="target_cog",
                cog_path="demo.TargetCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=has_clockwork_state,
                snapshot_channels=tuple(snapshot_channels),
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
