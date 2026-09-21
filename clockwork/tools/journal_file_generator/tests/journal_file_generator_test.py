# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal file generator request validation and writing."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass, replace
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from clockwork.journal import journal_pb2, journal_topology_pb2
from clockwork.tools.journal_file_generator import generator
from clockwork.tools.journal_file_generator import topology_file as topology_file_module
from clockwork.tools.journal_file_generator.alignment import AlignmentExtraction, ExtractedExecutionAlignment
from clockwork.tools.journal_file_generator.channel_summary import ChannelSummaryExtraction
from clockwork.tools.journal_file_generator.cog_channel import (
    CogChannelMap,
    CogChannelRef,
    DiscoveredSnapshotChannelRef,
)
from clockwork.tools.journal_file_generator.execution_extractor import ExecutionExtraction, ExtractedCogExecutions
from clockwork.tools.journal_file_generator.generator import (
    GENERATOR_VERSION,
    JournalExtractions,
    build_journal_file,
    build_journal_file_from_log,
    snapshot_selection_to_proto,
)
from clockwork.tools.journal_file_generator.log_index import LogIndex, TopicInfo
from clockwork.tools.journal_file_generator.message_index import MessageIndexExtraction
from clockwork.tools.journal_file_generator.readiness import (
    build_readiness_summary,
    format_readiness_summary,
    implemented_readiness_checks_pass,
    write_readiness_summary_json,
)
from clockwork.tools.journal_file_generator.request import (
    RequestValidationError,
    SnapshotSelection,
    create_journal_request,
)
from clockwork.tools.journal_file_generator.scope import (
    ChannelSchemaRef,
    ChannelTopology,
    ResolvedCogScope,
    ResolvedJournalScope,
    ScopeGap,
)
from clockwork.tools.journal_file_generator.snapshot_selector import SelectedCogSnapshot, SnapshotExtraction
from clockwork.tools.journal_file_generator.writer import write_journal_file
from google.protobuf import text_format

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence


def _journal_file_generator_binary() -> Path:
    runfiles_dir = os.getenv("RUNFILES_DIR")
    assert runfiles_dir is not None

    matches = sorted(Path(runfiles_dir).glob("*/clockwork/tools/journal_file_generator/journal_file_generator"))
    assert matches
    return matches[0]


def _write_journal_topology(path: Path) -> None:
    path.write_text(
        text_format.MessageToString(
            journal_topology_pb2.JournalTopology(
                format_version=1,
                system_target_name="runtime.System",
                cogs=[
                    journal_topology_pb2.CogTopology(cog_instance_path=cog_path)
                    for cog_path in ("runtime.Alpha", "runtime.AlphaOther", "runtime.Beta")
                ],
            )
        )
    )


@dataclass(frozen=True, kw_only=True)
class _FakeCogInstanceMetadata:
    """Minimal fake cog instance signal metadata."""

    cog_path: str
    cog_instance_path: str


@dataclass(frozen=True, kw_only=True)
class _FakeReportGroupChannelMetadata:
    """Minimal fake report-group channel signal metadata."""

    channel_name: str
    cog_path: str
    cog_instance_path: str
    is_cog_metrics_channel: bool


@dataclass(frozen=True, kw_only=True)
class _FakeSignalMetadata:
    """Minimal fake signal metadata object."""

    cog_instances: tuple[_FakeCogInstanceMetadata, ...]
    report_group_channels: tuple[_FakeReportGroupChannelMetadata, ...] = ()


def _topic_info(*, name: str, schema_name: str, schema_uuid: str) -> TopicInfo:
    return TopicInfo(
        name=name,
        schema_name=schema_name,
        schema_uuid=schema_uuid,
        message_encoding="tachyon",
        channel_type="persistent",
        schema_encoding="clockwork_tachyon",
    )


def _assert_resolved_snapshot_refs(resolved_scope: ResolvedJournalScope) -> None:
    snapshot_refs = resolved_scope.cog_scopes[0].snapshot_channels

    assert resolved_scope.cog_scopes[0].has_clockwork_state
    assert [(ref.cog_member_name, ref.channel_name) for ref in snapshot_refs] == [
        ("state", "StateSnapshotChannel"),
    ]
    assert snapshot_refs[0].schema_name == "demo.State"
    assert snapshot_refs[0].schema_uuid == "33333333-3333-3333-3333-333333333333"
    assert snapshot_refs[0].has_logged_messages


def test_request_builds_journal_metadata(tmp_path: Path) -> None:
    """Verify generation writes request metadata and resolved cog scope."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "nested" / "cog.journal.pb",
        start_time_ns=123,
        end_time_ns=456,
        cog_instance_path="runtime.path.Cog",
    )

    journal = build_journal_file(request)
    expected = journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=123,
            end_time_ns=456,
            log_uri="/logs/demo.clog",
            generator_version=GENERATOR_VERSION,
            scope=journal_pb2.JournalScope(
                cog_instance_paths=["runtime.path.Cog"],
            ),
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=False,
                has_state_snapshot=False,
            ),
        )
    )

    assert request.output.parent.exists()
    assert journal == expected


def test_resolved_scope_round_trips_to_file(tmp_path: Path) -> None:
    """Verify resolved cog-instance mode writes deterministic protobuf bytes."""
    output = tmp_path / "explicit.journal.pb"
    request = create_journal_request(
        log_uri="s3://bucket/demo",
        output=output,
        start_time_ns=1,
        end_time_ns=1,
        cog_instance_path="runtime.path.Cog",
    )
    journal = build_journal_file(request)

    write_journal_file(journal, output)
    parsed = journal_pb2.JournalFile()
    parsed.ParseFromString(output.read_bytes())

    assert parsed == journal
    assert list(parsed.metadata.scope.cog_instance_paths) == ["runtime.path.Cog"]


def test_group_scope_paths_are_sorted(tmp_path: Path) -> None:
    """Verify group journal scope is normalized into deterministic order."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "group.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_paths=["runtime.path.Zed", "runtime.path.Alpha", "runtime.path.Zed"],
    )

    journal = build_journal_file(request)

    assert request.cog_instance_paths == ("runtime.path.Alpha", "runtime.path.Zed")
    assert list(journal.metadata.scope.cog_instance_paths) == ["runtime.path.Alpha", "runtime.path.Zed"]


