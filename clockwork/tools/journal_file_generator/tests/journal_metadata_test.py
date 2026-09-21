# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal input metadata and output signal parsing."""

from __future__ import annotations

from dataclasses import dataclass

import pytest
from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.cog_channel import CogChannelRef
from clockwork.tools.journal_file_generator.journal_metadata import parse_execution_journal_metadata


@dataclass(frozen=True, kw_only=True)
class _InputSequenceMetadata:
    """Input sequence metadata fixture."""

    message_sequence_numbers: tuple[int, ...]
    cursor_position: int


@pytest.mark.parametrize(
    ("entry", "expected_input_view"),
    [
        pytest.param(
            {
                "camera_unseen_messages_value_metadata": _InputSequenceMetadata(
                    message_sequence_numbers=(10, 11, 12),
                    cursor_position=1,
                ),
            },
            journal_pb2.InputViewState(
                channel_name="camera",
                cog_member_name="camera",
                cursor_sequence_number=11,
                visible_sequence_numbers=[10, 11, 12],
                first_new_index=1,
            ),
            id="multiple-new-messages",
        ),
        pytest.param(
            {
                "camera_unseen_messages_value": 0,
                "camera_unseen_messages_value_metadata": {
                    "message_sequence_numbers": (10, 11),
                    "cursor_position": 2,
                },
            },
            journal_pb2.InputViewState(
                channel_name="camera",
                cog_member_name="camera",
                cursor_sequence_number=12,
                visible_sequence_numbers=[10, 11],
                first_new_index=2,
            ),
            id="no-new-messages",
        ),
        pytest.param(
            {
                "camera_unseen_messages_value": 0,
                "camera_unseen_messages_value_metadata": _InputSequenceMetadata(
                    message_sequence_numbers=(),
                    cursor_position=0,
                ),
            },
            journal_pb2.InputViewState(
                channel_name="camera",
                cog_member_name="camera",
                cursor_sequence_number=0,
                first_new_index=0,
            ),
            id="empty-view",
        ),
        pytest.param(
            {
                "camera_unseen_messages_value": 4,
                "camera_unseen_messages_value_metadata": _InputSequenceMetadata(
                    message_sequence_numbers=(20, 21),
                    cursor_position=0,
                ),
            },
            journal_pb2.InputViewState(
                channel_name="camera",
                cog_member_name="camera",
                cursor_sequence_number=20,
                visible_sequence_numbers=[20, 21],
                first_new_index=0,
            ),
            id="evicted-unseen-messages",
        ),
    ],
)
def test_input_metadata_records_cursor_state(
    entry: dict[str, object],
    expected_input_view: journal_pb2.InputViewState,
) -> None:
    """Verify input metadata records cursor state for representative views."""
    result = parse_execution_journal_metadata(
        entry,
        input_channel_names=("camera",),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=7,
    )

    assert not result.gaps
    assert result.input_views == (expected_input_view,)


def test_single_expected_channel_can_use_metric_stem() -> None:
    """Verify one expected channel can be mapped from a metric stem."""
    result = parse_execution_journal_metadata(
        {
            "sensor_unseen_messages_value_metadata": _InputSequenceMetadata(
                message_sequence_numbers=(1,),
                cursor_position=0,
            ),
            "sensor_dropped_messages_value": 3,
        },
        input_channel_names=("input_channel",),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=0,
    )

    assert not result.gaps
    assert result.input_views[0].channel_name == "input_channel"
    assert result.input_views[0].cog_member_name == "sensor"
    assert result.input_views[0].cursor_sequence_number == 1
    assert result.input_views[0].HasField("dropped_message_count")
    assert result.input_views[0].dropped_message_count == 3


def test_channel_refs_map_multiple_input_and_output_metrics_to_channels() -> None:
    """Verify channel refs map metric fields to concrete journal channel names."""
    result = parse_execution_journal_metadata(
        {
            "left_sensor_unseen_messages_value_metadata": _InputSequenceMetadata(
                message_sequence_numbers=(10,),
                cursor_position=0,
            ),
            "right_sensor_unseen_messages_value_metadata": _InputSequenceMetadata(
                message_sequence_numbers=(20,),
                cursor_position=0,
            ),
            "left_command_num_messages_value": 1,
            "left_command_first_sequence_number_value": 30,
            "right_command_num_messages_value": 1,
            "right_command_first_sequence_number_value": 40,
        },
        input_channel_names=("left_input_channel", "right_input_channel"),
        output_channel_names=("left_output_channel", "right_output_channel"),
        cog_instance_path="runtime.Cog",
        execution_index=0,
        input_channel_refs=(
            CogChannelRef(cog_member_name="left_sensor", channel_name="left_input_channel"),
            CogChannelRef(cog_member_name="right_sensor", channel_name="right_input_channel"),
        ),
        output_channel_refs=(
            CogChannelRef(cog_member_name="left_command", channel_name="left_output_channel"),
            CogChannelRef(cog_member_name="right_command", channel_name="right_output_channel"),
        ),
    )

    assert not result.gaps
    assert [input_view.channel_name for input_view in result.input_views] == [
        "left_input_channel",
        "right_input_channel",
    ]
    assert [input_view.cog_member_name for input_view in result.input_views] == ["left_sensor", "right_sensor"]
    assert [input_view.cursor_sequence_number for input_view in result.input_views] == [10, 20]
    assert [output.channel_name for output in result.outputs] == ["left_output_channel", "right_output_channel"]
    assert [list(output.produced_sequence_numbers) for output in result.outputs] == [[30], [40]]


