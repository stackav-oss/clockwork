# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python channel spy library."""

import logging
from collections.abc import Callable
from time import sleep
from typing import Any, Final

import clockwork.tools.channel_spy.nb_channel_spy as _nb_channel_spy
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.serialization.metadata import tachyon
from clockwork.serialization.py import protocol, tachyon_dyn_from_metadata
from typing_extensions import override

_logger: Final = logging.getLogger(__name__)


class ChannelSpy(_nb_channel_spy.ChannelSpy):
    """Implements deserializing callbacks for the clockwork channel spy."""

    def __init__(self, shm_root_dir: str = "/dev/shm", tmp_dir: str = "/tmp", socket_ns: str = "") -> None:  # noqa: S108
        """Construct a new channel spy instance."""
        super().__init__(shm_root_dir, tmp_dir, socket_ns)
        self.compiler_context = CompilerContext()

    @override
    def run(self) -> None:
        """Loop processing subscriptions."""
        while True:
            super().run_once()
            sleep(0.001)

    def subscribe_raw(
        self,
        channel_name: str,
        callback: Callable[[int, int, bytearray], None],
    ) -> None:
        """Subscribe to receive raw message data.

        Args:
            channel_name: Channel name
            callback: Callback function
        """

        def raw_callback_function(cb_handle: _nb_channel_spy.PythonCallbackHandle) -> None:
            nonlocal callback, channel_name
            message_data = cb_handle.data
            if not cb_handle.overrun_check():
                _logger.warning("Detected overrun processing callback for channel %s", channel_name)
                return
            callback(cb_handle.sequence_number, cb_handle.message_time, message_data)

        super().subscribe_raw_python(channel_name, raw_callback_function)

    def subscribe_typed(
        self,
        channel_name: str,
        message_type: type[protocol.TachyClass],
        callback: Callable[[int, int, protocol.TachyClass], None],
    ) -> None:
        """Subscribe to receive deserialied messages using the specified wrapper type.

        Args:
            channel_name: Channel name
            message_type: Message type
            callback: Callback function
        """

        def typed_callback_function(cb_handle: _nb_channel_spy.PythonCallbackHandle) -> None:
            nonlocal callback, channel_name, message_type
            message_data = cb_handle.data
            if not cb_handle.overrun_check():
                _logger.warning("Detected overrun processing callback for channel %s", channel_name)
                return
            deserialized_message = message_type.deserialize_tachyon(memoryview(message_data))
            callback(cb_handle.sequence_number, cb_handle.message_time, deserialized_message)

        super().subscribe_raw_python(channel_name, typed_callback_function)

    def subscribe_auto(
        self,
        channel_name: str,
        callback: Callable[[int, int, protocol.Tachyon[Any]], None],
    ) -> None:
        """Subscribe to receive deserialied messages using an auto generated wrapper type.

        Args:
            channel_name: Channel name
            callback: Callback function
        """
        schema_definition = super().schema_definition(channel_name)
        schema_name = super().schema_name(channel_name)
        if not schema_definition or not schema_name:
            msg = f"Schema definition for {channel_name} is empty"
            raise ValueError(msg)
        tachyon_metadata = tachyon.get_metadata_from_protobuf(schema_definition)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        message_type, _ = tachyon_dyn_from_metadata.py_type_from_metadata(
            self.compiler_context, schema_name, tachyon_metadata
        )

        self.subscribe_typed(channel_name, message_type, callback)