def test_box_scope_paths_are_sorted_and_serialized(tmp_path: Path) -> None:
    """Verify box scope preserves requested boxes and expanded cog scope."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "box.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        box_instance_paths=["runtime.box.Zed", "runtime.box.Alpha", "runtime.box.Zed"],
        system_target="@clockwork//clockwork/tests/support:test_system_multi_route_clk",
    )

    journal = build_journal_file(replace(request, cog_instance_paths=("runtime.box.Alpha.Cog", "runtime.box.Zed.Cog")))

    assert request.cog_instance_paths == ()
    assert request.box_instance_paths == ("runtime.box.Alpha", "runtime.box.Zed")
    assert list(journal.metadata.scope.box_instance_paths) == ["runtime.box.Alpha", "runtime.box.Zed"]
    assert list(journal.metadata.scope.cog_instance_paths) == ["runtime.box.Alpha.Cog", "runtime.box.Zed.Cog"]


def test_request_accepts_optional_system_context(tmp_path: Path) -> None:
    """Verify generation requests can carry a system context for channel-map resolution."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "out.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
        system_target="@clockwork//clockwork/tests/support:signal_test_system_clk",
    )

    assert request.system_target == "@clockwork//clockwork/tests/support:signal_test_system_clk"
    assert request.system_clk_file is None


def test_box_scope_expands_before_log_extractions(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify box scope expands before resolved-scope validation and extraction."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "box.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        box_instance_path="runtime.box",
        system_target="@clockwork//clockwork/tests/support:test_system_multi_route_clk",
    )
    expanded_cog_paths = ("runtime.box.CogA", "runtime.box.CogB")
    expected_log_index = LogIndex(topics=())
    expected_resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=expanded_cog_paths,
        cog_scopes=(),
        channel_topologies=(),
        gaps=(),
    )

    def fake_expand_box_instance_paths(
        *,
        system_clk_file: Path | None = None,
        system_target: str | None = None,
        box_instance_paths: Sequence[str],
    ) -> tuple[str, ...]:
        assert system_clk_file is None
        assert system_target == request.system_target
        assert tuple(box_instance_paths) == ("runtime.box",)
        return expanded_cog_paths

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return expected_log_index

    def fake_load_signal_metadata(log_uri: str) -> None:
        assert log_uri == "/logs/demo.clog"

    def fake_discover_cog_channel_maps(
        *,
        system_clk_file: Path | None = None,
        system_target: str | None = None,
    ) -> dict[str, CogChannelMap]:
        assert system_clk_file is None
        assert system_target == request.system_target
        return {}

    def fake_resolve_scope(
        *,
        requested_cog_instance_paths: Sequence[str],
        log_index: LogIndex,
        signal_metadata: object | None,
        cog_channel_maps: Mapping[str, CogChannelMap] | None = None,
    ) -> ResolvedJournalScope:
        assert tuple(requested_cog_instance_paths) == expanded_cog_paths
        assert log_index is expected_log_index
        assert signal_metadata is None
        assert cog_channel_maps == {}
        return expected_resolved_scope

    def fake_extract_execution_metrics(
        extraction_request: object,
        extraction_scope: ResolvedJournalScope,
    ) -> ExecutionExtraction:
        assert isinstance(extraction_request, type(request))
        assert extraction_request.box_instance_paths == ("runtime.box",)
        assert extraction_request.cog_instance_paths == expanded_cog_paths
        assert extraction_scope is expected_resolved_scope
        return ExecutionExtraction(cog_executions=(), gaps=())

    monkeypatch.setattr(generator, "expand_box_instance_paths", fake_expand_box_instance_paths)
    monkeypatch.setattr(generator, "load_log_index", fake_load_log_index)
    monkeypatch.setattr(generator, "load_signal_metadata", fake_load_signal_metadata)
    monkeypatch.setattr(generator, "discover_cog_channel_maps", fake_discover_cog_channel_maps)
    monkeypatch.setattr(generator, "resolve_scope", fake_resolve_scope)
    monkeypatch.setattr(generator, "extract_execution_metrics", fake_extract_execution_metrics)

    journal = build_journal_file_from_log(request)

    assert list(journal.metadata.scope.box_instance_paths) == ["runtime.box"]
    assert list(journal.metadata.scope.cog_instance_paths) == list(expanded_cog_paths)


def test_request_accepts_summary_json_and_fail_flag(tmp_path: Path) -> None:
    """Verify generation requests can carry CLI output and readiness-exit options."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "out.journal.pb",
        summary_json=tmp_path / "nested" / "summary.json",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
        fail_if_not_replayable=True,
    )

    assert request.summary_json == tmp_path / "nested" / "summary.json"
    assert request.summary_json is not None
    assert request.summary_json.parent.exists()
    assert request.fail_if_not_replayable


def test_build_journal_file_includes_resolved_scope_details(tmp_path: Path) -> None:
    """Verify PR4 resolved-scope data is represented in the journal protobuf."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "resolved.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Cog",
                cog_path="demo.Cog",
                input_schemas=(
                    ChannelSchemaRef(
                        channel_name="input",
                        schema_name="demo.Input",
                        schema_uuid="11111111-1111-1111-1111-111111111111",
                    ),
                ),
                output_schemas=(
                    ChannelSchemaRef(
                        channel_name="output",
                        schema_name="demo.Output",
                        schema_uuid="22222222-2222-2222-2222-222222222222",
                    ),
                ),
                metrics_channels=("/_clockwork/journal/execution-metrics/demo.Cog/id",),
            ),
        ),
        channel_topologies=(
            ChannelTopology(
                channel_name="input",
                producer_cog_instance="runtime.path.Source",
                consumer_cog_instances=("runtime.path.Cog",),
                has_logged_messages=True,
            ),
        ),
        gaps=(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA,
                message="Signal metadata was not found in the log.",
            ),
        ),
    )

    journal = build_journal_file(request, resolved_scope)

    assert journal.cog_journals[0].cog_instance_path == "runtime.path.Cog"
    assert journal.cog_journals[0].input_schemas[0].schema_uuid == "11111111-1111-1111-1111-111111111111"
    assert journal.cog_journals[0].output_schemas[0].schema_name == "demo.Output"
    assert journal.channel_summaries[0].producer_cog_instance == "runtime.path.Source"
    assert journal.channel_summaries[0].has_logged_messages
    assert journal.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA
    )


