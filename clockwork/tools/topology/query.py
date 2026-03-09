# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Query the topology of a clockwork system."""

import itertools
import os
import subprocess
from pathlib import Path
from typing import Final

import click
from clockwork.tools.topology import topology

_WORKING_DIR_VAR: Final = "BUILD_WORKING_DIRECTORY"


def get_build_working_dir() -> str:
    """Get the build working directory from bazel.

    This maps to the root of the bazel repository where `bazel run`
    was run from.
    """
    var = os.getenv(_WORKING_DIR_VAR)
    if not var:
        msg = f"Environment variable '{_WORKING_DIR_VAR}' is not set.  Bazel should set this when using `bazel run`."
        raise RuntimeError(msg)
    return var


def update_topology_information(topology_label: str) -> None:
    """Make sure the topology_summary target is up-to-date."""
    subprocess.run(
        ["bazel", "build", "--remote_download_toplevel", topology_label],
        check=True,
        cwd=get_build_working_dir(),
    )


def to_topology_file(topology_label: str) -> Path:
    """Convert the label to the expected output file."""
    working_dir = get_build_working_dir()
    prefix = Path(working_dir) / "bazel-bin"
    repo, label = topology_label.split("//")
    if repo:
        prefix /= f"external/{repo[1:]}+"
    return prefix / (label.replace(":", "/") + ".pkl")


@click.group()
@click.argument(
    "topology_label",
    type=str,
    required=True,
)
@click.pass_context
def cli(ctx: click.Context, topology_label: str) -> None:
    """CLI group."""
    ctx.ensure_object(dict)
    update_topology_information(topology_label)
    topology_file = to_topology_file(topology_label)
    if not topology_file.exists():
        msg = f"Target {topology_label} did not produce a topology_file.  Is it created by the 'topology_summary' rule?"
        raise ValueError(msg)
    with topology_file.open("rb") as f:
        ctx.obj["system"] = topology.load_system(f)


@cli.command(short_help="List all entities (cogs, sockets, etc.) in the system.")
@click.pass_context
def list_entities(ctx: click.Context) -> None:
    """List all entities in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.entities)))


@cli.command(
    short_help="List all channels in the system.",
    help="List all channels in the system.  Some channels such as diagnostics are ignored to keep the graph more easily interpreted.",
)
@click.pass_context
def list_channels(ctx: click.Context) -> None:
    """List all channels in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.channels)))


@cli.command(short_help="List all CPUs in the system.")
@click.pass_context
def list_cpus(ctx: click.Context) -> None:
    """List all cpus in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.cpus)))


@cli.command(
    short_help="Describe a specific cpu.",
    help="List processes and entities on a cpu and describe channel topology.",
)
@click.argument(
    "cpu_name",
    type=str,
    required=True,
)
@click.pass_context
def cpu(ctx: click.Context, cpu_name: str) -> None:
    """Extract topology information for a specific cpu from a system target."""
    system = ctx.obj["system"]

    if cpu_name not in system.cpus:
        msg = f"Invalid cpu name.  Expected one of: {', '.join(system.cpus.keys())}"
        raise ValueError(msg)

    channel_to_publisher_cpu = topology.channel_to_publisher_cpu_mapping(system)
    channel_to_subscriber_cpu = topology.channel_to_subscriber_cpu_mapping(system)

    channel_flow = topology.channel_flow_for_cpu(cpu_name, system, channel_to_publisher_cpu, channel_to_subscriber_cpu)

    print(f"CPU: {cpu_name}")
    print("Processes")
    for process in sorted(system.cpus[cpu_name].processes):
        print(f"  - {process}")
    print("Entities")
    for entity in sorted(
        itertools.chain.from_iterable(system.processes[proc].entities for proc in system.cpus[cpu_name].processes)
    ):
        print(f"  - {entity}")
    print("Local channels")
    for channel_name in sorted(channel_flow.local_channels):
        channel = system.channels[channel_name]
        print(f"  - {channel.name}")
        print(f"    Size:         {channel.size} messages")
        print(f"    Type:         {channel.message_type}")
        print(f"    Message Size: {channel.message_size} bytes")

    print("Outbound channels")
    for channel_name in sorted(channel_flow.outbound_channels):
        channel = system.channels[channel_name]
        print(f"  - {channel.name}")
        print(f"    Size:         {channel.size} messages")
        print(f"    Type:         {channel.message_type}")
        print(f"    Message Size: {channel.message_size} bytes")
        print(f"    Destinations: {', '.join(sorted(channel_to_subscriber_cpu[channel_name]))}")

    print("Inbound channels")
    for channel_name in sorted(channel_flow.inbound_channels):
        channel = system.channels[channel_name]
        print(f"  - {channel.name}")
        print(f"    Size:         {channel.size} messages")
        print(f"    Type:         {channel.message_type}")
        print(f"    Message Size: {channel.message_size} bytes")
        print(f"    Origin:       {channel_to_publisher_cpu[channel_name]}")


@cli.command(short_help="List all processes in the system.")
@click.pass_context
def list_processes(ctx: click.Context) -> None:
    """List all processes in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.processes)))


