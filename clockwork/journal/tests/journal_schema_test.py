# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Smoke tests for the journal protobuf schema."""

from __future__ import annotations

from clockwork.journal import journal_pb2


def test_minimal_journal_file_serializes_and_parses() -> None:
    """Verify the generated Python protobuf can construct and round-trip a journal."""
    journal = journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=123,
            end_time_ns=456,
            log_uri="s3://bucket/path/to/journal",
            generator_version="schema-smoke-test",
            scope=journal_pb2.JournalScope(
                cog_instance_paths=[
                    "demo.box.Cog",
                    "demo.other_box.Cog",
                ],
            ),
            replay_readiness=journal_pb2.ReplayReadiness(
                sufficient_for_replay=False,
                has_state_snapshot=False,
                missing_inputs=["input/channel"],
                gaps=[
                    journal_pb2.ReplayReadinessGap(
                        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT,
                        message="No state snapshot was available for the requested selection mode.",
                        cog_instance_path="demo.box.Cog",
                    ),
                    journal_pb2.ReplayReadinessGap(
                        reason=journal_pb2.REPLAY_READINESS_GAP_REASON_REQUESTED_COG_NOT_FOUND,
                        message="The requested cog instance path was not present in metadata.",
                        cog_instance_path="demo.other_box.Cog",
                    ),
                ],
            ),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="demo.box.Cog",
                has_clockwork_state=True,
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=1,
                        dial_start_time_ns=123,
                        execution_start_time_ns=234,
                        execution_duration_ns=10,
                        input_views=[
                            journal_pb2.InputViewState(
                                channel_name="input/channel",
                                cog_member_name="input_member",
                                cursor_sequence_number=7,
                                visible_sequence_numbers=[7, 8],
                                first_new_index=0,
                                dropped_message_count=2,
                            ),
                        ],
                    ),
                ],
            ),
        ],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="input/channel",
                message_count=0,
                has_logged_messages=True,
            ),
        ],
        channel_messages=[
            journal_pb2.ChannelMessage(
                channel_name="input/channel",
                sequence_number=7,
                publish_time_ns=230,
                payload_size_bytes=16,
            ),
        ],
    )

    serialized = journal.SerializeToString(deterministic=True)
    parsed = journal_pb2.JournalFile()
    parsed.ParseFromString(serialized)

    assert parsed == journal
    assert list(parsed.metadata.scope.cog_instance_paths) == [
        "demo.box.Cog",
        "demo.other_box.Cog",
    ]
    assert parsed.metadata.replay_readiness.gaps[0].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_STATE_SNAPSHOT
    )
    assert parsed.metadata.replay_readiness.gaps[1].reason == (
        journal_pb2.REPLAY_READINESS_GAP_REASON_REQUESTED_COG_NOT_FOUND
    )
    assert parsed.cog_journals[0].executions[0].execution_index == 1
    assert parsed.cog_journals[0].executions[0].dial_start_time_ns == 123
    assert parsed.cog_journals[0].executions[0].input_views[0].cog_member_name == "input_member"
    assert parsed.cog_journals[0].executions[0].input_views[0].HasField("dropped_message_count")
    assert parsed.cog_journals[0].executions[0].input_views[0].dropped_message_count == 2
    assert parsed.cog_journals[0].has_clockwork_state
    assert parsed.channel_summaries[0].has_logged_messages
    assert parsed.channel_messages[0].payload_size_bytes == 16


def test_resolved_scope_field_round_trips() -> None:
    """Verify resolved cog instance paths are available to Python users."""
    scope = journal_pb2.JournalScope(
        cog_instance_paths=["demo.box.Cog"],
        box_instance_paths=["demo.box"],
    )

    serialized = scope.SerializeToString(deterministic=True)
    parsed = journal_pb2.JournalScope()
    parsed.ParseFromString(serialized)

    assert parsed == scope
    assert list(parsed.cog_instance_paths) == ["demo.box.Cog"]
    assert list(parsed.box_instance_paths) == ["demo.box"]
