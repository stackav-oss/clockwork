# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Summarize topology of a system target."""

from __future__ import annotations

import itertools
from pathlib import Path
from typing import TYPE_CHECKING

import click
import root_repo_py
from clockwork.dsl.composition import logger_config
from clockwork.dsl.composition import system as composition_system
from clockwork.dsl.ir import box, cog, extern_type, typesys
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.uuid_reg import lookup_uuid
from clockwork.logical_system_interface import (
    CogInterface,
    ConnectableInterface,
    IoConnectionInterface,
    LogicalSystemInterface,
)
from clockwork.tools.topology import topology

if TYPE_CHECKING:
    from uuid import UUID


def _stable_unique(items: list[str]) -> list[str]:
    """Return the first occurrence of each item while preserving order."""
    return list(dict.fromkeys(items))


def _normalize_channel_names(channel_names: list[str], valid_channel_names: set[str]) -> list[str]:
    """Filter to known channels and drop duplicate entity-channel edges."""
    return _stable_unique([name for name in channel_names if name in valid_channel_names])


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


def extract_log_locations(
    topo_system: topology.System,
    physical_system: composition_system.PhysicalSystem,
) -> None:
    """Extract event and telemetry log locations from the physical system."""
    log_entities = logger_config.get_entities(physical_system.system.module.context)
    channels = topo_system.channels | topo_system.unlisted_channels
    for cpu_domain in physical_system.cpu_domains.values():
        buffers = cpu_domain.buffers | cpu_domain.metrics_buffers
        for observer in cpu_domain.log_observers.values():
            buffer = buffers.get(observer.pinion_buffer)
            if buffer is None:
                continue

            channel_name = buffer.channel.channel.channel_name
            channel = channels.get(channel_name)
            if channel is None:
                continue

            location = topology.LogLocation(
                cpu=cpu_domain.logical.name,
                is_redundant=observer.is_redundant,
            )
            if observer.log_type in (
                log_entities.log_type.non_redundant_telemetry,
                log_entities.log_type.redundant_telemetry,
            ):
                channel.event_log_locations.append(location)
                channel.telemetry_log_locations.append(location)
            elif observer.log_type == log_entities.log_type.event:
                channel.event_log_locations.append(location)

    for channel in channels.values():
        channel.event_log_locations = sorted(
            set(channel.event_log_locations), key=lambda item: (item.cpu, item.is_redundant)
        )
        channel.telemetry_log_locations = sorted(
            set(channel.telemetry_log_locations), key=lambda item: (item.cpu, item.is_redundant)
        )


def _extract_memory(lsi: LogicalSystemInterface, system: topology.System) -> None:
    """Extract memory resources from the system interface and add them to the system topology."""
    for mem in lsi.get_memory_resource_instances():
        entity = mem.connectable.entity
        endpoints = mem.get_endpoints()
        entities: list[str] = []
        states: list[str] = []
        for endpoint in endpoints:
            endpoint_entity = endpoint.endpoint.entity
            if (
                isinstance(endpoint_entity, cog.CogInstanceMember)
                and not endpoint_entity.cog_instance.cog_class.is_init()
            ):
                entities.append(endpoint_entity.cog_instance.value_key())
            elif isinstance(endpoint_entity, box.StateInstance):
                states.append(endpoint_entity.value_key())
        mem_type = "HeapMemory" if entity.resource_type == box.MemResourceType.HEAP else "Unknown"
        system.memory_resources[mem.get_name()] = topology.Memory(
            name=mem.get_name(),
            uuid=str(lookup_uuid(lsi.logical_system.module.context, mem.get_name())),
            type=mem_type,
            size_bytes=entity.max_size,
            entities=entities,
            states=states,
        )


def _extract_states(lsi: LogicalSystemInterface, system: topology.System) -> None:
    """Extract states from the system interface and add them to the system topology."""
    for state in lsi.get_state_instances():
        entity = state.connectable.entity
        endpoints = state.get_endpoints()
        state_type = ""
        if isinstance(entity.repr_typespec, extern_type.ExternType):
            if entity.repr_typespec.attributes is not None and entity.repr_typespec.attributes.cpp_attr is not None:
                state_type = f"{entity.repr_typespec.attributes.cpp_attr.type_namespace}::{entity.repr_typespec.name}"
            else:
                state_type = entity.repr_typespec.name
        elif isinstance(entity.repr_typespec, typesys.Instantiation) and "schema" in entity.repr_typespec.arguments:
            state_type = entity.repr_typespec.arguments["schema"].value_key()

        system.states[state.get_name()] = topology.State(
            name=state.get_name(),
            uuid=str(lookup_uuid(lsi.logical_system.module.context, state.get_name())),
            is_extern=isinstance(entity.repr_typespec, extern_type.ExternType),
            type=state_type,
            memory_resource=entity.memory_resource.value_key() if entity.memory_resource else "",
            entities=[
                endpoint.endpoint.entity.cog_instance.value_key()
                for endpoint in endpoints
                if not endpoint.endpoint.entity.cog_instance.cog_class.is_init()
            ],
        )


