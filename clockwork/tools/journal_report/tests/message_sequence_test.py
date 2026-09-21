# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for rich journal report message sequences."""

from __future__ import annotations

from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.tests.support.journal_fixtures import message_sequence_journal
from clockwork.tools.journal_report.view_model import build_report_model


def test_message_sequence_model_sorts_messages_and_derives_execution_links() -> None:
    model = build_report_model(message_sequence_journal(), title="Messages")
    channel = model.channels[0]
    messages = channel["messages"]

    assert [(message.sequence_number, message.publish_time_ns, message.payload_size_bytes) for message in messages] == [
        (1, 110, 10),
        (3, 130, 30),
    ]
    assert messages[0].producer_links[0].execution_id == "cog-0-execution-1"
    assert messages[0].consumer_links[0].execution_id == "cog-1-execution-4"
    assert messages[1].producer_links[0].execution_id == "cog-0-execution-1"
    assert messages[1].consumer_links == ()
    assert messages[1].is_produced_unconsumed

    producer_execution = model.cogs[0].executions[0]
    consumer_execution = model.cogs[1].executions[0]
    output = producer_execution["outputs"][0]
    input_view = consumer_execution["input_views"][0]
    assert output["produced_sequence_message_ids"] == {1: "message-channel-0-1", 3: "message-channel-0-3"}
    assert input_view["visible_sequence_message_ids"] == {1: "message-channel-0-1"}


def test_message_sequence_renderer_outputs_rich_rows_and_cross_links() -> None:
    html = render_report(message_sequence_journal(), title="Messages")

    assert '<tr id="message-channel-0-1">' in html
    assert '<tr id="message-channel-0-3" class="message-row-unconsumed">' in html
    assert ('<a href="#cog-0-execution-1" title="runtime.path.AProducer">AProducer execution 1</a>') in html
    assert ('<a href="#cog-1-execution-4" title="runtime.path.BConsumer">BConsumer execution 4</a>') in html
    assert '<span class="message-unconsumed">not consumed</span>' in html
    assert (
        '<mark class="sequence-new"><a href="#message-channel-0-1" class="message-data-link" '
        'data-channel-name="DataChannel" data-sequence-number="1" aria-expanded="false">1</a></mark>'
    ) in html
    assert (
        '<a href="#message-channel-0-3" class="message-data-link" data-channel-name="DataChannel" '
        'data-sequence-number="3" aria-expanded="false">3</a>'
    ) in html
    assert html.count('data-channel-name="DataChannel" data-sequence-number="1"') == 3
    assert "data-message-data-api" not in html
    assert "Per-message detail is not present in this journal" not in html


def test_message_sequence_renderer_escapes_execution_link_labels() -> None:
    journal = message_sequence_journal()
    journal.cog_journals[0].cog_instance_path = "runtime.path.<script>alert(1)</script>"

    html = render_report(journal, title="Messages")

    assert "<script>alert(1)</script> execution 1" not in html
    assert "&lt;script&gt;alert(1)&lt;/script&gt; execution 1" in html
