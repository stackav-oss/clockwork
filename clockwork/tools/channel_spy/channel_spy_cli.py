# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CLI to spy on clockwork channels published on the local machine."""

import json
import logging
import sys
from dataclasses import asdict
from enum import Enum
from typing import Any
from uuid import UUID

import click
import clockwork.tools.channel_spy.py_channel_spy as channel_spy
from typing_extensions import override


class TachyClassJsonEncoder(json.JSONEncoder):
    """Encoder for echo command."""

    @override
    def default(self, o: Any) -> Any:
        """Default encoder."""
        if isinstance(o, UUID):
            return o.hex
        if isinstance(o, Enum):
            try:
                return o.name
            except ValueError:
                return f"Invalid value ({o.value})"
        return json.JSONEncoder.default(self, o)


_default_shm_dir: str = "/dev/shm"  # noqa: S108 /dev/shm is the default directory for pinion shm buffers
_shm_dir_help: str = f"Root directory for channel shared memory. Default: {_default_shm_dir}"
_default_tmp_dir: str = "/tmp"  # noqa: S108 /tmp is the default directory configuration files
_tmp_dir_help: str = f"Temporary directory. Default: {_default_tmp_dir}"
_socket_ns_help: str = "Optional namespace prefix for sockets."


@click.group(help=__doc__)
def cli() -> None:
    """CLI entrypoint."""


@cli.command()
@click.option("--shm-root-dir", "-d", default=_default_shm_dir, help=_shm_dir_help)
@click.option("--tmp-dir", "-t", default=_default_tmp_dir, help=_tmp_dir_help)
@click.option("--socket_ns", "-n", default="", help=_socket_ns_help)
def list_channels(shm_root_dir: str, tmp_dir: str, socket_ns: str) -> None:
    """List the channels that can be spied on the local machine."""
    spy = channel_spy.ChannelSpy(shm_root_dir, tmp_dir, socket_ns)
    print()
    for channel_name in spy.channels:
        print(f"{channel_name}")


@cli.command()
@click.argument("channel_name")
@click.option("--shm-root-dir", "-d", default=_default_shm_dir, help=_shm_dir_help)
@click.option("--tmp-dir", "-t", default=_default_tmp_dir, help=_tmp_dir_help)
@click.option("--socket_ns", "-n", default="", help=_socket_ns_help)
@click.option(
    "--message_count",
    "-c",
    default=None,
    type=click.IntRange(min=1),
    help="Number of messages to echo before exiting. When not specified, echoes indefinitely.",
)
def echo(channel_name: str, shm_root_dir: str, tmp_dir: str, socket_ns: str, message_count: int | None) -> None:
    """Echo the messages published on a channel."""
    spy = channel_spy.ChannelSpy(shm_root_dir, tmp_dir, socket_ns)

    def echo_callback(sequence_number: int, message_time: int, message: Any) -> None:  # noqa: ANN401 Any type needed to handle arbitrary message types.
        nonlocal channel_name
        nonlocal message_count
        print()
        print(f"{channel_name}: {message_time} [{sequence_number}]")
        print(f"{json.dumps(asdict(message), indent=2, cls=TachyClassJsonEncoder)}")
        if message_count and (message_count := message_count - 1) <= 0:
            sys.exit(0)

    spy.subscribe_auto(channel_name, echo_callback)
    spy.run()


if __name__ == "__main__":
    # Set up logging
    logging.basicConfig(level=logging.WARNING)

    cli()
