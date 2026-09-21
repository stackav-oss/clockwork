# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal channel summary extraction."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.channel_summary import extract_channel_summaries_from_reader
from clockwork.tools.journal_file_generator.log_index import LogIndex, TopicInfo
from clockwork.tools.journal_file_generator.scope import (
    ChannelSchemaRef,
    ChannelTopology,
    ResolvedCogScope,
    ResolvedJournalScope,
)

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator


@dataclass(frozen=True, kw_only=True)
class _FakeTimestamp:
    """Minimal timestamp with nanoseconds."""

    nanoseconds: int


@dataclass(frozen=True, kw_only=True)
class _FakeRawMessage:
    """Minimal raw logged message."""

    topic: str
    sequence_number: int
    publish_time: _FakeTimestamp


@dataclass(frozen=True, kw_only=True)
class _FakeRawReader:
    """Raw reader backed by in-memory messages."""

    messages: tuple[_FakeRawMessage, ...]

    def raw_messages(self, topic_filter: Callable[[str], bool] | None = None) -> Iterator[_FakeRawMessage]:
        """Iterate fake messages accepted by the topic filter."""
        for message in self.messages:
            if topic_filter is None or topic_filter(message.topic):
                yield message


def test_channel_summaries_count_scoped_messages_and_sequence_ranges() -> None:
    """Verify scoped messages are counted and sequence ranges use the requested time window."""
    result = extract_channel_summaries_from_reader(
        _FakeRawReader(
            messages=(
                _message("input_channel", sequence_number=12, publish_time_ns=150),
                _message("input_channel", sequence_number=10, publish_time_ns=100),
                _message("input_channel", sequence_number=14, publish_time_ns=250),
                _message("output_channel", sequence_number=20, publish_time_ns=200),
                _message("unscoped_channel", sequence_number=1, publish_time_ns=100),
            )
        ),
        resolved_scope=_make_resolved_scope(),
        log_index=_make_log_index("input_channel", "output_channel", "unscoped_channel"),
        start_time_ns=100,
        end_time_ns=200,
    )

    assert [summary.channel_name for summary in result.channel_summaries] == ["input_channel", "output_channel"]
    assert result.channel_summaries[0] == journal_pb2.ChannelSummary(
        channel_name="input_channel",
        producer_cog_instance="source_cog",
        consumer_cog_instances=["target_cog"],
        first_sequence_number=10,
        last_sequence_number=12,
        message_count=2,
        has_logged_messages=True,
    )
    assert result.channel_summaries[1].first_sequence_number == 20
    assert result.channel_summaries[1].last_sequence_number == 20
    assert result.channel_summaries[1].message_count == 1
    assert not result.gaps


def test_logged_zero_message_channel_is_distinct_from_absent_input() -> None:
    """Verify a logged channel with no in-window messages does not become a missing-input gap."""
    result = extract_channel_summaries_from_reader(
        _FakeRawReader(messages=()),
        resolved_scope=_make_resolved_scope(input_channel_names=("empty_input", "missing_input")),
        log_index=_make_log_index("empty_input"),
        start_time_ns=100,
        end_time_ns=200,
    )

    summaries_by_channel = {summary.channel_name: summary for summary in result.channel_summaries}

    assert summaries_by_channel["empty_input"].has_logged_messages
    assert summaries_by_channel["empty_input"].message_count == 0
    assert not summaries_by_channel["missing_input"].has_logged_messages
    assert summaries_by_channel["missing_input"].message_count == 0
    assert [gap.reason for gap in result.gaps] == [journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL]
    assert result.gaps[0].channel_name == "missing_input"
    assert result.gaps[0].cog_instance_path == "target_cog"


def test_absent_output_channel_does_not_create_missing_input_gap() -> None:
    """Verify only required inputs create missing-input readiness gaps."""
    result = extract_channel_summaries_from_reader(
        _FakeRawReader(messages=()),
        resolved_scope=_make_resolved_scope(input_channel_names=(), output_channel_names=("missing_output",)),
        log_index=LogIndex(topics=()),
        start_time_ns=100,
        end_time_ns=200,
    )

    assert [summary.channel_name for summary in result.channel_summaries] == ["missing_output"]
    assert not result.channel_summaries[0].has_logged_messages
    assert not result.gaps


def _message(topic: str, *, sequence_number: int, publish_time_ns: int) -> _FakeRawMessage:
    return _FakeRawMessage(
        topic=topic,
        sequence_number=sequence_number,
        publish_time=_FakeTimestamp(nanoseconds=publish_time_ns),
    )


def _make_resolved_scope(
    *,
    input_channel_names: tuple[str, ...] = ("input_channel",),
    output_channel_names: tuple[str, ...] = ("output_channel",),
) -> ResolvedJournalScope:
    return ResolvedJournalScope(
        requested_cog_instance_paths=("target_cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="target_cog",
                cog_path="demo.TargetCog",
                input_schemas=tuple(_schema_ref(channel_name) for channel_name in input_channel_names),
                output_schemas=tuple(_schema_ref(channel_name) for channel_name in output_channel_names),
                metrics_channels=(),
            ),
        ),
        channel_topologies=(
            ChannelTopology(
                channel_name="input_channel",
                producer_cog_instance="source_cog",
                consumer_cog_instances=("target_cog",),
                has_logged_messages=True,
            ),
            ChannelTopology(
                channel_name="output_channel",
                producer_cog_instance="target_cog",
                consumer_cog_instances=("sink_cog",),
                has_logged_messages=True,
            ),
        )
        if input_channel_names == ("input_channel",) and output_channel_names == ("output_channel",)
        else (),
        gaps=(),
    )


def _schema_ref(channel_name: str) -> ChannelSchemaRef:
    return ChannelSchemaRef(channel_name=channel_name, schema_name="demo.Message", schema_uuid="")


def _make_log_index(*channel_names: str) -> LogIndex:
    return LogIndex(
        topics=tuple(
            TopicInfo(
                name=channel_name,
                schema_name="demo.Message",
                schema_uuid="",
                message_encoding="tachyon",
                channel_type="regular",
                schema_encoding="clockwork_tachyon",
            )
            for channel_name in sorted(channel_names)
        )
    )
