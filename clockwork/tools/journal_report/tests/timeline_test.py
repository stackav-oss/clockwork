# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report execution timeline data."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.view_model import DurationBucket, build_report_model, report_model_to_dict


def _tiny_journal() -> journal_pb2.JournalFile:
    """Build a minimal journal with a metadata time range."""
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


def test_timeline_model_uses_execution_order_and_duration_buckets() -> None:
    journal = _tiny_journal()
    journal.cog_journals.extend(
        [
            journal_pb2.CogJournal(
                cog_path="demo.CogB",
                cog_instance_path="runtime.path.CogB",
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=1,
                        execution_start_time_ns=130,
                        execution_duration_ns=30,
                    )
                ],
            ),
            journal_pb2.CogJournal(
                cog_path="demo.CogA",
                cog_instance_path="runtime.path.CogA",
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=2,
                        execution_start_time_ns=120,
                        execution_duration_ns=20,
                    ),
                    journal_pb2.CogExecution(
                        execution_index=1,
                        execution_start_time_ns=110,
                        execution_duration_ns=0,
                    ),
                    journal_pb2.CogExecution(
                        execution_index=3,
                        execution_start_time_ns=140,
                        execution_duration_ns=100,
                    ),
                ],
            ),
        ]
    )

    timeline = build_report_model(journal, title="Demo").timeline

    assert (timeline.start_time_ns, timeline.end_time_ns) == (110, 240)
    assert [row.cog_instance_path for row in timeline.rows] == ["runtime.path.CogA", "runtime.path.CogB"]
    assert [(bar.execution_index, bar.duration_ns, bar.duration_bucket) for bar in timeline.rows[0].bars] == [
        (1, 0, DurationBucket.ZERO),
        (2, 20, DurationBucket.LOW),
        (3, 100, DurationBucket.HIGH),
    ]
    assert [(bar.execution_id, bar.start_time_ns, bar.end_time_ns) for bar in timeline.rows[1].bars] == [
        ("cog-1-execution-1", 130, 160),
    ]


def test_timeline_is_embedded_in_report_data_and_shell() -> None:
    journal = _tiny_journal()
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.Cog",
            cog_instance_path="runtime.path.Cog",
            executions=[
                journal_pb2.CogExecution(
                    execution_index=7,
                    execution_start_time_ns=12,
                    execution_duration_ns=5,
                )
            ],
        )
    )

    model = report_model_to_dict(build_report_model(journal, title="Demo"))
    html = render_report(journal, title="Demo")

    assert model["timeline"] == {
        "start_time_ns": 12,
        "end_time_ns": 17,
        "rows": (
            {
                "id": "timeline-row-0",
                "cog_id": "cog-0",
                "cog_instance_path": "runtime.path.Cog",
                "bars": (
                    {
                        "id": "cog-0-timeline-7",
                        "execution_id": "cog-0-execution-7",
                        "execution_index": 7,
                        "start_time_ns": 12,
                        "end_time_ns": 17,
                        "duration_ns": 5,
                        "duration_bucket": "low",
                    },
                ),
            },
        ),
    }
    assert 'id="execution-timeline-root"' in html
    assert 'class="timeline-svg"' in html
    assert 'width="1120"' in html
    assert 'class="timeline-row-bg timeline-row-bg-even"' in html
    assert 'class="timeline-tick-label"' in html
    assert ">+0.000s</text>" in html
    assert '<text class="timeline-row-count"' in html
    assert ">1 exec</text>" in html
    assert 'class="timeline-bar timeline-bar-low"' in html
    assert 'data-cog-id="cog-0"' in html
    assert 'data-start-time-ns="12"' in html
    assert 'data-end-time-ns="17"' in html
    assert 'data-duration-ns="5"' in html
    assert html.count("<title>Execution 7: start 12 ns, end 17 ns, duration 5 ns</title>") >= 3
    assert '"timeline":' in html


def test_timeline_expands_width_for_dense_execution_rows() -> None:
    journal = _tiny_journal()
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.DenseCog",
            cog_instance_path="runtime.path.DenseCog",
            executions=[
                journal_pb2.CogExecution(
                    execution_index=index,
                    execution_start_time_ns=100 + index,
                    execution_duration_ns=1,
                )
                for index in range(400)
            ],
        )
    )

    html = render_report(journal, title="Dense")

    assert 'width="1918"' in html
    assert 'viewBox="0 0 1918 110"' in html
    assert 'x2="1900"' in html
    assert ">400 execs</text>" in html


def test_timeline_model_skips_cogs_without_executions() -> None:
    journal = _tiny_journal()
    journal.cog_journals.append(
        journal_pb2.CogJournal(
            cog_path="demo.EmptyCog",
            cog_instance_path="runtime.path.EmptyCog",
        )
    )

    timeline = build_report_model(journal, title="Demo").timeline

    assert timeline.rows == ()
    assert (timeline.start_time_ns, timeline.end_time_ns) == (10, 20)