def test_build_journal_file_includes_execution_metrics(tmp_path: Path) -> None:
    """Verify extracted execution metrics are represented in the journal protobuf."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "executions.journal.pb",
        start_time_ns=10,
        end_time_ns=30,
        cog_instance_path="runtime.path.Cog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Cog",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=("/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/id",),
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.Cog",
                condition_names=("periodic", "new_input"),
                executions=(
                    journal_pb2.CogExecution(
                        dial_start_time_ns=5,
                        execution_start_time_ns=10,
                        execution_duration_ns=20,
                        condition_flags=0b01,
                        execution_index=0,
                        memory_stats=[
                            journal_pb2.MemoryStats(
                                resource_name="state_allocator",
                                peak_allocated=4096,
                                current_allocated=2048,
                                total_allocated=8192,
                                total_deallocated=6144,
                            )
                        ],
                    ),
                    journal_pb2.CogExecution(
                        dial_start_time_ns=25,
                        execution_start_time_ns=30,
                        execution_duration_ns=40,
                        condition_flags=0b11,
                        execution_index=1,
                    ),
                ),
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(request, resolved_scope, JournalExtractions(execution=execution_extraction))

    assert journal == journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=10,
            end_time_ns=30,
            log_uri="/logs/demo.clog",
            generator_version=GENERATOR_VERSION,
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=True,
                has_state_snapshot=False,
            ),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                condition_names=["periodic", "new_input"],
                executions=[
                    journal_pb2.CogExecution(
                        dial_start_time_ns=5,
                        execution_start_time_ns=10,
                        execution_duration_ns=20,
                        condition_flags=0b01,
                        execution_index=0,
                        memory_stats=[
                            journal_pb2.MemoryStats(
                                resource_name="state_allocator",
                                peak_allocated=4096,
                                current_allocated=2048,
                                total_allocated=8192,
                                total_deallocated=6144,
                            )
                        ],
                    ),
                    journal_pb2.CogExecution(
                        dial_start_time_ns=25,
                        execution_start_time_ns=30,
                        execution_duration_ns=40,
                        condition_flags=0b11,
                        execution_index=1,
                    ),
                ],
            )
        ],
    )


def test_build_journal_file_includes_channel_summaries_and_missing_inputs(tmp_path: Path) -> None:
    """Verify channel summaries and missing-input gaps are represented in the journal protobuf."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "channels.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(),
        channel_topologies=(),
        gaps=(),
    )
    channel_summary_extraction = ChannelSummaryExtraction(
        channel_summaries=(
            journal_pb2.ChannelSummary(
                channel_name="missing_input",
                consumer_cog_instances=["runtime.path.Cog"],
                has_logged_messages=False,
            ),
            journal_pb2.ChannelSummary(
                channel_name="logged_input",
                producer_cog_instance="runtime.path.Source",
                consumer_cog_instances=["runtime.path.Cog"],
                first_sequence_number=3,
                last_sequence_number=5,
                message_count=2,
                has_logged_messages=True,
            ),
        ),
        gaps=(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="Required input channel was not found in the log.",
                cog_instance_path="runtime.path.Cog",
                channel_name="missing_input",
            ),
        ),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(channel_summary=channel_summary_extraction),
    )

    assert list(journal.metadata.replay_readiness.missing_inputs) == ["missing_input"]
    assert journal.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL
    )
    assert [summary.channel_name for summary in journal.channel_summaries] == ["logged_input", "missing_input"]


def test_build_journal_file_includes_channel_messages_without_readiness_gap(tmp_path: Path) -> None:
    """Verify report message-index records do not affect replay readiness gaps."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "messages.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    message_index_extraction = MessageIndexExtraction(
        channel_messages=(
            journal_pb2.ChannelMessage(
                channel_name="output_channel",
                sequence_number=2,
                publish_time_ns=12,
                payload_size_bytes=9,
            ),
            journal_pb2.ChannelMessage(
                channel_name="input_channel",
                sequence_number=1,
                publish_time_ns=11,
                payload_size_bytes=7,
            ),
        ),
        skipped_message_metadata_count=1,
    )

    journal = build_journal_file(
        request,
        extractions=JournalExtractions(message_index=message_index_extraction),
    )

    assert [message.channel_name for message in journal.channel_messages] == ["input_channel", "output_channel"]
    assert not journal.metadata.replay_readiness.gaps


def test_readiness_summary_json_records_gap_details(tmp_path: Path) -> None:
    """Verify summary JSON exposes deterministic readiness fields and gap reason names."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "channels.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    channel_summary_extraction = ChannelSummaryExtraction(
        channel_summaries=(),
        gaps=(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="Required input channel was not found in the log.",
                cog_instance_path="runtime.path.Cog",
                channel_name="missing_input",
            ),
        ),
    )
    journal = build_journal_file(
        request,
        resolved_scope=ResolvedJournalScope(
            requested_cog_instance_paths=("runtime.path.Cog",),
            cog_scopes=(),
            channel_topologies=(),
            gaps=(),
        ),
        extractions=JournalExtractions(channel_summary=channel_summary_extraction),
    )
    output = tmp_path / "nested" / "summary.json"

    write_readiness_summary_json(journal, output)

    summary = json.loads(output.read_text(encoding="utf-8"))
    assert output.read_text(encoding="utf-8").endswith("\n")
    assert summary["sufficient_for_replay"] is False
    assert summary["implemented_readiness_checks_passed"] is False
    assert summary["missing_inputs"] == ["missing_input"]
    assert summary["gaps"] == [
        {
            "channel_name": "missing_input",
            "cog_instance_path": "runtime.path.Cog",
            "execution_index": 0,
            "message": "Required input channel was not found in the log.",
            "reason": "REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL",
        }
    ]


def test_verbose_readiness_summary_groups_gap_details(tmp_path: Path) -> None:
    """Verify verbose console readiness output summarizes repeated gaps."""
    summary_json = tmp_path / "summary.json"
    journal = journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=False,
                has_state_snapshot=True,
                gaps=[
                    journal_pb2.ReplayReadinessGap(
                        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA,
                        message=f"gap message {index}",
                        cog_instance_path="runtime.path.Cog",
                        channel_name="output_a",
                        execution_index=index,
                    )
                    for index in range(6)
                ]
                + [
                    journal_pb2.ReplayReadinessGap(
                        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                        message="input channel missing",
                        cog_instance_path="runtime.path.Cog",
                        channel_name="missing_input",
                    )
                ],
            )
        )
    )

    summary = format_readiness_summary(journal, summary_json_path=summary_json, include_gaps=True)

    assert "Replay-readiness gaps: total=7" in summary
    assert "- REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA: 6" in summary
    assert "- REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL: 1" in summary
    assert "- output_a: 6" in summary
    assert "- missing_input: 1" in summary
    assert "Sample gaps:" in summary
    assert "input channel missing" in summary
    assert "gap message 0" in summary
    assert "gap message 5" not in summary
    assert "Omitted 2 readiness gap details from console output." in summary
    assert f"Full readiness gap details are in: {summary_json}" in summary


