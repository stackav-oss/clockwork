# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Deterministic anomaly detection for journal reports."""

from __future__ import annotations

import itertools
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Final, TypeAlias, final

from clockwork.journal import journal_pb2

if TYPE_CHECKING:
    from collections.abc import Sequence

_TIMING_SAMPLE_MIN: Final = 4
_TIMING_PERCENTILE: Final = 90
_GROUPED_DETAIL_DISPLAY_LIMIT: Final = 5

_SEVERITY_ORDER: Final = {
    "warning": 0,
    "info": 1,
}

_MISSING_METADATA_REASONS: Final = {
    journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EVENT_METRICS_CHANNEL,
    journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA,
    journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA,
}

# Channel messages do not carry execution links, so message anomalies join records by channel and sequence number.
_MessageKey: TypeAlias = tuple[str, int]
_MessageSequencesByChannel: TypeAlias = dict[str, tuple[int, ...]]


@final
@dataclass(frozen=True, kw_only=True)
class AnomalyReportModel:
    """Top-level anomaly model."""

    summary_counts: tuple[AnomalyCountModel, ...]
    items: tuple[AnomalyModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class AnomalyCountModel:
    """Count for one anomaly type."""

    label: str
    count: int


@final
@dataclass(frozen=True, kw_only=True)
class AnomalyModel:
    """One report anomaly or capability warning."""

    id: str
    type: str
    severity: str
    title: str
    message: str
    cog_instance_path: str
    channel_name: str
    execution_index: int | None
    value_ns: int | None = None
    threshold_ns: int | None = None


@final
@dataclass(frozen=True, kw_only=True)
class _ExecutionRef:
    """Execution reference used while deriving message anomalies."""

    cog_instance_path: str
    execution_index: int


# Use the mutable map only while collecting references; helpers receive the frozen map shape.
_ExecutionRefsByMessage: TypeAlias = dict[_MessageKey, tuple[_ExecutionRef, ...]]
_MutableExecutionRefsByMessage: TypeAlias = dict[_MessageKey, list[_ExecutionRef]]


@final
@dataclass(frozen=True, kw_only=True)
class _MessageExecutionRefs:
    """Derived execution references for channel messages."""

    produced_by_key: _ExecutionRefsByMessage
    consumed_keys: frozenset[_MessageKey]


def build_anomaly_report(journal: journal_pb2.JournalFile) -> AnomalyReportModel:
    """Build deterministic anomalies from the current journal data."""
    items: list[AnomalyModel] = []
    items.extend(_readiness_gap_anomalies(journal.metadata.replay_readiness.gaps))
    items.extend(_missing_input_anomalies(journal.metadata.replay_readiness))
    items.extend(_requeue_anomalies(journal))
    items.extend(_timing_anomalies(journal))
    items.extend(_message_sequence_anomalies(journal))
    items.extend(_capability_warnings(journal))
    anomalies = _with_ids(items)
    return AnomalyReportModel(summary_counts=_summary_counts(anomalies), items=anomalies)


def _readiness_gap_anomalies(gaps: Sequence[journal_pb2.ReplayReadinessGap]) -> tuple[AnomalyModel, ...]:
    items: list[AnomalyModel] = []
    missing_input_messages_by_channel: dict[str, list[str]] = {}
    missing_output_messages_by_channel: dict[str, list[str]] = {}
    for gap in _sorted_gaps(gaps):
        if gap.reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL:
            missing_input_messages_by_channel.setdefault(gap.channel_name, []).append(
                gap.message or _gap_reason_name(gap.reason)
            )
            continue
        if gap.reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA:
            missing_output_messages_by_channel.setdefault(gap.channel_name, []).append(
                gap.message or _gap_reason_name(gap.reason)
            )
            continue
        items.append(_anomaly_from_gap(gap))
    items.extend(
        AnomalyModel(
            id="",
            type="missing_input",
            severity="warning",
            title="Missing input channel",
            message=_grouped_gap_message(
                tuple(messages),
                message_prefix="Input channel is missing",
                fallback_reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
            ),
            cog_instance_path="",
            channel_name=channel_name,
            execution_index=None,
        )
        for channel_name, messages in sorted(missing_input_messages_by_channel.items())
    )
    items.extend(
        AnomalyModel(
            id="",
            type="missing_output",
            severity="warning",
            title="Missing output message data",
            message=_grouped_gap_message(
                tuple(messages),
                message_prefix="Output message data is missing",
                fallback_reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA,
            ),
            cog_instance_path="",
            channel_name=channel_name,
            execution_index=None,
        )
        for channel_name, messages in sorted(missing_output_messages_by_channel.items())
    )
    return tuple(items)


def _anomaly_from_gap(gap: journal_pb2.ReplayReadinessGap) -> AnomalyModel:
    anomaly_type = _gap_anomaly_type(gap.reason)
    title = _gap_title(anomaly_type)
    # fmt: off
    return AnomalyModel(
        id="",
        type=anomaly_type,
        severity="warning",
        title=title,
        message=gap.message or _gap_reason_name(gap.reason),
        cog_instance_path=gap.cog_instance_path,
        channel_name=gap.channel_name,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        execution_index=int(gap.execution_index),
    )
    # fmt: on


def _grouped_gap_message(messages: Sequence[str], *, message_prefix: str, fallback_reason: int) -> str:
    messages = tuple(dict.fromkeys(message for message in messages if message))
    if len(messages) == 1:
        return messages[0]
    if messages:
        displayed_messages = messages[:_GROUPED_DETAIL_DISPLAY_LIMIT]
        detail_text = "; ".join(displayed_messages)
        omitted_count = len(messages) - len(displayed_messages)
        if omitted_count > 0:
            detail_label = "detail" if omitted_count == 1 else "details"
            detail_text = f"{detail_text}; {omitted_count} more {detail_label} omitted"
        return f"{message_prefix}. Details: {detail_text}"
    return _gap_reason_name(fallback_reason)


def _missing_input_anomalies(readiness: journal_pb2.ReplayReadiness) -> tuple[AnomalyModel, ...]:
    gap_channels = {
        gap.channel_name
        for gap in readiness.gaps
        if gap.reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL
    }
    return tuple(
        AnomalyModel(
            id="",
            type="missing_input",
            severity="warning",
            title="Missing input channel",
            message="Input channel is listed as missing without a structured readiness gap.",
            cog_instance_path="",
            channel_name=channel_name,
            execution_index=None,
        )
        for channel_name in sorted(readiness.missing_inputs)
        if channel_name not in gap_channels
    )


def _requeue_anomalies(journal: journal_pb2.JournalFile) -> tuple[AnomalyModel, ...]:
    # fmt: off
    return tuple(
        AnomalyModel(
            id="",
            type="requeue",
            severity="warning",
            title="Execution requeued",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            message=f"Execution was requeued {int(execution.requeue_count)} time(s).",
            cog_instance_path=cog.cog_instance_path,
            channel_name="",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            execution_index=int(execution.execution_index),
        )
        for cog in _sorted_cogs(journal.cog_journals)
        for execution in _sorted_executions(cog.executions)
        if execution.requeue_count > 0
    )
    # fmt: on


def _timing_anomalies(journal: journal_pb2.JournalFile) -> tuple[AnomalyModel, ...]:
    execution_refs = tuple(
        (cog.cog_instance_path, execution)
        for cog in _sorted_cogs(journal.cog_journals)
        for execution in _sorted_executions(cog.executions)
    )
    metric_specs = (
        ("high_duration", "High execution duration", "execution_duration_ns"),
        ("high_ready_latency", "High ready-to-execution latency", "ready_to_exec_latency_ns"),
        ("high_attempt_latency", "High attempt-to-execution latency", "attempt_to_exec_latency_ns"),
    )
    items: list[AnomalyModel] = []
    for anomaly_type, title, field_name in metric_specs:
        values = tuple(_execution_metric(execution, field_name) for _, execution in execution_refs)
        threshold = _timing_threshold(values)
        if threshold is None:
            continue
        # fmt: off
        items.extend(
            AnomalyModel(
                id="",
                type=anomaly_type,
                severity="warning",
                title=title,
                message=f"{field_name} meets or exceeds the report-local threshold.",
                cog_instance_path=cog_instance_path,
                channel_name="",
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                execution_index=int(execution.execution_index),
                value_ns=value,
                threshold_ns=threshold,
            )
            for cog_instance_path, execution in execution_refs
            for value in (_execution_metric(execution, field_name),)
            if value >= threshold
        )
        # fmt: on
    return tuple(items)


def _message_sequence_anomalies(journal: journal_pb2.JournalFile) -> tuple[AnomalyModel, ...]:
    messages_by_channel = _message_sequences_by_channel(journal.channel_messages)
    if not messages_by_channel:
        return ()
    message_keys = _message_keys(messages_by_channel)
    execution_refs = _message_execution_refs(journal)
    return _sequence_gap_anomalies(messages_by_channel) + _unconsumed_message_anomalies(
        message_keys,
        execution_refs,
    )


def _sequence_gap_anomalies(messages_by_channel: _MessageSequencesByChannel) -> tuple[AnomalyModel, ...]:
    items: list[AnomalyModel] = []
    for channel_name in sorted(messages_by_channel):
        sequence_numbers = messages_by_channel[channel_name]
        for previous_sequence, current_sequence in itertools.pairwise(sequence_numbers):
            if current_sequence <= previous_sequence + 1:
                continue
            items.append(
                AnomalyModel(
                    id="",
                    type="sequence_gap",
                    severity="warning",
                    title="Message sequence gap",
                    message=f"Sequence numbers {_sequence_gap_label(previous_sequence, current_sequence)} are absent.",
                    cog_instance_path="",
                    channel_name=channel_name,
                    execution_index=None,
                )
            )
    return tuple(items)


def _unconsumed_message_anomalies(
    message_keys: frozenset[_MessageKey],
    execution_refs: _MessageExecutionRefs,
) -> tuple[AnomalyModel, ...]:
    items: list[AnomalyModel] = []
    for key in sorted(execution_refs.produced_by_key):
        if key not in message_keys or key in execution_refs.consumed_keys:
            continue
        channel_name, sequence_number = key
        items.extend(
            AnomalyModel(
                id="",
                type="unconsumed_message",
                severity="warning",
                title="Produced message not consumed",
                message=f"Sequence {sequence_number} has no consuming execution in this journal.",
                cog_instance_path=ref.cog_instance_path,
                channel_name=channel_name,
                execution_index=ref.execution_index,
            )
            for ref in execution_refs.produced_by_key[key]
        )
    return tuple(items)


def _message_sequences_by_channel(
    messages: Sequence[journal_pb2.ChannelMessage],
) -> _MessageSequencesByChannel:
    sequences_by_channel: dict[str, set[int]] = {}
    for message in messages:
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        sequences_by_channel.setdefault(message.channel_name, set()).add(int(message.sequence_number))
    return {
        channel_name: tuple(sorted(sequence_numbers)) for channel_name, sequence_numbers in sequences_by_channel.items()
    }


def _message_keys(messages_by_channel: _MessageSequencesByChannel) -> frozenset[_MessageKey]:
    return frozenset(
        (channel_name, sequence_number)
        for channel_name, sequence_numbers in messages_by_channel.items()
        for sequence_number in sequence_numbers
    )


def _message_execution_refs(journal: journal_pb2.JournalFile) -> _MessageExecutionRefs:
    produced_refs: _MutableExecutionRefsByMessage = {}
    consumed_keys: set[_MessageKey] = set()
    for cog in _sorted_cogs(journal.cog_journals):
        for execution in _sorted_executions(cog.executions):
            # fmt: off
            ref = _ExecutionRef(
                cog_instance_path=cog.cog_instance_path,
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                execution_index=int(execution.execution_index),
            )
            # fmt: on
            for output in sorted(execution.outputs, key=lambda item: item.channel_name):
                for sequence in output.produced_sequence_numbers:
                    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                    _append_execution_ref(produced_refs, (output.channel_name, int(sequence)), ref)
            for input_view in sorted(execution.input_views, key=lambda item: (item.channel_name, item.cog_member_name)):
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                visible = tuple(int(sequence) for sequence in input_view.visible_sequence_numbers)
                # fmt: off
                consumed_keys.update(
                    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                    (input_view.channel_name, sequence) for sequence in visible[int(input_view.first_new_index) :]
                )
                # fmt: on
    return _MessageExecutionRefs(
        produced_by_key={key: tuple(refs) for key, refs in produced_refs.items()},
        consumed_keys=frozenset(consumed_keys),
    )


def _append_execution_ref(
    refs_by_key: _MutableExecutionRefsByMessage,
    key: _MessageKey,
    ref: _ExecutionRef,
) -> None:
    refs = refs_by_key.setdefault(key, [])
    if ref not in refs:
        refs.append(ref)


def _sequence_gap_label(previous_sequence: int, current_sequence: int) -> str:
    first_missing = previous_sequence + 1
    last_missing = current_sequence - 1
    if first_missing == last_missing:
        return str(first_missing)
    return f"{first_missing}-{last_missing}"


def _capability_warnings(journal: journal_pb2.JournalFile) -> tuple[AnomalyModel, ...]:
    if (
        len(journal.channel_summaries) == 0
        or len(journal.channel_messages) > 0
        or not _has_observed_channel_messages(journal.channel_summaries)
    ):
        return ()
    return (
        AnomalyModel(
            id="",
            type="capability_warning",
            severity="info",
            title="Message sequence detail unavailable",
            message=(
                "This journal does not include report channel message records; "
                "sequence-gap and unconsumed-message anomalies are unavailable."
            ),
            cog_instance_path="",
            channel_name="",
            execution_index=None,
        ),
    )


def _has_observed_channel_messages(channel_summaries: Sequence[journal_pb2.ChannelSummary]) -> bool:
    return any(channel.has_logged_messages and channel.message_count > 0 for channel in channel_summaries)


def _timing_threshold(values: tuple[int, ...]) -> int | None:
    positive_values = tuple(sorted(value for value in values if value > 0))
    if len(positive_values) < _TIMING_SAMPLE_MIN:
        return None
    median = _median(positive_values)
    threshold = _nearest_rank_percentile(positive_values, _TIMING_PERCENTILE)
    if threshold < median * 2:
        return None
    return threshold


def _median(values: tuple[int, ...]) -> int:
    middle = len(values) // 2
    if len(values) % 2 == 1:
        return values[middle]
    return (values[middle - 1] + values[middle]) // 2


def _nearest_rank_percentile(values: tuple[int, ...], percentile: int) -> int:
    rank = ((percentile * len(values)) + 99) // 100
    return values[max(0, rank - 1)]


def _execution_metric(execution: journal_pb2.CogExecution, field_name: str) -> int:
    if field_name == "execution_duration_ns":
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return int(execution.execution_duration_ns)
    if field_name == "ready_to_exec_latency_ns":
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return int(execution.ready_to_exec_latency_ns)
    if field_name == "attempt_to_exec_latency_ns":
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return int(execution.attempt_to_exec_latency_ns)
    msg = f"Unsupported execution metric: {field_name}"
    raise ValueError(msg)


def _with_ids(items: list[AnomalyModel]) -> tuple[AnomalyModel, ...]:
    sorted_items = sorted(
        items,
        key=lambda item: (
            _SEVERITY_ORDER[item.severity],
            item.type,
            item.cog_instance_path,
            -1 if item.execution_index is None else item.execution_index,
            item.channel_name,
            item.message,
        ),
    )
    return tuple(replace(item, id=f"anomaly-{index}") for index, item in enumerate(sorted_items))


def _summary_counts(items: tuple[AnomalyModel, ...]) -> tuple[AnomalyCountModel, ...]:
    counts: dict[str, int] = {}
    for item in items:
        counts[item.type] = counts.get(item.type, 0) + 1
    return tuple(AnomalyCountModel(label=label, count=counts[label]) for label in sorted(counts))


def _gap_anomaly_type(reason: int) -> str:
    if reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL:
        return "missing_input"
    if reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA:
        return "missing_output"
    if reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT:
        return "missing_alignment"
    if reason in _MISSING_METADATA_REASONS:
        return "missing_metadata"
    return "readiness_gap"


def _gap_title(anomaly_type: str) -> str:
    if anomaly_type == "missing_input":
        return "Missing input channel"
    if anomaly_type == "missing_output":
        return "Missing output message data"
    if anomaly_type == "missing_alignment":
        return "Missing alignment result"
    if anomaly_type == "missing_metadata":
        return "Missing journal metadata"
    return "Replay-readiness gap"


def _sorted_cogs(cogs: Sequence[journal_pb2.CogJournal]) -> tuple[journal_pb2.CogJournal, ...]:
    return tuple(sorted(cogs, key=lambda item: (item.cog_instance_path, item.cog_path)))


def _sorted_executions(executions: Sequence[journal_pb2.CogExecution]) -> tuple[journal_pb2.CogExecution, ...]:
    return tuple(sorted(executions, key=lambda item: (int(item.execution_index), int(item.execution_start_time_ns))))


def _sorted_gaps(gaps: Sequence[journal_pb2.ReplayReadinessGap]) -> tuple[journal_pb2.ReplayReadinessGap, ...]:
    return tuple(
        sorted(
            gaps,
            key=lambda item: (
                int(item.reason),
                item.cog_instance_path,
                item.channel_name,
                int(item.execution_index),
                item.message,
            ),
        )
    )


def _gap_reason_name(reason: int) -> str:
    try:
        return journal_pb2.ReplayReadinessGapReason.Name(reason)
    except ValueError:
        return f"UNKNOWN_REPLAY_READINESS_GAP_REASON_{reason}"
