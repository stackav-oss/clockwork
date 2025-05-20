# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Channel spy test."""

import uuid

import clockwork.tools.channel_spy.py_channel_spy as channel_spy
import clockwork.tools.channel_spy.tests.support.nb_test_helper as test_helper
import jewels.testing.nb_tmp_directory_guard as tmp_directory_guard  # type: ignore[import-untyped]
from clockwork.serialization.py import protocol
from clockwork.tools.channel_spy.tests.support import test_message_clk_nb as test_message_clk


class TestChannelSpyClass:
    """Channel spy test class."""

    def test_subscribe_raw(self) -> None:
        """Test subscribing with raw callbacks."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        helper = test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        spy = channel_spy.ChannelSpy(shm_root_dir, socket_ns)

        publisher = helper.open_publisher(0)

        last_sequence_number: int = 9999
        last_message_time: int = 9999
        last_message_string: str = ""

        def raw_callback(sequence_number: int, message_time: int, message_data: bytearray) -> None:
            nonlocal last_sequence_number, last_message_time, last_message_string
            last_sequence_number = sequence_number
            last_message_time = message_time
            message = test_message_clk.TestMessage.deserialize_tachyon(memoryview(message_data))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            last_message_string = message.message_string

        spy.subscribe_raw("channel_1", raw_callback)

        buffer = bytearray(test_message_clk.TestMessage.get_tachyon_constraint().size)

        message0 = test_message_clk.TestMessage()
        message0.message_string = "Test message 0"
        message0.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        publisher.publish(0, buffer)

        spy.run_once()

        assert last_sequence_number == 0
        assert last_message_time == 0
        assert last_message_string == "Test message 0"

        message1 = test_message_clk.TestMessage()
        message1.message_string = "Test message 1"
        message1.serialize_tachyon(memoryview(buffer))
        publisher.publish(1, buffer)

        spy.run_once()

        assert last_sequence_number == 1
        assert last_message_time == 1
        assert last_message_string == "Test message 1"

    def test_subscribe_typed(self) -> None:
        """Test subscribing with typed deserialized callbacks."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        helper = test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        spy = channel_spy.ChannelSpy(shm_root_dir, socket_ns)

        publisher = helper.open_publisher(0)

        last_sequence_number: int = 9999
        last_message_time: int = 9999
        last_message_string: str = ""

        def typed_callback(sequence_number: int, message_time: int, message: test_message_clk.TestMessage) -> None:
            nonlocal last_sequence_number, last_message_time, last_message_string
            last_sequence_number = sequence_number
            last_message_time = message_time
            last_message_string = message.message_string

        spy.subscribe_typed("channel_1", test_message_clk.TestMessage, typed_callback)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        buffer = bytearray(test_message_clk.TestMessage.get_tachyon_constraint().size)

        message0 = test_message_clk.TestMessage()
        message0.message_string = "Test message 0"
        message0.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        publisher.publish(0, buffer)

        spy.run_once()

        assert last_sequence_number == 0
        assert last_message_time == 0
        assert last_message_string == "Test message 0"

        message1 = test_message_clk.TestMessage()
        message1.message_string = "Test message 1"
        message1.serialize_tachyon(memoryview(buffer))
        publisher.publish(1, buffer)

        spy.run_once()

        assert last_sequence_number == 1
        assert last_message_time == 1
        assert last_message_string == "Test message 1"

    def test_subscribe_auto(self) -> None:
        """Test subscribing with auto generated wrappers."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        helper = test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        spy = channel_spy.ChannelSpy(shm_root_dir, socket_ns)

        publisher = helper.open_publisher(0)

        last_sequence_number: int = 9999
        last_message_time: int = 9999
        last_message_string: str = ""

        def auto_callback(sequence_number: int, message_time: int, message: protocol.TachyClass) -> None:  # pyright: ignore[reportInvalidTypeVarUse] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            nonlocal last_sequence_number, last_message_time, last_message_string
            last_sequence_number = sequence_number
            last_message_time = message_time
            last_message_string = message.message_string  # type: ignore[attr-defined]

        spy.subscribe_auto("channel_1", auto_callback)

        buffer = bytearray(test_message_clk.TestMessage.get_tachyon_constraint().size)

        message0 = test_message_clk.TestMessage()
        message0.message_string = "Test message 0"
        message0.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        publisher.publish(0, buffer)

        spy.run_once()

        assert last_sequence_number == 0
        assert last_message_time == 0
        assert last_message_string == "Test message 0"

        message1 = test_message_clk.TestMessage()
        message1.message_string = "Test message 1"
        message1.serialize_tachyon(memoryview(buffer))
        publisher.publish(1, buffer)

        spy.run_once()

        assert last_sequence_number == 1
        assert last_message_time == 1
        assert last_message_string == "Test message 1"
