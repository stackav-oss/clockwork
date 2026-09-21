# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helpers for building journal protobufs."""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, final

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.alignment import (
    AlignmentExtraction,
    extract_alignment_results,
    has_alignment_channels,
)
from clockwork.tools.journal_file_generator.channel_summary import (
    ChannelSummaryExtraction,
    extract_channel_summaries,
)
from clockwork.tools.journal_file_generator.execution_extractor import (
    ExecutionExtraction,
    ExtractedCogExecutions,
    extract_execution_metrics,
)
from clockwork.tools.journal_file_generator.instance_discovery import (
    discover_cog_channel_maps,
    expand_box_instance_paths,
)
from clockwork.tools.journal_file_generator.log_index import load_log_index
from clockwork.tools.journal_file_generator.message_index import (
    MessageIndexExtraction,
    extract_channel_messages,
)
from clockwork.tools.journal_file_generator.readiness import replay_readiness_is_sufficient
from clockwork.tools.journal_file_generator.request import SnapshotSelection
from clockwork.tools.journal_file_generator.scope import (
    ChannelSchemaRef,
    ResolvedJournalScope,
    ScopeGap,
    load_signal_metadata,
    resolve_scope,
)
from clockwork.tools.journal_file_generator.snapshot_selector import (
    SelectedCogSnapshot,
    SnapshotExtraction,
    extract_state_snapshots,
)
from clockwork.tools.journal_file_generator.topology_file import (
    JournalTopologyError,
    load_cog_channel_maps,
    topology_file_exists,
    topology_file_for_log,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from clockwork.tools.journal_file_generator.cog_channel import CogChannelMap
    from clockwork.tools.journal_file_generator.request import JournalRequest

GENERATOR_VERSION: Final = "journal-file-generator"

_SNAPSHOT_SELECTION_TO_PROTO: Final[dict[SnapshotSelection, journal_pb2.SnapshotSelection]] = {
    SnapshotSelection.BEFORE: journal_pb2.SNAPSHOT_SELECTION_BEFORE,
    SnapshotSelection.AFTER: journal_pb2.SNAPSHOT_SELECTION_AFTER,
    SnapshotSelection.CLOSEST: journal_pb2.SNAPSHOT_SELECTION_CLOSEST,
}


@final
@dataclass(frozen=True, kw_only=True)
class JournalExtractions:
    """Extracted log artifacts used to build a journal."""

    execution: ExecutionExtraction | None = None
    """Extracted cog execution metrics."""

    channel_summary: ChannelSummaryExtraction | None = None
    """Extracted input and output channel summaries."""

    snapshot: SnapshotExtraction | None = None
    """Selected state snapshots."""

    alignment: AlignmentExtraction | None = None
    """Extracted alignment results correlated to cog executions."""

    message_index: MessageIndexExtraction | None = None
    """Extracted report-oriented channel message index."""


def build_journal_file(
    request: JournalRequest,
    resolved_scope: ResolvedJournalScope | None = None,
    extractions: JournalExtractions | None = None,
) -> journal_pb2.JournalFile:
    """Build a journal containing request metadata and replay readiness."""
    if extractions is None:
        extractions = JournalExtractions()
    journal = journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=request.start_time_ns,
            end_time_ns=request.end_time_ns,
            log_uri=request.log_uri,
            generator_version=GENERATOR_VERSION,
            scope=_build_scope(request),
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=False,
                missing_inputs=_missing_input_channel_names(extractions.channel_summary),
                has_state_snapshot=_has_state_snapshot(resolved_scope, extractions.snapshot),
                gaps=_build_readiness_gaps(
                    resolved_scope,
                    extractions.execution,
                    extractions.channel_summary,
                    extractions.snapshot,
                    extractions.alignment,
                ),
            ),
        ),
        cog_journals=_build_cog_journals(
            resolved_scope,
            extractions.execution,
            extractions.snapshot,
            extractions.alignment,
            request,
        ),
        channel_summaries=_build_channel_summaries(resolved_scope, extractions.channel_summary),
        channel_messages=_build_channel_messages(extractions.message_index),
    )
    journal.metadata.replay_readiness.sufficient_for_replay = replay_readiness_is_sufficient(
        journal,
        alignment_readiness_assessed=extractions.alignment is not None or not has_alignment_channels(resolved_scope),
    )
    return journal


