# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Log reader python wrapper."""

from clockwork.logging.readers.nb_types import (
    LoggedMessage,
    LoggedTopicMetrics,
    LogInterval,
    LogMetrics,
    LogReaderConfig,
    LogTimestamp,
    RelativeInterval,
    TopicMetadata,
)


def test_log_timestamp() -> None:
    """Test log timestamp binding."""
    time1 = 1
    time2 = 2

    timestamp1 = LogTimestamp(time1)
    timestamp2 = LogTimestamp(time2)

    assert timestamp1.nanoseconds == time1
    assert timestamp2.nanoseconds == time2

    timestamp1_2 = LogTimestamp(time1)
    timestamp2_2 = LogTimestamp(time2)

    assert timestamp1 == timestamp1_2
    assert timestamp1 != timestamp2_2
    assert timestamp2 != timestamp1_2
    assert timestamp2 == timestamp2_2

    assert timestamp1 == timestamp1_2
    assert timestamp1 != timestamp2_2
    assert timestamp2 != timestamp1_2
    assert timestamp2 == timestamp2_2

    assert not timestamp1 < timestamp1_2
    assert timestamp1 < timestamp2_2
    assert not timestamp2 < timestamp1_2
    assert not timestamp2 < timestamp2_2

    assert not timestamp1 > timestamp1_2
    assert not timestamp1 > timestamp2_2
    assert timestamp2 > timestamp1_2
    assert not timestamp2 > timestamp2_2

    assert timestamp1 <= timestamp1_2
    assert timestamp1 <= timestamp2_2
    assert not timestamp2 <= timestamp1_2
    assert timestamp2 <= timestamp2_2

    assert timestamp1 >= timestamp1_2
    assert not timestamp1 >= timestamp2_2
    assert timestamp2 >= timestamp1_2
    assert timestamp2 >= timestamp2_2


def test_log_interval() -> None:
    """Test log interval binding."""
    time1 = LogTimestamp(1)
    time2 = LogTimestamp(2)
    time3 = LogTimestamp(3)

    interval1 = LogInterval(time1)
    interval2 = LogInterval(time2, time3)

    assert interval1.start_timestamp == time1
    assert interval1.end_timestamp == time1

    assert interval2.start_timestamp == time2
    assert interval2.end_timestamp == time3

    interval1_2 = LogInterval(time1)
    interval2_2 = LogInterval(time2, time3)

    assert interval1 == interval1_2
    assert interval1 != interval2_2

    assert interval2 != interval1_2
    assert interval2 == interval2_2


def test_relative_interval() -> None:
    """Test relative interval binding."""
    time1 = 1
    time2 = 2
    time3 = 3

    interval1 = RelativeInterval(time1, time2)
    interval2 = RelativeInterval(time2, time3)

    assert interval1.start_offset == time1
    assert interval1.end_offset == time2
    assert interval2.start_offset == time2
    assert interval2.end_offset == time3

    interval1_2 = RelativeInterval(time1, time2)
    interval2_2 = RelativeInterval(time2, time3)

    assert interval1 == interval1_2
    assert interval1 != interval2_2
    assert interval2 != interval1_2
    assert interval2 == interval2_2

    assert interval1 == interval1_2
    assert interval1 != interval2_2
    assert interval2 != interval1_2
    assert interval2 == interval2_2


def test_log_reader_config() -> None:
    """Test log reader config binding."""
    uri1 = "log/uri/1"
    logtime1 = LogTimestamp(1)
    logtime2 = LogTimestamp(2)
    time3 = 3
    time4 = 4
    interval1 = LogInterval(logtime1, logtime2)
    relative_interval1 = RelativeInterval(time3, time4)

    def topic_callback(topic_name: str) -> bool:
        """Callback that only recognizes topics that start with '/'."""
        return topic_name.startswith("/")

    config1 = LogReaderConfig(uri1, interval1, topic_callback)
    assert config1
    assert config1.uri == uri1
    assert config1.interval
    assert config1.interval.start_timestamp == logtime1
    assert config1.interval.end_timestamp == logtime2
    assert config1.topic_filter("/topic")
    assert not config1.topic_filter("topic")

    config2 = LogReaderConfig(uri1, relative_interval1, topic_callback)
    assert config2
    assert config2.uri == uri1
    assert config2.relative_interval
    assert config2.relative_interval.start_offset == time3
    assert config2.relative_interval.end_offset == time4
    assert config2.topic_filter("/topic")
    assert not config2.topic_filter("topic")


def test_topic_metadata() -> None:
    """Test topic metadata binding."""
    name1 = "name1"
    type1 = "type1"
    message_encoding1 = "cdr"
    channel_type1 = "regular"
    schema_encoding1 = "ros2msg"
    schema_definition1 = b"schema_definition1"
    metadata1 = TopicMetadata(name1, type1, message_encoding1, channel_type1, schema_encoding1, schema_definition1)
    assert metadata1.name == name1
    assert metadata1.type == type1
    assert metadata1.message_encoding == message_encoding1
    assert metadata1.channel_type == channel_type1
    assert metadata1.schema_encoding == schema_encoding1
    assert metadata1.schema_definition == schema_definition1
    assert metadata1 == TopicMetadata(
        name1,
        type1,
        message_encoding1,
        channel_type1,
        schema_encoding1,
        schema_definition1,
    )
    name2 = "name2"
    assert metadata1 != TopicMetadata(
        name2,
        type1,
        message_encoding1,
        channel_type1,
        schema_encoding1,
        schema_definition1,
    )
    type2 = "type2"
    assert metadata1 != TopicMetadata(
        name1,
        type2,
        message_encoding1,
        channel_type1,
        schema_encoding1,
        schema_definition1,
    )
    message_encoding2 = "undefined"
    assert metadata1 != TopicMetadata(
        name1,
        type1,
        message_encoding2,
        channel_type1,
        schema_encoding1,
        schema_definition1,
    )
    channel_type2 = "persistent"
    assert metadata1 != TopicMetadata(
        name1,
        type1,
        message_encoding2,
        channel_type2,
        schema_encoding1,
        schema_definition1,
    )
    schema_encoding2 = "ros2idl"
    assert metadata1 != TopicMetadata(
        name1,
        type1,
        message_encoding1,
        channel_type1,
        schema_encoding2,
        schema_definition1,
    )
    schema_definition2 = b"schema_definition2"
    assert metadata1 != TopicMetadata(
        name1,
        type1,
        message_encoding1,
        channel_type1,
        schema_encoding1,
        schema_definition2,
    )


def test_logged_message() -> None:
    """Test logged message binding."""
    topic1 = "topic1"
    sequence_number1 = 1
    publish_time1 = LogTimestamp(1)
    log_time1 = LogTimestamp(10)
    header1 = b"hello"
    data1 = b"world"
    is_repeated_persistent1 = False
    message_encoding1 = "cdr"
    message1 = LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header1, data1, is_repeated_persistent1, message_encoding1
    )
    assert message1.topic == topic1
    assert message1.sequence_number == sequence_number1
    assert message1.publish_time == publish_time1
    assert message1.log_time == log_time1
    assert message1.header == header1
    assert message1.data == data1
    assert message1.is_repeated_persistent == is_repeated_persistent1
    assert message1.message_encoding == message_encoding1

    assert message1 == LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header1, data1, is_repeated_persistent1, message_encoding1
    )

    topic2 = "topic2"
    assert message1 != LoggedMessage(
        topic2, sequence_number1, publish_time1, log_time1, header1, data1, is_repeated_persistent1, message_encoding1
    )
    sequence_number2 = 2
    assert message1 != LoggedMessage(
        topic2, sequence_number2, publish_time1, log_time1, header1, data1, is_repeated_persistent1, message_encoding1
    )
    publish_time2 = LogTimestamp(20)
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time2, log_time1, header1, data1, is_repeated_persistent1, message_encoding1
    )
    log_time2 = LogTimestamp(2)
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time2, header1, data1, is_repeated_persistent1, message_encoding1
    )
    header2 = b"HELLO"
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header2, data1, is_repeated_persistent1, message_encoding1
    )
    data2 = b"WORLD"
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header1, data2, is_repeated_persistent1, message_encoding1
    )
    is_repeated_persistent2 = True
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header1, data1, is_repeated_persistent2, message_encoding1
    )
    message_encoding2 = "tachyon"
    assert message1 != LoggedMessage(
        topic1, sequence_number1, publish_time1, log_time1, header1, data1, is_repeated_persistent1, message_encoding2
    )


