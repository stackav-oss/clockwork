# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""JSON-friendly view model for journal reports."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
from typing import TYPE_CHECKING, TypedDict, final

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.anomalies import AnomalyReportModel, build_anomaly_report

if TYPE_CHECKING:
    from collections.abc import Sequence


@final
@dataclass(frozen=True, kw_only=True)
class ReportModel:
    """Top-level report data embedded in the HTML output."""

    schema_version: int
    title: str
    metadata: MetadataModel
    overview_cards: tuple[OverviewCard, ...]
    readiness: ReadinessModel
    anomalies: AnomalyReportModel
    timeline: TimelineModel
    channel_flow: ChannelFlowModel
    cogs: tuple[CogModel, ...]
    channels: tuple[ChannelModel, ...]
    state_snapshots: tuple[SnapshotModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class MetadataModel:
    """Source journal metadata."""

    log_uri: str
    start_time_ns: int
    end_time_ns: int
    generator_version: str
    requested_cog_instance_paths: tuple[str, ...]
    cog_count: int
    execution_count: int
    channel_count: int


@final
@dataclass(frozen=True, kw_only=True)
class OverviewCard:
    """One overview value."""

    label: str
    value: str


@final
@dataclass(frozen=True, kw_only=True)
class ReadinessModel:
    """Replay-readiness status and missing data."""

    status: str
    sufficient_for_replay: bool
    has_state_snapshot: bool
    missing_inputs: tuple[str, ...]
    gaps: tuple[ReadinessGapModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class ReadinessGapModel:
    """One structured replay-readiness gap."""

    id: str
    reason: str
    message: str
    cog_instance_path: str
    channel_name: str
    execution_index: int


@final
@dataclass(frozen=True, kw_only=True)
class ConditionModel:
    """One expanded condition flag."""

    name: str
    value: bool


@final
class InputViewModel(TypedDict):
    """One execution input view."""

    id: str
    channel_name: str
    cog_member_name: str
    cursor_sequence_number: int
    visible_sequence_numbers: tuple[int, ...]
    first_new_index: int
    new_sequence_numbers: tuple[int, ...]
    visible_sequence_message_ids: dict[int, str]


@final
class OutputModel(TypedDict):
    """One execution output."""

    id: str
    channel_name: str
    produced_sequence_numbers: tuple[int, ...]
    produced_sequence_message_ids: dict[int, str]


@final
class AlignedInputModel(TypedDict):
    """One alignment result input."""

    input_name: str
    selected_sequence_number: int
    present: bool
    is_reused: bool
    is_batch: bool
    batch_begin_sequence_number: int
    batch_end_sequence_number: int


@final
class AlignmentModel(TypedDict):
    """Execution alignment result."""

    aligner_name: str
    aligned_inputs: tuple[AlignedInputModel, ...]


@final
class ExecutionModel(TypedDict):
    """One cog execution."""

    id: str
    execution_index: int
    execution_start_time_ns: int
    execution_duration_ns: int
    ready_to_exec_latency_ns: int
    attempt_to_exec_latency_ns: int
    requeue_count: int
    conditions: tuple[ConditionModel, ...]
    input_views: tuple[InputViewModel, ...]
    outputs: tuple[OutputModel, ...]
    alignment: AlignmentModel | None
    memory_stats: tuple[MemoryStatModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class MemoryStatModel:
    """Memory statistics recorded for one resource during an execution."""

    resource_name: str
    peak_allocated: int
    current_allocated: int
    total_allocated: int
    total_deallocated: int


@final
@dataclass(frozen=True, kw_only=True)
class MemoryTimelinePointModel:
    """One execution current-memory sample used by the cog memory graph."""

    execution_id: str
    execution_index: int
    time_ns: int
    current_allocated: int


@final
@dataclass(frozen=True, kw_only=True)
class MemoryTimelineSeriesModel:
    """Current-memory samples for one memory resource."""

    resource_name: str
    points: tuple[MemoryTimelinePointModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class MemoryTimelineModel:
    """Per-cog peak-memory samples over execution time."""

    start_time_ns: int
    end_time_ns: int
    series: tuple[MemoryTimelineSeriesModel, ...]


@final
class ChannelModel(TypedDict):
    """One channel summary."""

    id: str
    channel_name: str
    producer_cog_instance: str
    consumer_cog_instances: tuple[str, ...]
    first_sequence_number: int
    last_sequence_number: int
    message_count: int
    has_logged_messages: bool
    messages: tuple[ChannelMessageModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class TimelineModel:
    """Execution timeline data."""

    start_time_ns: int
    end_time_ns: int
    rows: tuple[TimelineRowModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class TimelineRowModel:
    """One cog row in the execution timeline."""

    id: str
    cog_id: str
    cog_instance_path: str
    bars: tuple[TimelineBarModel, ...]


@final
class DurationBucket(str, Enum):
    """Execution-duration bucket used for timeline styling."""

    ZERO = "zero"
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"


@final
@dataclass(frozen=True, kw_only=True)
class TimelineBarModel:
    """One execution bar in the timeline."""

    id: str
    execution_id: str
    execution_index: int
    start_time_ns: int
    end_time_ns: int
    duration_ns: int
    duration_bucket: DurationBucket


@final
@dataclass(frozen=True, kw_only=True)
class ChannelFlowModel:
    """Channel topology data for the flow diagram."""

    nodes: tuple[ChannelFlowNodeModel, ...]
    edges: tuple[ChannelFlowEdgeModel, ...]


@final
@dataclass(frozen=True, kw_only=True)
class ChannelFlowNodeModel:
    """One node in the channel flow diagram."""

    id: str
    label: str
    layer: int
    row: int
    is_unknown: bool


@final
@dataclass(frozen=True, kw_only=True)
class ChannelFlowEdgeModel:
    """One channel edge in the channel flow diagram."""

    id: str
    channel_id: str
    channel_name: str
    source_node_id: str
    target_node_id: str
    message_count: int
    first_sequence_number: int
    last_sequence_number: int
    label: str


@final
@dataclass(frozen=True, kw_only=True)
class MessageExecutionLinkModel:
    """Link from one channel message to an execution detail."""

    execution_id: str
    cog_instance_path: str
    execution_index: int
    label: str


@final
@dataclass(frozen=True, kw_only=True)
class ChannelMessageModel:
    """One report-indexed channel message."""

    id: str
    channel_id: str
    channel_name: str
    sequence_number: int
    publish_time_ns: int
    payload_size_bytes: int
    producer_links: tuple[MessageExecutionLinkModel, ...]
    consumer_links: tuple[MessageExecutionLinkModel, ...]
    is_produced_unconsumed: bool


@final
@dataclass(frozen=True, kw_only=True)
class SnapshotModel:
    """Selected cog state snapshot metadata."""

    id: str
    cog_id: str
    cog_instance_path: str
    snapshot_time_ns: int
    selection: str
    state_schema_uuid: str
    state_size_bytes: int


@final
@dataclass(frozen=True, kw_only=True)
class CogModel:
    """One cog and its execution data."""

    id: str
    cog_path: str
    cog_instance_path: str
    condition_names: tuple[str, ...]
    executions: tuple[ExecutionModel, ...]
    memory_timeline: MemoryTimelineModel | None
    snapshot: SnapshotModel | None


@final
@dataclass(frozen=True, kw_only=True)
class _MessageExecutionLinks:
    """Derived execution links keyed by channel and sequence number."""

    producers_by_key: dict[tuple[str, int], tuple[MessageExecutionLinkModel, ...]]
    consumers_by_key: dict[tuple[str, int], tuple[MessageExecutionLinkModel, ...]]


def build_report_model(journal: journal_pb2.JournalFile, *, title: str) -> ReportModel:
    """Convert a journal protobuf into deterministic report data."""
    sorted_cogs = _sorted_cogs(journal.cog_journals)
    message_links = _build_message_execution_links(sorted_cogs)
    channels = _build_channels(
        journal.channel_summaries,
        _unique_channel_messages(journal.channel_messages),
        message_links,
    )
    message_ids_by_key = _message_ids_by_key(channels)
    cog_records: list[CogModel] = []
    snapshots: list[SnapshotModel] = []
    for index, cog in enumerate(sorted_cogs):
        cog_record = _build_cog(index, cog, message_ids_by_key)
        cog_records.append(cog_record)
        if cog_record.snapshot is not None:
            snapshots.append(cog_record.snapshot)

    # fmt: off
    metadata = MetadataModel(
        log_uri=journal.metadata.log_uri,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        start_time_ns=int(journal.metadata.start_time_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        end_time_ns=int(journal.metadata.end_time_ns),
        generator_version=journal.metadata.generator_version,
        requested_cog_instance_paths=tuple(sorted(journal.metadata.scope.cog_instance_paths)),
        cog_count=len(cog_records),
        execution_count=sum(len(cog.executions) for cog in sorted_cogs),
        channel_count=len(channels),
    )
    # fmt: on
    readiness = _build_readiness(journal.metadata.replay_readiness)
    anomalies = build_anomaly_report(journal)
    timeline = _build_timeline(sorted_cogs, metadata.start_time_ns, metadata.end_time_ns)
    channel_flow = _build_channel_flow(cog_records, channels)
    return ReportModel(
        schema_version=9,
        title=title,
        metadata=metadata,
        overview_cards=_build_overview_cards(
            metadata,
            readiness,
            anomaly_count=len(anomalies.items),
            snapshot_count=len(snapshots),
        ),
        readiness=readiness,
        anomalies=anomalies,
        timeline=timeline,
        channel_flow=channel_flow,
        cogs=tuple(cog_records),
        channels=channels,
        state_snapshots=tuple(snapshots),
    )


def report_model_to_dict(model: ReportModel) -> dict[str, object]:
    """Return a JSON-serializable dictionary for a report model."""
    # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return asdict(model)


def expand_condition_flags(condition_names: Sequence[str], condition_flags: int) -> tuple[ConditionModel, ...]:
    """Expand condition bit flags using the journal's condition-name order."""
    return tuple(
        ConditionModel(name=name, value=bool(condition_flags & (1 << index)))
        for index, name in enumerate(condition_names)
    )


def _build_overview_cards(
    metadata: MetadataModel,
    readiness: ReadinessModel,
    *,
    anomaly_count: int,
    snapshot_count: int,
) -> tuple[OverviewCard, ...]:
    return (
        OverviewCard(label="Cogs", value=str(metadata.cog_count)),
        OverviewCard(label="Executions", value=str(metadata.execution_count)),
        OverviewCard(label="Channels", value=str(metadata.channel_count)),
        OverviewCard(label="Readiness", value=readiness.status),
        OverviewCard(label="Anomalies", value=str(anomaly_count)),
        OverviewCard(label="State Snapshots", value=str(snapshot_count)),
    )


def _build_readiness(readiness: journal_pb2.ReplayReadiness) -> ReadinessModel:
    return ReadinessModel(
        status="sufficient" if readiness.sufficient_for_replay else "insufficient",
        sufficient_for_replay=readiness.sufficient_for_replay,
        has_state_snapshot=readiness.has_state_snapshot,
        missing_inputs=tuple(sorted(readiness.missing_inputs)),
        gaps=tuple(_build_gap(index, gap) for index, gap in enumerate(_sorted_gaps(readiness.gaps))),
    )


def _build_gap(index: int, gap: journal_pb2.ReplayReadinessGap) -> ReadinessGapModel:
    # fmt: off
    return ReadinessGapModel(
        id=f"gap-{index}",
        reason=_gap_reason_name(gap.reason),
        message=gap.message,
        cog_instance_path=gap.cog_instance_path,
        channel_name=gap.channel_name,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        execution_index=int(gap.execution_index),
    )
    # fmt: on


def _build_cog(
    index: int,
    cog: journal_pb2.CogJournal,
    message_ids_by_key: dict[tuple[str, int], str],
) -> CogModel:
    cog_id = f"cog-{index}"
    condition_names = tuple(cog.condition_names)
    snapshot = (
        _build_snapshot(cog_id, cog.cog_instance_path, cog.state_snapshot) if cog.HasField("state_snapshot") else None
    )
    executions = tuple(
        _build_execution(cog_id, execution, condition_names, message_ids_by_key)
        for execution in _sorted_executions(cog.executions)
    )
    return CogModel(
        id=cog_id,
        cog_path=cog.cog_path,
        cog_instance_path=cog.cog_instance_path,
        condition_names=condition_names,
        executions=executions,
        memory_timeline=_build_memory_timeline(executions),
        snapshot=snapshot,
    )


def _build_execution(
    cog_id: str,
    execution: journal_pb2.CogExecution,
    condition_names: Sequence[str],
    message_ids_by_key: dict[tuple[str, int], str],
) -> ExecutionModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    execution_id = f"{cog_id}-execution-{int(execution.execution_index)}"
    # fmt: off
    return ExecutionModel(
        id=execution_id,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        execution_index=int(execution.execution_index),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        execution_start_time_ns=int(execution.execution_start_time_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        execution_duration_ns=int(execution.execution_duration_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        ready_to_exec_latency_ns=int(execution.ready_to_exec_latency_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        attempt_to_exec_latency_ns=int(execution.attempt_to_exec_latency_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        requeue_count=int(execution.requeue_count),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        conditions=expand_condition_flags(condition_names, int(execution.condition_flags)),
        input_views=tuple(
            _build_input(execution_id, index, item, message_ids_by_key)
            for index, item in enumerate(_sorted_inputs(execution.input_views))
        ),
        outputs=tuple(
            _build_output(execution_id, index, item, message_ids_by_key)
            for index, item in enumerate(_sorted_outputs(execution.outputs))
        ),
        alignment=_build_alignment(execution.alignment_result) if execution.HasField("alignment_result") else None,
        memory_stats=tuple(
            MemoryStatModel(
                resource_name=memory_stat.resource_name,
                peak_allocated=memory_stat.peak_allocated,
                current_allocated=memory_stat.current_allocated,
                total_allocated=memory_stat.total_allocated,
                total_deallocated=memory_stat.total_deallocated,
            )
            for memory_stat in _sorted_memory_stats(execution.memory_stats)
        ),
    )


def _build_memory_timeline(executions: Sequence[ExecutionModel]) -> MemoryTimelineModel | None:
    series_points: dict[str, list[MemoryTimelinePointModel]] = {}
    for execution in executions:
        for memory_stat in execution["memory_stats"]:
            series_points.setdefault(memory_stat.resource_name, []).append(
                MemoryTimelinePointModel(
                    execution_id=execution["id"],
                    execution_index=execution["execution_index"],
                    time_ns=execution["execution_start_time_ns"],
                    current_allocated=memory_stat.current_allocated,
                )
            )
    if not series_points:
        return None
    series = tuple(
        MemoryTimelineSeriesModel(
            resource_name=resource_name,
            points=tuple(sorted(points, key=lambda point: (point.time_ns, point.execution_index, point.execution_id))),
        )
        for resource_name, points in sorted(series_points.items())
    )
    all_times = tuple(point.time_ns for item in series for point in item.points)
    start_time_ns = min(all_times)
    end_time_ns = max(all_times)
    return MemoryTimelineModel(
        start_time_ns=start_time_ns,
        end_time_ns=end_time_ns if end_time_ns > start_time_ns else start_time_ns + 1,
        series=series,
    )
    # fmt: on


def _build_input(
    execution_id: str,
    index: int,
    input_view: journal_pb2.InputViewState,
    message_ids_by_key: dict[tuple[str, int], str],
) -> InputViewModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    visible = tuple(int(sequence) for sequence in input_view.visible_sequence_numbers)
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    first_new_index = int(input_view.first_new_index)
    # fmt: off
    return InputViewModel(
        id=f"{execution_id}-input-{index}",
        channel_name=input_view.channel_name,
        cog_member_name=input_view.cog_member_name,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        cursor_sequence_number=int(input_view.cursor_sequence_number),
        visible_sequence_numbers=visible,
        first_new_index=first_new_index,
        new_sequence_numbers=visible[first_new_index:],
        visible_sequence_message_ids=_message_ids_for_sequences(
            input_view.channel_name,
            visible,
            message_ids_by_key,
        ),
    )
    # fmt: on


def _build_output(
    execution_id: str,
    index: int,
    output: journal_pb2.OutputState,
    message_ids_by_key: dict[tuple[str, int], str],
) -> OutputModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    produced_sequences = tuple(int(sequence) for sequence in output.produced_sequence_numbers)
    return OutputModel(
        id=f"{execution_id}-output-{index}",
        channel_name=output.channel_name,
        produced_sequence_numbers=produced_sequences,
        produced_sequence_message_ids=_message_ids_for_sequences(
            output.channel_name,
            produced_sequences,
            message_ids_by_key,
        ),
    )


def _build_timeline(
    cogs: Sequence[journal_pb2.CogJournal],
    metadata_start_time_ns: int,
    metadata_end_time_ns: int,
) -> TimelineModel:
    # fmt: off
    durations = tuple(
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        int(execution.execution_duration_ns)
        for cog in cogs
        for execution in cog.executions
        if execution.execution_duration_ns > 0
    )
    # fmt: on
    median_duration = _nearest_rank_percentile(durations, 50)
    high_duration = _nearest_rank_percentile(durations, 90)
    rows: list[TimelineRowModel] = []
    for index, cog in enumerate(cogs):
        cog_id = f"cog-{index}"
        bars = tuple(
            _build_timeline_bar(cog_id, execution, median_duration, high_duration)
            for execution in _sorted_executions(cog.executions)
        )
        if bars:
            rows.append(
                TimelineRowModel(
                    id=f"timeline-row-{index}",
                    cog_id=cog_id,
                    cog_instance_path=cog.cog_instance_path,
                    bars=bars,
                )
            )
    return TimelineModel(
        start_time_ns=_timeline_start_time(rows, metadata_start_time_ns),
        end_time_ns=_timeline_end_time(rows, metadata_start_time_ns, metadata_end_time_ns),
        rows=tuple(rows),
    )


def _build_timeline_bar(
    cog_id: str,
    execution: journal_pb2.CogExecution,
    median_duration_ns: int | None,
    high_duration_ns: int | None,
) -> TimelineBarModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    duration_ns = max(0, int(execution.execution_duration_ns))
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    start_time_ns = int(execution.execution_start_time_ns)
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    execution_index = int(execution.execution_index)
    return TimelineBarModel(
        id=f"{cog_id}-timeline-{execution_index}",
        execution_id=f"{cog_id}-execution-{execution_index}",
        execution_index=execution_index,
        start_time_ns=start_time_ns,
        end_time_ns=start_time_ns + duration_ns,
        duration_ns=duration_ns,
        duration_bucket=_duration_bucket(duration_ns, median_duration_ns, high_duration_ns),
    )


def _duration_bucket(duration_ns: int, median_duration_ns: int | None, high_duration_ns: int | None) -> DurationBucket:
    if duration_ns <= 0:
        return DurationBucket.ZERO
    if median_duration_ns is None or high_duration_ns is None or duration_ns <= median_duration_ns:
        return DurationBucket.LOW
    if duration_ns < high_duration_ns:
        return DurationBucket.MEDIUM
    return DurationBucket.HIGH


def _timeline_start_time(rows: Sequence[TimelineRowModel], metadata_start_time_ns: int) -> int:
    bar_starts = tuple(bar.start_time_ns for row in rows for bar in row.bars)
    if not bar_starts:
        return metadata_start_time_ns
    return min(bar_starts)


def _timeline_end_time(
    rows: Sequence[TimelineRowModel],
    metadata_start_time_ns: int,
    metadata_end_time_ns: int,
) -> int:
    start_time_ns = _timeline_start_time(rows, metadata_start_time_ns)
    bar_ends = tuple(bar.end_time_ns for row in rows for bar in row.bars)
    end_time_ns = max(bar_ends) if bar_ends else metadata_end_time_ns
    if end_time_ns <= start_time_ns:
        return start_time_ns + 1
    return end_time_ns


def _nearest_rank_percentile(values: tuple[int, ...], percentile: int) -> int | None:
    if not values:
        return None
    sorted_values = tuple(sorted(values))
    rank = ((percentile * len(sorted_values)) + 99) // 100
    return sorted_values[max(0, rank - 1)]


def _build_alignment(alignment: journal_pb2.AlignmentResult) -> AlignmentModel:
    # fmt: off
    return AlignmentModel(
        aligner_name=alignment.aligner_name,
        aligned_inputs=tuple(
            AlignedInputModel(
                input_name=item.input_name,
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                selected_sequence_number=int(item.selected_sequence_number),
                present=item.present,
                is_reused=item.is_reused,
                is_batch=item.is_batch,
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                batch_begin_sequence_number=int(item.batch_begin_sequence_number),
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                batch_end_sequence_number=int(item.batch_end_sequence_number),
            )
            for item in sorted(alignment.aligned_inputs, key=lambda value: value.input_name)
        ),
    )
    # fmt: on


def _build_snapshot(cog_id: str, cog_instance_path: str, snapshot: journal_pb2.StateSnapshot) -> SnapshotModel:
    # fmt: off
    return SnapshotModel(
        id=f"{cog_id}-snapshot",
        cog_id=cog_id,
        cog_instance_path=cog_instance_path,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        snapshot_time_ns=int(snapshot.snapshot_time_ns),
        selection=_snapshot_selection_name(snapshot.selection),
        state_schema_uuid=snapshot.state_schema_uuid,
        state_size_bytes=len(snapshot.state_data),
    )
    # fmt: on


def _build_channels(
    channel_summaries: Sequence[journal_pb2.ChannelSummary],
    channel_messages: Sequence[journal_pb2.ChannelMessage],
    message_links: _MessageExecutionLinks,
) -> tuple[ChannelModel, ...]:
    messages_by_channel = _channel_messages_by_channel(channel_messages)
    channels: list[ChannelModel] = []
    seen_channel_names: set[str] = set()
    for channel in _sorted_channels(channel_summaries):
        seen_channel_names.add(channel.channel_name)
        channels.append(
            _build_channel(
                len(channels),
                channel,
                messages_by_channel.get(channel.channel_name, ()),
                message_links,
            )
        )
    for channel_name in sorted(set(messages_by_channel) - seen_channel_names):
        channels.append(
            _build_message_only_channel(
                len(channels),
                channel_name,
                messages_by_channel[channel_name],
                message_links,
            )
        )
    return tuple(channels)


def _build_channel(
    index: int,
    channel: journal_pb2.ChannelSummary,
    messages: Sequence[journal_pb2.ChannelMessage],
    message_links: _MessageExecutionLinks,
) -> ChannelModel:
    channel_id = f"channel-{index}"
    # fmt: off
    return ChannelModel(
        id=channel_id,
        channel_name=channel.channel_name,
        producer_cog_instance=channel.producer_cog_instance,
        consumer_cog_instances=tuple(sorted(channel.consumer_cog_instances)),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        first_sequence_number=int(channel.first_sequence_number),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        last_sequence_number=int(channel.last_sequence_number),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        message_count=int(channel.message_count),
        has_logged_messages=channel.has_logged_messages,
        messages=_build_channel_messages(channel_id, messages, message_links),
    )
    # fmt: on


def _build_message_only_channel(
    index: int,
    channel_name: str,
    messages: Sequence[journal_pb2.ChannelMessage],
    message_links: _MessageExecutionLinks,
) -> ChannelModel:
    channel_id = f"channel-{index}"
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    sequence_numbers = tuple(int(message.sequence_number) for message in messages)
    return ChannelModel(
        id=channel_id,
        channel_name=channel_name,
        producer_cog_instance="",
        consumer_cog_instances=(),
        first_sequence_number=min(sequence_numbers, default=0),
        last_sequence_number=max(sequence_numbers, default=0),
        message_count=len(sequence_numbers),
        has_logged_messages=bool(messages),
        messages=_build_channel_messages(channel_id, messages, message_links),
    )


def _build_channel_messages(
    channel_id: str,
    messages: Sequence[journal_pb2.ChannelMessage],
    message_links: _MessageExecutionLinks,
) -> tuple[ChannelMessageModel, ...]:
    return tuple(
        _build_channel_message(channel_id, message, message_links) for message in _sorted_channel_messages(messages)
    )


def _build_channel_message(
    channel_id: str,
    message: journal_pb2.ChannelMessage,
    message_links: _MessageExecutionLinks,
) -> ChannelMessageModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    sequence_number = int(message.sequence_number)
    key = (message.channel_name, sequence_number)
    producer_links = message_links.producers_by_key.get(key, ())
    consumer_links = message_links.consumers_by_key.get(key, ())
    # fmt: off
    return ChannelMessageModel(
        id=f"message-{channel_id}-{sequence_number}",
        channel_id=channel_id,
        channel_name=message.channel_name,
        sequence_number=sequence_number,
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        publish_time_ns=int(message.publish_time_ns),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        payload_size_bytes=int(message.payload_size_bytes),
        producer_links=producer_links,
        consumer_links=consumer_links,
        is_produced_unconsumed=bool(producer_links) and not consumer_links,
    )
    # fmt: on


def _build_message_execution_links(cogs: Sequence[journal_pb2.CogJournal]) -> _MessageExecutionLinks:
    producer_links: dict[tuple[str, int], list[MessageExecutionLinkModel]] = {}
    consumer_links: dict[tuple[str, int], list[MessageExecutionLinkModel]] = {}
    for cog_index, cog in enumerate(cogs):
        cog_id = f"cog-{cog_index}"
        for execution in _sorted_executions(cog.executions):
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            execution_index = int(execution.execution_index)
            link = MessageExecutionLinkModel(
                execution_id=f"{cog_id}-execution-{execution_index}",
                cog_instance_path=cog.cog_instance_path,
                execution_index=execution_index,
                label=f"{_short_cog_label(cog.cog_instance_path)} execution {execution_index}",
            )
            for output in _sorted_outputs(execution.outputs):
                for sequence in output.produced_sequence_numbers:
                    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                    _append_message_link(producer_links, (output.channel_name, int(sequence)), link)
            for input_view in _sorted_inputs(execution.input_views):
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                visible = tuple(int(sequence) for sequence in input_view.visible_sequence_numbers)
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                for sequence in visible[int(input_view.first_new_index) :]:
                    _append_message_link(consumer_links, (input_view.channel_name, sequence), link)
    return _MessageExecutionLinks(
        producers_by_key=_freeze_link_map(producer_links),
        consumers_by_key=_freeze_link_map(consumer_links),
    )


def _append_message_link(
    links_by_key: dict[tuple[str, int], list[MessageExecutionLinkModel]],
    key: tuple[str, int],
    link: MessageExecutionLinkModel,
) -> None:
    links = links_by_key.setdefault(key, [])
    if link not in links:
        links.append(link)


def _freeze_link_map(
    links_by_key: dict[tuple[str, int], list[MessageExecutionLinkModel]],
) -> dict[tuple[str, int], tuple[MessageExecutionLinkModel, ...]]:
    return {key: tuple(links) for key, links in links_by_key.items()}


def _unique_channel_messages(
    messages: Sequence[journal_pb2.ChannelMessage],
) -> tuple[journal_pb2.ChannelMessage, ...]:
    unique_messages: list[journal_pb2.ChannelMessage] = []
    seen_keys: set[tuple[str, int]] = set()
    for message in sorted(
        messages,
        key=lambda item: (
            item.channel_name,
            int(item.publish_time_ns),
            int(item.sequence_number),
            int(item.payload_size_bytes),
        ),
    ):
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        key = (message.channel_name, int(message.sequence_number))
        if key in seen_keys:
            continue
        seen_keys.add(key)
        unique_messages.append(message)
    return tuple(unique_messages)


def _channel_messages_by_channel(
    messages: Sequence[journal_pb2.ChannelMessage],
) -> dict[str, tuple[journal_pb2.ChannelMessage, ...]]:
    grouped_messages: dict[str, list[journal_pb2.ChannelMessage]] = {}
    for message in messages:
        grouped_messages.setdefault(message.channel_name, []).append(message)
    return {channel_name: tuple(channel_messages) for channel_name, channel_messages in grouped_messages.items()}


def _message_ids_by_key(channels: Sequence[ChannelModel]) -> dict[tuple[str, int], str]:
    ids_by_key: dict[tuple[str, int], str] = {}
    for channel in channels:
        for message in channel["messages"]:
            ids_by_key[(message.channel_name, message.sequence_number)] = message.id
    return ids_by_key


def _message_ids_for_sequences(
    channel_name: str,
    sequences: Sequence[int],
    message_ids_by_key: dict[tuple[str, int], str],
) -> dict[int, str]:
    return {
        sequence: message_ids_by_key[(channel_name, sequence)]
        for sequence in sequences
        if (channel_name, sequence) in message_ids_by_key
    }


def _build_channel_flow(
    cogs: Sequence[CogModel],
    channels: Sequence[ChannelModel],
) -> ChannelFlowModel:
    labels_by_id = {cog.id: cog.cog_instance_path for cog in cogs}
    node_ids_by_path = {label: node_id for node_id, label in labels_by_id.items()}
    edges: list[ChannelFlowEdgeModel] = []
    for channel in channels:
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        source_id = _flow_node_id(str(channel["producer_cog_instance"]), node_ids_by_path, labels_by_id)
        consumers = _channel_consumers(channel)
        if not consumers:
            labels_by_id.setdefault("unknown-sink", "unknown sink")
            edges.append(_build_channel_flow_edge(channel, source_id, "unknown-sink", len(edges)))
        else:
            for consumer in consumers:
                target_id = _flow_node_id(consumer, node_ids_by_path, labels_by_id)
                edges.append(_build_channel_flow_edge(channel, source_id, target_id, len(edges)))

    layers = _flow_layers(tuple(labels_by_id), tuple(edges))
    rows_by_node = _flow_rows(labels_by_id, layers)
    nodes = tuple(
        ChannelFlowNodeModel(
            id=node_id,
            label=labels_by_id[node_id],
            layer=layers[node_id],
            row=rows_by_node[node_id],
            is_unknown=node_id.startswith("unknown-"),
        )
        for node_id in sorted(labels_by_id, key=lambda item: (layers[item], rows_by_node[item], labels_by_id[item]))
    )
    return ChannelFlowModel(nodes=nodes, edges=tuple(edges))


def _channel_consumers(channel: ChannelModel) -> tuple[str, ...]:
    """Return typed channel consumer paths."""
    return channel["consumer_cog_instances"]


def _flow_node_id(path: str, node_ids_by_path: dict[str, str], labels_by_id: dict[str, str]) -> str:
    if not path:
        labels_by_id.setdefault("unknown-source", "unknown source")
        return "unknown-source"
    if path not in node_ids_by_path:
        node_id = f"flow-node-{len(node_ids_by_path)}"
        node_ids_by_path[path] = node_id
        labels_by_id[node_id] = path
    return node_ids_by_path[path]


def _build_channel_flow_edge(
    channel: ChannelModel,
    source_node_id: str,
    target_node_id: str,
    index: int,
) -> ChannelFlowEdgeModel:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    message_count = int(channel["message_count"])
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    first_sequence_number = int(channel["first_sequence_number"])
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    last_sequence_number = int(channel["last_sequence_number"])
    # fmt: off
    return ChannelFlowEdgeModel(
        id=f"flow-edge-{index}",
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        channel_id=str(channel["id"]),
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        channel_name=str(channel["channel_name"]),
        source_node_id=source_node_id,
        target_node_id=target_node_id,
        message_count=message_count,
        first_sequence_number=first_sequence_number,
        last_sequence_number=last_sequence_number,
        label=f"{message_count} msg, seq {first_sequence_number}-{last_sequence_number}",
    )
    # fmt: on


def _flow_layers(node_ids: Sequence[str], edges: Sequence[ChannelFlowEdgeModel]) -> dict[str, int]:
    layers = dict.fromkeys(node_ids, 0)
    max_layer = max(0, len(node_ids) - 1)
    for _ in node_ids:
        changed = False
        for edge in edges:
            if edge.source_node_id == edge.target_node_id:
                continue
            next_layer = min(max_layer, layers[edge.source_node_id] + 1)
            if next_layer > layers[edge.target_node_id]:
                layers[edge.target_node_id] = next_layer
                changed = True
        if not changed:
            break
    return layers


def _flow_rows(labels_by_id: dict[str, str], layers: dict[str, int]) -> dict[str, int]:
    rows: dict[str, int] = {}
    for layer in sorted(set(layers.values())):
        node_ids = sorted(
            (node_id for node_id, node_layer in layers.items() if node_layer == layer),
            key=lambda node_id: labels_by_id[node_id],
        )
        rows.update({node_id: row for row, node_id in enumerate(node_ids)})
    return rows


def _sorted_cogs(cogs: Sequence[journal_pb2.CogJournal]) -> tuple[journal_pb2.CogJournal, ...]:
    return tuple(sorted(cogs, key=lambda item: (item.cog_instance_path, item.cog_path)))


def _sorted_channels(channels: Sequence[journal_pb2.ChannelSummary]) -> tuple[journal_pb2.ChannelSummary, ...]:
    return tuple(sorted(channels, key=lambda item: (item.channel_name, item.producer_cog_instance)))


def _sorted_channel_messages(
    messages: Sequence[journal_pb2.ChannelMessage],
) -> tuple[journal_pb2.ChannelMessage, ...]:
    return tuple(sorted(messages, key=lambda item: (int(item.publish_time_ns), int(item.sequence_number))))


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


def _sorted_inputs(inputs: Sequence[journal_pb2.InputViewState]) -> tuple[journal_pb2.InputViewState, ...]:
    return tuple(sorted(inputs, key=lambda item: (item.channel_name, item.cog_member_name)))


def _sorted_outputs(outputs: Sequence[journal_pb2.OutputState]) -> tuple[journal_pb2.OutputState, ...]:
    return tuple(sorted(outputs, key=lambda item: item.channel_name))


def _sorted_memory_stats(memory_stats: Sequence[journal_pb2.MemoryStats]) -> tuple[journal_pb2.MemoryStats, ...]:
    return tuple(
        sorted(
            memory_stats,
            key=lambda item: (
                item.resource_name,
                int(item.peak_allocated),
                int(item.current_allocated),
                int(item.total_allocated),
                int(item.total_deallocated),
            ),
        )
    )


def _sorted_executions(executions: Sequence[journal_pb2.CogExecution]) -> tuple[journal_pb2.CogExecution, ...]:
    return tuple(sorted(executions, key=lambda item: (int(item.execution_index), int(item.execution_start_time_ns))))


def _gap_reason_name(reason: int) -> str:
    try:
        return journal_pb2.ReplayReadinessGapReason.Name(reason)
    except ValueError:
        return f"UNKNOWN_REPLAY_READINESS_GAP_REASON_{reason}"


def _snapshot_selection_name(selection: int) -> str:
    try:
        return journal_pb2.SnapshotSelection.Name(selection)
    except ValueError:
        return f"UNKNOWN_SNAPSHOT_SELECTION_{selection}"


def _short_cog_label(label: str) -> str:
    path_parts = tuple(part for part in label.split(".") if part)
    if path_parts:
        return path_parts[-1]
    module_parts = tuple(part for part in label.split("::") if part)
    if module_parts:
        return module_parts[-1]
    return label
