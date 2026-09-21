# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Log reader python wrapper."""

import tempfile
from pathlib import Path
from typing import ClassVar

from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_log_reader import LogReader, log_file_exists, read_log_file
from clockwork.logging.readers.nb_types import (
    LogInterval,
    LogTimestamp,
    TopicMetadata,
)
from clockwork.logging.tests.support import test_message_clk_py
from clockwork.serialization.metadata import tachyon as tachyon_meta


def test_read_log_file(tmp_path: Path) -> None:
    """Opaque files can be read through the log URI storage abstraction."""
    file_path = tmp_path / "sidecar.bin"
    contents = b"sidecar\x00contents"
    file_path.write_bytes(contents)

    assert log_file_exists(str(file_path))
    assert not log_file_exists(str(tmp_path / "missing.bin"))
    assert read_log_file(str(file_path)) == contents


class TestLogReaderClass:
    """Log reader test class with clockwork python binding."""

    channel_names: ClassVar[list[str]] = ["channel1", "channel2", "channel3"]
    start_time: ClassVar[LogTimestamp] = LogTimestamp(1000)
    message_interval: ClassVar[LogTimestamp] = LogTimestamp(1000)

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

    def test_log_metadata(self) -> None:
        """Test log metadata."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            self._write_test_log(log_path)

            reader = LogReader(log_path)
            metadata = reader.metadata
            expected_metadata = set(self.channel_names)
            assert {t.name for t in metadata} == expected_metadata

            message_type_metadata = test_message_clk_py.TestMessage.get_tachyon_metadata()
            message_type_name = test_message_clk_py.TestMessage.get_tachyon_metadata_name()
            schema_definition = tachyon_meta.to_protobuf(message_type_metadata).SerializeToString()

            for channel_name in self.channel_names:
                maybe_metadata = reader.try_get_topic_metadata(channel_name)
                assert maybe_metadata
                assert maybe_metadata == TopicMetadata(
                    channel_name,
                    message_type_name,
                    "tachyon",
                    "regular",
                    "clockwork_tachyon",
                    schema_definition,
                )
            maybe_metadata = reader.try_get_topic_metadata("INVALID_CHANNEL")
            assert not maybe_metadata

    def test_raw_message_iterator(self) -> None:
        """Test reading messages through the iterator."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path)

            reader = LogReader(log_path)

            def topic_filter(_: str) -> bool:
                return True

            for msg in reader.raw_messages(topic_filter):
                assert msg.sequence_number == int(len(logged_messages) / len(self.channel_names))
                assert msg.log_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) + 1
                )
                assert msg.publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                assert len(msg.header) == 0
                deserialized_msg = test_message_clk_py.TestMessage.deserialize_tachyon(memoryview(msg.data))
                logged_messages.append(deserialized_msg.message_string)

            assert logged_messages == published_messages

    def test_raw_message_iterator_with_filter(self) -> None:
        """Test reading messages through the iterator."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path)

            reader = LogReader(log_path)

            def topic_filter(channel_name: str) -> bool:
                return channel_name == self.channel_names[0]

            for msg in reader.raw_messages(topic_filter):
                assert msg.sequence_number == len(logged_messages)
                assert msg.log_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) * 3 + 1
                )
                assert msg.publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) * 3
                )
                assert len(msg.header) == 0
                deserialized_msg = test_message_clk_py.TestMessage.deserialize_tachyon(memoryview(msg.data))
                logged_messages.append(deserialized_msg.message_string)

            assert len(logged_messages) == len(published_messages) / 3
            assert logged_messages == published_messages[0::3]

    def test_log_reader_metrics(self) -> None:
        """Test log reader metrics."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")

            published_messages = self._write_test_log(log_path)
            log_path = str(Path(test_dir_name) / "test_log")
            reader = LogReader(log_path)
            metrics = reader.try_get_metrics()
            assert metrics
            assert metrics.transmit_time_interval == LogInterval(
                LogTimestamp(self.start_time.nanoseconds),
                LogTimestamp(
                    self.start_time.nanoseconds + self.message_interval.nanoseconds * (len(published_messages) - 1)
                ),
            )
            assert metrics.message_count == len(published_messages)
            assert (
                metrics.byte_count
                <= len(published_messages) * test_message_clk_py.TestMessage.get_tachyon_constraint().size
            )
            assert len(metrics.topic_metrics) == len(self.channel_names)
            for i in range(len(self.channel_names)):
                assert metrics.topic_metrics[i].topic == self.channel_names[i]
                assert metrics.topic_metrics[i].transmit_time_interval == LogInterval(
                    LogTimestamp(self.start_time.nanoseconds + (self.message_interval.nanoseconds * i)),
                    LogTimestamp(
                        self.start_time.nanoseconds
                        + self.message_interval.nanoseconds * (len(published_messages) - len(self.channel_names) + i)
                    ),
                )
                assert metrics.topic_metrics[i].message_count == len(published_messages) / len(self.channel_names)
                assert metrics.topic_metrics[i].byte_count <= metrics.byte_count / len(self.channel_names)
