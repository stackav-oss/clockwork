# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Alignment result extraction for journal generation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, final

from clockwork.journal import journal_pb2
from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.tools.journal_file_generator.alignment_message import alignment_result_from_message
from clockwork.tools.journal_file_generator.scope import ResolvedJournalScope, ScopeGap

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping, Sequence

    from clockwork.tools.journal_file_generator.execution_extractor import ExecutionExtraction
    from clockwork.tools.journal_file_generator.log_index import LogIndex
    from clockwork.tools.journal_file_generator.request import JournalRequest

_ALIGNMENT_SCHEMA_SUFFIX = "AlignmentMsg"


class DeserializedAlignmentMessageLike(Protocol):
    """Deserialized log message fields used for alignment extraction."""

    @property
    def topic(self) -> str:
        """Logged topic name."""
        ...

    @property
    def sequence_number(self) -> int:
        """Logged sequence number."""
        ...

    @property
    def message(self) -> object:
        """Deserialized alignment message."""
        ...


class AlignmentLogReaderLike(Protocol):
    """Log reader API needed for alignment channels."""

    def add_topic(self, topic: str) -> None:
        """Register one topic for deserialization."""
        ...

    def messages(self) -> Iterator[DeserializedAlignmentMessageLike]:
        """Iterate deserialized messages for registered topics."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class ExtractedExecutionAlignment:
    """Alignment result correlated to one cog execution."""

    cog_instance_path: str
    """Runtime cog instance path."""

    execution_index: int
    """Execution index within the containing cog journal."""

    alignment_result: journal_pb2.AlignmentResult
    """Alignment result consumed by the execution."""


@final
@dataclass(frozen=True, kw_only=True)
class AlignmentExtraction:
    """Alignment extraction result plus replay-readiness gaps."""

    execution_alignments: tuple[ExtractedExecutionAlignment, ...]
    """Alignment results correlated to executions."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps found while extracting alignment results."""


@final
@dataclass(frozen=True, kw_only=True)
class _AlignmentChannelRef:
    """Resolved alignment channel consumed by one cog."""

    cog_instance_path: str
    cog_member_name: str
    channel_name: str
    aligner_name: str
    has_logged_messages: bool


@final
@dataclass(frozen=True, kw_only=True)
class _LoggedAlignmentResult:
    """One logged alignment result with its channel sequence number."""

    sequence_number: int
    alignment_result: journal_pb2.AlignmentResult


def extract_alignment_results(
    request: JournalRequest,
    *,
    resolved_scope: ResolvedJournalScope,
    execution_extraction: ExecutionExtraction,
    log_index: LogIndex,
) -> AlignmentExtraction:
    """Extract and correlate alignment results from a Clockwork log."""
    alignment_refs = _alignment_channel_refs(resolved_scope, log_index)
    if not alignment_refs:
        return AlignmentExtraction(execution_alignments=(), gaps=())
    return _extract_alignment_results_from_reader(
        LogReader(request.log_uri),
        alignment_refs=alignment_refs,
        execution_extraction=execution_extraction,
    )


def extract_alignment_results_from_reader(
    reader: AlignmentLogReaderLike,
    *,
    resolved_scope: ResolvedJournalScope,
    execution_extraction: ExecutionExtraction,
    log_index: LogIndex,
) -> AlignmentExtraction:
    """Extract and correlate alignment results from a supplied log reader."""
    alignment_refs = _alignment_channel_refs(resolved_scope, log_index)
    if not alignment_refs:
        return AlignmentExtraction(execution_alignments=(), gaps=())
    return _extract_alignment_results_from_reader(
        reader,
        alignment_refs=alignment_refs,
        execution_extraction=execution_extraction,
    )


def _extract_alignment_results_from_reader(
    reader: AlignmentLogReaderLike,
    *,
    alignment_refs: Sequence[_AlignmentChannelRef],
    execution_extraction: ExecutionExtraction,
) -> AlignmentExtraction:
    """Extract and correlate alignment results from a supplied log reader."""
    logged_refs = tuple(ref for ref in alignment_refs if ref.has_logged_messages)
    for channel_name in sorted({ref.channel_name for ref in logged_refs}):
        reader.add_topic(channel_name)

    logged_results = _logged_results_by_channel(reader, logged_refs)
    return _correlate_alignment_results(
        alignment_refs=alignment_refs,
        execution_extraction=execution_extraction,
        logged_results=logged_results,
    )


