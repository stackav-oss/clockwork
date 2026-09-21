# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Execution metrics extraction for journal generation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, Protocol, final

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.journal_metadata import parse_execution_journal_metadata
from clockwork.tools.journal_file_generator.scope import ResolvedJournalScope, ScopeGap
from clockwork.tools.signal_extractor.signal_extractor import (
    ReportGroupAggregation,
    ReportGroupBatch,
    SignalExtractionFilter,
    SignalExtractor,
)

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping, Sequence

    from clockwork.tools.journal_file_generator.cog_channel import CogChannelRef
    from clockwork.tools.journal_file_generator.request import JournalRequest

_EVENT_METRICS_GROUP_NAME: Final = "cog_event_metrics_group"
_EVENT_METRICS_CHANNEL_MARKER: Final = f"/{_EVENT_METRICS_GROUP_NAME}/"
_TEN_NANOSECOND_FACTOR: Final = 10
_MAX_CONDITION_FLAGS: Final = 64
_MEMORY_METRICS_SUFFIXES: Final = (
    "peak_allocated",
    "current_allocated",
    "total_allocated",
    "total_deallocated",
)
_COMMON_EVENT_FIELDS: Final = {
    "cog_dial_start_time_value",
    "cog_exec_start_time_value",
    "cog_exec_duration_value",
    "cog_ready_to_exec_latency_value",
    "cog_attempt_to_exec_latency_value",
    "cog_requeue_count_value",
}


class ReportGroupSignalInfoLike(Protocol):
    """Signal metadata fields used for condition-name discovery."""

    @property
    def signal_name(self) -> str:
        """Base signal name."""
        ...

    @property
    def alias(self) -> str | None:
        """Report-group alias, when the signal is instantiated."""
        ...


class StreamingReportGroupLike(Protocol):
    """Streaming report group fields used by execution extraction."""

    @property
    def report_group_name(self) -> str:
        """Report group name."""
        ...

    @property
    def cog_instance_path(self) -> str:
        """Runtime cog instance path."""
        ...

    @property
    def channel_name(self) -> str:
        """Logged report-group channel name."""
        ...

    @property
    def signal_info(self) -> Sequence[ReportGroupSignalInfoLike]:
        """Report-group signal metadata."""
        ...

    def batches(self) -> Iterator[ReportGroupBatch | ReportGroupAggregation]:
        """Stream report-group batches."""
        ...


class SignalExtractorLike(Protocol):
    """Signal extractor API used by the journal generator."""

    def stream(self, extraction_filter: SignalExtractionFilter | None = None) -> Iterator[StreamingReportGroupLike]:
        """Stream report groups matching the filter."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class ExtractedCogExecutions:
    """Execution metrics extracted for one cog instance."""

    cog_instance_path: str
    """Runtime cog instance path."""

    condition_names: tuple[str, ...]
    """Condition names in condition-flag bit order."""

    executions: tuple[journal_pb2.CogExecution, ...]
    """Ordered execution records for the requested time range."""


@final
@dataclass(frozen=True, kw_only=True)
class ExecutionExtraction:
    """Execution extraction result plus replay-readiness gaps."""

    cog_executions: tuple[ExtractedCogExecutions, ...]
    """Per-cog execution records in deterministic cog-instance order."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps found while extracting executions."""


@final
@dataclass(frozen=True, kw_only=True)
class _PendingExecution:
    """Execution proto paired with its raw event-metrics entry."""

    execution: journal_pb2.CogExecution
    entry: Mapping[str, object]


@final
@dataclass(frozen=True, kw_only=True)
class _ExecutionRecords:
    """Parsed execution records and metadata gaps for one cog."""

    executions: tuple[journal_pb2.CogExecution, ...]
    gaps: tuple[ScopeGap, ...]


