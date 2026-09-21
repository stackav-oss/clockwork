# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal alignment result extraction."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, final

from clockwork.journal import journal_pb2
from clockwork.tools.journal_file_generator.alignment import extract_alignment_results_from_reader
from clockwork.tools.journal_file_generator.execution_extractor import ExecutionExtraction, ExtractedCogExecutions
from clockwork.tools.journal_file_generator.log_index import LogIndex, TopicInfo
from clockwork.tools.journal_file_generator.scope import ChannelSchemaRef, ResolvedCogScope, ResolvedJournalScope

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping

_COG_INSTANCE_PATH = "runtime.path.Consumer"
_ALIGNMENT_CHANNEL = "alignment_channel"


@final
@dataclass(frozen=True, kw_only=True)
class _FakeDeserializedMessage:
    """Minimal deserialized log message."""

    topic: str
    sequence_number: int
    message: Mapping[str, object]


@final
@dataclass(kw_only=True)
class _FakeAlignmentReader:
    """Fake alignment reader that only yields registered topics."""

    all_messages: tuple[_FakeDeserializedMessage, ...]
    added_topics: list[str] = field(default_factory=list)

    def add_topic(self, topic: str) -> None:
        """Record a topic registered for deserialization."""
        self.added_topics.append(topic)

    def messages(self) -> Iterator[_FakeDeserializedMessage]:
        """Yield messages from registered topics."""
        added_topic_set = set(self.added_topics)
        for message in self.all_messages:
            if message.topic in added_topic_set:
                yield message


def test_alignment_result_correlates_standard_batch_and_optional_inputs() -> None:
    """Verify one logged alignment message is converted and attached to the matching execution."""
    reader = _FakeAlignmentReader(
        all_messages=(
            _message(
                sequence_number=7,
                fields={
                    "lidar_seq": 10,
                    "lidar_is_new": False,
                    "radar_seq": 0,
                    "has_radar": False,
                    "camera_seq": 12,
                    "has_camera": True,
                    "imu_begin_seq": 20,
                    "imu_end_seq": 22,
                    "imu_first_new_seq": 21,
                },
            ),
        )
    )

    result = extract_alignment_results_from_reader(
        reader,
        resolved_scope=_make_scope(logged_alignment=True),
        execution_extraction=_make_execution_extraction(
            journal_pb2.CogExecution(
                execution_index=0,
                input_views=[
                    _input_view(_ALIGNMENT_CHANNEL, "aligned", (6, 7), first_new_index=1),
                    _input_view("lidar_channel", "lidar", (10,)),
                    _input_view("camera_channel", "camera", (12,)),
                    _input_view("imu_channel", "imu", (20, 21, 22)),
                ],
            )
        ),
        log_index=_make_log_index(_ALIGNMENT_CHANNEL),
    )

    assert reader.added_topics == [_ALIGNMENT_CHANNEL]
    assert not result.gaps
    assert len(result.execution_alignments) == 1
    alignment = result.execution_alignments[0].alignment_result
    assert alignment.aligner_name == "SimpleAligner"
    assert [
        (
            aligned_input.input_name,
            aligned_input.selected_sequence_number,
            aligned_input.present,
            aligned_input.is_reused,
            aligned_input.is_batch,
            aligned_input.batch_begin_sequence_number,
            aligned_input.batch_end_sequence_number,
        )
        for aligned_input in alignment.aligned_inputs
    ] == [
        ("lidar", 10, True, False, False, 0, 0),
        ("radar", 0, False, False, False, 0, 0),
        ("camera", 12, True, False, False, 0, 0),
        ("imu", 0, True, False, True, 20, 22),
    ]


def test_alignment_batch_correlation_requires_full_visible_range() -> None:
    """Verify a batch does not correlate when an interior selected sequence is missing."""
    reader = _FakeAlignmentReader(
        all_messages=(
            _message(
                sequence_number=7,
                fields={
                    "imu_begin_seq": 20,
                    "imu_end_seq": 22,
                },
            ),
        )
    )

    result = extract_alignment_results_from_reader(
        reader,
        resolved_scope=_make_scope(logged_alignment=True),
        execution_extraction=_make_execution_extraction(
            journal_pb2.CogExecution(
                execution_index=0,
                input_views=[
                    _input_view(_ALIGNMENT_CHANNEL, "aligned", (7,)),
                    _input_view("imu_channel", "imu", (20, 22)),
                ],
            )
        ),
        log_index=_make_log_index(_ALIGNMENT_CHANNEL),
    )

    assert not result.execution_alignments
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT


def test_alignment_result_missing_and_ambiguous_correlation_record_gaps() -> None:
    """Verify missing and ambiguous alignment-message matches become readiness gaps."""
    reader = _FakeAlignmentReader(
        all_messages=(
            _message(sequence_number=2, fields={"lidar_seq": 10}),
            _message(sequence_number=3, fields={"lidar_seq": 10}),
        )
    )

    result = extract_alignment_results_from_reader(
        reader,
        resolved_scope=_make_scope(logged_alignment=True),
        execution_extraction=_make_execution_extraction(
            journal_pb2.CogExecution(
                execution_index=0,
                input_views=[
                    _input_view(_ALIGNMENT_CHANNEL, "aligned", (1,)),
                    _input_view("lidar_channel", "lidar", (10,)),
                ],
            ),
            journal_pb2.CogExecution(
                execution_index=1,
                input_views=[
                    _input_view(_ALIGNMENT_CHANNEL, "aligned", (2, 3)),
                    _input_view("lidar_channel", "lidar", (10,)),
                ],
            ),
        ),
        log_index=_make_log_index(_ALIGNMENT_CHANNEL),
    )

    assert not result.execution_alignments
    assert [gap.reason for gap in result.gaps] == [
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
        journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT,
    ]
    assert "No matching alignment result" in result.gaps[0].message
    assert result.gaps[0].execution_index == 0
    assert "Multiple alignment results matched" in result.gaps[1].message
    assert result.gaps[1].execution_index == 1


def test_missing_logged_alignment_channel_records_gap() -> None:
    """Verify an aligned cog records a gap when its alignment channel is absent from log metadata."""
    result = extract_alignment_results_from_reader(
        _FakeAlignmentReader(all_messages=()),
        resolved_scope=_make_scope(logged_alignment=False),
        execution_extraction=_make_execution_extraction(journal_pb2.CogExecution(execution_index=0)),
        log_index=LogIndex(topics=()),
    )

    assert not result.execution_alignments
    assert len(result.gaps) == 1
    assert result.gaps[0].reason == journal_pb2.REPLAY_READINESS_GAP_REASON_MISSING_ALIGNMENT_RESULT
    assert result.gaps[0].cog_instance_path == _COG_INSTANCE_PATH
    assert result.gaps[0].channel_name == _ALIGNMENT_CHANNEL


def _message(*, sequence_number: int, fields: Mapping[str, object]) -> _FakeDeserializedMessage:
    return _FakeDeserializedMessage(
        topic=_ALIGNMENT_CHANNEL,
        sequence_number=sequence_number,
        message=fields,
    )


def _input_view(
    channel_name: str,
    cog_member_name: str,
    visible_sequence_numbers: tuple[int, ...],
    *,
    first_new_index: int = 0,
) -> journal_pb2.InputViewState:
    return journal_pb2.InputViewState(
        channel_name=channel_name,
        cog_member_name=cog_member_name,
        visible_sequence_numbers=list(visible_sequence_numbers),
        first_new_index=first_new_index,
    )


def _make_execution_extraction(*executions: journal_pb2.CogExecution) -> ExecutionExtraction:
    return ExecutionExtraction(
        cog_executions=(
            ExtractedCogExecutions(
                cog_instance_path=_COG_INSTANCE_PATH,
                condition_names=(),
                executions=executions,
            ),
        ),
        gaps=(),
    )


def _make_scope(*, logged_alignment: bool) -> ResolvedJournalScope:
    schema_name = "demo.SimpleAlignerAlignmentMsg"
    return ResolvedJournalScope(
        requested_cog_instance_paths=(_COG_INSTANCE_PATH,),
        cog_scopes=(
            ResolvedCogScope(
                cog_instance_path=_COG_INSTANCE_PATH,
                cog_path="demo.Consumer",
                input_schemas=(
                    ChannelSchemaRef(
                        channel_name=_ALIGNMENT_CHANNEL,
                        schema_name=schema_name,
                        schema_uuid="11111111-1111-1111-1111-111111111111" if logged_alignment else "",
                    ),
                ),
                output_schemas=(),
                metrics_channels=(),
            ),
        ),
        channel_topologies=(),
        gaps=(),
    )


def _make_log_index(*channel_names: str) -> LogIndex:
    return LogIndex(
        topics=tuple(
            TopicInfo(
                name=channel_name,
                schema_name="demo.SimpleAlignerAlignmentMsg",
                schema_uuid="11111111-1111-1111-1111-111111111111",
                message_encoding="tachyon",
                channel_type="regular",
                schema_encoding="clockwork_tachyon",
            )
            for channel_name in channel_names
        )
    )
