# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Small journal fixtures for report tests and manual examples."""

from __future__ import annotations

from clockwork.journal import journal_pb2


def single_cog_journal() -> journal_pb2.JournalFile:
    """Build a complete single-cog journal fixture."""
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/single-cog.clog",
            generator_version="fixture",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                condition_names=["ready"],
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=0,
                        execution_start_time_ns=120,
                        execution_duration_ns=5,
                        condition_flags=1,
                        input_views=[
                            journal_pb2.InputViewState(
                                channel_name="InputChannel",
                                cog_member_name="input",
                                cursor_sequence_number=1,
                                visible_sequence_numbers=[1, 2],
                                first_new_index=1,
                            )
                        ],
                        outputs=[
                            journal_pb2.OutputState(
                                channel_name="OutputChannel",
                                produced_sequence_numbers=[3],
                            )
                        ],
                    )
                ],
            )
        ],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="OutputChannel",
                producer_cog_instance="runtime.path.Cog",
                consumer_cog_instances=["runtime.path.Consumer"],
                first_sequence_number=3,
                last_sequence_number=3,
                message_count=1,
                has_logged_messages=True,
            )
        ],
    )


def group_journal() -> journal_pb2.JournalFile:
    """Build a two-cog group journal fixture."""
    journal = single_cog_journal()
    journal.metadata.log_uri = "/logs/group.clog"
    journal.metadata.scope.cog_instance_paths.append("runtime.path.Consumer")
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.Consumer",
            cog_instance_path="runtime.path.Consumer",
            executions=[
                journal_pb2.CogExecution(
                    execution_index=0,
                    execution_start_time_ns=130,
                    execution_duration_ns=7,
                    input_views=[
                        journal_pb2.InputViewState(
                            channel_name="OutputChannel",
                            cog_member_name="input",
                            cursor_sequence_number=3,
                            visible_sequence_numbers=[3],
                            first_new_index=0,
                        )
                    ],
                )
            ],
        )
    )
    return journal


def missing_data_journal() -> journal_pb2.JournalFile:
    """Build a journal fixture with replay-readiness gaps."""
    journal = single_cog_journal()
    journal.metadata.log_uri = "/logs/missing-data.clog"
    journal.metadata.replay_readiness.sufficient_for_replay = False
    journal.metadata.replay_readiness.missing_inputs.append("MissingChannel")
    journal.metadata.replay_readiness.gaps.append(
        journal_pb2.ReplayReadinessGap(
            reason=journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_CHANNEL,
            message="Input channel was not logged.",
            cog_instance_path="runtime.path.Cog",
            channel_name="MissingChannel",
            execution_index=0,
        )
    )
    return journal


def alignment_journal() -> journal_pb2.JournalFile:
    """Build a journal fixture with one alignment result."""
    journal = single_cog_journal()
    journal.metadata.log_uri = "/logs/alignment.clog"
    journal.cog_journals[0].executions[0].alignment_result.CopyFrom(
        journal_pb2.AlignmentResult(
            aligner_name="aligner",
            aligned_inputs=[
                journal_pb2.AlignedInput(
                    input_name="input",
                    selected_sequence_number=2,
                    present=True,
                )
            ],
        )
    )
    return journal


def message_sequence_journal() -> journal_pb2.JournalFile:
    """Build a group journal with report-indexed channel messages."""
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/message-sequence.clog",
            generator_version="test",
            scope=journal_pb2.JournalScope(
                cog_instance_paths=["runtime.path.AProducer", "runtime.path.BConsumer"],
            ),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Producer",
                cog_instance_path="runtime.path.AProducer",
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=1,
                        execution_start_time_ns=110,
                        execution_duration_ns=5,
                        outputs=[
                            journal_pb2.OutputState(
                                channel_name="DataChannel",
                                produced_sequence_numbers=[1, 3],
                            )
                        ],
                    )
                ],
            ),
            journal_pb2.CogJournal(
                cog_path="demo.Consumer",
                cog_instance_path="runtime.path.BConsumer",
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=4,
                        execution_start_time_ns=150,
                        execution_duration_ns=7,
                        input_views=[
                            journal_pb2.InputViewState(
                                channel_name="DataChannel",
                                cog_member_name="input",
                                cursor_sequence_number=1,
                                visible_sequence_numbers=[1],
                                first_new_index=0,
                            )
                        ],
                    )
                ],
            ),
        ],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="DataChannel",
                producer_cog_instance="runtime.path.AProducer",
                consumer_cog_instances=["runtime.path.BConsumer"],
                first_sequence_number=1,
                last_sequence_number=3,
                message_count=2,
                has_logged_messages=True,
            )
        ],
        channel_messages=[
            journal_pb2.ChannelMessage(
                channel_name="DataChannel",
                sequence_number=3,
                publish_time_ns=130,
                payload_size_bytes=30,
            ),
            journal_pb2.ChannelMessage(
                channel_name="DataChannel",
                sequence_number=1,
                publish_time_ns=110,
                payload_size_bytes=10,
            ),
        ],
    )
