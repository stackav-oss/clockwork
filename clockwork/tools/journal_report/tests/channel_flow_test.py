# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report channel flow and summary message sequences."""

from __future__ import annotations

from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.view_model import build_report_model


def _flow_journal() -> journal_pb2.JournalFile:
    """Build a small producer-to-consumer group journal."""
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/flow.clog",
            generator_version="test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Producer"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[
            journal_pb2.CogJournal(cog_path="demo.Producer", cog_instance_path="runtime.path.Producer"),
            journal_pb2.CogJournal(cog_path="demo.Middle", cog_instance_path="runtime.path.Middle"),
            journal_pb2.CogJournal(cog_path="demo.Consumer", cog_instance_path="runtime.path.Consumer"),
        ],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="ChannelA",
                producer_cog_instance="runtime.path.Producer",
                consumer_cog_instances=["runtime.path.Middle"],
                first_sequence_number=1,
                last_sequence_number=3,
                message_count=3,
                has_logged_messages=True,
            ),
            journal_pb2.ChannelSummary(
                channel_name="ChannelB",
                producer_cog_instance="runtime.path.Middle",
                consumer_cog_instances=["runtime.path.Consumer"],
                first_sequence_number=10,
                last_sequence_number=12,
                message_count=2,
                has_logged_messages=True,
            ),
        ],
    )


def test_channel_flow_assigns_stable_layers() -> None:
    flow = build_report_model(_flow_journal(), title="Flow").channel_flow

    assert [(node.label, node.layer) for node in flow.nodes] == [
        ("runtime.path.Producer", 0),
        ("runtime.path.Middle", 1),
        ("runtime.path.Consumer", 2),
    ]
    assert [(edge.channel_name, edge.label, edge.source_node_id, edge.target_node_id) for edge in flow.edges] == [
        ("ChannelA", "3 msg, seq 1-3", "cog-2", "cog-1"),
        ("ChannelB", "2 msg, seq 10-12", "cog-1", "cog-0"),
    ]


def test_channel_flow_uses_unknown_source_for_missing_producer() -> None:
    journal = _flow_journal()
    del journal.channel_summaries[:]
    journal.channel_summaries.append(
        journal_pb2.ChannelSummary(
            channel_name="MysteryChannel",
            consumer_cog_instances=["runtime.path.Consumer"],
            first_sequence_number=5,
            last_sequence_number=5,
            message_count=1,
            has_logged_messages=True,
        )
    )

    flow = build_report_model(journal, title="Flow").channel_flow

    assert ("unknown-source", "unknown source", 0, True) in [
        (node.id, node.label, node.layer, node.is_unknown) for node in flow.nodes
    ]
    assert [(edge.channel_name, edge.source_node_id, edge.target_node_id) for edge in flow.edges] == [
        ("MysteryChannel", "unknown-source", "cog-0"),
    ]


def test_channel_flow_uses_unknown_sink_for_missing_consumers() -> None:
    journal = _flow_journal()
    del journal.channel_summaries[:]
    journal.channel_summaries.append(
        journal_pb2.ChannelSummary(
            channel_name="DanglingChannel",
            producer_cog_instance="runtime.path.Producer",
            first_sequence_number=8,
            last_sequence_number=8,
            message_count=1,
            has_logged_messages=True,
        )
    )

    flow = build_report_model(journal, title="Flow").channel_flow

    assert ("unknown-sink", "unknown sink", True) in [(node.id, node.label, node.is_unknown) for node in flow.nodes]
    assert [node.id for node in flow.nodes].count("unknown-sink") == 1
    assert [(edge.channel_name, edge.source_node_id, edge.target_node_id) for edge in flow.edges] == [
        ("DanglingChannel", "cog-2", "unknown-sink"),
    ]


def test_channel_flow_and_message_sequence_placeholders_render() -> None:
    html = render_report(_flow_journal(), title="Flow")

    assert "Channel Flow" in html
    assert 'class="channel-flow-svg"' in html
    assert 'href="#message-sequence-channel-0"' in html
    assert 'aria-label="ChannelA: 3 msg, seq 1-3"' in html
    assert 'class="flow-edge-label"' in html
    assert "1 channel, 3 msgs" in html
    assert '<span title="runtime.path.Producer">Producer</span>' in html
    assert "Channel Edges" in html
    assert "<th>Channel</th><th>Source</th><th>Sink</th><th>Messages</th>" in html
    assert "ChannelA" in html
    assert "Message Sequences" in html
    assert 'id="message-sequence-channel-0"' in html
    assert "Per-message detail is not present in this journal" in html


def test_channel_flow_renders_short_names_for_long_cog_instance_paths() -> None:
    long_path = (
        "@demo::platforms::deploy::system::long_vehicle_platform::long_vehicle_platform"
        ".vehicle_system.common_box.planner_box.planner_box.planner_box_no_sockets"
        ".node_box.primary_planner_cog"
    )
    journal = journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=100,
            end_time_ns=200,
            log_uri="/logs/flow.clog",
            generator_version="test",
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        ),
        cog_journals=[journal_pb2.CogJournal(cog_path="demo.Planner", cog_instance_path=long_path)],
        channel_summaries=[
            journal_pb2.ChannelSummary(
                channel_name="PlannerOutput",
                producer_cog_instance=long_path,
                first_sequence_number=1,
                last_sequence_number=3,
                message_count=3,
                has_logged_messages=True,
            )
        ],
    )

    html = render_report(journal, title="Flow")

    assert f"primary_planner_cog<title>{long_path}</title>" in html
    assert '<span title="unknown sink">external outputs</span>' in html
