# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for topology library."""

# pyright: reportPrivateUsage=false
from pathlib import Path

import pytest
from clockwork.tests.support.py_test_utils import fix_clockwork_path
from clockwork.tools.topology import topology


@pytest.fixture()
def expected_system() -> topology.System:
    cpu_1 = "TestSystemCpu1"
    cpu_2 = "TestSystemCpu2"
    cpus = [cpu_1, cpu_2]
    entities = [
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_socket",
            inputs=[],
            outputs=["SourceChan"],
            cpu=cpu_1,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog",
            inputs=["SourceChan"],
            outputs=["MultiNodeChan"],
            cpu=cpu_1,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog",
            inputs=["MultiNodeChan"],
            outputs=["SinkChan"],
            cpu=cpu_2,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_socket",
            inputs=["SinkChan"],
            outputs=[],
            cpu=cpu_2,
        ),
    ]
    channels = [
        topology.Channel(
            name="SourceChan",
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.SourceMessage",
            message_size=8,
        ),
        topology.Channel(
            name="MultiNodeChan",
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage",
            message_size=8,
        ),
        topology.Channel(
            name="SinkChan",
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.SinkMessage",
            message_size=8,
        ),
    ]
    entity_dict = {entity.name: entity for entity in entities}
    cpu_dict = {
        cpu: topology.Cpu(name=cpu, entities=sorted(entity.name for entity in entities if entity.cpu == cpu))
        for cpu in cpus
    }
    for channel in channels:
        channel.publishers = sorted([entity.name for entity in entities if channel.name in entity.outputs])
        channel.subscribers = sorted([entity.name for entity in entities if channel.name in entity.inputs])
    channel_dict = {channel.name: channel for channel in channels}

    return topology.System(
        cpus=cpu_dict,
        entities=entity_dict,
        channels=channel_dict,
    )


def test_validate_expected_system(expected_system: topology.System) -> None:
    topology.validate_system(expected_system)


def test_summarize(expected_system: topology.System) -> None:
    path = fix_clockwork_path(Path("clockwork/tools/topology/tests/test_system_multi_node_topology_summary.pkl"))
    with path.open("rb") as f:
        loaded_system = topology.load_system(f)
    assert expected_system == loaded_system
