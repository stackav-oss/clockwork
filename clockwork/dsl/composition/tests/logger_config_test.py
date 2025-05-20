# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit test for logger configs."""

from uuid import uuid4

from clockwork.dsl.composition import logger_config


def test_logged_channel_config() -> None:
    config1 = logger_config.LoggedChannelConfig(
        uuid=uuid4(),
        num_slots=1,
        message_size=2,
        channel_name="channelA",
        message_encoding=logger_config.MessageEncoding.tachyon,
        schema_name="schemaZ",
        schema_encoding=logger_config.SchemaEncoding.unspecified,
        schema_definition=[],
        channel_type=logger_config.ChannelType.persistent,
    )
    buffer = bytearray(logger_config.LoggedChannelConfig.get_tachyon_constraint().size)
    config1.serialize_tachyon(memoryview(buffer))
    config2 = logger_config.LoggedChannelConfig.deserialize_tachyon(memoryview(bytes(buffer)))
    assert config1 == config2


def test_log_writer_config() -> None:
    config1 = logger_config.LogWriterConfig(
        channels=[
            logger_config.LoggedChannelConfig(
                uuid=uuid4(),
                num_slots=1,
                message_size=2,
                channel_name=f"channelA{i}",
                message_encoding=logger_config.MessageEncoding.tachyon,
                schema_name=f"schemaZ{i}",
                schema_encoding=logger_config.SchemaEncoding.unspecified,
                schema_definition=[],
                channel_type=logger_config.ChannelType.persistent,
            )
            for i in range(3)
        ],
    )
    buffer = bytearray(logger_config.LogWriterConfig.get_tachyon_constraint().size)
    config1.serialize_tachyon(memoryview(buffer))
    config2 = logger_config.LogWriterConfig.deserialize_tachyon(memoryview(bytes(buffer)))
    assert config1 == config2