def build_journal_file_from_log(request: JournalRequest) -> journal_pb2.JournalFile:
    """Build a journal after resolving log metadata and requested cog scope."""
    request = _request_with_expanded_box_scope(request)
    log_index = load_log_index(request.log_uri)
    signal_metadata = load_signal_metadata(request.log_uri)
    cog_channel_maps = _discover_cog_channel_maps(request)
    resolved_scope = resolve_scope(
        requested_cog_instance_paths=request.cog_instance_paths,
        log_index=log_index,
        signal_metadata=signal_metadata,
        cog_channel_maps=cog_channel_maps,
    )
    execution_extraction = extract_execution_metrics(request, resolved_scope)
    channel_summary_extraction = extract_channel_summaries(request, resolved_scope=resolved_scope, log_index=log_index)
    snapshot_extraction = extract_state_snapshots(request, resolved_scope=resolved_scope)
    alignment_extraction = extract_alignment_results(
        request,
        resolved_scope=resolved_scope,
        execution_extraction=execution_extraction,
        log_index=log_index,
    )
    message_index_extraction = extract_channel_messages(request, resolved_scope=resolved_scope, log_index=log_index)
    return build_journal_file(
        request,
        resolved_scope,
        JournalExtractions(
            execution=execution_extraction,
            channel_summary=channel_summary_extraction,
            snapshot=snapshot_extraction,
            alignment=alignment_extraction,
            message_index=message_index_extraction,
        ),
    )


def _discover_cog_channel_maps(request: JournalRequest) -> Mapping[str, CogChannelMap] | None:
    if request.journal_topology_file is not None:
        return load_cog_channel_maps(request.journal_topology_file)

    try:
        topology_file = topology_file_for_log(request.telemetry_log_uri or request.log_uri)
    except JournalTopologyError:
        topology_file = None
    if topology_file is not None and topology_file_exists(topology_file):
        return load_cog_channel_maps(topology_file)

    if request.system_clk_file is None and request.system_target is None:
        return None
    return discover_cog_channel_maps(
        system_clk_file=request.system_clk_file,
        system_target=request.system_target,
    )


def _request_with_expanded_box_scope(request: JournalRequest) -> JournalRequest:
    if not request.box_instance_paths:
        return request
    return dataclasses.replace(
        request,
        cog_instance_paths=expand_box_instance_paths(
            system_clk_file=request.system_clk_file,
            system_target=request.system_target,
            box_instance_paths=request.box_instance_paths,
        ),
    )


def snapshot_selection_to_proto(snapshot_selection: SnapshotSelection) -> journal_pb2.SnapshotSelection:
    """Convert a validated snapshot selection string to the journal protobuf enum value."""
    return _SNAPSHOT_SELECTION_TO_PROTO[snapshot_selection]


def _build_scope(request: JournalRequest) -> journal_pb2.JournalScope:
    """Build the resolved request scope."""
    return journal_pb2.JournalScope(
        cog_instance_paths=list(request.cog_instance_paths),
        box_instance_paths=list(request.box_instance_paths),
    )


def _build_readiness_gaps(
    resolved_scope: ResolvedJournalScope | None,
    execution_extraction: ExecutionExtraction | None,
    channel_summary_extraction: ChannelSummaryExtraction | None,
    snapshot_extraction: SnapshotExtraction | None,
    alignment_extraction: AlignmentExtraction | None,
) -> list[journal_pb2.ReplayReadinessGap]:
    gaps: list[journal_pb2.ReplayReadinessGap] = []
    if resolved_scope is not None:
        gaps.extend(_build_readiness_gap(gap) for gap in resolved_scope.gaps)
    if execution_extraction is not None:
        gaps.extend(_build_readiness_gap(gap) for gap in execution_extraction.gaps)
    if channel_summary_extraction is not None:
        gaps.extend(_build_readiness_gap(gap) for gap in channel_summary_extraction.gaps)
    if snapshot_extraction is not None:
        gaps.extend(_build_readiness_gap(gap) for gap in snapshot_extraction.gaps)
    if alignment_extraction is not None:
        gaps.extend(_build_readiness_gap(gap) for gap in alignment_extraction.gaps)
    return sorted(gaps, key=_readiness_gap_key)


def _has_state_snapshot(
    resolved_scope: ResolvedJournalScope | None,
    snapshot_extraction: SnapshotExtraction | None,
) -> bool:
    if resolved_scope is None or snapshot_extraction is None or not resolved_scope.cog_scopes:
        return False
    stateful_cog_paths = {
        cog_scope.cog_instance_path for cog_scope in resolved_scope.cog_scopes if cog_scope.has_clockwork_state
    }
    if not stateful_cog_paths:
        return False
    selected_cog_paths = {snapshot.cog_instance_path for snapshot in snapshot_extraction.selected_snapshots}
    return stateful_cog_paths <= selected_cog_paths


