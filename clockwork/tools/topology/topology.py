# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Data types to represent the topology of the system."""

from __future__ import annotations

import itertools
import pickle
import typing
from dataclasses import dataclass, field


@dataclass
class Route:
    """A bridge route between CPUs for a channel."""

    source_cpu: str
    dest_cpu: str
    bridge_type: str
    endpoint: str | None = None


@dataclass
class Channel:
    """A clockwork channel."""

    name: str
    size: int
    publishers: list[str]
    subscribers: list[str]
    message_type: str
    message_size: int
    routes: list[Route] = field(default_factory=list)


@dataclass
class Entity:
    """An entity with inputs / outputs."""

    name: str
    process: str
    outputs: list[str]
    inputs: list[str]


@dataclass
class Process:
    """A process with entities."""

    name: str
    cpu: str
    entities: list[str]


@dataclass
class Cpu:
    """A physical cpu."""

    name: str
    processes: list[str]


@dataclass
class System:
    """A complete system."""

    cpus: dict[str, Cpu]
    entities: dict[str, Entity]
    channels: dict[str, Channel]
    processes: dict[str, Process]


def save_system(system: System, io_handle: typing.BinaryIO) -> None:
    """Write system to an io handle."""
    # Validate the system before saving.  Avoids needing to validate
    # one very load.
    pickle.dump(validate_system(system), io_handle)


def load_system(io_handle: typing.BinaryIO) -> System:
    """Write system to an io handle."""
    system = pickle.load(io_handle)  # noqa: S301 The data loaded is produced internally by the summarize tool.
    if not isinstance(system, System):
        msg = f"Loaded an unknown object of type {type(system)}."
        raise TypeError(msg)
    return system


def _validate_names(system: System) -> None:
    """Validate names."""
    dict_fields = (system.cpus, system.entities, system.channels)

    for dict_field in dict_fields:
        for key, value in dict_field.items():
            if key != value.name:
                msg = f"Entity {value.name} has unexpected key {key}."
                raise ValueError(msg)


def _validate_entities_in_procs(system: System) -> None:
    """Validate entities are mapped to the correct processes."""
    procs = {proc.name: set(proc.entities) for proc in system.processes.values()}
    for entity in system.entities.values():
        procs_with_entity = [proc for proc in procs if entity.name in procs[proc]]
        if procs_with_entity != [entity.process]:
            msg = (
                f"Entity {entity.name} is expected to be in process {entity.process}, but found in {procs_with_entity}"
            )
            raise ValueError(msg)

        procs[entity.process].remove(entity.name)

    for proc_name, proc_entities in procs.items():
        if not proc_entities:
            continue
        msg = f"Process {proc_name} expected additional entities that didn't exist: {', '.join(proc_entities)}"
        raise ValueError(msg)


def _validate_procs_in_cpus(system: System) -> None:
    """Validate processes are mapped to the correct nodes."""
    cpus = {cpu.name: set(cpu.processes) for cpu in system.cpus.values()}
    for proc in system.processes.values():
        cpus_with_proc = [cpu for cpu in cpus if proc.name in cpus[cpu]]
        if cpus_with_proc != [proc.cpu]:
            msg = f"Process {proc.name} is expected to be in cpu {proc.cpu}, but found in {cpus_with_proc}."
            raise ValueError(msg)

        cpus[proc.cpu].remove(proc.name)

    for cpu_name, cpu_processes in cpus.items():
        if not cpu_processes:
            continue
        msg = f"CPU {cpu_name} expected additional processes that didn't exist: {', '.join(cpu_processes)}"
        raise ValueError(msg)


