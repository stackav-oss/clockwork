# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Summarize topology of a system target."""

from __future__ import annotations

import itertools
from pathlib import Path
from typing import TYPE_CHECKING

import click
import root_repo_py
from clockwork.dsl.composition import system as composition_system
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.logical_system_interface import LogicalSystemInterface
from clockwork.tools.topology import topology

if TYPE_CHECKING:
    from uuid import UUID


def insert_or_get_channel(
    system: topology.System, name: str, size: int, message_type: str, message_size: int
) -> topology.Channel:
    """Get or insert channel."""
    return system.channels.setdefault(
        name,
        topology.Channel(
            name=name, size=size, publishers=[], subscribers=[], message_type=message_type, message_size=message_size
        ),
    )


def _get_bridge_info(
    observer: composition_system.BridgeObserver,
) -> tuple[str, str | None]:
    """Get bridge type and endpoint info from an observer."""
    if isinstance(observer, composition_system.TcpBridgeObserver):
        return "tcp", str(getattr(observer, "lan_port", None))
    return "unknown", None


def _extract_routes_for_observer(
    observer: composition_system.BridgeObserver,
    source_cpu: str,
    all_producers: dict[UUID, composition_system.BridgeProducer],
    physical_system: composition_system.PhysicalSystem,
) -> list[topology.Route]:
    """Extract routes for a single bridge observer."""
    bridge_type, endpoint_info = _get_bridge_info(observer)
    dest_cpus_seen: set[str] = set()
    routes: list[topology.Route] = []
    for producer_uuid in observer.remote_producers:
        producer = all_producers[producer_uuid]
        dest_domain = physical_system.cpu_domains.get(producer.dest_domain)
        if dest_domain is None:
            continue
        dest_cpu = dest_domain.logical.name
        if dest_cpu in dest_cpus_seen:
            continue
        dest_cpus_seen.add(dest_cpu)
        routes.append(
            topology.Route(
                source_cpu=source_cpu,
                dest_cpu=dest_cpu,
                bridge_type=bridge_type,
                endpoint=endpoint_info,
            )
        )
    return routes


def extract_routes(
    topo_system: topology.System,
    physical_system: composition_system.PhysicalSystem,
) -> None:
    """Extract route information from physical system and add to topology system channels."""
    channel_routes: dict[str, list[topology.Route]] = {}
    all_producers: dict[UUID, composition_system.BridgeProducer] = {}
    for domain in physical_system.cpu_domains.values():
        all_producers.update(domain.bridge_producers)

    for domain in physical_system.cpu_domains.values():
        source_cpu = domain.logical.name
        for observer in domain.bridge_observers.values():
            source_buffer = domain.buffers[observer.source_pinion_buffer]
            channel_name = source_buffer.channel.channel.channel_name
            routes = _extract_routes_for_observer(observer, source_cpu, all_producers, physical_system)
            if routes:
                channel_routes.setdefault(channel_name, []).extend(routes)
    for channel_name, routes in channel_routes.items():
        if channel_name in topo_system.channels:
            topo_system.channels[channel_name].routes = routes


def extract_topology(lsi: LogicalSystemInterface) -> topology.System:
    """Extract topology information from a logical system interface."""
    init_cog_channels = set(
        itertools.chain.from_iterable(cog.get_output_channel_names() for cog in lsi.get_cogs() if cog.is_init())
    )
    channel_data = {
        channel.get_name(): (
            channel.get_queue_size(),
            channel.get_message_repr_name(),
            channel.get_message_size_bytes(),
        )
        for channel in lsi.get_channels()
        if not channel.is_multi_producer() and channel.get_name() not in init_cog_channels
    }

    def filter_channel_names(channel_names: list[str]) -> list[str]:
        return [name for name in channel_names if name in channel_data]

    system = topology.System(cpus={}, entities={}, channels={}, processes={})
    for cpu in lsi.get_cpu_domains():
        system.cpus[cpu.get_name()] = topology.Cpu(name=cpu.get_name(), processes=[])

    all_processes = lsi.get_processes()
    for process in all_processes:
        system.processes[process.get_name()] = topology.Process(
            name=process.get_name(), cpu=process.get_cpu_domain().get_name(), entities=[]
        )
        system.cpus[process.get_cpu_domain().get_name()].processes.append(process.get_name())

    all_entities = itertools.chain((cog for cog in lsi.get_cogs() if not cog.is_init()), lsi.get_io_connections())

    for entity in all_entities:
        process = entity.get_process().get_name()
        entity_outputs = filter_channel_names(entity.get_output_channel_names())
        entity_inputs = filter_channel_names(entity.get_input_channel_names())
        system.entities[entity.get_name()] = topology.Entity(
            name=entity.get_name(),
            process=process,
            outputs=entity_outputs,
            inputs=entity_inputs,
        )
        system.processes[process].entities.append(entity.get_name())

        for output_name in entity_outputs:
            queue_size, message_type, message_size = channel_data[output_name]
            channel = insert_or_get_channel(system, output_name, queue_size, message_type, message_size)
            channel.publishers.append(entity.get_name())

        for input_name in entity_inputs:
            queue_size, message_type, message_size = channel_data[input_name]
            channel = insert_or_get_channel(system, input_name, queue_size, message_type, message_size)
            channel.subscribers.append(entity.get_name())

    return system


@click.command()
@click.argument(
    "input_repo",
    type=str,
    required=True,
)
@click.argument(
    "input_file",
    type=click.Path(file_okay=True, dir_okay=False, readable=True, exists=True, path_type=Path),
    required=True,
)
@click.argument(
    "output_file",
    type=click.Path(exists=False, path_type=Path),
    required=True,
)
def main(input_repo: str, input_file: Path, output_file: Path) -> None:
    """Extract topology information from a system target."""
    input_repo = input_repo or root_repo_py.ROOT_REPO
    lsi = LogicalSystemInterface(
        ModuleID.from_path(input_repo, input_file),
    )

    system = extract_topology(lsi)
    physical_system = composition_system.make_physical_system(lsi.logical_system)
    extract_routes(system, physical_system)
    with output_file.open("wb") as f:
        topology.save_system(system, f)


if __name__ == "__main__":
    main()
