# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report memory statistics."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.view_model import build_report_model


def _memory_statistics_journal() -> journal_pb2.JournalFile:
    """Build a journal with memory statistics from two executions."""
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/memory.clog",
            generator_version="test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(
                cog_path="demo.Cog",
                cog_instance_path="runtime.path.Cog",
                executions=[
                    journal_pb2.CogExecution(
                        execution_index=2,
                        execution_start_time_ns=120,
                        memory_stats=[
                            journal_pb2.MemoryStats(
                                resource_name="beta",
                                peak_allocated=30,
                                current_allocated=20,
                                total_allocated=70,
                                total_deallocated=50,
                            ),
                            journal_pb2.MemoryStats(
                                resource_name="alpha",
                                peak_allocated=12,
                                current_allocated=8,
                                total_allocated=40,
                                total_deallocated=32,
                            ),
                            journal_pb2.MemoryStats(resource_name="delta", peak_allocated=8),
                            journal_pb2.MemoryStats(resource_name="epsilon", peak_allocated=6),
                            journal_pb2.MemoryStats(resource_name="gamma", peak_allocated=4),
                        ],
                    ),
                    journal_pb2.CogExecution(
                        execution_index=1,
                        execution_start_time_ns=140,
                        memory_stats=[
                            journal_pb2.MemoryStats(
                                resource_name="alpha",
                                peak_allocated=10,
                                current_allocated=6,
                                total_allocated=20,
                                total_deallocated=14,
                            )
                        ],
                    ),
                ],
            )
        ],
    )


def test_memory_timeline_is_sorted_by_resource_and_execution_time() -> None:
    model = build_report_model(_memory_statistics_journal(), title="Memory")

    memory_timeline = model.cogs[0].memory_timeline

    assert memory_timeline is not None
    assert (memory_timeline.start_time_ns, memory_timeline.end_time_ns) == (120, 140)
    assert [
        (series.resource_name, [point.current_allocated for point in series.points])
        for series in memory_timeline.series
    ] == [
        ("alpha", [8, 6]),
        ("beta", [20]),
        ("delta", [0]),
        ("epsilon", [0]),
        ("gamma", [0]),
    ]
    assert [point.execution_id for point in memory_timeline.series[0].points] == [
        "cog-0-execution-2",
        "cog-0-execution-1",
    ]


def test_memory_statistics_render_in_execution_details_and_graph() -> None:
    html = render_report(_memory_statistics_journal(), title="Memory")

    assert "Memory Usage Over Time" in html
    assert "Current allocated bytes by resource." in html
    assert html.count('class="memory-usage-graph"') == 5
    assert 'data-resource-name="alpha"' in html
    assert 'data-resource-name="beta"' in html
    assert "<h5>alpha</h5>" in html
    assert "<h5>beta</h5>" in html
    assert "Memory Statistics" in html
    assert "<td>alpha</td><td>10</td><td>6</td><td>20</td><td>14</td>" in html
    assert "<td>beta</td><td>30</td><td>20</td><td>70</td><td>50</td>" in html
    assert 'href="#cog-0-execution-1"' in html


def test_memory_statistics_render_empty_states() -> None:
    html = render_report(
        journal_pb2.JournalFile(cog_journals=[journal_pb2.CogJournal(executions=[journal_pb2.CogExecution()])]),
        title="Memory",
    )

    assert "No memory statistics for this cog." in html
    assert "No memory statistics." in html
