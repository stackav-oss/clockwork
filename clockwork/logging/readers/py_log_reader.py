# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python log reader library."""

import logging
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from functools import cached_property
from typing import Any, Final, cast

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.logging.readers.nb_log_reader import (
    LogReader as _NBLogReader,
)
from clockwork.logging.readers.nb_types import (
    LoggedMessage,
    LogInterval,
    LogTimestamp,
    RelativeInterval,
)
from clockwork.serialization.metadata import tachyon as tachyon_meta
from clockwork.serialization.py import compatibility, protocol, tachyon_dyn_from_metadata

_logger: Final = logging.getLogger(__name__)


def _handle_error(msg: str, suppress_errors: bool) -> None:
    if not suppress_errors:
        raise ValueError(msg)
    _logger.warning(msg)


@dataclass
class SerializedMessage:
    """Serialized log message."""

    topic: str
    sequence_number: int
    log_time: LogTimestamp
    publish_time: LogTimestamp
    deserialize: Callable[[memoryview], Any]
    _data: bytes

    @cached_property
    def message(self) -> Any:  # noqa: ANN401 (Message type is unknown)
        """Deserialization is done lazily on first access."""
        try:
            message = self.deserialize(memoryview(self._data))
        except RuntimeError as e:
            error_msg = f"Failed to deserialize message for topic '{self.topic}'"
            raise RuntimeError(error_msg) from e
        finally:
            del self._data
        return message


class DeserializingIterator:
    """Implements a deserializing iterator for logged messages."""

    def __init__(
        self,
        raw_iter: Iterator[LoggedMessage],
        topic_cb_map: dict[str, Callable[[memoryview], Any]],
    ) -> None:
        """Construct a deserializing iterator.

        Arguments:
          raw_iter: Raw logged message iterator.
          topic_cb_map: Map from topic name to message deserializer callback.
        """
        self.raw_iter = raw_iter
        self.topic_cb_map = topic_cb_map

    def __iter__(self) -> Iterator[SerializedMessage]:
        """Get this iterator."""
        return self

    def __next__(self) -> SerializedMessage:
        """Advance to the next message."""
        msg = next(self.raw_iter)
        return SerializedMessage(
            topic=msg.topic,
            sequence_number=msg.sequence_number,
            log_time=msg.log_time,
            publish_time=msg.publish_time,
            deserialize=self.topic_cb_map[msg.topic],
            _data=msg.data,
        )


class LogReader(_NBLogReader):
    """Implements deserializing iterator for the LogReader nanobind wrapper."""

    def __init__(
        self,
        log_uri: str,
        maybe_log_interval: LogInterval | None = None,
        maybe_relative_interval: RelativeInterval | None = None,
        compiler_context: CompilerContext | None = None,
    ) -> None:
        """Construct a new LogReader.

        Arguments:
            log_uri: Log URI
            maybe_log_interval: Log interval to read or None to read entire log
            maybe_relative_interval: Relative log interval to read or None to read entire log
            compiler_context: Optional compiler context for deserialization
        """
        super().__init__(log_uri, maybe_log_interval, maybe_relative_interval)
        self.topic_cb_map: dict[str, Callable[[memoryview], Any]] = {}
        self.compiler_context = compiler_context if compiler_context else CompilerContext()

    def add_topic(self, topic: str, message_type: type[protocol.Tachyon[Any]] | None = None) -> None:
        """Add a topic to be read.

        Arguments:
            topic: Topic name
            message_type: Message type or None for automatic wrapper generation.
        """
        topic_metadata = self.try_get_topic_metadata(topic)
        if not topic_metadata:
            _handle_error(f"No topic metadata defined for channel: {topic}", True)
            return
        if topic_metadata.message_encoding != "tachyon":
            _handle_error(
                f"Unrecognized message encoding {topic_metadata.message_encoding} on channel {topic}",
                False,
            )
            return
        if message_type:
            if not hasattr(message_type, "deserialize_tachyon"):
                _handle_error(f"Unsupported message type {type(message_type)} on channel {topic}", False)
                return
            tachyon_metadata = tachyon_meta.get_metadata_from_protobuf(topic_metadata.schema_definition)
            if tachyon_metadata.types:
                compiler_context = self.compiler_context
                if hasattr(message_type, "get_tachyon_compiler_context"):
                    # create_deserializer needs to use the same context that created the
                    # message it's upgrading, if upgrade is needed.
                    compiler_context = cast("Any", message_type).get_tachyon_compiler_context()
                try:
                    _, deserializer = compatibility.create_deserializer(
                        compiler_context, message_type, tachyon_metadata, topic_metadata.type
                    )
                except RuntimeError as e:
                    _handle_error(str(e), False)
                    return
                except ValueError as e:
                    _handle_error(str(e), False)
                    return
                except TypeError as e:
                    _handle_error(str(e), False)
                    return
                self.topic_cb_map[topic] = deserializer
            else:
                self.topic_cb_map[topic] = message_type.deserialize_tachyon
        else:
            tachyon_metadata = tachyon_meta.get_metadata_from_protobuf(topic_metadata.schema_definition)
            tachyon_type, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
                self.compiler_context, topic_metadata.type, tachyon_metadata
            )
            self.topic_cb_map[topic] = tachyon_type.deserialize_tachyon

    def messages(self) -> DeserializingIterator:
        """Get an iterator for the messages in the log."""

        def topic_filter(topic: str) -> bool:
            return topic in self.topic_cb_map

        return DeserializingIterator(super().raw_messages(topic_filter), self.topic_cb_map)
