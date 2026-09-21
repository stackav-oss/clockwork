# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal execution metrics extraction."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, final

import pytest
from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.cog_channel import CogChannelRef
from clockwork.tools.journal_file_generator.execution_extractor import extract_execution_metrics_from_extractor
from clockwork.tools.journal_file_generator.scope import ChannelSchemaRef, ResolvedCogScope, ResolvedJournalScope
from clockwork.tools.signal_extractor.signal_extractor import (
    ReportGroupAggregation,
    ReportGroupBatch,
    SignalExtractionFilter,
)

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping, Sequence

_COG_INSTANCE_PATH = "runtime.path.Cog"
_EVENT_CHANNEL = "/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/uuid"
_FQ_CONDITION_SIGNAL = "@clockwork::clockwork::dsl::cog::cog_execution_metrics_signals.execution_condition_active"


@final
@dataclass(frozen=True, kw_only=True)
class _FakeSignalInfo:
    """Minimal fake report-group signal info."""

    signal_name: str
    alias: str | None = None


@final
@dataclass(frozen=True, kw_only=True)
class _FakeStreamingReportGroup:
    """Minimal fake streaming report group."""

    cog_instance_path: str
    channel_name: str
    batches_data: tuple[ReportGroupBatch | ReportGroupAggregation, ...]
    report_group_name: str = "cog_event_metrics_group"
    signal_info: tuple[_FakeSignalInfo, ...] = ()

    def batches(self) -> Iterator[ReportGroupBatch | ReportGroupAggregation]:
        """Return the configured fake batches."""
        yield from self.batches_data


@final
@dataclass(kw_only=True)
class _FakeSignalExtractor:
    """Minimal fake signal extractor."""

    report_groups: tuple[_FakeStreamingReportGroup, ...]
    filters: list[SignalExtractionFilter | None] = field(default_factory=list)

    def stream(self, extraction_filter: SignalExtractionFilter | None = None) -> Iterator[_FakeStreamingReportGroup]:
        """Record the filter and return configured fake report groups."""
        self.filters.append(extraction_filter)
        yield from self.report_groups


@final
@dataclass(frozen=True, kw_only=True)
class _InputSequenceMetadata:
    """Input sequence metadata fixture."""

    message_sequence_numbers: tuple[int, ...]
    cursor_position: int


