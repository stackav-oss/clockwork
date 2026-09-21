# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Channel activity summaries for journal generation."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Protocol, final

from clockwork.journal import journal_pb2
from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.tools.journal_file_generator.scope import ResolvedJournalScope, ScopeGap

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator, Mapping, Sequence

    from clockwork.tools.journal_file_generator.log_index import LogIndex
    from clockwork.tools.journal_file_generator.request import JournalRequest


class LogTimestampLike(Protocol):
    """Timestamp fields used from raw log messages."""

    @property
    def nanoseconds(self) -> int:
        """Timestamp in nanoseconds."""
        ...


class RawLoggedMessageLike(Protocol):
    """Raw log message fields needed for channel summaries."""

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


class RawLogReaderLike(Protocol):
    """Raw log reader API used without payload deserialization."""

    def raw_messages(self, topic_filter: Callable[[str], bool] | None = None) -> Iterator[RawLoggedMessageLike]:
        """Iterate raw logged messages."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class ChannelSummaryExtraction:
    """Channel summary extraction result plus replay-readiness gaps."""

    channel_summaries: tuple[journal_pb2.ChannelSummary, ...]
    """Scoped channel summaries in deterministic order."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps found while summarizing channels."""


@final
@dataclass(frozen=True, kw_only=True)
class _ScopedChannel:
    """One channel included in the requested journal scope."""

    channel_name: str
    producer_cog_instance: str
    consumer_cog_instances: tuple[str, ...]
    required_input_cog_instances: tuple[str, ...]


@final
@dataclass(kw_only=True)
class _ScopedChannelBuilder:
    """Mutable channel context while merging scope sources."""

    producer_cog_instances: set[str] = field(default_factory=set)
    consumer_cog_instances: set[str] = field(default_factory=set)
    required_input_cog_instances: set[str] = field(default_factory=set)


@final
@dataclass(frozen=True, kw_only=True)
class _MessageStats:
    """Message count and sequence range for one channel."""

    message_count: int
    first_sequence_number: int
    last_sequence_number: int


@final
@dataclass(kw_only=True)
class _MessageStatsBuilder:
    """Mutable message stats while scanning raw log messages."""

    message_count: int = 0
    first_sequence_number: int | None = None
    last_sequence_number: int | None = None

    def add_sequence_number(self, sequence_number: int) -> None:
        """Record one observed sequence number."""
        self.message_count += 1
        if self.first_sequence_number is None or sequence_number < self.first_sequence_number:
            self.first_sequence_number = sequence_number
        if self.last_sequence_number is None or sequence_number > self.last_sequence_number:
            self.last_sequence_number = sequence_number

    def build(self) -> _MessageStats:
        """Build immutable stats."""
        return _MessageStats(
            message_count=self.message_count,
            first_sequence_number=self.first_sequence_number or 0,
            last_sequence_number=self.last_sequence_number or 0,
        )


def extract_channel_summaries(
    request: JournalRequest,
    *,
    resolved_scope: ResolvedJournalScope,
    log_index: LogIndex,
) -> ChannelSummaryExtraction:
    """Extract scoped channel summaries from a Clockwork log without deserializing payloads."""
    scoped_channels = _scoped_channels(resolved_scope)
    if not scoped_channels:
        return ChannelSummaryExtraction(channel_summaries=(), gaps=())

    logged_channel_names = tuple(
        channel.channel_name for channel in scoped_channels if log_index.has_topic(channel.channel_name)
    )
    if not logged_channel_names:
        return _build_channel_summary_extraction(
            scoped_channels=scoped_channels,
            log_index=log_index,
            message_stats_by_channel={},
        )

    return extract_channel_summaries_from_reader(
        LogReader(request.log_uri),
        resolved_scope=resolved_scope,
        log_index=log_index,
        start_time_ns=request.start_time_ns,
        end_time_ns=request.end_time_ns,
    )


def extract_channel_summaries_from_reader(
    reader: RawLogReaderLike,
    *,
    resolved_scope: ResolvedJournalScope,
    log_index: LogIndex,
    start_time_ns: int,
    end_time_ns: int,
) -> ChannelSummaryExtraction:
    """Extract scoped channel summaries from a supplied raw log reader."""
    scoped_channels = _scoped_channels(resolved_scope)
    if not scoped_channels:
        return ChannelSummaryExtraction(channel_summaries=(), gaps=())

    logged_channel_names = tuple(
        channel.channel_name for channel in scoped_channels if log_index.has_topic(channel.channel_name)
    )
    message_stats_by_channel = _message_stats_by_channel(
        reader,
        channel_names=logged_channel_names,
        start_time_ns=start_time_ns,
        end_time_ns=end_time_ns,
    )
    return _build_channel_summary_extraction(
        scoped_channels=scoped_channels,
        log_index=log_index,
        message_stats_by_channel=message_stats_by_channel,
    )