def _validate_publishers(system: System) -> None:
    """Validate channels are published by the right publisher."""
    publishers = {channel.name: set(channel.publishers) for channel in system.channels.values()}
    for channel_name, publisher_names in publishers.items():
        if not publisher_names:
            msg = f"Channel {channel_name} has no publishers."
            raise ValueError(msg)

    for entity in system.entities.values():
        for output_name in entity.outputs:
            if output_name not in publishers:
                msg = f"Entity {entity.name} has an unknown output {output_name}."
                raise ValueError(msg)
            if entity.name not in publishers[output_name]:
                msg = f"Channel {output_name} is not expecting {entity.name} to be a publisher."
                raise ValueError(msg)

            publishers[output_name].remove(entity.name)

    for channel_name, channel_publishers in publishers.items():
        if not channel_publishers:
            continue
        msg = f"Channel {channel_name} is expecting publishers: {' '.join(channel_publishers)}"
        raise ValueError(msg)


def _validate_subscribers(system: System) -> None:
    """Validate channels are published by the right publisher."""
    subscribers = {channel.name: set(channel.subscribers) for channel in system.channels.values()}
    for entity in system.entities.values():
        for input_name in entity.inputs:
            if input_name not in subscribers:
                msg = f"Entity {entity.name} has an unknown input {input_name}."
                raise ValueError(msg)
            if entity.name not in subscribers[input_name]:
                msg = f"Channel {input_name} is not expecting {entity.name} to be a subscriber."
                raise ValueError(msg)

            subscribers[input_name].remove(entity.name)

    for channel_name, channel_subscribers in subscribers.items():
        if not channel_subscribers:
            continue
        msg = f"Channel {channel_name} is expecting subscribers: {' '.join(channel_subscribers)}"
        raise ValueError(msg)


def validate_system(system: System) -> System:
    """Validate the structure of the system."""
    _validate_names(system)
    _validate_entities_in_procs(system)
    _validate_procs_in_cpus(system)
    _validate_publishers(system)
    _validate_subscribers(system)
    return system


def channel_to_publisher_cpu_mapping(system: System) -> dict[str, str]:
    """Produce a mapping from channel to cpu where the publisher lives."""
    mapping = {}
    for entity in system.entities.values():
        for channel_name in entity.outputs:
            if channel_name in mapping:
                msg = f'A publisher already exists for channel: "{channel_name}"'
                raise RuntimeError(msg)
            mapping[channel_name] = system.processes[entity.process].cpu

    return mapping


def channel_to_subscriber_cpu_mapping(system: System) -> dict[str, set[str]]:
    """Produce a mapping from channel to every cpu where a subscriber lives."""
    mapping = {}
    for entity in system.entities.values():
        for channel_name in entity.inputs:
            mapping.setdefault(channel_name, set()).add(system.processes[entity.process].cpu)

    return mapping


@dataclass
class ChannelFlow:
    """Describes channels local or in/out of a cpu."""

    local_channels: set[str]
    inbound_channels: set[str]
    outbound_channels: set[str]


def channel_flow_for_cpu(
    cpu_name: str,
    system: System,
    channel_to_cpu_publisher: dict[str, str],
    channel_to_subscriber_cpu: dict[str, set[str]],
) -> ChannelFlow:
    """Extract channel flow for a cpu."""
    flow = ChannelFlow(
        local_channels=set(),
        inbound_channels=set(),
        outbound_channels=set(),
    )
    for entity in itertools.chain.from_iterable(
        system.processes[process].entities for process in system.cpus[cpu_name].processes
    ):
        for channel_name in system.entities[entity].outputs:
            subscribed_cpus = channel_to_subscriber_cpu.get(channel_name)
            if not subscribed_cpus:
                # No registered subscribers, assume this is logged only.
                flow.local_channels.add(channel_name)
            elif len(subscribed_cpus) > 1 or cpu_name not in subscribed_cpus:
                flow.outbound_channels.add(channel_name)

        for channel_name in system.entities[entity].inputs:
            publisher_cpu = channel_to_cpu_publisher[channel_name]
            if publisher_cpu == cpu_name:
                flow.local_channels.add(channel_name)
            else:
                flow.inbound_channels.add(channel_name)
    return flow