def test_logged_topic_metrics() -> None:
    """Test logged topic metrics binding."""
    topic1 = "topic1"
    start_time1 = LogTimestamp(10)
    end_time1 = LogTimestamp(11)
    interval1 = LogInterval(start_time1, end_time1)
    message_count1 = 100
    byte_count1 = 1000
    metrics1 = LoggedTopicMetrics(topic1, interval1, message_count1, byte_count1)
    assert metrics1.topic == topic1
    assert metrics1.transmit_time_interval == interval1
    assert metrics1.message_count == message_count1
    assert metrics1.byte_count == byte_count1

    assert metrics1 == LoggedTopicMetrics(topic1, interval1, message_count1, byte_count1)

    topic2 = "topic2"
    assert metrics1 != LoggedTopicMetrics(topic2, interval1, message_count1, byte_count1)
    interval2 = LogInterval(end_time1)
    assert metrics1 != LoggedTopicMetrics(topic1, interval2, message_count1, byte_count1)
    message_count2 = 200
    assert metrics1 != LoggedTopicMetrics(topic1, interval1, message_count2, byte_count1)
    byte_count2 = 2000
    assert metrics1 != LoggedTopicMetrics(topic1, interval1, message_count1, byte_count2)


def test_log_metrics() -> None:
    """Test log metrics binding."""
    topic1 = "topic1"
    start_time1 = LogTimestamp(10)
    end_time1 = LogTimestamp(11)
    interval1 = LogInterval(start_time1, end_time1)
    message_count1 = 100
    byte_count1 = 1000
    topic_metrics1 = LoggedTopicMetrics(topic1, interval1, message_count1, byte_count1)
    topic2 = "topic2"
    start_time2 = LogTimestamp(20)
    end_time2 = LogTimestamp(21)
    interval2 = LogInterval(start_time2, end_time2)
    message_count2 = 200
    byte_count2 = 2000
    topic_metrics2 = LoggedTopicMetrics(topic2, interval2, message_count2, byte_count2)
    metrics1 = LogMetrics(
        LogInterval(start_time1, end_time2),
        message_count1 + message_count2,
        byte_count1 + byte_count2,
        [topic_metrics1, topic_metrics2],
    )

    assert metrics1.transmit_time_interval == LogInterval(start_time1, end_time2)
    assert metrics1.message_count == message_count1 + message_count2
    assert metrics1.byte_count == byte_count1 + byte_count2
    assert metrics1.topic_metrics == [topic_metrics1, topic_metrics2]

    assert metrics1 == LogMetrics(
        LogInterval(start_time1, end_time2),
        message_count1 + message_count2,
        byte_count1 + byte_count2,
        [topic_metrics1, topic_metrics2],
    )
    assert metrics1 != LogMetrics(
        LogInterval(start_time2, end_time2),
        message_count1 + message_count2,
        byte_count1 + byte_count2,
        [topic_metrics1, topic_metrics2],
    )
    assert metrics1 != LogMetrics(
        LogInterval(start_time1, end_time2),
        message_count2 + message_count2,
        byte_count1 + byte_count2,
        [topic_metrics1, topic_metrics2],
    )
    assert metrics1 != LogMetrics(
        LogInterval(start_time1, end_time2),
        message_count1 + message_count2,
        byte_count2 + byte_count2,
        [topic_metrics1, topic_metrics2],
    )
    assert metrics1 != LogMetrics(
        LogInterval(start_time1, end_time2),
        message_count1 + message_count2,
        byte_count1 + byte_count2,
        [topic_metrics2, topic_metrics1],
    )
