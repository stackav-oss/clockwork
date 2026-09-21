# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for report message-index extraction."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.message_index import (
    MessageIndexExtraction,
    extract_channel_messages_from_reader,
)

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator, Sized


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
    data: Sized | None


@dataclass(frozen=True, kw_only=True)
class _FakeRawReader:
    """Raw reader backed by in-memory messages."""

    messages: tuple[_FakeRawMessage, ...]

    def raw_messages(self, topic_filter: Callable[[str], bool] | None = None) -> Iterator[_FakeRawMessage]:
        """Iterate fake messages accepted by the topic filter."""
        for message in self.messages:
            if topic_filter is None or topic_filter(message.topic):
                yield message


def test_message_index_filters_scoped_channels_and_orders_records() -> None:
    """Verify the index records scoped message metadata without payload decoding."""
    result = extract_channel_messages_from_reader(
        _FakeRawReader(
            messages=(
                _message("output_channel", sequence_number=20, publish_time_ns=150, data=b"abcd"),
                _message("input_channel", sequence_number=12, publish_time_ns=150, data=b"xy"),
                _message("input_channel", sequence_number=10, publish_time_ns=100, data=b"abc"),
                _message("input_channel", sequence_number=14, publish_time_ns=250, data=b"late"),
                _message("unscoped_channel", sequence_number=1, publish_time_ns=100, data=b"skip"),
            )
        ),
        channel_names=("input_channel", "output_channel"),
        start_time_ns=100,
        end_time_ns=200,
    )

    assert result == MessageIndexExtraction(
        channel_messages=(
            journal_pb2.ChannelMessage(
                channel_name="input_channel",
                sequence_number=10,
                publish_time_ns=100,
                payload_size_bytes=3,
            ),
            journal_pb2.ChannelMessage(
                channel_name="input_channel",
                sequence_number=12,
                publish_time_ns=150,
                payload_size_bytes=2,
            ),
            journal_pb2.ChannelMessage(
                channel_name="output_channel",
                sequence_number=20,
                publish_time_ns=150,
                payload_size_bytes=4,
            ),
        ),
    )


def test_missing_payload_size_metadata_is_report_only() -> None:
    """Verify missing payload size metadata skips the message without readiness-style gaps."""
    result = extract_channel_messages_from_reader(
        _FakeRawReader(
            messages=(
                _message("input_channel", sequence_number=10, publish_time_ns=100, data=None),
                _message("input_channel", sequence_number=11, publish_time_ns=110, data=b"ok"),
            )
        ),
        channel_names=("input_channel",),
        start_time_ns=100,
        end_time_ns=200,
    )

    assert result.skipped_message_metadata_count == 1
    assert result.channel_messages == (
        journal_pb2.ChannelMessage(
            channel_name="input_channel",
            sequence_number=11,
            publish_time_ns=110,
            payload_size_bytes=2,
        ),
    )


def _message(topic: str, *, sequence_number: int, publish_time_ns: int, data: Sized | None) -> _FakeRawMessage:
    return _FakeRawMessage(
        topic=topic,
        sequence_number=sequence_number,
        publish_time=_FakeTimestamp(nanoseconds=publish_time_ns),
        data=data,
    )
