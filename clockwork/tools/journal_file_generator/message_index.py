# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Report-oriented channel message index extraction."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, final

from clockwork.journal import journal_pb2
from clockwork.logging.readers.py_log_reader import LogReader

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator, Sequence, Sized

    from clockwork.tools.journal_file_generator.log_index import LogIndex
    from clockwork.tools.journal_file_generator.request import JournalRequest
    from clockwork.tools.journal_file_generator.scope import ResolvedJournalScope


class LogTimestampLike(Protocol):
    """Timestamp fields used from raw log messages."""

    @property
    def nanoseconds(self) -> int:
        """Timestamp in nanoseconds."""
        ...


class RawIndexedMessageLike(Protocol):
    """Raw log message fields needed for report indexing."""

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
    def data(self) -> Sized | None:
        """Serialized payload bytes or another sized payload view."""
        ...


class RawMessageIndexReaderLike(Protocol):
    """Raw log reader API used without payload deserialization."""

    def raw_messages(self, topic_filter: Callable[[str], bool] | None = None) -> Iterator[RawIndexedMessageLike]:
        """Iterate raw logged messages."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class MessageIndexExtraction:
    """Report message-index result plus non-replay capability gaps."""

    channel_messages: tuple[journal_pb2.ChannelMessage, ...]
    """Scoped channel message metadata records in deterministic order."""

    skipped_message_metadata_count: int = 0
    """Messages skipped because required report metadata was unavailable."""


def extract_channel_messages(
    request: JournalRequest,
    *,
    resolved_scope: ResolvedJournalScope,
    log_index: LogIndex,
) -> MessageIndexExtraction:
    """Extract scoped channel message metadata from a Clockwork log."""
    scoped_channel_names = _logged_scoped_channel_names(resolved_scope, log_index)
    if not scoped_channel_names:
        return MessageIndexExtraction(channel_messages=())

    return extract_channel_messages_from_reader(
        LogReader(request.log_uri),
        channel_names=scoped_channel_names,
        start_time_ns=request.start_time_ns,
        end_time_ns=request.end_time_ns,
    )


def extract_channel_messages_from_reader(
    reader: RawMessageIndexReaderLike,
    *,
    channel_names: Sequence[str],
    start_time_ns: int,
    end_time_ns: int,
) -> MessageIndexExtraction:
    """Extract scoped channel message metadata from a supplied raw log reader."""
    channel_name_set = set(channel_names)
    if not channel_name_set:
        return MessageIndexExtraction(channel_messages=())

    skipped_message_metadata_count = 0
    channel_messages: list[journal_pb2.ChannelMessage] = []

    def topic_filter(topic: str) -> bool:
        return topic in channel_name_set

    for message in reader.raw_messages(topic_filter):
        if not _message_is_in_time_range(message, start_time_ns=start_time_ns, end_time_ns=end_time_ns):
            continue
        payload_size_bytes = _payload_size_bytes(message)
        if payload_size_bytes is None:
            skipped_message_metadata_count += 1
            continue
        channel_messages.append(
            journal_pb2.ChannelMessage(
                channel_name=message.topic,
                sequence_number=message.sequence_number,
                publish_time_ns=message.publish_time.nanoseconds,
                payload_size_bytes=payload_size_bytes,
            )
        )

    return MessageIndexExtraction(
        channel_messages=tuple(sorted(channel_messages, key=_channel_message_key)),
        skipped_message_metadata_count=skipped_message_metadata_count,
    )


def _logged_scoped_channel_names(resolved_scope: ResolvedJournalScope, log_index: LogIndex) -> tuple[str, ...]:
    return tuple(
        channel_name for channel_name in _scoped_channel_names(resolved_scope) if log_index.has_topic(channel_name)
    )


def _scoped_channel_names(resolved_scope: ResolvedJournalScope) -> tuple[str, ...]:
    channel_names: set[str] = {channel.channel_name for channel in resolved_scope.channel_topologies}
    for cog_scope in resolved_scope.cog_scopes:
        channel_names.update(schema.channel_name for schema in cog_scope.input_schemas)
        channel_names.update(schema.channel_name for schema in cog_scope.output_schemas)
    return tuple(sorted(channel_names))


def _message_is_in_time_range(
    message: RawIndexedMessageLike,
    *,
    start_time_ns: int,
    end_time_ns: int,
) -> bool:
    return start_time_ns <= message.publish_time.nanoseconds <= end_time_ns


def _payload_size_bytes(message: RawIndexedMessageLike) -> int | None:
    if message.data is None:
        return None
    return len(message.data)


def _channel_message_key(message: journal_pb2.ChannelMessage) -> tuple[str, int, int]:
    return (message.channel_name, message.sequence_number, message.publish_time_ns)
