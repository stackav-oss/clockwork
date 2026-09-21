# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report view-model content."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.view_model import build_report_model, expand_condition_flags


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


def _complete_journal() -> journal_pb2.JournalFile:
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/complete.clog",
            generator_version="generator-test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=False,
                missing_inputs=["MissingChannel"],
                has_state_snapshot=True,
                gaps=[
                    journal_pb2.ReplayReadinessGap(
                        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                        message="Input channel was not logged.",
                        cog_instance_path="runtime.path.Cog",
                        channel_name="MissingChannel",
                        execution_index=7,
                    )
                ],
            ),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                condition_names=["ready", "optional"],
                state_snapshot=journal_pb2.StateSnapshot(
                    snapshot_time_ns=95,
                    selection=journal_pb2.SNAPSHOT_SELECTION_BEFORE,
                    state_data=b"state",
                    state_schema_uuid="11111111-1111-1111-1111-111111111111",
                ),
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=7,
                        execution_start_time_ns=123,
                        execution_duration_ns=5,
                        ready_to_exec_latency_ns=2,
                        attempt_to_exec_latency_ns=3,
                        requeue_count=1,
                        condition_flags=1,
                        input_views=[
                            journal_pb2.InputViewState(
                                channel_name="InputChannel",
                                cog_member_name="input",
                                cursor_sequence_number=10,
                                visible_sequence_numbers=[10, 11],
                                first_new_index=1,
                            )
                        ],
                        outputs=[
                            journal_pb2.OutputState(
                                channel_name="OutputChannel",
                                produced_sequence_numbers=[20],
                            )
                        ],
                        alignment_result=journal_pb2.AlignmentResult(
                            aligner_name="aligner",
                            aligned_inputs=[
                                journal_pb2.AlignedInput(
                                    input_name="input",
                                    selected_sequence_number=11,
                                    present=True,
                                )
                            ],
                        ),
                    )
                ],
            )
        ],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="OutputChannel",
                producer_cog_instance="runtime.path.Cog",
                consumer_cog_instances=["runtime.path.Consumer"],
                first_sequence_number=20,
                last_sequence_number=20,
                message_count=1,
                has_logged_messages=True,
            )
        ],
    )


def test_condition_flags_expand_in_name_order() -> None:
    conditions = expand_condition_flags(("ready", "blocked", "optional"), 0b101)

    assert [(condition.name, condition.value) for condition in conditions] == [
        ("ready", True),
        ("blocked", False),
        ("optional", True),
    ]


def test_view_model_uses_deterministic_sorting_and_ids() -> None:
    journal = _tiny_journal()
    journal.cog_journals.extend(
        [
            journal_pb2.CogJournal(cog_instance_path="runtime.path.Zed", cog_path="demo.Zed"),
            journal_pb2.CogJournal(cog_instance_path="runtime.path.Alpha", cog_path="demo.Alpha"),
        ]
    )
    journal.channel_summaries.extend(
        [
            journal_pb2.ChannelSummary(channel_name="zed", producer_cog_instance="runtime.path.Zed"),
            journal_pb2.ChannelSummary(channel_name="alpha", producer_cog_instance="runtime.path.Alpha"),
        ]
    )
    journal.metadata.replay_readiness.gaps.extend(
        [
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
                message="snapshot",
            ),
            journal_pb2.ReplayReadinessGap(
                reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
                message="input",
            ),
        ]
    )

    model = build_report_model(journal, title="Demo")

    assert [(cog.id, cog.cog_instance_path) for cog in model.cogs] == [
        ("cog-0", "runtime.path.Alpha"),
        ("cog-1", "runtime.path.Zed"),
    ]
    assert [(channel["id"], channel["channel_name"]) for channel in model.channels] == [
        ("channel-0", "alpha"),
        ("channel-1", "zed"),
    ]
    assert [(gap.id, gap.reason) for gap in model.readiness.gaps] == [
        ("gap-0", "REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL"),
        ("gap-1", "REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT"),
    ]


def test_view_model_contains_overview_readiness_cogs_channels_and_snapshots() -> None:
    model = build_report_model(_complete_journal(), title="Complete")
    metadata = model.metadata
    readiness = model.readiness
    overview_cards = {card.label: card.value for card in model.overview_cards}
    expected_overview_cards = {"Cogs": "1", "Executions": "1", "Channels": "1", "State Snapshots": "1"}

    assert model.title == "Complete"
    assert metadata.log_uri == "/logs/complete.clog"
    assert metadata.generator_version == "generator-test"
    assert metadata.requested_cog_instance_paths == ("runtime.path.Cog",)
    assert (metadata.cog_count, metadata.execution_count, metadata.channel_count) == (1, 1, 1)
    assert overview_cards | expected_overview_cards == overview_cards
    assert readiness.status == "insufficient"
    assert readiness.missing_inputs == ("MissingChannel",)
    assert readiness.gaps[0].reason == "REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL"

    cog = model.cogs[0]
    executions = cog.executions
    execution = executions[0]
    conditions = execution["conditions"]
    input_views = execution["input_views"]
    alignment = execution["alignment"]

    assert alignment is not None

    aligned_inputs = alignment["aligned_inputs"]
    assert cog.condition_names == ("ready", "optional")
    assert [(condition.name, condition.value) for condition in conditions] == [("ready", True), ("optional", False)]
    assert input_views[0]["new_sequence_numbers"] == (11,)
    assert aligned_inputs[0]["selected_sequence_number"] == 11
    assert model.channels[0]["channel_name"] == "OutputChannel"
    assert model.state_snapshots[0].state_schema_uuid == "11111111-1111-1111-1111-111111111111"


def test_renderer_outputs_overview_readiness_and_snapshot_content() -> None:
    html = render_report(_complete_journal(), title="Complete")

    assert "Executions" in html
    assert "<dt>Time Range</dt><dd>1970-01-01 00:00:00.000000100 UTC to 1970-01-01 00:00:00.000000200 UTC</dd>" in html
    assert "<dt>Unix Time Range (ns)</dt><dd>100 to 200</dd>" in html
    assert "Replay Readiness" in html
    assert "REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL" in html
    assert "MissingChannel" in html
    assert "State Snapshots" in html
    assert "11111111-1111-1111-1111-111111111111" in html