def test_condition_flags_are_packed_in_metadata_order() -> None:
    """Verify condition flag bits follow report-group metadata order."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                signal_info=(
                    _FakeSignalInfo(signal_name=_FQ_CONDITION_SIGNAL, alias="beta_active"),
                    _FakeSignalInfo(signal_name=_FQ_CONDITION_SIGNAL, alias="alpha_active"),
                ),
                batches_data=(
                    _make_batch(
                        (
                            {
                                "cog_exec_start_time_value": 100,
                                "cog_dial_start_time_value": 90,
                                "cog_exec_duration_value": 3,
                                "cog_ready_to_exec_latency_value": 2,
                                "cog_attempt_to_exec_latency_value": 1,
                                "cog_requeue_count_value": 4,
                                "beta_active_value": 1,
                                "alpha_active_value": 0,
                            },
                            {
                                "cog_exec_start_time_value": 200,
                                "cog_dial_start_time_value": 190,
                                "cog_exec_duration_value": 4,
                                "cog_ready_to_exec_latency_value": 3,
                                "cog_attempt_to_exec_latency_value": 2,
                                "cog_requeue_count_value": 0,
                                "beta_active_value": 0,
                                "alpha_active_value": 1,
                            },
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=0,
        end_time_ns=1_000,
    )

    assert not result.gaps
    assert len(result.cog_executions) == 1
    assert result.cog_executions[0].condition_names == ("beta", "alpha")
    executions = result.cog_executions[0].executions
    assert [execution.condition_flags for execution in executions] == [0b01, 0b10]
    assert executions[0].execution_duration_ns == 30
    assert executions[0].dial_start_time_ns == 90
    assert executions[0].ready_to_exec_latency_ns == 20
    assert executions[0].attempt_to_exec_latency_ns == 10
    assert executions[0].requeue_count == 4
    assert executions[1].dial_start_time_ns == 190
    recorded_filter = extractor.filters[0]
    assert recorded_filter is not None
    assert recorded_filter.cog_instance_paths == [_COG_INSTANCE_PATH]
    assert recorded_filter.report_group_names == ["cog_event_metrics_group"]
    assert recorded_filter.start_time_ns == 0
    assert recorded_filter.end_time_ns is None


def test_entries_are_filtered_by_execution_time_and_sorted() -> None:
    """Verify execution records use entry timestamps for filtering and ordering."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(
                    _make_batch(
                        (
                            _entry(dial_start_time_ns=30, execution_start_time_ns=300, duration_ten_ns=1),
                            _entry(dial_start_time_ns=20, execution_start_time_ns=200, duration_ten_ns=2),
                            _entry(dial_start_time_ns=10, execution_start_time_ns=100, duration_ten_ns=3),
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=100,
        end_time_ns=250,
    )

    executions = result.cog_executions[0].executions
    assert [execution.dial_start_time_ns for execution in executions] == [10, 20]
    assert [execution.execution_start_time_ns for execution in executions] == [100, 200]
    assert [execution.execution_duration_ns for execution in executions] == [30, 20]
    assert [execution.execution_index for execution in executions] == [0, 1]
    assert not result.gaps


def test_memory_resource_metrics_populate_execution_memory_stats() -> None:
    """Verify named memory resource metrics populate the journal memory statistics."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(
                    _make_batch(
                        (
                            {
                                **_entry(dial_start_time_ns=50, execution_start_time_ns=100, duration_ten_ns=1),
                                "state_allocator_peak_allocated_value": 400,
                                "state_allocator_current_allocated_value": 300,
                                "state_allocator_total_allocated_value": 700,
                                "state_allocator_total_deallocated_value": 400,
                            },
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=0,
        end_time_ns=1_000,
    )

    assert list(result.cog_executions[0].executions[0].memory_stats) == [
        journal_pb2.MemoryStats(
            resource_name="state_allocator",
            peak_allocated=400,
            current_allocated=300,
            total_allocated=700,
            total_deallocated=400,
        )
    ]


def test_missing_event_metrics_records_gap() -> None:
    """Verify cogs without an event metrics stream produce a readiness gap."""
    result = extract_execution_metrics_from_extractor(
        _FakeSignalExtractor(report_groups=()),
        resolved_scope=_make_resolved_scope(metrics_channels=()),
        start_time_ns=0,
        end_time_ns=10,
    )

    assert not result.cog_executions
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EVENT_METRICS_CHANNEL
    assert result.gaps[0].cog_instance_path == _COG_INSTANCE_PATH


def test_empty_event_metrics_range_records_gap() -> None:
    """Verify an event stream with no in-window executions records a readiness gap."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(
                    _make_batch((_entry(dial_start_time_ns=10, execution_start_time_ns=20, duration_ten_ns=1),)),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=0,
        end_time_ns=10,
    )

    assert len(result.cog_executions) == 1
    assert not result.cog_executions[0].executions
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_TIME_RANGE_CONTAINS_NO_EXECUTIONS


def test_missing_execution_metric_skips_row_and_records_gap() -> None:
    """Explicitly missing signals make a row unusable without zero coercion."""
    missing_entry = _entry(dial_start_time_ns=5, execution_start_time_ns=10, duration_ten_ns=1)
    missing_entry["cog_exec_duration_value"] = None
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                signal_info=(_FakeSignalInfo(signal_name=_FQ_CONDITION_SIGNAL, alias="periodic_active"),),
                batches_data=(
                    _make_batch(
                        (
                            {**missing_entry, "periodic_active_value": None},
                            {
                                **_entry(dial_start_time_ns=10, execution_start_time_ns=20, duration_ten_ns=2),
                                "periodic_active_value": 1,
                            },
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=0,
        end_time_ns=30,
    )

    assert [execution.execution_start_time_ns for execution in result.cog_executions[0].executions] == [20]
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EXECUTION_METRICS
    assert result.gaps[0].channel_name == _EVENT_CHANNEL
    assert "cog_exec_duration_value" in result.gaps[0].message
    assert "periodic_active_value" in result.gaps[0].message


def test_missing_execution_timestamp_violates_report_group_invariant() -> None:
    """Every generated cog event report row must contain its execution timestamp."""
    entry = _entry(dial_start_time_ns=5, execution_start_time_ns=10, duration_ten_ns=1)
    entry["cog_exec_start_time_value"] = None
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(_make_batch((entry,)),),
            ),
        )
    )

    with pytest.raises(AssertionError, match="Cog execution start time must always be present"):
        extract_execution_metrics_from_extractor(
            extractor,
            resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
            start_time_ns=0,
            end_time_ns=30,
        )


