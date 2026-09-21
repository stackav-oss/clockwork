# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for logger configs."""

from uuid import uuid4

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.composition import logger_config
from clockwork.dsl.ir import compiler, importer, importer_registry


def _get_test_context() -> CompilerContext:
    """Create a test compiler context with an importer."""
    ctx = CompilerContext(name="test_logger_config")
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    registry = ctx[importer_registry.IMPORTER_REGISTRY_KEY]
    registry.importer = fs_importer
    return ctx


def test_logged_channel_config() -> None:
    ctx = _get_test_context()
    entities = logger_config.get_entities(ctx)

    config1 = entities.logged_channel_config(
        uuid=uuid4(),
        num_slots=1,
        message_size=2,
        channel_name="channelA",
        message_encoding=entities.message_encoding.tachyon,
        schema_name="schemaZ",
        schema_encoding=entities.schema_encoding.unspecified,
        schema_definition=[],
        channel_type=entities.channel_type.persistent,
    )
    buffer = bytearray(entities.logged_channel_config.get_tachyon_constraint().size)
    config1.serialize_tachyon(memoryview(buffer))
    config2 = entities.logged_channel_config.deserialize_tachyon(memoryview(bytes(buffer)))
    assert type(config1) is type(config2)
    assert type(config1.message_encoding) is type(config2.message_encoding)
    assert config1 == config2


def test_log_writer_config() -> None:
    ctx = _get_test_context()
    entities = logger_config.get_entities(ctx)

    config1 = entities.log_writer_config(
        channels=[
            entities.logged_channel_config(
                uuid=uuid4(),
                num_slots=1,
                message_size=2,
                channel_name=f"channelA{i}",
                message_encoding=entities.message_encoding.tachyon,
                schema_name=f"schemaZ{i}",
                schema_encoding=entities.schema_encoding.unspecified,
                schema_definition=[],
                channel_type=entities.channel_type.persistent,
            )
            for i in range(3)
        ],
    )
    buffer = bytearray(entities.log_writer_config.get_tachyon_constraint().size)
    config1.serialize_tachyon(memoryview(buffer))
    config2 = entities.log_writer_config.deserialize_tachyon(memoryview(bytes(buffer)))
    assert config1 == config2
