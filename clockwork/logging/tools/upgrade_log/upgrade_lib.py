# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Library for upgrading log files by migrating schemas."""

from abc import ABC, abstractmethod
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import Any, cast

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir.schema import InstantiatedSchema
from clockwork.logging.offboard.nb_log_writer_impl import LogWriter
from clockwork.logging.readers.nb_log_reader import LogReader
from clockwork.logging.readers.nb_types import LoggedMessage, TopicMetadata
from clockwork.serialization.metadata import tachyon as tachyon_meta
from clockwork.serialization.metadata import tachyon_model
from clockwork.serialization.py import protocol, tachyon_dyn
from clockwork.serialization.py.compatibility import validate_tachyon_types_compatibility
from clockwork.serialization.py.tachyon_dyn_from_metadata import py_type_from_metadata


class ChannelHandler(ABC):
    """Base class for channel handlers that process messages during log upgrade."""

    @abstractmethod
    def setup_output_channel(self, writer: LogWriter) -> None:
        """Set up the output channel in the log writer."""

    @abstractmethod
    def process_message(
        self,
        writer: LogWriter,
        message: LoggedMessage,
    ) -> None:
        """Process a message from input log and write to output log."""


@dataclass
class CopyHandler(ChannelHandler):
    """Handler that copies messages without modification."""

    topic_metadata: TopicMetadata

    def setup_output_channel(self, writer: LogWriter) -> None:
        """Set up the output channel with original metadata."""
        writer.create_channel(
            self.topic_metadata.name,
            self.topic_metadata.message_encoding,
            self.topic_metadata.channel_type,
            self.topic_metadata.type,
            self.topic_metadata.schema_encoding,
            self.topic_metadata.schema_definition,
        )

    def process_message(
        self,
        writer: LogWriter,
        message: LoggedMessage,
    ) -> None:
        """Copy message directly without modification."""
        writer.write(
            channel_name=self.topic_metadata.name,
            sequence_number=message.sequence_number,
            log_time=message.log_time,
            transmit_time=message.publish_time,
            header=cast("Any", message.header_view),
            data=cast("Any", message.data_view),
        )


@dataclass
class UpgradeHandler(ChannelHandler):
    """Handler that upgrades schema and remaps messages."""

    topic_metadata: TopicMetadata
    deserializer: Callable[[memoryview], protocol.Tachyon[Any]]
    target_schema: InstantiatedSchema
    compiler_context: CompilerContext

    def setup_output_channel(self, writer: LogWriter) -> None:
        """Set up the output channel with upgraded schema metadata."""
        # Generate metadata from the target schema

        writer.create_channel(
            self.topic_metadata.name,
            self.topic_metadata.message_encoding,
            self.topic_metadata.channel_type,
            self.topic_metadata.type,
            self.topic_metadata.schema_encoding,
            self.topic_metadata.schema_definition,
        )

    def process_message(
        self,
        writer: LogWriter,
        message: LoggedMessage,
    ) -> None:
        """Upgrade message using the deserializer which handles upgrading."""
        # Deserialize and upgrade in one step using the provided deserializer
        target_instance = self.deserializer(message.data_view)
        buffer = bytearray(target_instance.get_tachyon_constraint().size)
        target_instance.serialize_tachyon(memoryview(buffer))
        # Write to output
        writer.write(
            channel_name=self.topic_metadata.name,
            sequence_number=message.sequence_number,
            log_time=message.log_time,
            transmit_time=message.publish_time,
            header=cast("Any", message.header_view),
            data=cast("Any", memoryview(buffer)),
        )


def _create_upgrade_handler(  # noqa: PLR0913 (too many args) mitigated by kwonly args
    *,
    compiler_context: CompilerContext,
    topic_metadata: TopicMetadata,
    target_schema: InstantiatedSchema,
    source_metadata: tachyon_model.TachyonMetadata,
    target_metadata: tachyon_model.TachyonMetadata,
    channel_name: str,
) -> UpgradeHandler:
    """Create an upgrade handler for a channel that needs schema migration.

    Args:
        compiler_context: Compiler context for serialization operations
        topic_metadata: Metadata for the topic
        target_schema: The schema to upgrade to
        source_metadata: Metadata for the source schema
        target_metadata: Metadata for the target schema
        channel_name: Name of the channel

    Returns:
        An UpgradeHandler for the channel

    Raises:
        ValueError: If the upgrade plan is invalid
    """
    source_class, _ = py_type_from_metadata(compiler_context, topic_metadata.name, source_metadata)

    upgrade_plan = tachyon_dyn.create_upgrade_plan(compiler_context, target_schema, source_metadata)
    if not upgrade_plan.needs_upgrade():
        msg = f"Upgrade plan for channel {channel_name} does not require upgrade but metadata does"
        raise ValueError(msg)

    target_class = upgrade_plan.py_type
    if target_class is None:
        msg = f"Upgrade plan for channel {channel_name} has no target class"
        raise ValueError(msg)

    def upgrader(
        buffer: memoryview,
        src_class: type[protocol.Tachyon[Any]] = source_class,
        up_plan: tachyon_dyn.UpgradePlan = upgrade_plan,
    ) -> protocol.Tachyon[Any]:
        old_instance = src_class.deserialize_tachyon(buffer)
        return up_plan.upgrade(old_instance)

    return UpgradeHandler(
        TopicMetadata(
            name=channel_name,
            message_encoding=topic_metadata.message_encoding,
            channel_type=topic_metadata.channel_type,
            type=target_class.get_tachyon_metadata_name() or target_schema.value_key(),
            schema_encoding=topic_metadata.schema_encoding,
            schema_definition=tachyon_meta.to_protobuf(target_metadata).SerializeToString(),
        ),
        upgrader,
        target_schema,
        compiler_context,
    )


