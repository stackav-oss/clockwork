# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Summarize topology of a system target."""

from __future__ import annotations

import itertools
from pathlib import Path

import click
import root_repo_py
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.logical_system_interface import LogicalSystemInterface
from clockwork.tools.topology import topology


def insert_or_get_channel(system: topology.System, name: str, message_type: str, message_size: int) -> topology.Channel:
    """Get or insert channel."""
    return system.channels.setdefault(
        name,
        topology.Channel(
            name=name, publishers=[], subscribers=[], message_type=message_type, message_size=message_size
        ),
    )


def extract_topology(lsi: LogicalSystemInterface) -> topology.System:
    """Extract topology information from a logical system interface."""
    init_cog_channels = set(
        itertools.chain.from_iterable(cog.get_output_channel_names() for cog in lsi.get_cogs() if cog.is_init())
    )
    channel_data = {
        channel.get_name(): (channel.get_message_repr_name(), channel.get_message_size_bytes())
        for channel in lsi.get_channels()
        if not channel.is_multi_producer() and channel.get_name() not in init_cog_channels
    }

    def filter_channel_names(channel_names: list[str]) -> list[str]:
        return [name for name in channel_names if name in channel_data]

    system = topology.System(cpus={}, entities={}, channels={})
    for cpu in lsi.get_cpu_domains():
        system.cpus[cpu.get_name()] = topology.Cpu(name=cpu.get_name(), entities=[])

    all_entities = itertools.chain((cog for cog in lsi.get_cogs() if not cog.is_init()), lsi.get_io_connections())

    for entity in all_entities:
        cpu_domain = lsi.get_cpu_domain_for_entity(entity.get_uuid()).get_name()
        entity_outputs = filter_channel_names(entity.get_output_channel_names())
        entity_inputs = filter_channel_names(entity.get_input_channel_names())
        system.entities[entity.get_name()] = topology.Entity(
            name=entity.get_name(),
            cpu=cpu_domain,
            outputs=entity_outputs,
            inputs=entity_inputs,
        )
        system.cpus[cpu_domain].entities.append(entity.get_name())

        for output_name in entity_outputs:
            message_type, message_size = channel_data[output_name]
            channel = insert_or_get_channel(system, output_name, message_type, message_size)
            channel.publishers.append(entity.get_name())

        for input_name in entity_inputs:
            message_type, message_size = channel_data[input_name]
            channel = insert_or_get_channel(system, input_name, message_type, message_size)
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
    with output_file.open("wb") as f:
        topology.save_system(system, f)


if __name__ == "__main__":
    main()