def test_build_journal_file_includes_selected_state_snapshot(tmp_path: Path) -> None:
    """Verify selected snapshot metadata is represented in the journal protobuf."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "snapshot.journal.pb",
        start_time_ns=100,
        end_time_ns=200,
        cog_instance_path="runtime.path.Cog",
        snapshot_selection="closest",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Cog",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=True,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    snapshot_extraction = SnapshotExtraction(
        selected_snapshots=(
            SelectedCogSnapshot(
                cog_instance_path="runtime.path.Cog",
                channel_name="StateSnapshotChannel",
                snapshot_time_ns=90,
                state_data=b"serialized-state",
                state_schema_uuid="33333333-3333-3333-3333-333333333333",
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(snapshot=snapshot_extraction),
    )

    assert journal.metadata.replay_readiness.has_state_snapshot
    assert journal.cog_journals[0].has_clockwork_state
    assert journal.cog_journals[0].state_snapshot == journal_pb2.StateSnapshot(
        snapshot_time_ns=90,
        selection=journal_pb2.SNAPSHOT_SELECTION_CLOSEST,
        state_data=b"serialized-state",
        state_schema_uuid="33333333-3333-3333-3333-333333333333",
    )


def test_complete_single_cog_journal_passes_implemented_checks_and_is_deterministically_ordered(
    tmp_path: Path,
) -> None:  # Comprehensive protobuf ordering setup.
    """Verify complete single-cog extraction output is ordered and sufficient."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "complete.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Cog",
                cog_path="demo.Cog",
                input_schemas=(
                    ChannelSchemaRef(channel_name="input_b", schema_name="demo.InputB", schema_uuid="uuid-b"),
                    ChannelSchemaRef(channel_name="input_a", schema_name="demo.InputA", schema_uuid="uuid-a"),
                ),
                output_schemas=(
                    ChannelSchemaRef(channel_name="output_b", schema_name="demo.OutputB", schema_uuid="uuid-d"),
                    ChannelSchemaRef(channel_name="output_a", schema_name="demo.OutputA", schema_uuid="uuid-c"),
                ),
                metrics_channels=("/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/id",),
                has_clockwork_state=True,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.Cog",
                condition_names=("periodic",),
                executions=(
                    journal_pb2.CogExecution(
                        execution_start_time_ns=20,
                        execution_duration_ns=2,
                        execution_index=1,
                        input_views=[
                            journal_pb2.InputViewState(channel_name="input_b", cog_member_name="b"),
                            journal_pb2.InputViewState(channel_name="input_a", cog_member_name="a"),
                        ],
                        outputs=[
                            journal_pb2.OutputState(channel_name="output_b"),
                            journal_pb2.OutputState(channel_name="output_a"),
                        ],
                    ),
                    journal_pb2.CogExecution(
                        execution_start_time_ns=10,
                        execution_duration_ns=1,
                        execution_index=0,
                    ),
                ),
            ),
        ),
        gaps=(),
    )
    channel_summary_extraction = ChannelSummaryExtraction(
        channel_summaries=(
            journal_pb2.ChannelSummary(channel_name="output_b", producer_cog_instance="runtime.path.Cog"),
            journal_pb2.ChannelSummary(
                channel_name="input_a",
                producer_cog_instance="runtime.path.Source",
                consumer_cog_instances=["runtime.path.Other", "runtime.path.Cog"],
                message_count=2,
                has_logged_messages=True,
            ),
        ),
        gaps=(),
    )
    snapshot_extraction = SnapshotExtraction(
        selected_snapshots=(
            SelectedCogSnapshot(
                cog_instance_path="runtime.path.Cog",
                channel_name="StateSnapshotChannel",
                snapshot_time_ns=9,
                state_data=b"state",
                state_schema_uuid="uuid-state",
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(
            execution=execution_extraction,
            channel_summary=channel_summary_extraction,
            snapshot=snapshot_extraction,
        ),
    )

    assert implemented_readiness_checks_pass(journal)
    assert journal.metadata.replay_readiness.sufficient_for_replay
    assert [schema.channel_name for schema in journal.cog_journals[0].input_schemas] == ["input_a", "input_b"]
    assert [schema.channel_name for schema in journal.cog_journals[0].output_schemas] == ["output_a", "output_b"]
    assert [execution.execution_index for execution in journal.cog_journals[0].executions] == [0, 1]
    assert [view.channel_name for view in journal.cog_journals[0].executions[1].input_views] == [
        "input_a",
        "input_b",
    ]
    assert [output.channel_name for output in journal.cog_journals[0].executions[1].outputs] == [
        "output_a",
        "output_b",
    ]
    assert [summary.channel_name for summary in journal.channel_summaries] == ["input_a", "output_b"]
    assert list(journal.channel_summaries[0].consumer_cog_instances) == ["runtime.path.Cog", "runtime.path.Other"]
    assert build_readiness_summary(journal)["implemented_readiness_checks_passed"] is True
    assert "Replay readiness: sufficient" in format_readiness_summary(journal)


def test_build_journal_file_attaches_alignment_result(tmp_path: Path) -> None:
    """Verify correlated alignment results are serialized on the matching execution."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "aligned.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Cog",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.Cog",
                condition_names=(),
                executions=(journal_pb2.CogExecution(execution_start_time_ns=10, execution_index=0),),
            ),
        ),
        gaps=(),
    )
    alignment_extraction = AlignmentExtraction(
        execution_alignments=(
            ExtractedExecutionAlignment(
                cog_instance_path="runtime.path.Cog",
                execution_index=0,
                alignment_result=journal_pb2.AlignmentResult(
                    aligner_name="DemoAligner",
                    aligned_inputs=[
                        journal_pb2.AlignedInput(
                            input_name="lidar",
                            selected_sequence_number=10,
                            present=True,
                        )
                    ],
                ),
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(execution=execution_extraction, alignment=alignment_extraction),
    )

    assert journal.cog_journals[0].executions[0].alignment_result.aligner_name == "DemoAligner"
    assert journal.cog_journals[0].executions[0].alignment_result.aligned_inputs[0].input_name == "lidar"


def test_stateless_single_cog_without_snapshot_passes_implemented_checks(tmp_path: Path) -> None:
    """Verify stateless cogs do not require state snapshots for implemented readiness checks."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "stateless.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.StatelessCog",
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.StatelessCog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.StatelessCog",
                cog_path="demo.StatelessCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=("/_clockwork/report-groups/demo.StatelessCog/cog_event_metrics_group/id",),
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.StatelessCog",
                condition_names=(),
                executions=(journal_pb2.CogExecution(execution_start_time_ns=10, execution_index=0),),
            ),
        ),
        gaps=(),
    )
    snapshot_extraction = SnapshotExtraction(selected_snapshots=(), gaps=())

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(execution=execution_extraction, snapshot=snapshot_extraction),
    )

    assert implemented_readiness_checks_pass(journal)
    assert journal.metadata.replay_readiness.sufficient_for_replay
    assert not journal.metadata.replay_readiness.has_state_snapshot
    assert not journal.metadata.replay_readiness.gaps
    assert not journal.cog_journals[0].has_clockwork_state
    assert not journal.cog_journals[0].HasField("state_snapshot")