def extract_execution_metrics(request: JournalRequest, resolved_scope: ResolvedJournalScope) -> ExecutionExtraction:
    """Extract event metrics from a Clockwork log for the resolved journal scope."""
    if not _scope_has_event_metrics_channels(resolved_scope):
        return _missing_event_channels_result(resolved_scope)

    return extract_execution_metrics_from_extractor(
        SignalExtractor(request.log_uri),
        resolved_scope=resolved_scope,
        start_time_ns=request.start_time_ns,
        end_time_ns=request.end_time_ns,
    )


def extract_execution_metrics_from_extractor(
    extractor: SignalExtractorLike,
    *,
    resolved_scope: ResolvedJournalScope,
    start_time_ns: int,
    end_time_ns: int,
) -> ExecutionExtraction:
    """Extract event metrics from a supplied signal extractor."""
    cog_paths = tuple(scope.cog_instance_path for scope in resolved_scope.cog_scopes)
    if not cog_paths:
        return ExecutionExtraction(cog_executions=(), gaps=())

    groups = tuple(
        sorted(
            extractor.stream(
                SignalExtractionFilter(
                    cog_instance_paths=list(cog_paths),
                    report_group_names=[_EVENT_METRICS_GROUP_NAME],
                    start_time_ns=start_time_ns,
                )
            ),
            key=lambda group: (group.cog_instance_path, group.channel_name),
        )
    )
    grouped = _groups_by_cog_instance_path(groups)

    extracted: list[ExtractedCogExecutions] = []
    gaps: list[ScopeGap] = []
    for cog_scope in resolved_scope.cog_scopes:
        cog_groups = grouped.get(cog_scope.cog_instance_path, ())
        if not _has_event_metrics_channel(cog_scope.metrics_channels) and not cog_groups:
            gaps.append(_missing_event_metrics_gap(cog_scope.cog_instance_path))
            continue
        if not cog_groups:
            gaps.append(_missing_event_metrics_gap(cog_scope.cog_instance_path))
            continue

        condition_names = _condition_names_for_groups(cog_groups)
        records = _executions_for_groups(
            cog_groups,
            condition_names,
            input_channel_names=tuple(schema.channel_name for schema in cog_scope.input_schemas),
            output_channel_names=tuple(schema.channel_name for schema in cog_scope.output_schemas),
            input_channel_refs=cog_scope.input_channel_refs,
            output_channel_refs=cog_scope.output_channel_refs,
            cog_instance_path=cog_scope.cog_instance_path,
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
        )
        gaps.extend(records.gaps)
        if not records.executions and not any(
            gap.reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EXECUTION_METRICS for gap in records.gaps
        ):
            gaps.append(_empty_time_range_gap(cog_scope.cog_instance_path))
        extracted.append(
            ExtractedCogExecutions(
                cog_instance_path=cog_scope.cog_instance_path,
                condition_names=condition_names,
                executions=records.executions,
            )
        )

    return ExecutionExtraction(
        cog_executions=tuple(sorted(extracted, key=lambda result: result.cog_instance_path)),
        gaps=tuple(gaps),
    )


def _scope_has_event_metrics_channels(resolved_scope: ResolvedJournalScope) -> bool:
    return any(_has_event_metrics_channel(cog_scope.metrics_channels) for cog_scope in resolved_scope.cog_scopes)


def _missing_event_channels_result(resolved_scope: ResolvedJournalScope) -> ExecutionExtraction:
    return ExecutionExtraction(
        cog_executions=(),
        gaps=tuple(_missing_event_metrics_gap(scope.cog_instance_path) for scope in resolved_scope.cog_scopes),
    )


def _groups_by_cog_instance_path(
    groups: Sequence[StreamingReportGroupLike],
) -> dict[str, tuple[StreamingReportGroupLike, ...]]:
    grouped: dict[str, list[StreamingReportGroupLike]] = {}
    for group in groups:
        grouped.setdefault(group.cog_instance_path, []).append(group)
    return {cog_path: tuple(cog_groups) for cog_path, cog_groups in grouped.items()}