def _scoped_channels(resolved_scope: ResolvedJournalScope) -> tuple[_ScopedChannel, ...]:
    builders: dict[str, _ScopedChannelBuilder] = {}
    for channel_topology in resolved_scope.channel_topologies:
        builder = builders.setdefault(channel_topology.channel_name, _ScopedChannelBuilder())
        if channel_topology.producer_cog_instance:
            builder.producer_cog_instances.add(channel_topology.producer_cog_instance)
        builder.consumer_cog_instances.update(channel_topology.consumer_cog_instances)

    for cog_scope in resolved_scope.cog_scopes:
        for schema_ref in cog_scope.input_schemas:
            builder = builders.setdefault(schema_ref.channel_name, _ScopedChannelBuilder())
            builder.consumer_cog_instances.add(cog_scope.cog_instance_path)
            builder.required_input_cog_instances.add(cog_scope.cog_instance_path)
        for schema_ref in cog_scope.output_schemas:
            builder = builders.setdefault(schema_ref.channel_name, _ScopedChannelBuilder())
            builder.producer_cog_instances.add(cog_scope.cog_instance_path)

    return tuple(
        _ScopedChannel(
            channel_name=channel_name,
            producer_cog_instance=_single_value(builder.producer_cog_instances),
            consumer_cog_instances=tuple(sorted(builder.consumer_cog_instances)),
            required_input_cog_instances=tuple(sorted(builder.required_input_cog_instances)),
        )
        for channel_name, builder in sorted(builders.items())
    )


def _single_value(values: set[str]) -> str:
    if len(values) != 1:
        return ""
    return next(iter(values))


def _message_stats_by_channel(
    reader: RawLogReaderLike,
    *,
    channel_names: Sequence[str],
    start_time_ns: int,
    end_time_ns: int,
) -> Mapping[str, _MessageStats]:
    channel_name_set = set(channel_names)
    if not channel_name_set:
        return {}

    builders: dict[str, _MessageStatsBuilder] = {}

    def topic_filter(topic: str) -> bool:
        return topic in channel_name_set

    for message in reader.raw_messages(topic_filter):
        if not _message_is_in_time_range(message, start_time_ns=start_time_ns, end_time_ns=end_time_ns):
            continue
        builders.setdefault(message.topic, _MessageStatsBuilder()).add_sequence_number(message.sequence_number)

    return {channel_name: builder.build() for channel_name, builder in builders.items()}


def _message_is_in_time_range(
    message: RawLoggedMessageLike,
    *,
    start_time_ns: int,
    end_time_ns: int,
) -> bool:
    return start_time_ns <= message.publish_time.nanoseconds <= end_time_ns


def _build_channel_summary_extraction(
    *,
    scoped_channels: Sequence[_ScopedChannel],
    log_index: LogIndex,
    message_stats_by_channel: Mapping[str, _MessageStats],
) -> ChannelSummaryExtraction:
    summaries: list[journal_pb2.ChannelSummary] = []
    gaps: list[ScopeGap] = []
    for channel in scoped_channels:
        stats = message_stats_by_channel.get(channel.channel_name)
        has_logged_messages = log_index.has_topic(channel.channel_name)
        summaries.append(
            journal_pb2.ChannelSummary(
                channel_name=channel.channel_name,
                producer_cog_instance=channel.producer_cog_instance,
                consumer_cog_instances=list(channel.consumer_cog_instances),
                first_sequence_number=stats.first_sequence_number if stats is not None else 0,
                last_sequence_number=stats.last_sequence_number if stats is not None else 0,
                message_count=stats.message_count if stats is not None else 0,
                has_logged_messages=has_logged_messages,
            )
        )
        if not has_logged_messages:
            gaps.extend(_missing_input_channel_gaps(channel))

    return ChannelSummaryExtraction(channel_summaries=tuple(summaries), gaps=tuple(gaps))


def _missing_input_channel_gaps(channel: _ScopedChannel) -> tuple[ScopeGap, ...]:
    return tuple(
        ScopeGap(
            reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
            message=(
                f"Required input channel was not found in the log: {channel.channel_name} "
                f"for cog instance path: {cog_instance_path}"
            ),
            cog_instance_path=cog_instance_path,
            channel_name=channel.channel_name,
        )
        for cog_instance_path in channel.required_input_cog_instances
    )