@cli.command(
    short_help="Describe a specific process.",
    help="List the entities in a process.",
)
@click.argument(
    "process_name",
    type=str,
    required=True,
)
@click.pass_context
def process(ctx: click.Context, process_name: str) -> None:
    """Extract topology information for a specific process from a system target."""
    system = ctx.obj["system"]

    if process_name not in system.processes:
        msg = f"Invalid process name.  Expected one of: {', '.join(system.processes.keys())}"
        raise ValueError(msg)

    process = system.processes[process_name]
    print(f"Process: {process.name}")
    print(f"  - CPU: {process.cpu}")
    print("Entities")
    for entity in sorted(process.entities):
        print(f"  - {entity}")


def _format_route_extra(route: topology.Route) -> str:
    if route.bridge_type == "tcp" and route.endpoint:
        return f" (Port: {route.endpoint})"
    if route.bridge_type == "pcie" and route.endpoint:
        return f" (Endpoint: {route.endpoint})"
    return ""


def _print_channel_routes(channel: topology.Channel) -> None:
    if not channel.routes:
        return
    print("  Routes:")
    for route in sorted(channel.routes, key=lambda r: (r.source_cpu, r.dest_cpu)):
        extra = _format_route_extra(route)
        print(f"    - {route.source_cpu} -> {route.dest_cpu}: {route.bridge_type}{extra}")


@cli.command(
    short_help="Describe a channel.",
    help="List details of a channel including message type, message size, and all connections.",
)
@click.argument(
    "channel_name",
    type=str,
    required=True,
)
@click.pass_context
def channel(ctx: click.Context, channel_name: str) -> None:
    """Extract topology information for a specific channel from a system target."""
    system = ctx.obj["system"]

    if channel_name not in system.channels:
        msg = f"Invalid channel name.  Expected one of: {', '.join(system.channels.keys())}"
        raise ValueError(msg)

    channel = system.channels[channel_name]

    print(f"Channel: {channel.name}")
    print(f"  Size: {channel.size} messages ({channel.size * channel.message_size} bytes)")
    print(f"  Type: {channel.message_type}")
    print(f"  Message Size: {channel.message_size} bytes")

    pubs = {}
    for pub_name in channel.publishers:
        pubs.setdefault(system.processes[system.entities[pub_name].process].cpu, []).append(pub_name)
    subs = {}
    for sub_name in channel.subscribers:
        subs.setdefault(system.processes[system.entities[sub_name].process].cpu, []).append(sub_name)

    print("  Publishers:")
    for cpu in sorted(pubs):
        print(f"    - CPU: {cpu}")
        for pub in sorted(pubs[cpu]):
            print(f"        - {pub}")

    print("  Subscribers")
    for cpu in sorted(subs):
        print(f"    - CPU: {cpu}")
        for sub in sorted(subs[cpu]):
            print(f"        - {sub}")

    _print_channel_routes(channel)


@cli.command(
    short_help="Describe an entity.",
    help="List details of an entity including where it runs, input channels, and output channels.",
)
@click.argument(
    "entity_name",
    type=str,
    required=True,
)
@click.pass_context
def entity(ctx: click.Context, entity_name: str) -> None:
    """Extract topology information for a specific entity from a system target."""
    system = ctx.obj["system"]

    if entity_name not in system.entities:
        msg = f"Invalid entity name.  Expected one of: {', '.join(system.entity.keys())}"
        raise ValueError(msg)

    entity = system.entities[entity_name]

    print(f"Enity: {entity.name}")
    print(f"  Process: {entity.process}")
    print(f"  CPU: {system.processes[entity.process].cpu}")
    print("  Inputs:")
    for input_name in sorted(entity.inputs):
        print(f"    - {input_name}")
    print("  Outputs:")
    for output_name in sorted(entity.outputs):
        print(f"    - {output_name}")


if __name__ == "__main__":
    cli()
