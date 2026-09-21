# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for rendered journal report execution details."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.renderer import render_report


def _execution_detail_journal() -> journal_pb2.JournalFile:
    """Build a journal with one detailed execution."""
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/detail.clog",
            generator_version="test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                condition_names=["ready", "optional"],
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
                                    is_reused=True,
                                )
                            ],
                        ),
                    )
                ],
            )
        ],
    )


def test_execution_detail_renders_timing_inputs_outputs_and_alignment() -> None:
    html = render_report(_execution_detail_journal(), title="Detail")

    assert 'class="execution-cog-links"' in html
    assert 'href="#cog-0-executions"' in html
    assert '<section id="cog-0-executions" class="execution-cog-group">' in html
    assert 'data-cog-id="cog-0"' in html
    assert 'id="cog-0-execution-jump-input"' in html
    assert 'id="cog-0-execution-jump-button"' in html
    assert '<details id="cog-0-execution-7" class="execution-detail">' in html
    assert (
        'Cog execution 7 @ <time class="timestamp" datetime="1970-01-01T00:00:00.000000123Z" '
        'data-timestamp-ns="123">1970-01-01 00:00:00.000000123 UTC</time> (123 ns)'
    ) in html
    assert "<dt>Start Time (ns)</dt><dd>123</dd>" in html
    assert "<dt>Duration</dt><dd>5 ns</dd>" in html
    assert "<td>ready</td><td>yes</td>" in html
    assert "<td>optional</td><td>no</td>" in html
    assert '<mark class="sequence-new">11</mark>' in html
    assert "<td>unavailable</td>" in html
    assert "OutputChannel" in html
    assert "Alignment: aligner" in html
    assert "<td>input</td><td>11</td><td>yes</td><td>yes</td>" in html


def test_execution_jump_controls_are_rendered_per_cog() -> None:
    journal = _execution_detail_journal()
    journal.metadata.scope.cog_instance_paths.append("runtime.path.OtherCog")
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.OtherCog",
            cog_instance_path="runtime.path.OtherCog",
            executions=[
                journal_pb2.CogExecution(
                    execution_index=8,
                    execution_start_time_ns=150,
                    execution_duration_ns=10,
                )
            ],
        )
    )

    html = render_report(journal, title="Detail")

    assert html.count('class="execution-jump-controls"') == 2
    assert 'id="cog-0-execution-jump-input"' in html
    assert 'id="cog-1-execution-jump-input"' in html
    assert 'data-cog-id="cog-0"' in html
    assert 'data-cog-id="cog-1"' in html


def test_execution_detail_renders_empty_detail_states() -> None:
    journal = _execution_detail_journal()
    journal.cog_journals[0].executions[0].ClearField("input_views")
    journal.cog_journals[0].executions[0].ClearField("outputs")
    journal.cog_journals[0].executions[0].ClearField("alignment_result")
    journal.cog_journals[0].ClearField("condition_names")

    html = render_report(journal, title="Detail")

    assert "No conditions." in html
    assert "No input views." in html
    assert "No outputs." in html
    assert "No alignment result." in html