def test_group_journal_is_not_sufficient_for_single_cog_replay(tmp_path: Path) -> None:
    """Verify group journals stay report-oriented even when implemented checks have no gaps."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "group_complete.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_paths=["runtime.path.CogA", "runtime.path.CogB"],
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.CogA", "runtime.path.CogB"),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.CogA",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=("/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/a",),
                has_clockwork_state=True,
            ),
            ResolvedCogScope(
                cog_instance_path="runtime.path.CogB",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=("/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/b",),
                has_clockwork_state=True,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.CogA",
                condition_names=(),
                executions=(journal_pb2.CogExecution(execution_start_time_ns=10, execution_index=0),),
            ),
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.CogB",
                condition_names=(),
                executions=(journal_pb2.CogExecution(execution_start_time_ns=20, execution_index=0),),
            ),
        ),
        gaps=(),
    )
    snapshot_extraction = SnapshotExtraction(
        selected_snapshots=(
            SelectedCogSnapshot(
                cog_instance_path="runtime.path.CogA",
                channel_name="StateSnapshotChannelA",
                snapshot_time_ns=9,
                state_data=b"state-a",
                state_schema_uuid="uuid-state-a",
            ),
            SelectedCogSnapshot(
                cog_instance_path="runtime.path.CogB",
                channel_name="StateSnapshotChannelB",
                snapshot_time_ns=9,
                state_data=b"state-b",
                state_schema_uuid="uuid-state-b",
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(execution=execution_extraction, snapshot=snapshot_extraction),
    )

    assert journal.metadata.replay_readiness.has_state_snapshot
    assert not journal.metadata.replay_readiness.sufficient_for_replay


def test_box_journal_is_not_sufficient_for_single_cog_replay(tmp_path: Path) -> None:
    """Verify box journals remain report-oriented even when one cog passes implemented checks."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "box_complete.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        box_instance_path="runtime.path.Box",
        system_target="@clockwork//clockwork/tests/support:test_system_multi_route_clk",
    )
    request = replace(request, cog_instance_paths=("runtime.path.Box.Cog",))
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Box.Cog",),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.Box.Cog",
                cog_path="demo.Cog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=("/_clockwork/report-groups/demo.Cog/cog_event_metrics_group/id",),
                has_clockwork_state=False,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    execution_extraction = ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path="runtime.path.Box.Cog",
                condition_names=(),
                executions=(journal_pb2.CogExecution(execution_start_time_ns=10, execution_index=0),),
            ),
        ),
        gaps=(),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(
            execution=execution_extraction,
            snapshot=SnapshotExtraction(selected_snapshots=(), gaps=()),
        ),
    )

    assert not implemented_readiness_checks_pass(journal)
    assert not journal.metadata.replay_readiness.sufficient_for_replay