def has_alignment_channels(resolved_scope: ResolvedJournalScope | None, log_index: LogIndex | None = None) -> bool:
    """Return whether the resolved scope contains logged alignment-result channels."""
    if resolved_scope is None:
        return False
    if log_index is None:
        return any(
            _schema_aligner_name(schema.schema_name) != ""
            for scope in resolved_scope.cog_scopes
            for schema in scope.input_schemas
        )
    return bool(_alignment_channel_refs(resolved_scope, log_index))


def _alignment_channel_refs(
    resolved_scope: ResolvedJournalScope,
    log_index: LogIndex,
) -> tuple[_AlignmentChannelRef, ...]:
    refs: list[_AlignmentChannelRef] = []
    for cog_scope in resolved_scope.cog_scopes:
        member_names_by_channel = {ref.channel_name: ref.cog_member_name for ref in cog_scope.input_channel_refs}
        for schema_ref in cog_scope.input_schemas:
            aligner_name = _schema_aligner_name(schema_ref.schema_name)
            if aligner_name == "":
                continue
            refs.append(
                _AlignmentChannelRef(
                    cog_instance_path=cog_scope.cog_instance_path,
                    cog_member_name=member_names_by_channel.get(schema_ref.channel_name, schema_ref.channel_name),
                    channel_name=schema_ref.channel_name,
                    aligner_name=aligner_name,
                    has_logged_messages=log_index.has_topic(schema_ref.channel_name),
                )
            )
    return tuple(sorted(refs, key=lambda ref: (ref.cog_instance_path, ref.channel_name, ref.cog_member_name)))


def _schema_aligner_name(schema_name: str) -> str:
    leaf_name = schema_name.rsplit("::", maxsplit=1)[-1].rsplit(".", maxsplit=1)[-1]
    if not leaf_name.endswith(_ALIGNMENT_SCHEMA_SUFFIX):
        return ""
    return leaf_name.removesuffix(_ALIGNMENT_SCHEMA_SUFFIX)


def _logged_results_by_channel(
    reader: AlignmentLogReaderLike,
    alignment_refs: Sequence[_AlignmentChannelRef],
) -> dict[str, tuple[_LoggedAlignmentResult, ...]]:
    refs_by_channel = {ref.channel_name: ref for ref in alignment_refs}
    builders: dict[str, list[_LoggedAlignmentResult]] = {}
    for message in reader.messages():
        ref = refs_by_channel.get(message.topic)
        if ref is None:
            continue
        alignment_result = alignment_result_from_message(message.message, ref.aligner_name)
        if alignment_result is None:
            continue
        builders.setdefault(message.topic, []).append(
            _LoggedAlignmentResult(
                sequence_number=message.sequence_number,
                alignment_result=alignment_result,
            )
        )
    return {
        channel_name: tuple(sorted(results, key=lambda result: result.sequence_number))
        for channel_name, results in builders.items()
    }


def _correlate_alignment_results(
    *,
    alignment_refs: Sequence[_AlignmentChannelRef],
    execution_extraction: ExecutionExtraction,
    logged_results: Mapping[str, Sequence[_LoggedAlignmentResult]],
) -> AlignmentExtraction:
    executions_by_cog = {item.cog_instance_path: item.executions for item in execution_extraction.cog_executions}
    refs_by_cog: dict[str, list[_AlignmentChannelRef]] = {}
    for ref in alignment_refs:
        refs_by_cog.setdefault(ref.cog_instance_path, []).append(ref)

    extracted: list[ExtractedExecutionAlignment] = []
    gaps: list[ScopeGap] = []
    for cog_instance_path, cog_refs in sorted(refs_by_cog.items()):
        executions = executions_by_cog.get(cog_instance_path, ())
        logged_refs = tuple(ref for ref in cog_refs if ref.has_logged_messages)
        gaps.extend(_missing_alignment_channel_gap(ref) for ref in cog_refs if not ref.has_logged_messages)
        if not logged_refs:
            continue
        for execution in executions:
            candidates = _candidate_alignment_results(execution, logged_refs, logged_results)
            if len(candidates) == 1:
                extracted.append(
                    ExtractedExecutionAlignment(
                        cog_instance_path=cog_instance_path,
                        execution_index=execution.execution_index,
                        alignment_result=candidates[0],
                    )
                )
            elif len(candidates) == 0:
                gaps.append(_missing_alignment_result_gap(cog_instance_path, execution.execution_index, logged_refs))
            else:
                gaps.append(_ambiguous_alignment_result_gap(cog_instance_path, execution.execution_index, logged_refs))

    return AlignmentExtraction(
        execution_alignments=tuple(sorted(extracted, key=lambda item: (item.cog_instance_path, item.execution_index))),
        gaps=tuple(gaps),
    )


