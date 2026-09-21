# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Topic metadata index for Clockwork logs."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, final

from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model
from google.protobuf.message import DecodeError

if TYPE_CHECKING:
    from collections.abc import Sequence


class TopicMetadataLike(Protocol):
    """Topic metadata fields used by the journal generator."""

    @property
    def name(self) -> str:
        """Topic name."""
        ...

    @property
    def type(self) -> str:  # The log reader metadata API names this field "type".
        """Schema type name."""
        ...

    @property
    def message_encoding(self) -> str:
        """Message encoding name."""
        ...

    @property
    def channel_type(self) -> str:
        """Clockwork channel type."""
        ...

    @property
    def schema_encoding(self) -> str:
        """Schema encoding name."""
        ...

    @property
    def schema_definition(self) -> bytes:
        """Serialized schema metadata."""
        ...


class LogMetadataReader(Protocol):
    """Reader protocol needed for metadata-only log indexing."""

    @property
    def metadata(self) -> Sequence[TopicMetadataLike]:
        """All logged topic metadata."""
        ...

    def try_get_topic_metadata(self, topic: str) -> TopicMetadataLike | None:
        """Return metadata for one topic when present."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class TopicInfo:
    """Journal-friendly topic metadata."""

    name: str
    """Topic name."""

    schema_name: str
    """Schema type name recorded in the log metadata."""

    schema_uuid: str
    """Schema UUID when it can be read from Tachyon metadata."""

    message_encoding: str
    """Message encoding name."""

    channel_type: str
    """Clockwork channel type."""

    schema_encoding: str
    """Schema encoding name."""


@final
@dataclass(frozen=True, kw_only=True)
class LogIndex:
    """Metadata-only index of the topics available in a Clockwork log."""

    topics: tuple[TopicInfo, ...]
    """Topic metadata sorted by topic name."""

    def topic_names(self) -> tuple[str, ...]:
        """Return all topic names in deterministic order."""
        return tuple(topic.name for topic in self.topics)

    def topic(self, topic_name: str) -> TopicInfo | None:
        """Return topic metadata for ``topic_name`` when it is present."""
        for topic in self.topics:
            if topic.name == topic_name:
                return topic
        return None

    def has_topic(self, topic_name: str) -> bool:
        """Return whether ``topic_name`` is present in the log metadata."""
        return self.topic(topic_name) is not None


def load_log_index(log_uri: str) -> LogIndex:
    """Read topic metadata from a Clockwork log without deserializing message payloads."""
    return make_log_index(LogReader(log_uri))


def make_log_index(reader: LogMetadataReader) -> LogIndex:
    """Build a deterministic topic index from a metadata reader."""
    topics = tuple(sorted((_topic_info_from_metadata(metadata) for metadata in reader.metadata), key=lambda t: t.name))
    return LogIndex(topics=topics)


def _topic_info_from_metadata(metadata: TopicMetadataLike) -> TopicInfo:
    return TopicInfo(
        name=metadata.name,
        schema_name=metadata.type,
        schema_uuid=_schema_uuid_from_definition(metadata.schema_definition),
        message_encoding=metadata.message_encoding,
        channel_type=metadata.channel_type,
        schema_encoding=metadata.schema_encoding,
    )


def _schema_uuid_from_definition(schema_definition: bytes) -> str:
    if not schema_definition:
        return ""

    try:
        metadata = tachyon_metadata.get_metadata_from_protobuf(schema_definition)
    except (DecodeError, IndexError, TypeError, ValueError):
        return ""

    if metadata.outer_type_id >= len(metadata.types):
        return ""

    outer_type = metadata.types[metadata.outer_type_id]
    if not isinstance(outer_type, tachyon_model.SchemaType):
        return ""
    return str(outer_type.schema_uuid)