def test_build_journal_file_requires_snapshots_for_every_resolved_cog(tmp_path: Path) -> None:
    """Verify partial group snapshot selection does not mark snapshot readiness complete."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "partial_snapshot.journal.pb",
        start_time_ns=100,
        end_time_ns=200,
        cog_instance_paths=["runtime.path.ReadyCog", "runtime.path.MissingCog"],
    )
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.MissingCog", "runtime.path.ReadyCog"),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path="runtime.path.MissingCog",
                cog_path="demo.MissingCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=True,
            ),
            ResolvedCogScope(
                cog_instance_path="runtime.path.ReadyCog",
                cog_path="demo.ReadyCog",
                input_schemas=(),
                output_schemas=(),
                metrics_channels=(),
                has_clockwork_state=True,
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )
    snapshot_extraction = SnapshotExtraction(
        selected_snapshots=(
            SelectedCogSnapshot(
                cog_instance_path="runtime.path.ReadyCog",
                channel_name="ReadySnapshotChannel",
                snapshot_time_ns=100,
                state_data=b"ready-state",
                state_schema_uuid="33333333-3333-3333-3333-333333333333",
            ),
        ),
        gaps=(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
                message="No configured state snapshot channel was discovered.",
                cog_instance_path="runtime.path.MissingCog",
            ),
        ),
    )

    journal = build_journal_file(
        request,
        resolved_scope=resolved_scope,
        extractions=JournalExtractions(snapshot=snapshot_extraction),
    )

    assert not journal.metadata.replay_readiness.has_state_snapshot
    assert not journal.cog_journals[0].HasField("state_snapshot")
    assert journal.cog_journals[1].state_snapshot.state_data == b"ready-state"
    assert journal.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT
    )


def test_build_journal_file_from_log_records_missing_signal_metadata(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify the metadata-loading generator path serializes missing signal metadata gaps."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "missing_metadata.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
    )

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return LogIndex(topics=())

    def fake_load_signal_metadata(log_uri: str) -> None:
        assert log_uri == "/logs/demo.clog"

    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.load_log_index", fake_load_log_index)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.load_signal_metadata",
        fake_load_signal_metadata,
    )

    journal = build_journal_file_from_log(request)

    assert not journal.cog_journals
    assert len(journal.metadata.replay_readiness.gaps) == 1
    assert journal.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA
    )


def test_build_journal_file_from_log_records_known_and_unknown_cogs(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify the metadata-loading generator path emits known cog shells and unknown-cog gaps."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "mixed_scope.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_paths=["runtime.path.MissingCog", "runtime.path.KnownCog"],
    )
    signal_metadata = _FakeSignalMetadata(
        cog_instances=(
            _FakeCogInstanceMetadata(
                cog_path="demo.KnownCog",
                cog_instance_path="runtime.path.KnownCog",
            ),
        )
    )

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return LogIndex(topics=())

    def fake_load_signal_metadata(log_uri: str) -> _FakeSignalMetadata:
        assert log_uri == "/logs/demo.clog"
        return signal_metadata

    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.load_log_index", fake_load_log_index)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.load_signal_metadata",
        fake_load_signal_metadata,
    )

    journal = build_journal_file_from_log(request)

    assert [cog_journal.cog_instance_path for cog_journal in journal.cog_journals] == ["runtime.path.KnownCog"]
    assert journal.cog_journals[0].cog_path == "demo.KnownCog"
    gaps_by_reason = {gap.reason: gap for gap in journal.metadata.replay_readiness.gaps}
    assert gaps_by_reason[journal_pb2.REPLAY_READINESS_GAP_REASON_REQUESTED_COG_NOT_FOUND].cog_instance_path == (
        "runtime.path.MissingCog"
    )
    assert gaps_by_reason[journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_EVENT_METRICS_CHANNEL].cog_instance_path == (
        "runtime.path.KnownCog"
    )


def test_build_journal_file_from_log_wires_channel_extractions(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify the metadata-loading generator path serializes channel extraction results."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "channels.journal.pb",
        start_time_ns=10,
        end_time_ns=20,
        cog_instance_path="runtime.path.Cog",
    )
    expected_log_index = LogIndex(topics=())
    resolved_scope = ResolvedJournalScope(
        requested_cog_instance_paths=("runtime.path.Cog",),
        cog_scopes=(),
        channel_topologies=(),
        gaps=(),
    )
    expected_resolved_scope = resolved_scope
    channel_summary_extraction = ChannelSummaryExtraction(
        channel_summaries=(
            journal_pb2.ChannelSummary(
                channel_name="missing_input",
                consumer_cog_instances=["runtime.path.Cog"],
                has_logged_messages=False,
            ),
        ),
        gaps=(
            ScopeGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="Required input channel was not found in the log.",
                cog_instance_path="runtime.path.Cog",
                channel_name="missing_input",
            ),
        ),
    )
    message_index_extraction = MessageIndexExtraction(
        channel_messages=(
            journal_pb2.ChannelMessage(
                channel_name="logged_input",
                sequence_number=3,
                publish_time_ns=12,
                payload_size_bytes=8,
            ),
        ),
    )

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return expected_log_index

    def fake_load_signal_metadata(log_uri: str) -> None:
        assert log_uri == "/logs/demo.clog"

    def fake_resolve_scope(
        *,
        requested_cog_instance_paths: Sequence[str],
        log_index: LogIndex,
        signal_metadata: object | None,
        cog_channel_maps: Mapping[str, CogChannelMap] | None = None,
    ) -> ResolvedJournalScope:
        assert tuple(requested_cog_instance_paths) == request.cog_instance_paths
        assert log_index is expected_log_index
        assert signal_metadata is None
        assert cog_channel_maps is None
        return resolved_scope

    def fake_extract_execution_metrics(
        extraction_request: object,
        extraction_scope: ResolvedJournalScope,
    ) -> ExecutionExtraction:
        assert extraction_request is request
        assert extraction_scope is resolved_scope
        return ExecutionExtraction(cog_executions=(), gaps=())

    def fake_extract_channel_summaries(
        summary_request: object,
        *,
        resolved_scope: ResolvedJournalScope,
        log_index: LogIndex,
    ) -> ChannelSummaryExtraction:
        assert summary_request is request
        assert resolved_scope is expected_resolved_scope
        assert log_index is expected_log_index
        return channel_summary_extraction

    def fake_extract_channel_messages(
        index_request: object,
        *,
        resolved_scope: ResolvedJournalScope,
        log_index: LogIndex,
    ) -> MessageIndexExtraction:
        assert index_request is request
        assert resolved_scope is expected_resolved_scope
        assert log_index is expected_log_index
        return message_index_extraction

    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.load_log_index", fake_load_log_index)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.load_signal_metadata",
        fake_load_signal_metadata,
    )
    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.resolve_scope", fake_resolve_scope)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_execution_metrics",
        fake_extract_execution_metrics,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_channel_summaries",
        fake_extract_channel_summaries,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_channel_messages",
        fake_extract_channel_messages,
    )

    journal = build_journal_file_from_log(request)

    assert list(journal.metadata.replay_readiness.missing_inputs) == ["missing_input"]
    assert journal.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL
    )
    assert list(journal.channel_summaries) == list(channel_summary_extraction.channel_summaries)
    assert list(journal.channel_messages) == list(message_index_extraction.channel_messages)


def test_build_journal_file_from_log_uses_system_channel_maps(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify optional system context provides channel schemas through channel maps."""
    system_clk_file = tmp_path / "system.clk"
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "channel_scope.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.KnownCog",
        system_clk_file=system_clk_file,
    )
    signal_metadata = _FakeSignalMetadata(
        cog_instances=(
            _FakeCogInstanceMetadata(
                cog_path="demo.KnownCog",
                cog_instance_path="runtime.path.KnownCog",
            ),
        )
    )

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return LogIndex(topics=())

    def fake_load_signal_metadata(log_uri: str) -> _FakeSignalMetadata:
        assert log_uri == "/logs/demo.clog"
        return signal_metadata

    def fake_discover_cog_channel_maps(
        *,
        system_clk_file: Path | None = None,
        system_target: str | None = None,
    ) -> dict[str, CogChannelMap]:
        assert system_clk_file == request.system_clk_file
        assert system_target is None
        return {
            "runtime.path.KnownCog": CogChannelMap(
                cog_instance_path="runtime.path.KnownCog",
                input_channels=(CogChannelRef(cog_member_name="input_member", channel_name="input_channel"),),
                output_channels=(CogChannelRef(cog_member_name="output_member", channel_name="output_channel"),),
            )
        }

    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.load_log_index", fake_load_log_index)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.load_signal_metadata",
        fake_load_signal_metadata,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.discover_cog_channel_maps",
        fake_discover_cog_channel_maps,
    )

    journal = build_journal_file_from_log(request)

    assert journal.cog_journals[0].input_schemas[0].channel_name == "input_channel"
    assert journal.cog_journals[0].output_schemas[0].channel_name == "output_channel"