def _validate_schema_compatibility(
    channel_name: str, target_metadata: tachyon_model.TachyonMetadata, source_metadata: tachyon_model.TachyonMetadata
) -> bool:
    """Validate compatibility between source and target schemas.

    Args:
        channel_name: Name of the channel
        target_metadata: Metadata for the target schema
        source_metadata: Metadata for the source schema

    Returns:
        True if upgrade is needed, False if schemas are compatible without upgrade

    Raises:
        ValueError: If schemas are incompatible
    """
    try:
        return validate_tachyon_types_compatibility(expected=target_metadata, incoming=source_metadata)
    except (ValueError, TypeError) as e:
        msg = f"Failed to validate compatibility for channel {channel_name}: {e}"
        raise ValueError(msg) from e


def _process_channel(
    topic_metadata: TopicMetadata,
    compiler_context: CompilerContext,
    upgrade_map: dict[str, InstantiatedSchema],
    copy_unspecified: bool,
) -> tuple[str, ChannelHandler | None]:
    """Process a single channel and determine the appropriate handler.

    Args:
        topic_metadata: Metadata for the topic
        compiler_context: Compiler context for serialization operations
        upgrade_map: Dictionary mapping channel names to target schemas
        copy_unspecified: If True, channels not in upgrade_map will be copied

    Returns:
        Tuple of (channel_name, handler or None)

    Raises:
        ValueError: If the channel cannot be processed
    """
    channel_name = topic_metadata.name

    if topic_metadata.message_encoding != "tachyon":
        msg = f"Cannot upgrade non-Tachyon channel {channel_name} (encoding: {topic_metadata.message_encoding})"
        raise ValueError(msg)

    try:
        target_schema = upgrade_map[channel_name]
    except KeyError:
        if copy_unspecified:
            return channel_name, CopyHandler(topic_metadata)
        return channel_name, None

    source_metadata = tachyon_meta.get_metadata_from_protobuf(topic_metadata.schema_definition)
    target_metadata = tachyon_meta.get_metadata(compiler_context, target_schema)

    needs_upgrade = _validate_schema_compatibility(channel_name, target_metadata, source_metadata)

    if not needs_upgrade:
        return channel_name, CopyHandler(topic_metadata)

    return channel_name, _create_upgrade_handler(
        compiler_context=compiler_context,
        topic_metadata=topic_metadata,
        target_schema=target_schema,
        source_metadata=source_metadata,
        target_metadata=target_metadata,
        channel_name=channel_name,
    )


def create_handlers(
    reader: LogReader,
    compiler_context: CompilerContext,
    upgrades: Iterable[tuple[str, InstantiatedSchema]],
    copy_unspecified: bool,
) -> dict[str, ChannelHandler]:
    """Create handlers for each channel in the log.

    Args:
        reader: The log reader
        compiler_context: Compiler context for serialization
        upgrades: Iterable of (channel_name, schema) pairs for channels to upgrade
        copy_unspecified: If True, channels not in upgrades will be copied without modification

    Returns:
        Dictionary mapping channel names to handlers
    """
    handlers: dict[str, ChannelHandler] = {}
    upgrade_map = dict(upgrades)

    # Process all channels in the log
    for topic_metadata in reader.metadata:
        channel_name, handler = _process_channel(topic_metadata, compiler_context, upgrade_map, copy_unspecified)
        if handler is not None:
            handlers[channel_name] = handler

    return handlers


def upgrade_log(
    compiler_context: CompilerContext,
    reader: LogReader,
    writer: LogWriter,
    upgrades: Iterable[tuple[str, InstantiatedSchema]],
    copy_unspecified_channels: bool = True,
) -> None:
    """Copy a log with schema upgrades for specified channels.

    Args:
        compiler_context: The compiler context for serialization operations
        reader: LogReader instance to read from
        writer: LogWriter instance to write to
        upgrades: Iterable of (channel_name, schema) pairs for channels to upgrade
        copy_unspecified_channels: If True, channels not specified in upgrades will
                                  be copied without modification
    """
    # Create handlers for each channel
    handlers = create_handlers(reader, compiler_context, upgrades, copy_unspecified_channels)

    # Set up output channels
    for handler in handlers.values():
        handler.setup_output_channel(writer)

    # Process all messages
    for message in reader.raw_messages():
        channel_name = message.topic

        if channel_name in handlers:
            handlers[channel_name].process_message(writer, message)
