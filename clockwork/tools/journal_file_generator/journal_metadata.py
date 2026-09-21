# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Phase 1 journal metadata parsing for cog event metrics."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, cast, final

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.scope import ScopeGap

if TYPE_CHECKING:
    from clockwork.tools.journal_file_generator.cog_channel import CogChannelRef

_INPUT_METADATA_SUFFIX: Final = "_unseen_messages_value_metadata"
_INPUT_VALUE_SUFFIX: Final = "_unseen_messages_value"
_INPUT_DROPPED_COUNT_SUFFIX: Final = "_dropped_messages_value"
_OUTPUT_COUNT_SUFFIX: Final = "_num_messages_value"
_OUTPUT_FIRST_SEQUENCE_SUFFIX: Final = "_first_sequence_number_value"
_MAX_UINT32: Final = (1 << 32) - 1
_MAX_UINT64: Final = (1 << 64) - 1


@final
@dataclass(frozen=True, kw_only=True)
class ParsedExecutionJournalMetadata:
    """Journal metadata parsed for one cog execution."""

    input_views: tuple[journal_pb2.InputViewState, ...]
    """Input view states parsed from input sequence metadata."""

    outputs: tuple[journal_pb2.OutputState, ...]
    """Output states parsed from output sequence signals."""

    gaps: tuple[ScopeGap, ...]
    """Replay-readiness gaps found while parsing metadata."""


@final
@dataclass(frozen=True, kw_only=True)
class _MetricFieldSpec:
    """Mapping from a report-group metric field stem to a journal channel name."""

    metric_stem: str | None

    cog_member_name: str

    channel_name: str


def parse_execution_journal_metadata(  # noqa: PLR0913 # Inputs mirror execution and channel context.
    entry: Mapping[str, object],
    *,
    input_channel_names: Sequence[str],
    output_channel_names: Sequence[str],
    cog_instance_path: str,
    execution_index: int,
    input_channel_refs: Sequence[CogChannelRef] = (),
    output_channel_refs: Sequence[CogChannelRef] = (),
) -> ParsedExecutionJournalMetadata:
    """Parse input metadata and output sequence signals for one execution entry."""
    input_views, input_gaps = _parse_input_views(
        entry,
        _field_specs(
            entry,
            expected_channel_names=input_channel_names,
            channel_refs=input_channel_refs,
            suffixes=(_INPUT_METADATA_SUFFIX, _INPUT_VALUE_SUFFIX),
        ),
        cog_instance_path=cog_instance_path,
        execution_index=execution_index,
    )
    outputs, output_gaps = _parse_outputs(
        entry,
        _field_specs(
            entry,
            expected_channel_names=output_channel_names,
            channel_refs=output_channel_refs,
            suffixes=(_OUTPUT_COUNT_SUFFIX, _OUTPUT_FIRST_SEQUENCE_SUFFIX),
        ),
        cog_instance_path=cog_instance_path,
        execution_index=execution_index,
    )
    return ParsedExecutionJournalMetadata(input_views=input_views, outputs=outputs, gaps=(*input_gaps, *output_gaps))


def _parse_input_views(
    entry: Mapping[str, object],
    specs: Sequence[_MetricFieldSpec],
    *,
    cog_instance_path: str,
    execution_index: int,
) -> tuple[tuple[journal_pb2.InputViewState, ...], tuple[ScopeGap, ...]]:
    input_views: list[journal_pb2.InputViewState] = []
    gaps: list[ScopeGap] = []
    for spec in specs:
        if spec.metric_stem is None:
            gaps.append(_missing_input_metadata_gap(spec.channel_name, cog_instance_path, execution_index))
            continue
        metadata = entry.get(f"{spec.metric_stem}{_INPUT_METADATA_SUFFIX}")
        if metadata is None:
            gaps.append(_missing_input_metadata_gap(spec.channel_name, cog_instance_path, execution_index))
            continue

        sequence_numbers = _sequence_numbers_from_metadata(metadata, "message_sequence_numbers")
        cursor_position = _nonnegative_int(_metadata_field(metadata, "cursor_position"))
        if sequence_numbers is None or cursor_position is None:
            gaps.append(_malformed_input_metadata_gap(spec.channel_name, cog_instance_path, execution_index))
            continue
        if cursor_position > len(sequence_numbers) or cursor_position > _MAX_UINT32:
            gaps.append(_malformed_input_metadata_gap(spec.channel_name, cog_instance_path, execution_index))
            continue

        cursor_sequence_number = _cursor_sequence_number(sequence_numbers, cursor_position)
        if cursor_sequence_number is None:
            gaps.append(_malformed_input_metadata_gap(spec.channel_name, cog_instance_path, execution_index))
            continue

        input_view = journal_pb2.InputViewState(
            channel_name=spec.channel_name,
            cog_member_name=spec.cog_member_name,
            cursor_sequence_number=cursor_sequence_number,
            visible_sequence_numbers=list(sequence_numbers),
            first_new_index=cursor_position,
        )
        dropped_message_count = _dropped_message_count(entry, spec.metric_stem)
        if dropped_message_count is not None:
            input_view.dropped_message_count = dropped_message_count
        input_views.append(input_view)
    return tuple(input_views), tuple(gaps)