def _candidate_alignment_results(
    execution: journal_pb2.CogExecution,
    alignment_refs: Sequence[_AlignmentChannelRef],
    logged_results: Mapping[str, Sequence[_LoggedAlignmentResult]],
) -> tuple[journal_pb2.AlignmentResult, ...]:
    candidates: list[journal_pb2.AlignmentResult] = []
    for ref in alignment_refs:
        sequence_numbers = _alignment_message_sequence_numbers(execution, ref.channel_name)
        for logged_result in logged_results.get(ref.channel_name, ()):
            if logged_result.sequence_number not in sequence_numbers:
                continue
            if _alignment_result_matches_execution_inputs(logged_result.alignment_result, execution):
                candidates.append(logged_result.alignment_result)
    return tuple(candidates)


def _alignment_message_sequence_numbers(execution: journal_pb2.CogExecution, channel_name: str) -> frozenset[int]:
    for input_view in execution.input_views:
        if input_view.channel_name != channel_name:
            continue
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        visible_sequences = tuple(int(sequence_number) for sequence_number in input_view.visible_sequence_numbers)
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        first_new_index = int(input_view.first_new_index)
        if first_new_index < len(visible_sequences):
            return frozenset(visible_sequences[first_new_index:])
        return frozenset(visible_sequences)
    return frozenset()


def _alignment_result_matches_execution_inputs(
    alignment_result: journal_pb2.AlignmentResult,
    execution: journal_pb2.CogExecution,
) -> bool:
    # fmt: off
    input_views_by_member_name = {
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        input_view.cog_member_name: frozenset(int(seq) for seq in input_view.visible_sequence_numbers)
        for input_view in execution.input_views
        if input_view.cog_member_name
    }
    # fmt: on
    # fmt: off
    input_views_by_channel_name = {
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        input_view.channel_name: frozenset(int(seq) for seq in input_view.visible_sequence_numbers)
        for input_view in execution.input_views
    }
    # fmt: on
    for aligned_input in alignment_result.aligned_inputs:
        if not aligned_input.present:
            continue
        visible_sequences = input_views_by_member_name.get(aligned_input.input_name)
        if visible_sequences is None:
            visible_sequences = input_views_by_channel_name.get(aligned_input.input_name)
        if visible_sequences is None:
            return False
        if aligned_input.is_batch:
            # fmt: off
            if not _batch_range_is_visible(
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                begin_sequence_number=int(aligned_input.batch_begin_sequence_number),
                # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                end_sequence_number=int(aligned_input.batch_end_sequence_number),
                visible_sequences=visible_sequences,
            ):
            # fmt: on
                return False
        # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        elif int(aligned_input.selected_sequence_number) not in visible_sequences:
            return False
    return True


def _batch_range_is_visible(
    *,
    begin_sequence_number: int,
    end_sequence_number: int,
    visible_sequences: frozenset[int],
) -> bool:
    if end_sequence_number < begin_sequence_number:
        return False
    return all(
        sequence_number in visible_sequences
        for sequence_number in range(begin_sequence_number, end_sequence_number + 1)
    )


def _missing_alignment_channel_gap(ref: _AlignmentChannelRef) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
        message=(
            f"Alignment channel was not found in the log: {ref.channel_name} "
            f"for cog instance path: {ref.cog_instance_path}"
        ),
        cog_instance_path=ref.cog_instance_path,
        channel_name=ref.channel_name,
    )


def _missing_alignment_result_gap(
    cog_instance_path: str,
    execution_index: int,
    alignment_refs: Sequence[_AlignmentChannelRef],
) -> ScopeGap:
    channel_name = alignment_refs[0].channel_name if len(alignment_refs) == 1 else ""
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
        message=f"No matching alignment result was found for cog {cog_instance_path} execution {execution_index}.",
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )


def _ambiguous_alignment_result_gap(
    cog_instance_path: str,
    execution_index: int,
    alignment_refs: Sequence[_AlignmentChannelRef],
) -> ScopeGap:
    channel_name = alignment_refs[0].channel_name if len(alignment_refs) == 1 else ""
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
        message=f"Multiple alignment results matched cog {cog_instance_path} execution {execution_index}.",
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )
