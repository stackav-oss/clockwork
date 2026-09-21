# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Python log processor test."""

import tempfile
from pathlib import Path
from typing import ClassVar

import pytest
from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_types import (
    LoggedMessage,
    LogInterval,
    LogReaderConfig,
    LogTimestamp,
    TopicMetadata,
)
from clockwork.logging.readers.py_log_processor import LogProcessor
from clockwork.logging.tests.support import test_message_clk_nb, test_message_clk_py
from clockwork.serialization.metadata import tachyon as tachyon_meta


class TestLogProcessorClass:
    """Log processor test class with clockwork python binding."""

    channel_names: ClassVar[list[str]] = ["channel1", "channel2", "channel3"]
    start_time: ClassVar[LogTimestamp] = LogTimestamp(1000)
    message_interval: ClassVar[LogTimestamp] = LogTimestamp(1000)

    def _write_test_log(self, log_path: str, message_type: type) -> list[str]:
        test_writer = LogWriter()
        test_writer.open(log_path, "")
        for channel_name in self.channel_names:
            test_writer.create_tachyon_channel(channel_name, message_type)
        message_time = self.start_time.nanoseconds
        published_messages: list[str] = []
        for sequence_number in range(100):
            for channel_number in range(len(self.channel_names)):
                message_string = f"{self.channel_names[channel_number]} {sequence_number}"
                if message_type == test_message_clk_py.TestMessage:
                    test_msg = message_type(message_string=message_string)
                else:
                    test_msg = message_type()
                    test_msg.message_string = message_string
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

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_log_metadata(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test log metadata."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            self._write_test_log(log_path, message_type)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            topics = processor.topics
            expected_topics = set(self.channel_names)
            assert {t.name for t in topics} == expected_topics

            message_type_metadata = message_type.get_tachyon_metadata()
            message_type_name = message_type.get_tachyon_metadata_name()
            schema_definition = tachyon_meta.to_protobuf(message_type_metadata).SerializeToString()  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

            for channel_name in self.channel_names:
                maybe_metadata = processor.try_get_topic_metadata(channel_name)
                assert maybe_metadata
                assert maybe_metadata == TopicMetadata(
                    channel_name,
                    message_type_name,
                    "tachyon",
                    "regular",
                    "clockwork_tachyon",
                    schema_definition,
                )
            maybe_metadata = processor.try_get_topic_metadata("INVALID_TOPIC")
            assert not maybe_metadata

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_raw_msg_callbacks(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading with raw message callbacks."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def raw_msg_callback(msg: LoggedMessage) -> None:
                assert msg.sequence_number == int(len(logged_messages) / len(self.channel_names))
                assert msg.log_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages) + 1
                )
                assert msg.publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                assert len(msg.header) == 0
                deserialized_msg = message_type.deserialize_tachyon(memoryview(msg.data))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_raw_msg_callback(channel_name, raw_msg_callback)
            processor.process()
            assert logged_messages == published_messages

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_deserialized_msg_callbacks(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading deserialized messages."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def deserialized_msg_callback(deserialized_msg: test_message_clk_py.TestMessage) -> None:  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_callback(channel_name, message_type, deserialized_msg_callback)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            processor.process()
            assert logged_messages == published_messages

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_deserialized_msg_callbacks_with_abort(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading deserialized messages with abort."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def deserialized_msg_callback_with_abort(
                deserialized_msg: test_message_clk_py.TestMessage,  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            ) -> None:
                processor.abort()
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_callback(channel_name, message_type, deserialized_msg_callback_with_abort)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            processor.process()
            assert len(logged_messages) == 1
            assert logged_messages[0] == published_messages[0]

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_deserialized_msg_callbacks_with_timestamp(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading deserialized messages with timestamp."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def deserialized_msg_callback_with_timestamp(
                publish_time: LogTimestamp,
                deserialized_msg: test_message_clk_py.TestMessage,  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            ) -> None:
                assert publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_callback_with_timestamp(
                    channel_name,
                    message_type,  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
                    deserialized_msg_callback_with_timestamp,
                )
            processor.process()
            assert logged_messages == published_messages

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_auto_generated_callbacks(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading deserialized messages with auto generated wrapper."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def auto_msg_callback(
                deserialized_msg: test_message_clk_py.TestMessage | test_message_clk_nb.TestMessage,  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            ) -> None:
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_callback(channel_name, None, auto_msg_callback)

            processor.process()
            assert logged_messages == published_messages

    @pytest.mark.parametrize("message_type", [test_message_clk_py.TestMessage, test_message_clk_nb.TestMessage])
    def test_auto_generated_callbacks_with_timestamp(
        self,
        message_type: type[test_message_clk_py.TestMessage] | type[test_message_clk_nb.TestMessage],  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
    ) -> None:
        """Test reading deserialized messages with auto generated wrapper."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")
            logged_messages: list[str] = []
            published_messages = self._write_test_log(log_path, message_type)

            def auto_msg_callback_with_timestamp(
                publish_time: LogTimestamp,
                deserialized_msg: test_message_clk_py.TestMessage | test_message_clk_nb.TestMessage,  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            ) -> None:
                assert publish_time.nanoseconds == self.start_time.nanoseconds + (
                    self.message_interval.nanoseconds * len(logged_messages)
                )
                logged_messages.append(deserialized_msg.message_string)

            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            for channel_name in self.channel_names:
                processor.add_callback_with_timestamp(channel_name, None, auto_msg_callback_with_timestamp)

            processor.process()
            assert logged_messages == published_messages

    def test_log_processor_metrics(self) -> None:
        """Test log processor metrics."""
        with tempfile.TemporaryDirectory() as test_dir_name:
            log_path = str(Path(test_dir_name) / "test_log")

            published_messages = self._write_test_log(log_path, test_message_clk_nb.TestMessage)
            log_path = str(Path(test_dir_name) / "test_log")
            config = LogReaderConfig(log_path)
            processor = LogProcessor(config)
            metrics = processor.try_get_metrics()
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
                <= len(published_messages) * test_message_clk_nb.TestMessage.get_tachyon_constraint().size
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