@pytest.mark.parametrize("topology_source", ["discovered", "explicit", "s3", "event"])
def test_build_journal_file_from_log_uses_logged_topology(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    topology_source: str,
) -> None:
    """Logged topology resolves a cog without the original system CLK."""
    topology_file = tmp_path / "journal_topology.pbtxt"
    topology_contents = text_format.MessageToString(
        journal_topology_pb2.JournalTopology(
            format_version=1,
            system_target_name="runtime.System",
            cogs=[
                journal_topology_pb2.CogTopology(
                    cog_instance_path="runtime.path.KnownCog",
                    input_channels=[
                        journal_topology_pb2.CogChannel(
                            member_name="input_member",
                            channel_name="input_channel",
                        )
                    ],
                    output_channels=[
                        journal_topology_pb2.CogChannel(
                            member_name="output_member",
                            channel_name="output_channel",
                        )
                    ],
                )
            ],
        )
    )
    topology_file.write_text(topology_contents)
    telemetry_log_uri = None
    log_uri = str(tmp_path)
    if topology_source in ("s3", "event"):
        telemetry_uri = "s3://bucket/telemetry/timestamp/telemetry/"
        expected_topology_uri = "s3://bucket/telemetry/timestamp/journal_topology.pbtxt"
        monkeypatch.setattr(topology_file_module, "log_file_exists", lambda uri: uri == expected_topology_uri)
        monkeypatch.setattr(
            topology_file_module,
            "read_log_file",
            lambda uri: topology_contents.encode() if uri == expected_topology_uri else b"",
        )
        if topology_source == "s3":
            log_uri = telemetry_uri
        else:
            log_uri = "s3://bucket/event/timestamp/id/event/"
            telemetry_log_uri = telemetry_uri
    request = create_journal_request(
        log_uri=log_uri,
        telemetry_log_uri=telemetry_log_uri,
        output=tmp_path / "channel_scope.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.KnownCog",
        journal_topology_file=topology_file if topology_source == "explicit" else None,
    )
    signal_metadata = _FakeSignalMetadata(
        cog_instances=(_FakeCogInstanceMetadata(cog_path="demo.KnownCog", cog_instance_path="runtime.path.KnownCog"),)
    )
    monkeypatch.setattr(generator, "load_log_index", lambda _log_uri: LogIndex(topics=()))
    monkeypatch.setattr(generator, "load_signal_metadata", lambda _log_uri: signal_metadata)

    journal = build_journal_file_from_log(request)

    assert request.system_clk_file is None
    assert request.system_target is None
    assert journal.cog_journals[0].input_schemas[0].channel_name == "input_channel"
    assert journal.cog_journals[0].output_schemas[0].channel_name == "output_channel"


def test_build_journal_file_from_log_threads_snapshot_refs_to_extraction_boundaries(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Verify system snapshot refs are available in resolved scope for later extraction."""
    system_clk_file = tmp_path / "system.clk"
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "snapshot_scope.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.KnownCog",
        system_clk_file=system_clk_file,
    )
    signal_metadata = _FakeSignalMetadata(
        cog_instances=(
            _FakeCogInstanceMetadata(
                cog_path="demo.KnownCog",
                cog_instance_path="runtime.path.KnownCog",
            ),
        )
    )

    def fake_load_log_index(log_uri: str) -> LogIndex:
        assert log_uri == "/logs/demo.clog"
        return LogIndex(
            topics=(
                _topic_info(
                    name="StateSnapshotChannel",
                    schema_name="demo.State",
                    schema_uuid="33333333-3333-3333-3333-333333333333",
                ),
            )
        )

    def fake_load_signal_metadata(log_uri: str) -> _FakeSignalMetadata:
        assert log_uri == "/logs/demo.clog"
        return signal_metadata

    def fake_discover_cog_channel_maps(
        *,
        system_clk_file: Path | None = None,
        system_target: str | None = None,
    ) -> dict[str, CogChannelMap]:
        assert system_clk_file == request.system_clk_file
        assert system_target is None
        return {
            "runtime.path.KnownCog": CogChannelMap(
                cog_instance_path="runtime.path.KnownCog",
                input_channels=(),
                output_channels=(),
                has_clockwork_state=True,
                snapshot_channels=(
                    DiscoveredSnapshotChannelRef(
                        cog_member_name="state",
                        channel_name="StateSnapshotChannel",
                    ),
                ),
            )
        }

    def fake_extract_execution_metrics(
        extraction_request: object,
        extraction_scope: ResolvedJournalScope,
    ) -> ExecutionExtraction:
        assert extraction_request is request
        _assert_resolved_snapshot_refs(extraction_scope)
        return ExecutionExtraction(cog_executions=(), gaps=())

    def fake_extract_channel_summaries(
        summary_request: object,
        *,
        resolved_scope: ResolvedJournalScope,
        log_index: LogIndex,
    ) -> ChannelSummaryExtraction:
        assert summary_request is request
        assert log_index.topic("StateSnapshotChannel") is not None
        _assert_resolved_snapshot_refs(resolved_scope)
        return ChannelSummaryExtraction(channel_summaries=(), gaps=())

    def fake_extract_state_snapshots(
        snapshot_request: object,
        *,
        resolved_scope: ResolvedJournalScope,
    ) -> SnapshotExtraction:
        assert snapshot_request is request
        _assert_resolved_snapshot_refs(resolved_scope)
        return SnapshotExtraction(selected_snapshots=(), gaps=())

    monkeypatch.setattr("clockwork.tools.journal_file_generator.generator.load_log_index", fake_load_log_index)
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.load_signal_metadata",
        fake_load_signal_metadata,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.discover_cog_channel_maps",
        fake_discover_cog_channel_maps,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_execution_metrics",
        fake_extract_execution_metrics,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_channel_summaries",
        fake_extract_channel_summaries,
    )
    monkeypatch.setattr(
        "clockwork.tools.journal_file_generator.generator.extract_state_snapshots",
        fake_extract_state_snapshots,
    )

    journal = build_journal_file_from_log(request)

    assert not journal.metadata.replay_readiness.has_state_snapshot
    assert not journal.metadata.replay_readiness.gaps


@pytest.mark.parametrize(
    ("cog_instance_path", "match"),
    [
        (None, "Specify --cog-instance-path or --box-instance-path"),
        ("", "cog_instance_path must not be empty"),
    ],
)
def test_invalid_scope_forms_are_rejected(tmp_path: Path, cog_instance_path: str | None, match: str) -> None:
    """Verify scope validation rejects unsupported option combinations."""
    with pytest.raises(RequestValidationError, match=match):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path=cog_instance_path,
        )


def test_invalid_box_scope_forms_are_rejected(tmp_path: Path) -> None:
    """Verify box-scope validation rejects unsupported option combinations."""
    with pytest.raises(RequestValidationError, match="box_instance_path must not be empty"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            box_instance_path="",
            system_target="@clockwork//clockwork/tests/support:test_system_multi_route_clk",
        )

    with pytest.raises(RequestValidationError, match="not both"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
            box_instance_path="runtime.path.Box",
            system_target="@clockwork//clockwork/tests/support:test_system_multi_route_clk",
        )

    with pytest.raises(RequestValidationError, match="requires --system"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            box_instance_path="runtime.path.Box",
            journal_topology_file=tmp_path / "journal_topology.pbtxt",
        )


def test_invalid_time_range_is_rejected(tmp_path: Path) -> None:
    """Verify start time must not be later than end time."""
    with pytest.raises(RequestValidationError, match="less than or equal"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=3,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
        )


def test_invalid_snapshot_selection_is_rejected(tmp_path: Path) -> None:
    """Verify snapshot selection must be one of the supported modes."""
    with pytest.raises(RequestValidationError, match="snapshot-selection"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
            snapshot_selection="during",
        )


def test_invalid_system_context_options_are_rejected(tmp_path: Path) -> None:
    """Verify generation accepts at most one valid system context source."""
    with pytest.raises(RequestValidationError, match="at most one"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
            system_clk_file=tmp_path / "system.clk",
            system_target="@clockwork//clockwork/tests/support:signal_test_system_clk",
        )

    with pytest.raises(RequestValidationError, match="system-clk-file"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
            system_clk_file=tmp_path / "system.txt",
        )

    with pytest.raises(RequestValidationError, match="not both"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
            journal_topology_file=tmp_path / "journal_topology.pbtxt",
            system_target="@clockwork//clockwork/tests/support:signal_test_system_clk",
        )


@pytest.mark.parametrize(
    ("raw_selection", "expected_selection", "expected_proto"),
    [
        ("before", SnapshotSelection.BEFORE, journal_pb2.SNAPSHOT_SELECTION_BEFORE),
        ("after", SnapshotSelection.AFTER, journal_pb2.SNAPSHOT_SELECTION_AFTER),
        ("closest", SnapshotSelection.CLOSEST, journal_pb2.SNAPSHOT_SELECTION_CLOSEST),
    ],
)
def test_valid_snapshot_selections_are_accepted(
    tmp_path: Path,
    raw_selection: str,
    expected_selection: SnapshotSelection,
    expected_proto: journal_pb2.SnapshotSelection,
) -> None:
    """Verify every supported snapshot-selection mode validates and maps to the schema enum."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "out.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
        snapshot_selection=raw_selection,
    )

    assert request.snapshot_selection == expected_selection
    assert snapshot_selection_to_proto(request.snapshot_selection) == expected_proto


