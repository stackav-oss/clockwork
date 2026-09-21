# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for upgrade_lib module."""

from pathlib import Path

import pytest
from clockwork.dsl.ir import compiler, node, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.logging.offboard.py_log_writer import LogWriter
from clockwork.logging.readers.nb_types import LogTimestamp
from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.logging.tools.upgrade_log import upgrade_lib
from clockwork.serialization.py import tachyon_dyn


def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for tests."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def compile_test_schema_v1() -> tuple[node.Module, schema.InstantiatedSchema]:
    """Compile a test schema version 1."""
    schema_source = """
    // Test schema version 1
    schema TestMessage
    {
      uuid: 11112222-3333-4444-5555-666677778888;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // String field
        #2 message_string: VarString<max_size=64>;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestMessage;
        representation Tachyon<TestMessage>;
        interface Tappy<TestMessage>;
    }
    """
    module = compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "test_schema"), importer=fs_importer())
    schema_ir = module.inner_scope.lookup("TestMessage")
    assert isinstance(schema_ir, schema.Schema)
    schema_instance = schema.InstantiatedSchema.from_typespec(schema_ir)
    return module, schema_instance


def compile_test_schema_v2() -> tuple[node.Module, schema.InstantiatedSchema]:
    """Compile a test schema version 2."""
    schema_source = """
    // Test schema version 2
    schema TestMessage
    {
      uuid: 11112222-3333-4444-5555-666677778888;
      fields
      {
        // Integer field
        #1 integer_field: Int32;
        // String field
        #2 message_string: VarString<max_size=64>;
        // New float field
        #3 float_field: Float32 = 42.0;
      }
      history
      {
        version: 3;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestMessage;
        representation Tachyon<TestMessage>;
        interface Tappy<TestMessage>;
    }
    """
    module = compiler.compile_source_text(schema_source, ModuleID(CLK_REPO, "test_schema"), importer=fs_importer())
    schema_ir = module.inner_scope.lookup("TestMessage")
    assert isinstance(schema_ir, schema.Schema)
    schema_instance = schema.InstantiatedSchema.from_typespec(schema_ir)
    return module, schema_instance


CHANNEL_NAMES = ("channel1", "channel2")
START_TIME = LogTimestamp(1000)
MESSAGE_INTERVAL = LogTimestamp(1000)


def write_test_log_v1(log_path: str) -> list[tuple[str, int, str]]:
    """Write a test log with version 1 schema."""
    module, schema_v1 = compile_test_schema_v1()
    serdes = tachyon_dyn.serdes_for_type(module.context, schema_v1)
    message_class_v1 = serdes.type_

    test_writer = LogWriter()
    test_writer.open(log_path, "")

    for channel_name in CHANNEL_NAMES:
        test_writer.create_tachyon_channel(channel_name, message_class_v1)

    message_time = START_TIME.nanoseconds
    published_messages: list[tuple[str, int, str]] = []

    for sequence_number in range(10):
        for channel_name in CHANNEL_NAMES:
            message_string = f"{channel_name} {sequence_number}"
            test_msg = message_class_v1(integer_field=sequence_number, message_string=message_string)
            test_writer.write_tachyon(
                channel_name,
                sequence_number,
                LogTimestamp(message_time + 1),
                LogTimestamp(message_time),
                test_msg,
            )
            published_messages.append((channel_name, sequence_number, message_string))
            message_time += MESSAGE_INTERVAL.nanoseconds

    test_writer.close()
    return published_messages


def test_upgrade_single_channel(tmpdir: Path) -> None:
    """Test upgrading a single channel."""
    input_log_path = str(tmpdir / "input_log")
    output_log_path = str(tmpdir / "output_log")

    published_messages = write_test_log_v1(input_log_path)

    module_v2, schema_v2 = compile_test_schema_v2()
    context = module_v2.context

    reader = LogReader(input_log_path, compiler_context=context)
    writer = LogWriter()
    writer.open(output_log_path, "")

    upgrades = [("channel1", schema_v2)]
    upgrade_lib.upgrade_log(context, reader, writer, upgrades)
    writer.close()

    upgraded_reader = LogReader(output_log_path, compiler_context=context)

    # Use auto-created message types so what we get back matches the log metadata exactly
    upgraded_reader.add_topic("channel1")
    upgraded_reader.add_topic("channel2")

    logged_messages = []
    for msg in upgraded_reader.messages():
        channel_name = msg.topic
        if channel_name == "channel1":
            assert hasattr(msg.message, "float_field")
            assert msg.message.float_field == 42.0  # Default value for new field
        else:
            assert not hasattr(msg.message, "float_field")

        assert isinstance(msg.message.message_string, str)
        logged_messages.append((channel_name, msg.sequence_number, msg.message.message_string))

    assert sorted(logged_messages) == sorted(published_messages)


def test_upgrade_all_channels(tmpdir: Path) -> None:
    """Test upgrading all channels."""
    input_log_path = str(tmpdir / "input_log")
    output_log_path = str(tmpdir / "output_log")

    published_messages = write_test_log_v1(input_log_path)

    module_v2, schema_v2 = compile_test_schema_v2()
    context = module_v2.context

    reader = LogReader(input_log_path)
    writer = LogWriter()
    writer.open(output_log_path, "")

    upgrades = [(name, schema_v2) for name in CHANNEL_NAMES]
    upgrade_lib.upgrade_log(context, reader, writer, upgrades)
    writer.close()

    upgraded_reader = LogReader(output_log_path)

    for channel_name in CHANNEL_NAMES:
        upgraded_reader.add_topic(channel_name)

    logged_messages = []
    for msg in upgraded_reader.messages():
        assert hasattr(msg.message, "float_field")
        logged_messages.append((msg.topic, msg.sequence_number, msg.message.message_string))

    assert sorted(logged_messages) == sorted(published_messages)


def test_copy_unspecified_false(tmpdir: Path) -> None:
    """Test with copy_unspecified_channels=False."""
    input_log_path = str(tmpdir / "input_log")
    output_log_path = str(tmpdir / "output_log")

    write_test_log_v1(input_log_path)

    module_v2, schema_v2 = compile_test_schema_v2()
    context = module_v2.context

    reader = LogReader(input_log_path)
    writer = LogWriter()
    writer.open(output_log_path, "")

    upgrades = [("channel1", schema_v2)]
    upgrade_lib.upgrade_log(context, reader, writer, upgrades, copy_unspecified_channels=False)
    writer.close()

    upgraded_reader = LogReader(output_log_path)

    channel_names = [metadata.name for metadata in upgraded_reader.metadata]
    assert "channel1" in channel_names
    assert "channel2" not in channel_names


def test_create_handlers(tmpdir: Path) -> None:
    """Test creating handlers."""
    input_log_path = str(tmpdir / "input_log")

    write_test_log_v1(input_log_path)

    module_c2, schema_v2 = compile_test_schema_v2()
    context = module_c2.context

    reader = LogReader(input_log_path)

    # Case 1: Upgrade one channel, copy others
    handlers = upgrade_lib.create_handlers(reader, context, [("channel1", schema_v2)], copy_unspecified=True)

    assert len(handlers) == 2
    assert "channel1" in handlers
    assert "channel2" in handlers
    assert isinstance(handlers["channel1"], upgrade_lib.UpgradeHandler)
    assert isinstance(handlers["channel2"], upgrade_lib.CopyHandler)

    # Case 2: Upgrade one channel, don't copy others
    handlers = upgrade_lib.create_handlers(reader, context, [("channel1", schema_v2)], copy_unspecified=False)

    assert len(handlers) == 1
    assert "channel1" in handlers
    assert "channel2" not in handlers
    assert isinstance(handlers["channel1"], upgrade_lib.UpgradeHandler)


def test_error_conditions(tmpdir: Path) -> None:
    """Test error conditions."""
    input_log_path = str(tmpdir / "input_log")

    write_test_log_v1(input_log_path)

    # Create incompatible schema (different UUID)
    incompatible_schema_source = """
    // Bad UUID
    schema TestMessage
    {
      uuid: 99999999-9999-9999-9999-999999999999;
      fields
      {
        // Integer field
        #1 integer_field: Int64;
      }
    }

    cpp_target test
    {
        options { namespace test; }
        schema TestMessage;
        representation Tachyon<TestMessage>;
        interface Tappy<TestMessage>;
    }
    """
    module = compiler.compile_source_text(
        incompatible_schema_source, ModuleID(CLK_REPO, "test_schema"), importer=fs_importer()
    )
    schema_ir = module.inner_scope.lookup("TestMessage")
    assert isinstance(schema_ir, schema.Schema)
    incompatible_schema = schema.InstantiatedSchema.from_typespec(schema_ir)

    reader = LogReader(input_log_path)

    with pytest.raises(ValueError, match="Tachyon metadata UUID mismatch"):
        upgrade_lib.create_handlers(reader, module.context, [("channel1", incompatible_schema)], copy_unspecified=True)
