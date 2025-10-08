# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python log reader library."""

import logging
import struct
from collections.abc import Callable
from typing import Any, Final, cast

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.logging.readers.nb_log_processor import (
    LogProcessor as _NBLogProcessor,
)
from clockwork.logging.readers.nb_types import (
    LoggedMessage,
    LogReaderConfig,
    LogTimestamp,
)
from clockwork.serialization.metadata import tachyon as tachyon_meta
from clockwork.serialization.py import compatibility, protocol, tachyon_dyn_from_metadata

_logger: Final = logging.getLogger(__name__)


def _handle_error(msg: str, suppress_errors: bool) -> None:
    if not suppress_errors:
        raise ValueError(msg)
    _logger.warning(msg)


class LogProcessor(_NBLogProcessor):
    """Implements deserializing callbacks for the LogProcessor nanobind wrapper."""

    def __init__(self, config: LogReaderConfig) -> None:
        """Construct a new LogProcessor."""
        super().__init__(config)
        self.compiler_context = CompilerContext()

    def add_callback(
        self,
        topic: str,
        message_type: type[protocol.Tachyon[Any]] | None,
        callback: Callable[[object], None],
        suppress_errors: bool = False,
    ) -> None:
        """Add a callback for a deserialized message.

        Args:
            topic: Topic name
            message_type: Message type
            callback: Callback function
            suppress_errors: Suppress deserialization errors
        """
        self.add_callback_with_timestamp(
            topic=topic,
            message_type=message_type,
            callback=lambda _, obj: callback(obj),
            suppress_errors=suppress_errors,
        )

    # TODO(PER-994): Add unit test for suppress_errors functionality
    def add_callback_with_timestamp(
        self,
        topic: str,
        message_type: type[protocol.Tachyon[Any]] | None,
        callback: Callable[[LogTimestamp, object], None],
        suppress_errors: bool = False,
    ) -> None:
        """Add a callback for a deserialized message with timestamp.

        Args:
            topic: Topic name
            message_type: Message type
            callback: Callback function
            suppress_errors: Suppress deserialization errors
        """
        topic_metadata = self.try_get_topic_metadata(topic)
        if not topic_metadata:
            _handle_error(f"No topic metadata defined for channel: {topic}", True)
        elif topic_metadata.message_encoding != "tachyon":
            _handle_error(
                f"Unrecognized message encoding {topic_metadata.message_encoding} on channel {topic}", suppress_errors
            )
        elif not message_type:
            tachyon_metadata = tachyon_meta.get_metadata_from_protobuf(topic_metadata.schema_definition)
            generated_type, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
                self.compiler_context, topic_metadata.type, tachyon_metadata
            )
            self._add_validated_pytachyon_callback(topic, generated_type, callback, suppress_errors)
        elif hasattr(message_type, "deserialize_tachyon"):
            tachyon_metadata = tachyon_meta.get_metadata_from_protobuf(topic_metadata.schema_definition)
            if tachyon_metadata.types:
                compiler_context = self.compiler_context
                if hasattr(message_type, "get_tachyon_compiler_context"):
                    # create_deserializer needs to use the same context that created the
                    # message it's upgrading, if upgrade is needed.
                    compiler_context = cast("Any", message_type).get_tachyon_compiler_context()
                try:
                    _, deserialize = compatibility.create_deserializer(
                        compiler_context, message_type, tachyon_metadata, topic_metadata.type
                    )
                except RuntimeError as e:
                    _handle_error(str(e), suppress_errors)
                    return
                except ValueError as e:
                    _handle_error(str(e), suppress_errors)
                    return
                except TypeError as e:
                    _handle_error(str(e), suppress_errors)
                    return
                self._add_deserializer_callback(topic, deserialize, callback, suppress_errors)
            else:
                self._add_validated_pytachyon_callback(topic, message_type, callback, suppress_errors)
        else:
            _handle_error(f"Unsupported message type {type(message_type)} on channel {topic}", suppress_errors)

    def _add_validated_pytachyon_callback(
        self,
        topic: str,
        message_type: type[protocol.TachyClass],
        callback: Callable[[LogTimestamp, object], None],
        suppress_errors: bool = False,
    ) -> None:
        ignored_topics: set[str] = set()

        def cb_fun(logged_msg: LoggedMessage) -> None:
            if logged_msg.topic in ignored_topics:
                return

            try:
                deserialized_msg = message_type.deserialize_tachyon(memoryview(logged_msg.data))
            except (RuntimeError, struct.error, TypeError):
                if not suppress_errors:
                    raise

                _logger.warning(
                    "LogProcessor failed to deserialize message, callback for topic %s will be removed.",
                    logged_msg.topic,
                )
                ignored_topics.add(logged_msg.topic)
            else:
                callback(logged_msg.publish_time, deserialized_msg)

        self.add_raw_msg_callback(topic, cb_fun)

    def _add_deserializer_callback(
        self,
        topic: str,
        deserialize: Callable[[memoryview], Any],
        callback: Callable[[LogTimestamp, object], None],
        suppress_errors: bool = False,
    ) -> None:
        ignored_topics: set[str] = set()

        def cb_fun(logged_msg: LoggedMessage) -> None:
            if logged_msg.topic in ignored_topics:
                return

            try:
                deserialized_msg = deserialize(memoryview(logged_msg.data))
            except (RuntimeError, struct.error, TypeError):
                if not suppress_errors:
                    raise

                _logger.warning(
                    "LogProcessor failed to deserialize message, callback for topic %s will be removed.",
                    logged_msg.topic,
                )
                ignored_topics.add(logged_msg.topic)
            else:
                callback(logged_msg.publish_time, deserialized_msg)

        self.add_raw_msg_callback(topic, cb_fun)
