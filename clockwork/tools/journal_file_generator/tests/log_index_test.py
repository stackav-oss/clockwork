# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for metadata-only log topic indexing."""

from __future__ import annotations

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from clockwork.tools.journal_file_generator.log_index import LogIndex, TopicInfo, make_log_index


@dataclass(frozen=True, kw_only=True)
class _FakeTopicMetadata:
    """Minimal fake for LogReader topic metadata."""

    name: str
    type_name: str
    message_encoding: str = "tachyon"
    channel_type: str = "regular"
    schema_encoding: str = "clockwork_tachyon"
    schema_definition: bytes = b"not a serialized tachyon metadata protobuf"

    @property
    def type(self) -> str:  # The log reader metadata API names this field "type".
        """Schema type name."""
        return self.type_name


@dataclass(frozen=True, kw_only=True)
class _FakeLogReader:
    """Minimal fake for metadata-only LogReader behavior."""

    metadata: tuple[_FakeTopicMetadata, ...]

    def try_get_topic_metadata(self, topic: str) -> _FakeTopicMetadata | None:
        """Return fake topic metadata by name."""
        for metadata in self.metadata:
            if metadata.name == topic:
                return metadata
        return None


def _serialized_tachyon_schema_metadata(schema_uuid: UUID) -> bytes:
    metadata = tachyon_model.TachyonMetadata(
        outer_type_id=0,
        types=(
            tachyon_model.SchemaType(
                fqn="demo.Valid",
                size=16,
                alignment=8,
                schema_uuid=schema_uuid,
                version=1,
                arguments=(),
                fields=(),
            ),
        ),
    )
    return tachyon_metadata.to_protobuf(metadata).SerializeToString()


def test_make_log_index_sorts_topics_and_preserves_metadata() -> None:
    """Verify topic metadata is indexed deterministically without payload reads."""
    index = make_log_index(
        _FakeLogReader(
            metadata=(
                _FakeTopicMetadata(name="output", type_name="demo.Output"),
                _FakeTopicMetadata(name="input", type_name="demo.Input"),
            )
        )
    )

    input_topic = index.topic("input")

    assert index.topic_names() == ("input", "output")
    assert input_topic == TopicInfo(
        name="input",
        schema_name="demo.Input",
        schema_uuid="",
        message_encoding="tachyon",
        channel_type="regular",
        schema_encoding="clockwork_tachyon",
    )
    assert index.topic("missing") is None
    assert index.has_topic("output")


def test_make_log_index_reads_schema_uuid_from_tachyon_metadata() -> None:
    """Verify valid Tachyon schema metadata populates the indexed schema UUID."""
    schema_uuid = UUID("11112222-3333-4444-5555-666677778888")

    index = make_log_index(
        _FakeLogReader(
            metadata=(
                _FakeTopicMetadata(
                    name="valid",
                    type_name="demo.Valid",
                    schema_definition=_serialized_tachyon_schema_metadata(schema_uuid),
                ),
            )
        )
    )

    topic = index.topic("valid")

    assert topic is not None
    assert topic.schema_uuid == str(schema_uuid)


def test_log_index_can_be_constructed_from_topic_info() -> None:
    """Verify tests and future extractors can use a prebuilt log index."""
    index = LogIndex(
        topics=(
            TopicInfo(
                name="chan",
                schema_name="demo.Schema",
                schema_uuid="33333333-3333-3333-3333-333333333333",
                message_encoding="tachyon",
                channel_type="regular",
                schema_encoding="clockwork_tachyon",
            ),
        )
    )

    topic = index.topic("chan")

    assert topic is not None
    assert topic.schema_uuid == "33333333-3333-3333-3333-333333333333"