def test_snapshot_selection_enum_values_are_accepted(tmp_path: Path) -> None:
    """Verify callers can pass the request enum directly."""
    request = create_journal_request(
        log_uri="/logs/demo.clog",
        output=tmp_path / "out.journal.pb",
        start_time_ns=1,
        end_time_ns=2,
        cog_instance_path="runtime.path.Cog",
        snapshot_selection=SnapshotSelection.AFTER,
    )

    assert request.snapshot_selection == SnapshotSelection.AFTER


def test_existing_output_directory_is_rejected(tmp_path: Path) -> None:
    """Verify output paths must identify files rather than directories."""
    with pytest.raises(RequestValidationError, match="Output path is a directory"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path,
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
        )

    with pytest.raises(RequestValidationError, match="summary-json"):
        create_journal_request(
            log_uri="/logs/demo.clog",
            output=tmp_path / "out.journal.pb",
            summary_json=tmp_path,
            start_time_ns=1,
            end_time_ns=2,
            cog_instance_path="runtime.path.Cog",
        )


def test_cli_accepts_repeated_cog_instance_paths(tmp_path: Path) -> None:
    """Verify the CLI binary accepts repeated cog-instance path options before loading metadata."""
    result = subprocess.run(
        [
            str(_journal_file_generator_binary()),
            "generate",
            "--log-uri",
            "/logs/demo.clog",
            "--output",
            str(tmp_path / "cli.journal.pb"),
            "--summary-json",
            str(tmp_path / "cli.summary.json"),
            "--start-time-ns",
            "1",
            "--end-time-ns",
            "2",
            "--cog-instance-path",
            "runtime.path.Cog",
            "--cog-instance-path",
            "runtime.path.OtherCog",
            "--fail-if-not-replayable",
            "--verbose",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert "Resolving scope and extracting journal data." in result.stderr
    assert "Cannot read log metadata" in result.stderr
    assert "Specify --cog-instance-path" not in result.stderr


def test_cli_surfaces_request_validation_errors(tmp_path: Path) -> None:
    """Verify the CLI binary surfaces invalid request errors from the request validator."""
    result = subprocess.run(
        [
            str(_journal_file_generator_binary()),
            "generate",
            "--log-uri",
            "/logs/demo.clog",
            "--output",
            str(tmp_path / "cli.journal.pb"),
            "--start-time-ns",
            "2",
            "--end-time-ns",
            "1",
            "--cog-instance-path",
            "runtime.path.Cog",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert "less than or equal" in result.stderr


@pytest.mark.parametrize(
    ("substring", "expected_output"),
    [
        ("Missing", "No matching cog instances.\n"),
        ("Beta", "runtime.Beta\n"),
        ("Alpha", "runtime.Alpha\nruntime.AlphaOther\n"),
    ],
)
def test_find_cogs_from_log(tmp_path: Path, substring: str, expected_output: str) -> None:
    """The CLI returns zero, one, or multiple literal substring matches in sorted order."""
    _write_journal_topology(tmp_path / "journal_topology.pbtxt")

    result = subprocess.run(
        [
            str(_journal_file_generator_binary()),
            "find-cogs",
            "--log-uri",
            str(tmp_path),
            "--name-contains",
            substring,
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0
    assert result.stdout == expected_output


def test_find_cogs_explicit_topology_override(tmp_path: Path) -> None:
    """An explicit topology file takes precedence over log discovery."""
    topology_file = tmp_path / "copied.pbtxt"
    _write_journal_topology(topology_file)

    result = subprocess.run(
        [
            str(_journal_file_generator_binary()),
            "find-cogs",
            "--log-uri",
            str(tmp_path / "missing-log"),
            "--journal-topology-file",
            str(topology_file),
            "--name-contains",
            "Beta",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0
    assert result.stdout == "runtime.Beta\n"
