# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Python log writer."""

import random
import string
import tempfile
from pathlib import Path

import clockwork.logging.readers.py_log_processor as py_reader
from clockwork.logging.offboard.nb_types import (
    LoggedChannelMetadata,
)
from clockwork.logging.offboard.nb_types import (
    LoggedMessage as OffboardLoggedMessage,
)
from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_types import (
    LoggedMessage as ReaderLoggedMessage,
)
from clockwork.logging.readers.nb_types import (
    LogReaderConfig,
    LogTimestamp,
)
from clockwork.logging.tests.support.py_test_message import TestMessage as PyTestMessage
from clockwork.logging.tests.support.test_message_clk_nb import TestMessage as NbTestMessage
from clockwork.serialization.metadata import tachyon as tachyon_meta


def test_open_close() -> None:
    """Test open and closing log directory."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        log_path = str(Path(test_dir_name) / "test_log")
        writer = LogWriter()
        writer.open(log_path, "")
        writer.close()
        assert Path(log_path).exists()
        assert Path(log_path).is_dir()
        assert {path.name for path in Path(log_path).iterdir()} == {"stack_log_metadata.pbtxt"}


def test_legacy_create_channel() -> None:
    """Test create channel in log directory using the legacy interface."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        log_path = str(Path(test_dir_name) / "test_log")
        writer = LogWriter()
        writer.open(log_path, "")

        # Similar to py_log_reader_test and internal write_test_log function
        writer.create_channel(
            "channel1",
            "undefined",
            "regular",
            "schema1",
            "undefined",
            b"Schema definition 1",
        )
        writer.create_channel_from_metadata(
            LoggedChannelMetadata(
                "channel2",
                "undefined",
                "regular",
                "schema2",
                "undefined",
                b"Schema definition 2",
            )
        )
        writer.close()

        assert {path.name for path in Path(log_path).iterdir()} == {
            "stack_log_metadata.pbtxt",
            "other_channels_0.slog",
        }

        config = LogReaderConfig(log_path)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 0

        metadata1 = processor.try_get_topic_metadata("channel1")
        assert metadata1 is not None
        assert metadata1.name == "channel1"
        assert metadata1.type == "schema1"
        assert metadata1.message_encoding == "undefined"
        assert metadata1.schema_encoding == "undefined"
        assert metadata1.schema_definition == b"Schema definition 1"

        metadata2 = processor.try_get_topic_metadata("channel2")
        assert metadata2 is not None
        assert metadata2.name == "channel2"
        assert metadata2.type == "schema2"
        assert metadata2.message_encoding == "undefined"
        assert metadata2.schema_encoding == "undefined"
        assert metadata2.schema_definition == b"Schema definition 2"


def test_legacy_write() -> None:
    """Actually test writing messages to the log using the legacy interface."""
    total_messages = 100
    channel1_header_size, channel2_header_size = 10, 20
    channel1_data_size, channel2_data_size = 150, 99

    messages = []

    with tempfile.TemporaryDirectory() as test_dir_name:
        log_path = str(Path(test_dir_name) / "test_log")
        writer = LogWriter()
        writer.open(log_path, "")

        # Similar to py_log_reader_test and internal write_test_log function
        writer.create_channel(
            "channel1",
            "undefined",
            "regular",
            "schema1",
            "undefined",
            b"Schema definition 1",
        )
        writer.create_channel_from_metadata(
            LoggedChannelMetadata(
                "channel2",
                "undefined",
                "regular",
                "schema2",
                "undefined",
                b"Schema definition 2",
            )
        )

        for message_num in range(total_messages):
            channel_name = "channel1" if random.random() < 0.5 else "channel2"  # noqa: S311 (not used for crypto)
            if channel_name == "channel1":
                header = random.randbytes(channel1_header_size)  # noqa: S311 (not used for crypto)
                data = random.randbytes(channel1_data_size)  # noqa: S311 (not used for crypto)
            else:
                header = random.randbytes(channel2_header_size)  # noqa: S311 (not used for crypto)
                data = random.randbytes(channel2_data_size)  # noqa: S311 (not used for crypto)
            log_time = LogTimestamp(int(0.1 * 1e9))
            transmit_time = LogTimestamp(int(0.1 * 1e9) + 1)
            message = [channel_name, message_num, log_time, transmit_time, header, data]
            messages.append(message)
            if channel_name == "channel1":
                writer.write(channel_name, message_num, log_time, transmit_time, header, data)
            else:
                writer.write_logged_message(
                    OffboardLoggedMessage(
                        channel_name,
                        message_num,
                        log_time,
                        transmit_time,
                        memoryview(header),
                        memoryview(data),
                        False,
                        "tachyon",
                        False,
                    )
                )

        writer.close()

        config = LogReaderConfig(log_path)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == total_messages
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]

        def callback(saved_msg: ReaderLoggedMessage) -> None:
            msg = messages[saved_msg.sequence_number]
            topic, sequence_number, log_time, publish_time, header, data = msg
            assert topic == saved_msg.topic
            assert sequence_number == saved_msg.sequence_number
            assert publish_time == saved_msg.publish_time
            assert log_time == saved_msg.log_time
            assert header == saved_msg.header
            assert data == saved_msg.data

        processor.add_raw_msg_callback("channel1", callback)
        processor.add_raw_msg_callback("channel2", callback)
        processor.process()


