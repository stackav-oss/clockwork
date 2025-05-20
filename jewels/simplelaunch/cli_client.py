# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CLI client for simplelaunch."""

from typing import Final

import click
import rich
import rich.table
from jewels.simplelaunch.client import DEFAULT_PORT, SimpleLaunchClient
from jewels.simplelaunch.v1.service_pb2 import ProcessState

_HOST_FLAG: Final = click.option("--host", default=f"localhost:{DEFAULT_PORT}", help="Host to connect to.")


@click.group(help=__doc__)
def cli() -> None:
    """CLI entrypoint."""


@cli.command("list")
@_HOST_FLAG
def _list(host: str) -> None:  # pyright: ignore[reportUnusedFunction] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    """List processes."""
    table = rich.table.Table("Name", "Status", "Core Dumped", "PID")

    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    process_list = client.list_procs()

    for process in process_list.process_info:
        table.add_row(process.name, ProcessState.Name(process.state), repr(process.core_dumped), str(process.pid))

    rich.print(table)

    for pre_launch_task in process_list.pre_launch_info:
        if not pre_launch_task.succeeded:
            rich.print(f"[red]Pre-launch task {pre_launch_task.name} failed")


@cli.command()
@_HOST_FLAG
@click.argument("name")
def stop(host: str, name: str) -> None:
    """Stop a process."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    client.stop(name)


@cli.command()
@_HOST_FLAG
def stop_all(host: str) -> None:
    """Stop all processes."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    client.stop_all()


@cli.command()
@_HOST_FLAG
@click.argument("name")
def start(host: str, name: str) -> None:
    """Start a process."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    client.start(name)


@cli.command()
@_HOST_FLAG
@click.argument("name")
def logs(host: str, name: str) -> None:
    """Get the log output from a process."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    click.echo(client.logs(name))


@cli.command()
@_HOST_FLAG
def is_running(host: str) -> None:
    """Returns 'yes' if all nodes are running, 'no' otherwise."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    click.echo("yes" if client.is_running() else "no")


@cli.command()
@_HOST_FLAG
def is_stopped(host: str) -> None:
    """Returns 'yes' if all nodes are not running, 'no' otherwise."""
    client = SimpleLaunchClient(host.split(":")[0], int(host.split(":")[1]))
    click.echo("yes" if client.is_stopped() else "no")


if __name__ == "__main__":
    cli()