def _parse_outputs(
    entry: Mapping[str, object],
    specs: Sequence[_MetricFieldSpec],
    *,
    cog_instance_path: str,
    execution_index: int,
) -> tuple[tuple[journal_pb2.OutputState, ...], tuple[ScopeGap, ...]]:
    outputs: list[journal_pb2.OutputState] = []
    gaps: list[ScopeGap] = []
    for spec in specs:
        if spec.metric_stem is None:
            gaps.append(_missing_output_sequence_gap(spec.channel_name, cog_instance_path, execution_index))
            continue
        output_count_value = entry.get(f"{spec.metric_stem}{_OUTPUT_COUNT_SUFFIX}")
        output_count = _nonnegative_int(output_count_value)
        if output_count is None:
            if output_count_value is None:
                gaps.append(_missing_output_sequence_gap(spec.channel_name, cog_instance_path, execution_index))
            else:
                gaps.append(_malformed_output_sequence_gap(spec.channel_name, cog_instance_path, execution_index))
            continue

        if output_count == 0:
            outputs.append(journal_pb2.OutputState(channel_name=spec.channel_name))
            continue

        first_sequence_value = entry.get(f"{spec.metric_stem}{_OUTPUT_FIRST_SEQUENCE_SUFFIX}")
        first_sequence_number = _nonnegative_int(first_sequence_value)
        if first_sequence_number is None:
            if first_sequence_value is None:
                gaps.append(_missing_output_sequence_gap(spec.channel_name, cog_instance_path, execution_index))
            else:
                gaps.append(_malformed_output_sequence_gap(spec.channel_name, cog_instance_path, execution_index))
            continue

        outputs.append(
            journal_pb2.OutputState(
                channel_name=spec.channel_name,
                produced_sequence_numbers=list(range(first_sequence_number, first_sequence_number + output_count)),
            )
        )
    return tuple(outputs), tuple(gaps)


def _field_specs(
    entry: Mapping[str, object],
    *,
    expected_channel_names: Sequence[str],
    channel_refs: Sequence[CogChannelRef],
    suffixes: Sequence[str],
) -> tuple[_MetricFieldSpec, ...]:
    discovered_stems = _field_stems(entry, suffixes)
    expected_names = tuple(sorted(expected_channel_names))
    ref_specs = _channel_ref_field_specs(
        entry,
        expected_channel_names=expected_names,
        channel_refs=channel_refs,
        suffixes=suffixes,
    )
    if ref_specs:
        return ref_specs

    if not expected_names:
        return tuple(
            _MetricFieldSpec(metric_stem=None, cog_member_name="", channel_name=stem) for stem in discovered_stems
        )

    if len(expected_names) == 1 and len(discovered_stems) == 1:
        return (
            _MetricFieldSpec(
                metric_stem=discovered_stems[0],
                cog_member_name=discovered_stems[0],
                channel_name=expected_names[0],
            ),
        )

    return tuple(_MetricFieldSpec(metric_stem=None, cog_member_name="", channel_name=name) for name in expected_names)


def _channel_ref_field_specs(
    entry: Mapping[str, object],
    *,
    expected_channel_names: Sequence[str],
    channel_refs: Sequence[CogChannelRef],
    suffixes: Sequence[str],
) -> tuple[_MetricFieldSpec, ...]:
    if not channel_refs:
        return ()

    refs_by_channel = {ref.channel_name: ref for ref in channel_refs}
    if expected_channel_names:
        return tuple(
            _channel_ref_or_direct_field_spec(entry, channel_name, refs_by_channel.get(channel_name), suffixes)
            for channel_name in expected_channel_names
        )

    return tuple(
        _MetricFieldSpec(
            metric_stem=_channel_ref_metric_stem(entry, channel_ref, suffixes),
            cog_member_name=channel_ref.cog_member_name,
            channel_name=channel_ref.channel_name,
        )
        for channel_ref in sorted(channel_refs, key=lambda item: (item.channel_name, item.cog_member_name))
    )


