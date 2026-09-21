# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Log reader python wrapper."""

import tempfile
from pathlib import Path
from typing import ClassVar

import pytest
from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_types import LogTimestamp
from clockwork.logging.readers.py_log_reader import LogReader, SerializedMessage
from clockwork.logging.tests.support import test_message_clk_py


class TestLogReaderClass:
    """Log reader test class with clockwork python binding."""

    channel_names: ClassVar[list[str]] = ["channel1", "channel2", "channel3"]
    start_time: ClassVar[LogTimestamp] = LogTimestamp(1000)
    message_interval: ClassVar[LogTimestamp] = LogTimestamp(1000)

    def test_serialized_message_bytes_are_lazy(self) -> None:
        """Raw bytes are available before decoding and unavailable afterward."""
        decode_counter = [0]

        def deserialize(payload: memoryview) -> object:
            decode_counter[0] += 1
            assert payload.tobytes() == b"serialized"
            return "decoded"

        message = SerializedMessage(
            topic="channel1",
            sequence_number=0,
            log_time=LogTimestamp(1),
            publish_time=LogTimestamp(2),
            deserialize=deserialize,
            _data=b"serialized",
        )

        assert message.serialized_bytes == b"serialized"
        assert decode_counter == [0]
        assert message.message == "decoded"
        assert decode_counter == [1]

        with pytest.raises(
            RuntimeError,
            match=r"Serialized message bytes are unavailable after deserialization\.",
        ):
            _ = message.serialized_bytes

    def _write_test_log(self, log_path: str) -> list[str]:
        test_writer = LogWriter()
        test_writer.open(log_path, "")
        for channel_name in self.channel_names:
            test_writer.create_tachyon_channel(channel_name, test_message_clk_py.TestMessage)
        message_time = self.start_time.nanoseconds
        published_messages: list[str] = []
        for sequence_number in range(100):
            for channel_number in range(len(self.channel_names)):
                message_string = f"{self.channel_names[channel_number]} {sequence_number}"
                test_msg = test_message_clk_py.TestMessage(message_string=message_string)
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
                reader.add_topic(channel, test_message_clk_py.TestMessage)

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

    def test_message_iterator_with_single_sequence_number(self) -> None:
        """Test reading messages with one sequence number or singleton collection."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            self._write_test_log(log_path)
            for sequence_numbers in ({"channel1": 42}, {"channel1": {42}}):
                reader = LogReader(log_path)
                for channel in self.channel_names:
                    reader.add_topic(channel, test_message_clk_py.TestMessage)

                messages = list(reader.messages(sequence_numbers=sequence_numbers))

                assert [message.sequence_number for message in messages] == [42]
                assert [message.message.message_string for message in messages] == ["channel1 42"]

    def test_message_iterator_with_sequence_numbers(self) -> None:
        """Test reading messages with multiple sequence numbers."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            self._write_test_log(log_path)
            reader = LogReader(log_path)
            for channel in self.channel_names:
                reader.add_topic(channel, test_message_clk_py.TestMessage)

            messages = list(reader.messages(sequence_numbers={"channel1": {3, 7}, "channel2": 7}))

            assert [message.sequence_number for message in messages] == [3, 7, 7]
            assert [message.message.message_string for message in messages] == [
                "channel1 3",
                "channel1 7",
                "channel2 7",
            ]