def test_create_tachyon_channel() -> None:
    """Test create channel in log directory using a generated tachyon type."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        log_path = str(Path(test_dir_name) / "test_log")
        writer = LogWriter()
        writer.open(log_path, "")

        writer.create_tachyon_channel(
            "channel1",
            PyTestMessage,
        )
        writer.create_tachyon_channel(
            "channel2",
            NbTestMessage,  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        )
        writer.close()

        assert {path.name for path in Path(log_path).iterdir()} == {
            "stack_log_metadata.pbtxt",
            "other_channels_0.slog",
        }

        config = LogReaderConfig(log_path)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 0

        assert PyTestMessage.get_tachyon_metadata_name() == NbTestMessage.get_tachyon_metadata_name()
        assert PyTestMessage.get_tachyon_metadata() == NbTestMessage.get_tachyon_metadata()

        metadata1 = processor.try_get_topic_metadata("channel1")
        assert metadata1 is not None
        assert metadata1.name == "channel1"
        assert metadata1.type == PyTestMessage.get_tachyon_metadata_name()
        assert metadata1.message_encoding == "tachyon"
        assert metadata1.schema_encoding == "clockwork_tachyon"
        assert (
            metadata1.schema_definition
            == tachyon_meta.to_protobuf(PyTestMessage.get_tachyon_metadata()).SerializeToString()
        )

        metadata2 = processor.try_get_topic_metadata("channel2")
        assert metadata2 is not None
        assert metadata2.name == "channel2"
        assert metadata2.type == NbTestMessage.get_tachyon_metadata_name()
        assert metadata2.message_encoding == "tachyon"
        assert metadata2.schema_encoding == "clockwork_tachyon"
        assert (
            metadata2.schema_definition
            == tachyon_meta.to_protobuf(NbTestMessage.get_tachyon_metadata()).SerializeToString()  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        )


def test_tachyon_write() -> None:
    """Actually test writing messages to the log using generated tachyon types."""
    total_messages = 100
    letters = string.ascii_lowercase

    messages = []

    with tempfile.TemporaryDirectory() as test_dir_name:
        log_path = str(Path(test_dir_name) / "test_log")
        writer = LogWriter()
        writer.open(log_path, "")

        writer.create_tachyon_channel(
            "channel1",
            PyTestMessage,
        )
        writer.create_tachyon_channel(
            "channel2",
            NbTestMessage,  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        )

        for message_num in range(total_messages):
            channel_name = "channel1" if random.random() < 0.5 else "channel2"  # noqa: S311 (not used for crypto)
            message_string = "".join(random.choice(letters) for i in range(239))  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: S311 (not used for crypto)
            if channel_name == "channel1":
                message = PyTestMessage(message_string=message_string)
            else:
                message = NbTestMessage()
                message.message_string = message_string
            log_time = LogTimestamp(message_num + 1000000)
            transmit_time = LogTimestamp(message_num + 2000000)
            messages.append(message)
            writer.write_tachyon(channel_name, message_num, log_time, transmit_time, message)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        writer.close()

        config = LogReaderConfig(log_path)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == total_messages
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]

        def callback(publish_time: LogTimestamp, message: object) -> None:
            message_num = publish_time.nanoseconds - 2000000
            expected_message = messages[message_num]
            assert isinstance(message, PyTestMessage | NbTestMessage)
            assert message.message_string == expected_message.message_string  # pyright: ignore[reportAttributeAccessIssue] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        # Callback message types are intentionally switched
        processor.add_callback_with_timestamp("channel1", NbTestMessage, callback)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        processor.add_callback_with_timestamp("channel2", PyTestMessage, callback)
        processor.process()
