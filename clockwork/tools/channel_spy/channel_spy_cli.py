# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CLI to spy on clockwork channels published on the local machine."""

import json
import logging
from dataclasses import asdict
from enum import Enum
from typing import Any
from uuid import UUID

import click
import clockwork.tools.channel_spy.py_channel_spy as channel_spy


class TachyClassJsonEncoder(json.JSONEncoder):
    """Encoder for echo command."""

    def default(self, obj: Any) -> Any:  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: ANN401
        """Default encoder."""
        if isinstance(obj, UUID):
            return obj.hex
        if isinstance(obj, Enum):
            try:
                return obj.name
            except ValueError:
                return f"Invalid value ({obj.value})"
        return json.JSONEncoder.default(self, obj)


@click.group(help=__doc__)
def cli() -> None:
    """CLI entrypoint."""


@cli.command()
@click.option("--shm-root-dir", "-d", default="/dev/shm")  # noqa: S108
@click.option("--socket_ns", "-n", default="")
def list_channels(shm_root_dir: str, socket_ns: str) -> None:
    """List the channels that can be spied on the local machine."""
    spy = channel_spy.ChannelSpy(shm_root_dir, socket_ns)
    print()
    for channel_name in spy.channels:
        print(f"{channel_name}")


@cli.command()
@click.argument("channel_name")
@click.option("--shm-root-dir", "-d", default="/dev/shm")  # noqa: S108
@click.option("--socket_ns", "-n", default="")
def echo(channel_name: str, shm_root_dir: str, socket_ns: str) -> None:
    """Echo the messages published on a channel."""
    spy = channel_spy.ChannelSpy(shm_root_dir, socket_ns)

    def echo_callback(sequence_number: int, message_time: int, message: Any) -> None:  # noqa: ANN401
        nonlocal channel_name
        print()
        print(f"{channel_name}: {message_time} [{sequence_number}]")
        print(f"{json.dumps(asdict(message), indent=2, cls=TachyClassJsonEncoder)}")

    spy.subscribe_auto(channel_name, echo_callback)
    spy.run()


if __name__ == "__main__":
    # Set up logging
    logging.basicConfig(level=logging.WARNING)

    cli()