def _channel_ref_or_direct_field_spec(
    entry: Mapping[str, object],
    channel_name: str,
    channel_ref: CogChannelRef | None,
    suffixes: Sequence[str],
) -> _MetricFieldSpec:
    if channel_ref is None:
        return _MetricFieldSpec(metric_stem=None, cog_member_name="", channel_name=channel_name)
    return _MetricFieldSpec(
        metric_stem=_channel_ref_metric_stem(entry, channel_ref, suffixes),
        cog_member_name=channel_ref.cog_member_name,
        channel_name=channel_name,
    )


def _channel_ref_metric_stem(entry: Mapping[str, object], channel_ref: CogChannelRef, suffixes: Sequence[str]) -> str:
    if _has_any_field(entry, channel_ref.cog_member_name, suffixes) or not _has_any_field(
        entry,
        channel_ref.channel_name,
        suffixes,
    ):
        return channel_ref.cog_member_name
    return channel_ref.channel_name


def _field_stems(entry: Mapping[str, object], suffixes: Sequence[str]) -> tuple[str, ...]:
    stems: set[str] = set()
    for field_name in entry:
        for suffix in suffixes:
            if field_name.endswith(suffix):
                stems.add(field_name.removesuffix(suffix))
    return tuple(sorted(stems))


def _has_any_field(entry: Mapping[str, object], metric_stem: str, suffixes: Sequence[str]) -> bool:
    return any(f"{metric_stem}{suffix}" in entry for suffix in suffixes)


def _sequence_numbers_from_metadata(metadata: object, field_name: str) -> tuple[int, ...] | None:
    return _sequence_of_uints(_metadata_field(metadata, field_name))


def _metadata_field(metadata: object, field_name: str) -> object | None:
    if isinstance(metadata, Mapping):
        return cast("Mapping[str, object]", metadata).get(field_name)
    return cast("object | None", getattr(metadata, field_name, None))


def _sequence_of_uints(value: object | None) -> tuple[int, ...] | None:
    if value is None or isinstance(value, str | bytes) or not isinstance(value, Sequence):
        return None

    sequence_numbers: list[int] = []
    for item in value:
        sequence_number = _nonnegative_int(item)
        if sequence_number is None or sequence_number > _MAX_UINT64:
            return None
        sequence_numbers.append(sequence_number)
    return tuple(sequence_numbers)


def _nonnegative_int(value: object | None) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value >= 0 else None
    if isinstance(value, float) and value >= 0 and value.is_integer():
        return int(value)
    return None


def _dropped_message_count(entry: Mapping[str, object], metric_stem: str | None) -> int | None:
    if metric_stem is None:
        return None
    count = _nonnegative_int(entry.get(f"{metric_stem}{_INPUT_DROPPED_COUNT_SUFFIX}"))
    if count is None or count > _MAX_UINT64:
        return None
    return count


def _cursor_sequence_number(sequence_numbers: Sequence[int], cursor_position: int) -> int | None:
    if cursor_position < len(sequence_numbers):
        return sequence_numbers[cursor_position]
    if not sequence_numbers:
        return 0
    next_sequence_number = sequence_numbers[-1] + 1
    return next_sequence_number if next_sequence_number <= _MAX_UINT64 else None


def _missing_input_metadata_gap(channel_name: str, cog_instance_path: str, execution_index: int) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA,
        message=(
            "Input view sequence metadata was not found for "
            f"channel {channel_name} on cog {cog_instance_path} execution {execution_index}."
        ),
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )


def _malformed_input_metadata_gap(channel_name: str, cog_instance_path: str, execution_index: int) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA,
        message=(
            "Input view sequence metadata was malformed for "
            f"channel {channel_name} on cog {cog_instance_path} execution {execution_index}."
        ),
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )


def _missing_output_sequence_gap(channel_name: str, cog_instance_path: str, execution_index: int) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA,
        message=(
            "Output sequence signals were not found for "
            f"channel {channel_name} on cog {cog_instance_path} execution {execution_index}."
        ),
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )


def _malformed_output_sequence_gap(channel_name: str, cog_instance_path: str, execution_index: int) -> ScopeGap:
    return ScopeGap(
        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA,
        message=(
            "Output sequence signals were malformed for "
            f"channel {channel_name} on cog {cog_instance_path} execution {execution_index}."
        ),
        cog_instance_path=cog_instance_path,
        channel_name=channel_name,
        execution_index=execution_index,
    )
