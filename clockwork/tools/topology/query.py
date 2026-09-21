# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Query the topology of a clockwork system."""

import itertools

import click
from clockwork.tools.topology import topology
from clockwork.tools.topology.topology_file import target_topology


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
    ctx.obj["system"] = target_topology(topology_label)


def _print_log_locations(log_name: str, locations: list[topology.LogLocation]) -> None:
    """Print the CPUs where a channel is written to a log."""
    print(f"  {log_name} log:")
    if not locations:
        print("    - None")
        return
    for location in locations:
        redundancy = " (redundant)" if location.is_redundant else ""
        print(f"    - CPU: {location.cpu}{redundancy}")


def _logging_channels(system: topology.System) -> dict[str, topology.Channel]:
    """Return all channels that can have logging locations."""
    return system.channels | system.unlisted_channels


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
@click.option(
    "--logged-telemetry",
    "logged_telemetry",
    is_flag=True,
    help="List all channels written to a telemetry log, including redundant telemetry channels.",
)
@click.option(
    "--redundant-telemetry",
    "redundant_telemetry",
    is_flag=True,
    help="List all channels written redundantly to telemetry logs.",
)
@click.option(
    "--non-redundant-telemetry",
    "non_redundant_telemetry",
    is_flag=True,
    help="List all channels written to telemetry logs without redundancy.",
)
@click.option(
    "--logged-events",
    "logged_events",
    is_flag=True,
    help="List all channels written to an event log.",
)
@click.pass_context
def list_channels(
    ctx: click.Context,
    logged_telemetry: bool,
    redundant_telemetry: bool,
    non_redundant_telemetry: bool,
    logged_events: bool,
) -> None:
    """List all channels in the system."""
    system = ctx.obj["system"]
    filters = [logged_telemetry, redundant_telemetry, non_redundant_telemetry, logged_events]
    if sum(filters) > 1:
        msg = "The channel logging filters are mutually exclusive."
        raise click.UsageError(msg)

    if not any(filters):
        print("\n".join(sorted(system.channels)))
        return

    if logged_events:
        channels = [channel for channel in _logging_channels(system).values() if channel.is_event_logged]
        log_name = "Event"
        show_event_logs = True
    elif redundant_telemetry:
        channels = [channel for channel in _logging_channels(system).values() if channel.is_redundant_telemetry_logged]
        log_name = "Telemetry"
        show_event_logs = False
    elif non_redundant_telemetry:
        channels = [
            channel for channel in _logging_channels(system).values() if channel.is_non_redundant_telemetry_logged
        ]
        log_name = "Telemetry"
        show_event_logs = False
    else:
        channels = [channel for channel in _logging_channels(system).values() if channel.is_telemetry_logged]
        log_name = "Telemetry"
        show_event_logs = False

    for channel in sorted(channels, key=lambda item: item.name):
        print(f"Channel: {channel.name}")
        locations = channel.event_log_locations if show_event_logs else channel.telemetry_log_locations
        _print_log_locations(log_name, locations)


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

    channels = _logging_channels(system)
    if channel_name not in channels:
        msg = f"Invalid channel name.  Expected one of: {', '.join(channels.keys())}"
        raise ValueError(msg)

    channel = channels[channel_name]

    print(f"Channel: {channel.name}")
    print(f"  Size: {channel.size} messages ({channel.size * channel.message_size} bytes)")
    print(f"  Type: {channel.message_type}")
    print(f"  Message Size: {channel.message_size} bytes")
    _print_log_locations("Event", channel.event_log_locations)
    _print_log_locations("Telemetry", channel.telemetry_log_locations)

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
        msg = f"Invalid entity name.  Expected one of: {', '.join(system.entities.keys())}"
        raise ValueError(msg)

    entity = system.entities[entity_name]

    print(f"Entity: {entity.name}")
    print(f"  UUID: {entity.uuid}")
    print(f"  Process: {entity.process}")
    print(f"  CPU: {system.processes[entity.process].cpu}")
    print("  Inputs:")
    for input_name in sorted(entity.inputs):
        print(f"    - {input_name}")
    print("  Outputs:")
    for output_name in sorted(entity.outputs):
        print(f"    - {output_name}")
    print("  States:")
    for endpoint in sorted(entity.states, key=lambda e: e.name):
        print(f"    - {endpoint.name}: {endpoint.entity}")
    print("  Memory Resources:")
    for endpoint in sorted(entity.memory_resources, key=lambda e: e.name):
        print(f"    - {endpoint.name}: {endpoint.entity}")


@cli.command(short_help="List all Memory Resources in the system.")
@click.pass_context
def list_memory(ctx: click.Context) -> None:
    """List all memory resources in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.memory_resources)))


@cli.command(
    short_help="Describe a memory resource.",
    help="List details of a memory resource including type, size, and connected entities.",
)
@click.argument(
    "memory_name",
    type=str,
    required=True,
)
@click.pass_context
def memory(ctx: click.Context, memory_name: str) -> None:
    """Extract topology information for a specific memory resource from a system target."""
    system = ctx.obj["system"]

    if memory_name not in system.memory_resources:
        msg = f"Invalid memory resource name.  Expected one of: {', '.join(system.memory_resources.keys())}"
        raise ValueError(msg)

    memory = system.memory_resources[memory_name]

    print(f"Memory Resource: {memory.name}")
    print(f"  UUID: {memory.uuid}")
    print(f"  Type: {memory.type}")
    print(f"  Size: {memory.size_bytes} bytes")
    print("  Entities:")
    for entity_name in sorted(memory.entities):
        print(f"    - {entity_name}")
    print("  States:")
    for state_name in sorted(memory.states):
        print(f"    - {state_name}")


@cli.command(short_help="List all States in the system.")
@click.pass_context
def list_states(ctx: click.Context) -> None:
    """List all states in the system."""
    system = ctx.obj["system"]
    print("\n".join(sorted(system.states)))


@cli.command(
    short_help="Describe a state.",
    help="List details of a state including type, size, and connected entities.",
)
@click.argument(
    "state_name",
    type=str,
    required=True,
)
@click.pass_context
def state(ctx: click.Context, state_name: str) -> None:
    """Extract topology information for a specific state from a system target."""
    system = ctx.obj["system"]

    if state_name not in system.states:
        msg = f"Invalid state name.  Expected one of: {', '.join(system.states.keys())}"
        raise ValueError(msg)

    state = system.states[state_name]

    print(f"State: {state.name}")
    print(f"  UUID: {state.uuid}")
    print(f"  Extern: {'Yes' if state.is_extern else 'No'}")
    print(f"  Type: {state.type}")
    print(f"  Memory Resource: {state.memory_resource}")
    print("  Entities:")
    for entity_name in sorted(state.entities):
        print(f"    - {entity_name}")


if __name__ == "__main__":
    cli()