def _condition_names_for_groups(groups: Sequence[StreamingReportGroupLike]) -> tuple[str, ...]:
    metadata_names: list[str] = []
    for group in groups:
        for signal_info in group.signal_info:
            if _signal_leaf_name(signal_info.signal_name) != "execution_condition_active":
                continue
            condition_name = _condition_name_from_alias(signal_info.alias)
            if condition_name != "" and condition_name not in metadata_names:
                metadata_names.append(condition_name)

    if metadata_names:
        return tuple(metadata_names[:_MAX_CONDITION_FLAGS])

    entry_names = sorted(
        {name for group in groups for batch in group.batches() for name in _condition_names_from_batch(batch)}
    )
    return tuple(entry_names[:_MAX_CONDITION_FLAGS])


def _condition_name_from_alias(alias: str | None) -> str:
    if alias is None:
        return ""
    return alias.removesuffix("_active")


def _signal_leaf_name(signal_name: str) -> str:
    return signal_name.rsplit("::", maxsplit=1)[-1].rsplit(".", maxsplit=1)[-1]


def _condition_names_from_batch(batch: ReportGroupBatch | ReportGroupAggregation) -> tuple[str, ...]:
    if not isinstance(batch, ReportGroupBatch):
        return ()
    return tuple(
        field_name.removesuffix("_active_value")
        for entry in batch.entries
        for field_name in entry
        if field_name.endswith("_active_value") and field_name not in _COMMON_EVENT_FIELDS
    )


def _executions_for_groups(  # noqa: PLR0913 # Parameters keep extraction context explicit at this boundary.
    groups: Sequence[StreamingReportGroupLike],
    condition_names: Sequence[str],
    *,
    input_channel_names: Sequence[str],
    output_channel_names: Sequence[str],
    input_channel_refs: Sequence[CogChannelRef],
    output_channel_refs: Sequence[CogChannelRef],
    cog_instance_path: str,
    start_time_ns: int,
    end_time_ns: int,
) -> _ExecutionRecords:
    pending_executions: list[_PendingExecution] = []
    gaps: list[ScopeGap] = []
    for group in groups:
        for batch in group.batches():
            if not isinstance(batch, ReportGroupBatch):
                continue
            for row_index, entry in enumerate(batch.entries):
                assert entry.get("cog_exec_start_time_value") is not None, (
                    "Cog execution start time must always be present"
                )
                if not _entry_is_in_time_range(
                    entry,
                    start_time_ns=start_time_ns,
                    end_time_ns=end_time_ns,
                ):
                    continue
                missing_fields = _missing_execution_fields(entry, condition_names)
                if missing_fields:
                    gaps.append(
                        _missing_execution_metrics_gap(
                            cog_instance_path=cog_instance_path,
                            channel_name=group.channel_name,
                            publish_time_ns=batch.publish_time_ns,
                            row_index=row_index,
                            missing_fields=missing_fields,
                        )
                    )
                    continue
                pending_executions.append(
                    _PendingExecution(execution=_execution_from_entry(entry, condition_names), entry=entry)
                )

    pending_executions.sort(key=lambda pending: pending.execution.execution_start_time_ns)
    executions: list[journal_pb2.CogExecution] = []
    for execution_index, pending in enumerate(pending_executions):
        pending.execution.execution_index = execution_index
        journal_metadata = parse_execution_journal_metadata(
            pending.entry,
            input_channel_names=input_channel_names,
            output_channel_names=output_channel_names,
            cog_instance_path=cog_instance_path,
            execution_index=execution_index,
            input_channel_refs=input_channel_refs,
            output_channel_refs=output_channel_refs,
        )
        pending.execution.input_views.extend(journal_metadata.input_views)
        pending.execution.outputs.extend(journal_metadata.outputs)
        gaps.extend(journal_metadata.gaps)
        executions.append(pending.execution)
    return _ExecutionRecords(executions=tuple(executions), gaps=tuple(gaps))


def _entry_is_in_time_range(entry: Mapping[str, object], *, start_time_ns: int, end_time_ns: int) -> bool:
    execution_start_time_ns = _entry_int(entry, "cog_exec_start_time_value")
    return start_time_ns <= execution_start_time_ns <= end_time_ns