def _missing_input_channel_names(channel_summary_extraction: ChannelSummaryExtraction | None) -> list[str]:
    if channel_summary_extraction is None:
        return []
    return sorted(
        {
            gap.channel_name
            for gap in channel_summary_extraction.gaps
            if gap.reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL
        }
    )


def _build_readiness_gap(gap: ScopeGap) -> journal_pb2.ReplayReadinessGap:
    return journal_pb2.ReplayReadinessGap(
        reason=gap.reason,
        message=gap.message,
        cog_instance_path=gap.cog_instance_path,
        channel_name=gap.channel_name,
        execution_index=gap.execution_index,
    )


def _readiness_gap_key(gap: journal_pb2.ReplayReadinessGap) -> tuple[int, str, str, int, str]:
    return (gap.reason, gap.cog_instance_path, gap.channel_name, gap.execution_index, gap.message)


def _build_cog_journals(
    resolved_scope: ResolvedJournalScope | None,
    execution_extraction: ExecutionExtraction | None,
    snapshot_extraction: SnapshotExtraction | None,
    alignment_extraction: AlignmentExtraction | None,
    request: JournalRequest,
) -> list[journal_pb2.CogJournal]:
    if resolved_scope is None:
        return []
    executions_by_cog = _executions_by_cog_instance_path(execution_extraction)
    snapshots_by_cog = _snapshots_by_cog_instance_path(snapshot_extraction)
    alignments_by_cog = _alignments_by_cog_instance_path(alignment_extraction)
    cog_journals: list[journal_pb2.CogJournal] = []
    for cog_scope in sorted(resolved_scope.cog_scopes, key=lambda scope: scope.cog_instance_path):
        extracted = executions_by_cog.get(cog_scope.cog_instance_path)
        snapshot = snapshots_by_cog.get(cog_scope.cog_instance_path)
        execution_alignments = alignments_by_cog.get(cog_scope.cog_instance_path, {})
        cog_journal = journal_pb2.CogJournal(
            cog_path=cog_scope.cog_path,
            cog_instance_path=cog_scope.cog_instance_path,
            input_schemas=_build_channel_schemas(cog_scope.input_schemas),
            output_schemas=_build_channel_schemas(cog_scope.output_schemas),
            condition_names=list(extracted.condition_names) if extracted is not None else [],
            executions=_build_executions(extracted.executions, execution_alignments) if extracted is not None else [],
            has_clockwork_state=cog_scope.has_clockwork_state,
        )
        if snapshot is not None:
            cog_journal.state_snapshot.CopyFrom(_build_state_snapshot(snapshot, request))
        cog_journals.append(cog_journal)
    return cog_journals


def _executions_by_cog_instance_path(
    execution_extraction: ExecutionExtraction | None,
) -> dict[str, ExtractedCogExecutions]:
    if execution_extraction is None:
        return {}
    return {cog_executions.cog_instance_path: cog_executions for cog_executions in execution_extraction.cog_executions}


def _snapshots_by_cog_instance_path(
    snapshot_extraction: SnapshotExtraction | None,
) -> dict[str, SelectedCogSnapshot]:
    if snapshot_extraction is None:
        return {}
    return {snapshot.cog_instance_path: snapshot for snapshot in snapshot_extraction.selected_snapshots}


def _alignments_by_cog_instance_path(
    alignment_extraction: AlignmentExtraction | None,
) -> dict[str, dict[int, journal_pb2.AlignmentResult]]:
    if alignment_extraction is None:
        return {}
    alignments: dict[str, dict[int, journal_pb2.AlignmentResult]] = {}
    for alignment in alignment_extraction.execution_alignments:
        alignments.setdefault(alignment.cog_instance_path, {})[alignment.execution_index] = alignment.alignment_result
    return alignments


def _build_state_snapshot(
    snapshot: SelectedCogSnapshot,
    request: JournalRequest,
) -> journal_pb2.StateSnapshot:
    return journal_pb2.StateSnapshot(
        snapshot_time_ns=snapshot.snapshot_time_ns,
        selection=snapshot_selection_to_proto(request.snapshot_selection),
        state_data=snapshot.state_data,
        state_schema_uuid=snapshot.state_schema_uuid,
    )


