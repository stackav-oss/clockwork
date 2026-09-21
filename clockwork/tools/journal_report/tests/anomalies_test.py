# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report anomaly detection and rendering."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.anomalies import build_anomaly_report
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.tests.support.journal_fixtures import message_sequence_journal


def _tiny_journal() -> journal_pb2.JournalFile:
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=10,
            end_time_ns=20,
            log_uri="/logs/demo.clog",
            generator_version="test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        )
    )


def _journal_with_rendered_anomalies() -> journal_pb2.JournalFile:
    journal = _tiny_journal()
    journal.metadata.replay_readiness.sufficient_for_replay = False
    journal.metadata.replay_readiness.missing_inputs.append("MissingChannel")
    journal.metadata.replay_readiness.gaps.append(
        journal_pb2.ReplayReadinessGap(
            reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
            message="Input channel was not logged.",
            cog_instance_path="runtime.path.Cog",
            channel_name="MissingChannel",
            execution_index=7,
        )
    )
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.Cog",
            cog_instance_path="runtime.path.Cog",
            executions=[journal_pb2.CogExecution(execution_index=7, requeue_count=1)],
        )
    )
    return journal


def _journal_with_execution_metrics(
    *,
    durations: tuple[int, ...],
    ready_latencies: tuple[int, ...],
    attempt_latencies: tuple[int, ...],
    requeue_counts: tuple[int, ...],
) -> journal_pb2.JournalFile:
    executions = [
        journal_pb2.CogExecution(
            execution_index=index,
            execution_start_time_ns=100 + index,
            execution_duration_ns=durations[index],
            ready_to_exec_latency_ns=ready_latencies[index],
            attempt_to_exec_latency_ns=attempt_latencies[index],
            requeue_count=requeue_counts[index],
        )
        for index in range(len(durations))
    ]
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/metrics.clog",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                executions=executions,
            )
        ],
    )


def test_renderer_outputs_anomaly_content() -> None:
    html = render_report(_journal_with_rendered_anomalies(), title="Complete")

    assert "Anomaly Highlights" in html
    assert "REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL" in html
    assert "Execution requeued" in html
    assert "MissingChannel" in html


def test_missing_input_gaps_are_grouped_by_channel() -> None:
    journal = _tiny_journal()
    journal.metadata.replay_readiness.gaps.extend(
        [
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="missing sequence number range 4-6",
                cog_instance_path="runtime.path.Cog",
                channel_name="MissingChannel",
                execution_index=7,
            ),
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="missing sequence number range 8-9",
                cog_instance_path="runtime.path.Cog",
                channel_name="MissingChannel",
                execution_index=8,
            ),
        ]
    )

    report = build_anomaly_report(journal)
    expected_message = (
        "Input channel is missing. Details: " + "missing sequence number range 4-6; missing sequence number range 8-9"
    )
    actual_items = [(item.channel_name, item.execution_index, item.message) for item in report.items]

    assert actual_items == [("MissingChannel", None, expected_message)]


def test_missing_output_gaps_are_grouped_by_channel() -> None:
    journal = _tiny_journal()
    journal.metadata.replay_readiness.gaps.extend(
        journal_pb2.ReplayReadinessGap(
            reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA,
            message=f"missing sequence number range {index}-{index}",
            cog_instance_path="runtime.path.Cog",
            channel_name="OutputChannel",
            execution_index=index,
        )
        for index in range(6)
    )

    report = build_anomaly_report(journal)
    expected_details = "; ".join(f"missing sequence number range {index}-{index}" for index in range(5))
    expected_message = f"Output message data is missing. Details: {expected_details}; 1 more detail omitted"
    actual_items = [
        (item.type, item.title, item.channel_name, item.execution_index, item.message) for item in report.items
    ]
    expected_item = ("missing_output", "Missing output message data", "OutputChannel", None, expected_message)

    assert actual_items == [expected_item]


def test_anomalies_cover_readiness_gap_types_and_missing_inputs() -> None:
    journal = _tiny_journal()
    journal.metadata.replay_readiness.missing_inputs.append("legacy-missing")
    journal.metadata.replay_readiness.gaps.extend(
        [
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
                message="alignment missing",
                cog_instance_path="runtime.path.Cog",
                execution_index=2,
            ),
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_SIGNAL_METADATA,
                message="metadata missing",
                cog_instance_path="runtime.path.Cog",
            ),
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
                message="snapshot missing",
                cog_instance_path="runtime.path.Cog",
            ),
        ]
    )

    report = build_anomaly_report(journal)

    assert [(item.type, item.title) for item in report.items] == [
        ("missing_alignment", "Missing alignment result"),
        ("missing_input", "Missing input channel"),
        ("missing_metadata", "Missing journal metadata"),
        ("readiness_gap", "Replay-readiness gap"),
    ]


def test_anomalies_detect_requeue_and_timing_outliers() -> None:
    journal = _journal_with_execution_metrics(
        durations=(1, 1, 1, 100),
        ready_latencies=(2, 2, 2, 20),
        attempt_latencies=(3, 3, 3, 30),
        requeue_counts=(0, 0, 0, 2),
    )

    report = build_anomaly_report(journal)

    assert [(item.type, item.execution_index, item.value_ns, item.threshold_ns) for item in report.items] == [
        ("high_attempt_latency", 3, 30, 30),
        ("high_duration", 3, 100, 100),
        ("high_ready_latency", 3, 20, 20),
        ("requeue", 3, None, None),
    ]


def test_anomalies_skip_timing_outliers_for_single_execution() -> None:
    journal = _journal_with_execution_metrics(
        durations=(1000,),
        ready_latencies=(1000,),
        attempt_latencies=(1000,),
        requeue_counts=(0,),
    )

    report = build_anomaly_report(journal)

    assert report.items == ()


def test_anomalies_skip_timing_when_percentile_is_near_median() -> None:
    journal = _journal_with_execution_metrics(
        durations=(10, 11, 12, 13),
        ready_latencies=(20, 21, 22, 23),
        attempt_latencies=(30, 31, 32, 33),
        requeue_counts=(0, 0, 0, 0),
    )

    report = build_anomaly_report(journal)

    assert report.items == ()


def test_anomalies_detect_sequence_gaps_and_unconsumed_messages() -> None:
    report = build_anomaly_report(message_sequence_journal())

    assert [(item.type, item.channel_name, item.execution_index, item.message) for item in report.items] == [
        ("sequence_gap", "DataChannel", None, "Sequence numbers 2 are absent."),
        (
            "unconsumed_message",
            "DataChannel",
            1,
            "Sequence 3 has no consuming execution in this journal.",
        ),
    ]


def test_missing_optional_message_index_is_capability_warning() -> None:
    journal = _tiny_journal()
    journal.channel_summaries.append(
        journal_pb2.ChannelSummary(
            channel_name="OutputChannel",
            producer_cog_instance="runtime.path.Cog",
            first_sequence_number=1,
            last_sequence_number=2,
            message_count=2,
            has_logged_messages=True,
        )
    )

    report = build_anomaly_report(journal)

    assert [(item.type, item.severity) for item in report.items] == [("capability_warning", "info")]
    assert "sequence_gap" not in {item.type for item in report.items}
    assert "unconsumed_message" not in {item.type for item in report.items}
