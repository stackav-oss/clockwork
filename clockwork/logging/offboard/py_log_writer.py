# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python log reader library."""

from typing import Any

from clockwork.logging.offboard.nb_log_writer_impl import (
    LogWriter as _LogWriterNanobindWrapper,
)
from clockwork.logging.readers.nb_types import LogTimestamp
from clockwork.serialization.metadata import tachyon as tachyon_meta
from clockwork.serialization.py import protocol


class LogWriter(_LogWriterNanobindWrapper):
    """Implements generated wrapper support for the offboard writer."""

    def __init__(self) -> None:
        """Initialize the log writer nanobind wrapper."""
        super().__init__()

    def create_tachyon_channel(
        self,
        channel_name: str,
        message_type: type[protocol.Tachyon[Any]],
    ) -> None:
        """Add a channel to the log using the tachyon message type to generate the log metadata.

        Args:
            channel_name: Channel name
            message_type: Message type
        """
        message_type_metadata = message_type.get_tachyon_metadata()
        message_type_name = message_type.get_tachyon_metadata_name()
        if message_type_metadata is None or message_type_name is None:
            msg = f"Provided Tachyon message class has no metadata on channel {channel_name}"
            raise ValueError(msg)
        schema_definition = tachyon_meta.to_protobuf(message_type_metadata).SerializeToString()

        super().create_channel(
            channel_name,
            "tachyon",
            "regular",
            message_type_name,
            "clockwork_tachyon",
            schema_definition,
        )

    def write_tachyon(
        self,
        channel_name: str,
        sequence_number: int,
        log_time: LogTimestamp,
        transmit_time: LogTimestamp,
        message: protocol.Tachyon[Any],
    ) -> None:
        """Write a tachyon message to the log.

        Args:
            channel_name: Channel name
            sequence_number: Sequence number
            log_time: Message log timestamp
            transmit_time: Message transmit timestamp
            message: Message to write
        """
        buffer = bytearray(message.get_tachyon_constraint().size)
        message.serialize_tachyon(memoryview(buffer))
        super().write(channel_name, sequence_number, log_time, transmit_time, b"", memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