def _build_channel_schemas(schema_refs: tuple[ChannelSchemaRef, ...]) -> list[journal_pb2.ChannelSchema]:
    return [
        journal_pb2.ChannelSchema(
            channel_name=schema_ref.channel_name,
            schema_name=schema_ref.schema_name,
            schema_uuid=schema_ref.schema_uuid,
        )
        for schema_ref in sorted(schema_refs, key=lambda ref: (ref.channel_name, ref.schema_name, ref.schema_uuid))
    ]


def _build_executions(
    executions: tuple[journal_pb2.CogExecution, ...],
    alignment_results: Mapping[int, journal_pb2.AlignmentResult],
) -> list[journal_pb2.CogExecution]:
    return [_build_execution(execution, alignment_results) for execution in sorted(executions, key=_execution_key)]


def _execution_key(execution: journal_pb2.CogExecution) -> tuple[int, int]:
    return (execution.execution_index, execution.execution_start_time_ns)


def _build_execution(
    execution: journal_pb2.CogExecution,
    alignment_results: Mapping[int, journal_pb2.AlignmentResult],
) -> journal_pb2.CogExecution:
    ordered = journal_pb2.CogExecution(
        dial_start_time_ns=execution.dial_start_time_ns,
        execution_start_time_ns=execution.execution_start_time_ns,
        execution_duration_ns=execution.execution_duration_ns,
        ready_to_exec_latency_ns=execution.ready_to_exec_latency_ns,
        attempt_to_exec_latency_ns=execution.attempt_to_exec_latency_ns,
        requeue_count=execution.requeue_count,
        condition_flags=execution.condition_flags,
        memory_stats=sorted(execution.memory_stats, key=lambda stats: stats.resource_name),
        input_views=sorted(execution.input_views, key=lambda view: (view.channel_name, view.cog_member_name)),
        outputs=sorted(execution.outputs, key=lambda output: output.channel_name),
        execution_index=execution.execution_index,
    )
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    alignment_result = alignment_results.get(int(execution.execution_index))
    if alignment_result is not None:
        ordered.alignment_result.CopyFrom(alignment_result)
    elif execution.HasField("alignment_result"):
        ordered.alignment_result.CopyFrom(execution.alignment_result)
    return ordered


def _build_channel_summaries(
    resolved_scope: ResolvedJournalScope | None,
    channel_summary_extraction: ChannelSummaryExtraction | None,
) -> list[journal_pb2.ChannelSummary]:
    if channel_summary_extraction is not None:
        return [
            _build_channel_summary(summary)
            for summary in sorted(channel_summary_extraction.channel_summaries, key=_channel_summary_key)
        ]
    if resolved_scope is None:
        return []
    return [
        journal_pb2.ChannelSummary(
            channel_name=channel.channel_name,
            producer_cog_instance=channel.producer_cog_instance,
            consumer_cog_instances=sorted(channel.consumer_cog_instances),
            has_logged_messages=channel.has_logged_messages,
        )
        for channel in sorted(resolved_scope.channel_topologies, key=lambda item: item.channel_name)
    ]


def _channel_summary_key(summary: journal_pb2.ChannelSummary) -> tuple[str, str]:
    return (summary.channel_name, summary.producer_cog_instance)


def _build_channel_summary(summary: journal_pb2.ChannelSummary) -> journal_pb2.ChannelSummary:
    return journal_pb2.ChannelSummary(
        channel_name=summary.channel_name,
        producer_cog_instance=summary.producer_cog_instance,
        consumer_cog_instances=sorted(summary.consumer_cog_instances),
        first_sequence_number=summary.first_sequence_number,
        last_sequence_number=summary.last_sequence_number,
        message_count=summary.message_count,
        has_logged_messages=summary.has_logged_messages,
    )


def _build_channel_messages(
    message_index_extraction: MessageIndexExtraction | None,
) -> list[journal_pb2.ChannelMessage]:
    if message_index_extraction is None:
        return []
    return [
        journal_pb2.ChannelMessage(
            channel_name=message.channel_name,
            sequence_number=message.sequence_number,
            publish_time_ns=message.publish_time_ns,
            payload_size_bytes=message.payload_size_bytes,
        )
        for message in sorted(message_index_extraction.channel_messages, key=_channel_message_key)
    ]


def _channel_message_key(message: journal_pb2.ChannelMessage) -> tuple[str, int, int]:
    return (message.channel_name, message.sequence_number, message.publish_time_ns)
