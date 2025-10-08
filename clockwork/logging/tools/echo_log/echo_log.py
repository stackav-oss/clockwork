# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CLI to find, manage, and use log events."""

import json
from dataclasses import asdict
from typing import Any

import click
import clockwork.logging.readers.py_log_processor as py_reader
from clockwork.logging.readers.nb_types import (
    LogReaderConfig,
    LogTimestamp,
)
from clockwork.tools.channel_spy.channel_spy_cli import TachyClassJsonEncoder


@click.command(help=__doc__)
@click.argument("channel_name")
@click.argument("log_uri")
def main(channel_name: str, log_uri: str) -> None:
    """Echo messages for a topic."""
    config = LogReaderConfig(log_uri)
    processor = py_reader.LogProcessor(config)

    def callback(publish_time: LogTimestamp, message: Any) -> None:  # noqa: ANN401 Needed for runtime polymorphism
        nonlocal channel_name
        print()
        print(f"{channel_name}: {publish_time.nanoseconds}")
        print(f"{json.dumps(asdict(message), indent=2, cls=TachyClassJsonEncoder)}")

    processor.add_callback_with_timestamp(channel_name, None, callback)
    processor.process()


if __name__ == "__main__":
    main()