def test_channel_ref_falls_back_to_direct_channel_metric_stem() -> None:
    """Verify direct channel metric names remain usable when channel refs are present."""
    result = parse_execution_journal_metadata(
        {
            "input_channel_unseen_messages_value_metadata": _InputSequenceMetadata(
                message_sequence_numbers=(1,),
                cursor_position=0,
            ),
        },
        input_channel_names=("input_channel",),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=0,
        input_channel_refs=(CogChannelRef(cog_member_name="sensor", channel_name="input_channel"),),
    )

    assert not result.gaps
    assert result.input_views[0].channel_name == "input_channel"
    assert result.input_views[0].cog_member_name == "sensor"
    assert result.input_views[0].cursor_sequence_number == 1


def test_malformed_input_metadata_records_gap() -> None:
    """Verify malformed input metadata produces a readiness gap."""
    result = parse_execution_journal_metadata(
        {
            "camera_unseen_messages_value_metadata": _InputSequenceMetadata(
                message_sequence_numbers=(10,),
                cursor_position=2,
            ),
        },
        input_channel_names=("camera",),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=3,
    )

    assert not result.input_views
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA
    assert result.gaps[0].channel_name == "camera"
    assert result.gaps[0].execution_index == 3


def test_missing_input_metadata_records_gap() -> None:
    """Verify missing expected input metadata produces a readiness gap."""
    result = parse_execution_journal_metadata(
        {},
        input_channel_names=("camera",),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=4,
    )

    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA
    assert result.gaps[0].channel_name == "camera"
    assert result.gaps[0].execution_index == 4


@pytest.mark.parametrize(
    ("entry", "metric_stem"),
    [
        pytest.param({"camera_unseen_messages_value": 1}, "camera", id="scalar-only"),
        pytest.param(
            {
                "sensor_member_unseen_messages_value_metadata": _InputSequenceMetadata(
                    message_sequence_numbers=(10,),
                    cursor_position=0,
                ),
            },
            "sensor_member",
            id="metadata-present",
        ),
    ],
)
def test_input_metrics_without_channel_context_record_gap(entry: dict[str, object], metric_stem: str) -> None:
    """Verify input metric stems are not serialized as channel names without channel context."""
    result = parse_execution_journal_metadata(
        entry,
        input_channel_names=(),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=4,
    )

    assert not result.input_views
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_INPUT_VIEW_SEQUENCE_METADATA
    assert result.gaps[0].channel_name == metric_stem


def test_output_sequence_signals_expand_produced_sequences() -> None:
    """Verify output count and first sequence signals record produced sequence numbers."""
    result = parse_execution_journal_metadata(
        {
            "planned_path_num_messages_value": 2,
            "planned_path_first_sequence_number_value": 100,
        },
        input_channel_names=(),
        output_channel_names=("planned_path",),
        cog_instance_path="runtime.Cog",
        execution_index=0,
    )

    assert not result.gaps
    assert result.outputs == (
        journal_pb2.OutputState(
            channel_name="planned_path",
            produced_sequence_numbers=[100, 101],
        ),
    )


def test_missing_output_first_sequence_records_gap_when_messages_were_produced() -> None:
    """Verify produced outputs without a first sequence signal produce a readiness gap."""
    result = parse_execution_journal_metadata(
        {"planned_path_num_messages_value": 1},
        input_channel_names=(),
        output_channel_names=("planned_path",),
        cog_instance_path="runtime.Cog",
        execution_index=5,
    )

    assert not result.outputs
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA
    assert result.gaps[0].channel_name == "planned_path"
    assert result.gaps[0].execution_index == 5


@pytest.mark.parametrize(
    ("entry", "metric_stem"),
    [
        pytest.param({"planned_path_num_messages_value": 1}, "planned_path", id="scalar-only"),
        pytest.param(
            {
                "command_member_num_messages_value": 1,
                "command_member_first_sequence_number_value": 30,
            },
            "command_member",
            id="first-sequence-present",
        ),
    ],
)
def test_output_metrics_without_channel_context_record_gap(entry: dict[str, object], metric_stem: str) -> None:
    """Verify output metric stems are not serialized as channel names without channel context."""
    result = parse_execution_journal_metadata(
        entry,
        input_channel_names=(),
        output_channel_names=(),
        cog_instance_path="runtime.Cog",
        execution_index=5,
    )

    assert not result.outputs
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA
    assert result.gaps[0].channel_name == metric_stem


@pytest.mark.parametrize(
    "entry",
    [
        pytest.param({"planned_path_num_messages_value": -1}, id="negative-count"),
        pytest.param(
            {
                "planned_path_num_messages_value": 1,
                "planned_path_first_sequence_number_value": -1,
            },
            id="negative-first-sequence",
        ),
    ],
)
def test_malformed_output_sequence_signals_record_gap(entry: dict[str, object]) -> None:
    """Verify malformed output sequence signals produce a readiness gap."""
    result = parse_execution_journal_metadata(
        entry,
        input_channel_names=(),
        output_channel_names=("planned_path",),
        cog_instance_path="runtime.Cog",
        execution_index=0,
    )

    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_OUTPUT_SEQUENCE_METADATA
    assert not result.outputs


def test_zero_output_count_without_first_sequence_records_empty_output_state() -> None:
    """Verify an output with no produced messages does not need a first sequence signal."""
    result = parse_execution_journal_metadata(
        {"planned_path_num_messages_value": 0},
        input_channel_names=(),
        output_channel_names=("planned_path",),
        cog_instance_path="runtime.Cog",
        execution_index=5,
    )

    assert not result.gaps
    assert result.outputs == (journal_pb2.OutputState(channel_name="planned_path"),)
