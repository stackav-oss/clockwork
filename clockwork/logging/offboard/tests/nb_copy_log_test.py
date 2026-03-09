# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Python log writer."""

import tempfile
from pathlib import Path

import clockwork.logging.readers.py_log_processor as py_reader
from clockwork.logging.offboard.nb_copy_log import copy_log
from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.offboard.v1.log_union_pb2 import LogUnion
from clockwork.logging.readers.nb_types import (
    LogReaderConfig,
    LogTimestamp,
    RelativeInterval,
)
from clockwork.logging.tests.support.test_message_clk_py import TestMessage as PyTestMessage
from google.protobuf import text_format


def create_single_log(log_dir: str) -> None:
    """Create a single (non-union) test log."""
    writer = LogWriter()
    writer.open(log_dir, "")

    writer.create_tachyon_channel(
        "channel1",
        PyTestMessage,
    )
    writer.create_tachyon_channel(
        "channel2",
        PyTestMessage,
    )

    message = PyTestMessage(message_string="testing")
    writer.write_tachyon("channel1", 0, LogTimestamp(100000), LogTimestamp(100001), message)
    writer.write_tachyon("channel2", 1, LogTimestamp(200000), LogTimestamp(200001), message)
    writer.close()


def create_log_union(log_dir: str) -> None:
    """Create a log union test log."""
    log_path = Path(log_dir)
    source_log1 = str(log_path.parent / "source_log1")
    writer1 = LogWriter()
    writer1.open(source_log1, "")
    source_log2 = str(log_path.parent / "source_log2")
    writer2 = LogWriter()
    writer2.open(source_log2, "")

    writer1.create_tachyon_channel(
        "channel1",
        PyTestMessage,
    )
    writer2.create_tachyon_channel(
        "channel2",
        PyTestMessage,
    )

    message = PyTestMessage(message_string="testing")
    writer1.write_tachyon("channel1", 0, LogTimestamp(100000), LogTimestamp(100001), message)
    writer2.write_tachyon("channel2", 1, LogTimestamp(200000), LogTimestamp(200001), message)
    writer1.close()
    writer2.close()

    log_union_proto = LogUnion()
    entry1 = log_union_proto.log_union_entry.add()
    entry1.absolute_path = source_log1
    entry2 = log_union_proto.log_union_entry.add()
    entry2.absolute_path = source_log2
    log_path.mkdir()
    with (log_path / "stack_log_union.pbtxt").open("w") as f:
        text_format.PrintMessage(log_union_proto, f)


def test_copy_single_log() -> None:
    """Test copying an entire single (non-union) log."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 2
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]


def test_filter_single_log_by_time_range() -> None:
    """Test filtering a single log (non-union) log by time range."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        log_interval = RelativeInterval(start_offset=100000, end_offset=100000)
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_log_interval=log_interval)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_single_log_by_desired_channels() -> None:
    """Test filtering a single log (non-union) log by desired channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        desired_channels = ["channel2"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_desired_channels=desired_channels)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_single_log_by_excluded_channels() -> None:
    """Test filtering a single log (non-union) log by excluded channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        excluded_channels = ["channel1"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_excluded_channels=excluded_channels)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_single_log_by_desired_and_excluded_channels() -> None:
    """Test filtering a single log (non-union) log by desired and excluded channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        desired_channels = ["channel1", "channel2"]
        excluded_channels = ["channel1"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(
            source_log, dest_log, maybe_desired_channels=desired_channels, maybe_excluded_channels=excluded_channels
        )

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_no_deep_copy_has_no_effect_on_single_log() -> None:
    """Test copying with no_deep_copy has no effect on a single (non-union) log."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_single_log(source_log)

        dest_log_path = Path(test_dir_name) / "dest_log"
        dest_log = str(dest_log_path)
        copy_log(source_log, dest_log, no_deep_copy=True)

        assert (dest_log_path / "stack_log_metadata.pbtxt").exists()

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 2
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]


def test_copy_log_union() -> None:
    """Test copying an entire single log union."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 2
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]


def test_filter_log_union_by_time_range() -> None:
    """Test filtering a log union by time range."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        log_interval = RelativeInterval(start_offset=100000, end_offset=100000)
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_log_interval=log_interval)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_log_union_by_desired_channels() -> None:
    """Test filtering a log union by desired channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        desired_channels = ["channel2"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_desired_channels=desired_channels)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_log_union_by_excluded_channels() -> None:
    """Test filtering a log union by excluded channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        excluded_channels = ["channel1"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(source_log, dest_log, maybe_excluded_channels=excluded_channels)

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_filter_log_union_by_desired_and_excluded_channels() -> None:
    """Test filtering a log union by desired and excluded channels."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        desired_channels = ["channel1", "channel2"]
        excluded_channels = ["channel1"]
        dest_log = str(Path(test_dir_name) / "dest_log")
        copy_log(
            source_log, dest_log, maybe_desired_channels=desired_channels, maybe_excluded_channels=excluded_channels
        )

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 1
        assert metrics.topic_metrics[0].topic == "channel2"


def test_no_deep_copy_just_copies_metadata_for_log_unions() -> None:
    """Test that the no_deep_copy flag just copies stack_log_union.pbtxt."""
    with tempfile.TemporaryDirectory() as test_dir_name:
        source_log = str(Path(test_dir_name) / "source_log")
        create_log_union(source_log)

        dest_log_path = Path(test_dir_name) / "dest_log"
        dest_log = str(dest_log_path)
        copy_log(source_log, dest_log, no_deep_copy=True)

        assert (dest_log_path / "stack_log_union.pbtxt").exists()

        config = LogReaderConfig(dest_log)
        processor = py_reader.LogProcessor(config)
        metrics = processor.try_get_metrics()
        assert metrics
        assert metrics.message_count == 2
        assert [topic_metric.topic for topic_metric in metrics.topic_metrics] == ["channel1", "channel2"]