def test_condition_names_fall_back_to_entry_fields() -> None:
    """Verify logs without condition metadata still get deterministic condition names."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(
                    _make_batch(
                        (
                            {
                                **_entry(dial_start_time_ns=5, execution_start_time_ns=10, duration_ten_ns=1),
                                "zed_active_value": 1,
                                "alpha_active_value": 1,
                            },
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(metrics_channels=(_EVENT_CHANNEL,)),
        start_time_ns=0,
        end_time_ns=20,
    )

    assert result.cog_executions[0].condition_names == ("alpha", "zed")
    assert result.cog_executions[0].executions[0].condition_flags == 0b11


def test_execution_records_map_member_metadata_to_channel_names() -> None:
    """Verify execution extraction attaches member-mapped journal metadata."""
    extractor = _FakeSignalExtractor(
        report_groups=(
            _FakeStreamingReportGroup(
                cog_instance_path=_COG_INSTANCE_PATH,
                channel_name=_EVENT_CHANNEL,
                batches_data=(
                    _make_batch(
                        (
                            {
                                **_entry(dial_start_time_ns=5, execution_start_time_ns=10, duration_ten_ns=1),
                                "sensor_unseen_messages_value": 1,
                                "sensor_unseen_messages_value_metadata": _InputSequenceMetadata(
                                    message_sequence_numbers=(100,),
                                    cursor_position=0,
                                ),
                                "command_num_messages_value": 1,
                                "command_first_sequence_number_value": 200,
                            },
                        )
                    ),
                ),
            ),
        )
    )

    result = extract_execution_metrics_from_extractor(
        extractor,
        resolved_scope=_make_resolved_scope(
            metrics_channels=(_EVENT_CHANNEL,),
            input_channel_names=("input_channel",),
            output_channel_names=("output_channel",),
            input_channel_refs=(CogChannelRef(cog_member_name="sensor", channel_name="input_channel"),),
            output_channel_refs=(CogChannelRef(cog_member_name="command", channel_name="output_channel"),),
        ),
        start_time_ns=0,
        end_time_ns=20,
    )

    assert not result.gaps
    execution = result.cog_executions[0].executions[0]
    assert list(execution.input_views) == [
        journal_pb2.InputViewState(
            channel_name="input_channel",
            cog_member_name="sensor",
            cursor_sequence_number=100,
            visible_sequence_numbers=[100],
            first_new_index=0,
        )
    ]
    assert list(execution.outputs) == [
        journal_pb2.OutputState(
            channel_name="output_channel",
            produced_sequence_numbers=[200],
        )
    ]


def _make_resolved_scope(
    *,
    metrics_channels: tuple[str, ...],
    input_channel_names: tuple[str, ...] = (),
    output_channel_names: tuple[str, ...] = (),
    input_channel_refs: tuple[CogChannelRef, ...] = (),
    output_channel_refs: tuple[CogChannelRef, ...] = (),
) -> ResolvedJournalScope:
    return ResolvedJournalScope(
        requested_cog_instance_paths=(_COG_INSTANCE_PATH,),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path=_COG_INSTANCE_PATH,
                cog_path="demo.Cog",
                input_schemas=tuple(
                    ChannelSchemaRef(channel_name=channel_name, schema_name="", schema_uuid="")
                    for channel_name in input_channel_names
                ),
                output_schemas=tuple(
                    ChannelSchemaRef(channel_name=channel_name, schema_name="", schema_uuid="")
                    for channel_name in output_channel_names
                ),
                metrics_channels=metrics_channels,
                input_channel_refs=input_channel_refs,
                output_channel_refs=output_channel_refs,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )


def _make_batch(entries: Sequence[Mapping[str, object]]) -> ReportGroupBatch:
    return ReportGroupBatch(
        publish_time_ns=0,
        execution_count=len(entries),
        execution_interval_ns=0,
        entries=[dict(entry) for entry in entries],
    )


def _entry(*, dial_start_time_ns: int, execution_start_time_ns: int, duration_ten_ns: int) -> dict[str, object]:
    entry: dict[str, object] = {
        "cog_dial_start_time_value": dial_start_time_ns,
        "cog_exec_start_time_value": execution_start_time_ns,
        "cog_exec_duration_value": duration_ten_ns,
        "cog_ready_to_exec_latency_value": 0,
        "cog_attempt_to_exec_latency_value": 0,
        "cog_requeue_count_value": 0,
    }
    return entry
