# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Log reader python wrapper."""

import tempfile
from pathlib import Path
from typing import ClassVar

from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_types import LogTimestamp
from clockwork.logging.readers.py_log_reader import LogReader

# gazelle:ignore clockwork.logging.tests.support.py_test_message
from clockwork.logging.tests.support import py_test_message


class TestLogReaderClass:
    """Log reader test class with clockwork python binding."""

    channel_names: ClassVar[list[str]] = ["channel1", "channel2", "channel3"]
    start_time: ClassVar[LogTimestamp] = LogTimestamp(1000)
    message_interval: ClassVar[LogTimestamp] = LogTimestamp(1000)

    def _write_test_log(self, log_path: str) -> list[str]:
        test_writer = LogWriter()
        test_writer.open(log_path, "")
        for channel_name in self.channel_names:
            test_writer.create_tachyon_channel(channel_name, py_test_message.TestMessage)
        message_time = self.start_time.nanoseconds
        published_messages: list[str] = []
        for sequence_number in range(100):
            for channel_number in range(len(self.channel_names)):
                message_string = f"{self.channel_names[channel_number]} {sequence_number}"
                test_msg = py_test_message.TestMessage(message_string=message_string)
                test_writer.write_tachyon(
                    self.channel_names[channel_number],
                    sequence_number,
                    LogTimestamp(message_time + 1),
                    LogTimestamp(message_time),
                    test_msg,
                )
                published_messages.append(message_string)
                message_time += self.message_interval.nanoseconds
        test_writer.close()
        return published_messages

    def test_message_iterator(self) -> None:
        """Test reading messages through the iterator."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path)

            reader = LogReader(log_path)

            def topic_filter(_: str) -> bool:  # pyright: ignore[reportUnusedFunction] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                return True

            for channel in self.channel_names:
                reader.add_topic(channel, py_test_message.TestMessage)

            for msg in reader.messages():
                assert msg.sequence_number == int(len(logged_messages) / len(self.channel_names))
                assert msg.log_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) + 1
                )
                assert msg.publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                logged_messages.append(msg.message.message_string)

            assert logged_messages == published_messages

    def test_message_iterator_with_generated_wrapper(self) -> None:
        """Test reading messages through the iterator."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path)

            reader = LogReader(log_path)

            def topic_filter(_: str) -> bool:  # pyright: ignore[reportUnusedFunction] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                return True

            for channel in self.channel_names:
                reader.add_topic(channel)

            for msg in reader.messages():
                assert msg.sequence_number == int(len(logged_messages) / len(self.channel_names))
                assert msg.log_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) + 1
                )
                assert msg.publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                logged_messages.append(msg.message.message_string)

            assert logged_messages == published_messages
