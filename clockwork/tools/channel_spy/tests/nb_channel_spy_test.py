# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Channel spy nanobind wrapper test."""

import uuid

import clockwork.tools.channel_spy.nb_channel_spy as channel_spy_impl
import clockwork.tools.channel_spy.tests.support.nb_test_helper as test_helper
import jewels.testing.nb_tmp_directory_guard as tmp_directory_guard
from clockwork.serialization.metadata import tachyon
from clockwork.tools.channel_spy.tests.support import test_message_clk_nb as test_message_clk


class TestChannelSpyNanobindClass:
    """Channel spy nanobind wrapper test class."""

    def test_spy_config(self) -> None:
        """Test the channel spy configuration."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        channel_spy = channel_spy_impl.ChannelSpy(shm_root_dir, socket_ns)

        assert channel_spy.channels == ["channel_1", "channel_2"]
        assert (
            tachyon.get_metadata_from_protobuf(channel_spy.schema_definition("channel_1"))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            == test_message_clk.TestMessage.get_tachyon_metadata()
        )

    def test_subscribe_raw_python(self) -> None:
        """Test subscribing with raw callbacks."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        helper = test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        channel_spy = channel_spy_impl.ChannelSpy(shm_root_dir, socket_ns)

        publisher = helper.open_publisher(0)

        last_sequence_number: int = 9999
        last_message_time: int = 9999
        last_message_string: str = ""

        def raw_python_callback(cb_handle: channel_spy_impl.PythonCallbackHandle) -> None:
            nonlocal last_sequence_number, last_message_time, last_message_string
            message = test_message_clk.TestMessage.deserialize_tachyon(memoryview(cb_handle.data))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            if not cb_handle.overrun_check():
                return
            last_sequence_number = cb_handle.sequence_number
            last_message_time = cb_handle.message_time
            last_message_string = message.message_string

        channel_spy.subscribe_raw_python("channel_1", raw_python_callback)

        buffer = bytearray(test_message_clk.TestMessage.get_tachyon_constraint().size)

        message0 = test_message_clk.TestMessage()
        message0.message_string = "Test message 0"
        message0.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        publisher.publish(0, buffer)

        channel_spy.run_once()

        assert last_sequence_number == 0
        assert last_message_time == 0
        assert last_message_string == "Test message 0"

        message1 = test_message_clk.TestMessage()
        message1.message_string = "Test message 1"
        message1.serialize_tachyon(memoryview(buffer))
        publisher.publish(1, buffer)

        channel_spy.run_once()

        assert last_sequence_number == 1
        assert last_message_time == 1
        assert last_message_string == "Test message 1"

    def test_subscribe_raw_python_detects_overruns(self) -> None:
        """Test subscribing with raw callbacks."""
        tmp_dir = tmp_directory_guard.TmpDirectoryGuard()
        shm_root_dir = tmp_dir.path
        socket_ns = str(uuid.uuid4())
        helper = test_helper.TestHelper.make_test_helper(shm_root_dir, socket_ns)
        channel_spy = channel_spy_impl.ChannelSpy(shm_root_dir, socket_ns)

        publisher = helper.open_publisher(0)

        buffer = bytearray(test_message_clk.TestMessage.get_tachyon_constraint().size)

        message0 = test_message_clk.TestMessage()
        message0.message_string = "Test message 0"
        message0.serialize_tachyon(memoryview(buffer))  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        publisher.publish(0, buffer)

        callback_ran: bool = False

        def raw_python_callback(cb_handle: channel_spy_impl.PythonCallbackHandle) -> None:
            nonlocal callback_ran
            assert cb_handle.overrun_check()
            publisher.publish(0, buffer)
            assert cb_handle.overrun_check()
            publisher.publish(0, buffer)
            assert not cb_handle.overrun_check()
            callback_ran = True

        channel_spy.subscribe_raw_python("channel_1", raw_python_callback)

        channel_spy.run_once()

        assert callback_ran