def _extract_processes(lsi: LogicalSystemInterface, system: topology.System) -> None:
    """Extract processes from the system interface and add them to the system topology.

    Adds process instances to the system and updates CPU domains with processes.
    """
    all_processes = lsi.get_processes()
    for process in all_processes:
        system.processes[process.get_name()] = topology.Process(
            name=process.get_name(), cpu=process.get_cpu_domain().get_name(), entities=[]
        )
        system.cpus[process.get_cpu_domain().get_name()].processes.append(process.get_name())


def _insert_unlisted_channels(
    system: topology.System,
    all_channel_data: dict[str, tuple[int, str, int]],
    channel_data: dict[str, tuple[int, str, int]],
) -> None:
    """Add multi-producer channels that are available only to logging queries."""
    for channel_name, (queue_size, message_type, message_size) in all_channel_data.items():
        if channel_name not in channel_data:
            system.unlisted_channels[channel_name] = topology.Channel(
                name=channel_name,
                size=queue_size,
                publishers=[],
                subscribers=[],
                message_type=message_type,
                message_size=message_size,
            )


def _extract_unlisted_channel_endpoints(
    system: topology.System,
    entity: CogInterface | IoConnectionInterface,
) -> None:
    """Add publishers and subscribers for channels omitted from regular topology."""
    unlisted_channel_names = set(system.unlisted_channels)
    for output_name in _normalize_channel_names(entity.get_output_channel_names(), unlisted_channel_names):
        system.unlisted_channels[output_name].publishers.append(entity.get_name())
    for input_name in _normalize_channel_names(entity.get_input_channel_names(), unlisted_channel_names):
        system.unlisted_channels[input_name].subscribers.append(entity.get_name())


def extract_topology(lsi: LogicalSystemInterface) -> topology.System:
    """Extract topology information from a logical system interface."""
    init_cog_channels = set(
        itertools.chain.from_iterable(cog.get_output_channel_names() for cog in lsi.get_cogs() if cog.is_init())
    )
    channels_by_name = {channel.get_name(): channel for channel in lsi.get_channels()}
    all_channel_data = {
        channel.get_name(): (
            channel.get_queue_size(),
            channel.get_message_repr_name(),
            channel.get_message_size_bytes(),
        )
        for channel in channels_by_name.values()
        if channel.get_name() not in init_cog_channels
    }
    channel_data = {
        name: data for name, data in all_channel_data.items() if not channels_by_name[name].is_multi_producer()
    }

    system = topology.System(cpus={}, entities={}, channels={}, processes={}, memory_resources={}, states={})
    _insert_unlisted_channels(system, all_channel_data, channel_data)
    for cpu in lsi.get_cpu_domains():
        system.cpus[cpu.get_name()] = topology.Cpu(name=cpu.get_name(), processes=[])

    _extract_memory(lsi, system)

    _extract_states(lsi, system)

    _extract_processes(lsi, system)

    all_entities = itertools.chain((cog for cog in lsi.get_cogs() if not cog.is_init()), lsi.get_io_connections())

    for entity in all_entities:
        process = entity.get_process().get_name()
        entity_outputs = _normalize_channel_names(entity.get_output_channel_names(), set(channel_data))
        entity_inputs = _normalize_channel_names(entity.get_input_channel_names(), set(channel_data))
        entity_states: list[topology.Endpoint] = []
        entity_memory_resources: set[topology.Endpoint] = set()
        if isinstance(entity, CogInterface):
            connectables: list[ConnectableInterface] = entity.get_connectables()
            for connectable in connectables:
                name = connectable.get_name()
                if isinstance(connectable.connectable.entity, box.StateInstance):
                    entity_endpoint = next(
                        ep
                        for ep in connectable.get_endpoints()
                        if ep.endpoint.entity.cog_instance.value_key() == entity.get_name()
                    )
                    entity_states.append(topology.Endpoint(name=entity_endpoint.endpoint.entity.name, entity=name))
                # Logical system interface associates state memory resources with cogs that use the state.
                # We don't want that connection here, skip any memory resources that don't actually have the cog entity as an endpoint.
                elif isinstance(connectable.connectable.entity, box.MemoryResourceInstance) and any(
                    endpoint.endpoint.entity.cog_instance.value_key() == entity.get_name()
                    for endpoint in connectable.get_endpoints()
                    if isinstance(endpoint.endpoint.entity, cog.CogInstanceMember)
                ):
                    entity_endpoint = next(
                        ep
                        for ep in connectable.get_endpoints()
                        if ep.endpoint.entity.cog_instance.value_key() == entity.get_name()
                    )
                    entity_memory_resources.add(
                        topology.Endpoint(name=entity_endpoint.endpoint.entity.name, entity=name)
                    )

        system.entities[entity.get_name()] = topology.Entity(
            name=entity.get_name(),
            uuid=str(lookup_uuid(lsi.logical_system.module.context, entity.get_name())),
            process=process,
            outputs=entity_outputs,
            inputs=entity_inputs,
            states=entity_states,
            memory_resources=list(entity_memory_resources),
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

        _extract_unlisted_channel_endpoints(system, entity)

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
    extract_log_locations(system, physical_system)
    with output_file.open("wb") as f:
        topology.save_system(system, f)


if __name__ == "__main__":
    main()
