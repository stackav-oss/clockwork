"""Python type implementations for log reader."""

# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

from clockwork.logging.readers.nb_types import LoggedMessage, LogTimestamp


class OwningLoggedMessage(LoggedMessage):
    """Subclass of LoggedMessage that manual keeps a reference to the underlying data buffer.

    LoggedMessage itself being nano bind does not keep a reference to the underlying data attr.
    This can result in python gc freeing that memory causing corruption in messages.

    This class is simply a wrapper on top of LoggedMessage that keeps a reference as a private
    variable to ensure that it is not gc-ed until the message itself is out of scope.
    """

    def __init__(  # noqa: PLR0913 Inputs needed to initialize all of the class's members
        self,
        topic: str,
        sequence_number: int,
        publish_time: LogTimestamp,
        log_time: LogTimestamp,
        header: bytes,
        data: bytes,
        is_repeated_persistent: bool,
        message_encoding: str,
    ) -> None:
        """Create instance of OwningLoggedMessage for keeping ref to data buffer."""
        self.__buffer = data
        self.__header = header
        self.__topic = topic
        super().__init__(
            self.__topic,
            sequence_number,
            publish_time,
            log_time,
            self.__header,
            self.__buffer,
            is_repeated_persistent,
            message_encoding,
        )