def _execution_from_entry(
    entry: Mapping[str, object],
    condition_names: Sequence[str],
) -> journal_pb2.CogExecution:
    return journal_pb2.CogExecution(
        dial_start_time_ns=_entry_int(entry, "cog_dial_start_time_value"),
        execution_start_time_ns=_entry_int(entry, "cog_exec_start_time_value"),
        execution_duration_ns=_entry_ten_nanoseconds(entry, "cog_exec_duration_value"),
        ready_to_exec_latency_ns=_entry_ten_nanoseconds(entry, "cog_ready_to_exec_latency_value"),
        attempt_to_exec_latency_ns=_entry_ten_nanoseconds(entry, "cog_attempt_to_exec_latency_value"),
        requeue_count=_entry_int(entry, "cog_requeue_count_value"),
        condition_flags=_condition_flags(entry, condition_names),
        memory_stats=_memory_stats_from_entry(entry),
    )


def _memory_stats_from_entry(entry: Mapping[str, object]) -> tuple[journal_pb2.MemoryStats, ...]:
    resource_names = {
        field_name.removesuffix(f"_{metric}_value")
        for field_name in entry
        for metric in _MEMORY_METRICS_SUFFIXES
        if field_name.endswith(f"_{metric}_value")
    }
    return tuple(
        journal_pb2.MemoryStats(
            resource_name=resource_name,
            peak_allocated=_entry_int(entry, f"{resource_name}_peak_allocated_value"),
            current_allocated=_entry_int(entry, f"{resource_name}_current_allocated_value"),
            total_allocated=_entry_int(entry, f"{resource_name}_total_allocated_value"),
            total_deallocated=_entry_int(entry, f"{resource_name}_total_deallocated_value"),
        )
        for resource_name in sorted(resource_names)
    )


def _condition_flags(entry: Mapping[str, object], condition_names: Sequence[str]) -> int:
    flags = 0
    for index, condition_name in enumerate(condition_names[:_MAX_CONDITION_FLAGS]):
        if bool(entry.get(f"{condition_name}_active_value", 0)):
            flags |= 1 << index
    return flags


def _missing_execution_fields(entry: Mapping[str, object], condition_names: Sequence[str]) -> tuple[str, ...]:
    required_fields = set(_COMMON_EVENT_FIELDS)
    required_fields.update(f"{condition_name}_active_value" for condition_name in condition_names)
    return tuple(sorted(field_name for field_name in required_fields if entry.get(field_name, 0) is None))


def _entry_int(entry: Mapping[str, object], field_name: str) -> int:
    value = entry.get(field_name, 0)
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int | float):
        return int(value)
    return 0


def _entry_ten_nanoseconds(entry: Mapping[str, object], field_name: str) -> int:
    return _entry_int(entry, field_name) * _TEN_NANOSECOND_FACTOR


def _has_event_metrics_channel(metrics_channels: Sequence[str]) -> bool:
    return any(_EVENT_METRICS_CHANNEL_MARKER in channel_name for channel_name in metrics_channels)


def _missing_event_metrics_gap(cog_instance_path: str) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EVENT_METRICS_CHANNEL,
        message=f"Cog event metrics channel was not found for cog instance path: {cog_instance_path}",
        cog_instance_path=cog_instance_path,
    )


def _empty_time_range_gap(cog_instance_path: str) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_TIME_RANGE_CONTAINS_NO_EXECUTIONS,
        message=f"No cog executions were found in the requested time range for cog instance path: {cog_instance_path}",
        cog_instance_path=cog_instance_path,
    )


def _missing_execution_metrics_gap(
    *,
    cog_instance_path: str,
    channel_name: str,
    publish_time_ns: int,
    row_index: int,
    missing_fields: Sequence[str],
) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EXECUTION_METRICS,
        message=(
            f"Skipped report-group row {row_index} published at {publish_time_ns} ns because required execution "
            f"metrics are missing: {', '.join(missing_fields)}"
        ),
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
    )
