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
    proc_1 = "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_process_1"
    proc_2 = "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_process_2"
    procs = [proc_1, proc_2]
    entities = [
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_socket",
            inputs=[],
            outputs=["SourceChan"],
            process=proc_1,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog",
            inputs=["SourceChan"],
            outputs=["MultiNodeChan"],
            process=proc_1,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog",
            inputs=["MultiNodeChan"],
            outputs=["SinkChan"],
            process=proc_2,
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_socket",
            inputs=["SinkChan"],
            outputs=[],
            process=proc_2,
        ),
    ]
    channels = [
        topology.Channel(
            name="SourceChan",
            size=9,
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.SourceMessage",
            message_size=8,
        ),
        topology.Channel(
            name="MultiNodeChan",
            size=9,
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage",
            message_size=8,
            routes=[topology.Route(source_cpu=cpu_1, dest_cpu=cpu_2, bridge_type="tcp", endpoint="1111")],
        ),
        topology.Channel(
            name="SinkChan",
            size=9,
            publishers=[],
            subscribers=[],
            message_type="@clockwork::clockwork::tests::support::test_messages_multi_node.SinkMessage",
            message_size=8,
        ),
    ]
    entity_dict = {entity.name: entity for entity in entities}
    process_dict = {
        proc: topology.Process(
            name=proc,
            cpu=cpu,
            entities=sorted(entity.name for entity in entity_dict.values() if entity.process == proc),
        )
        for proc, cpu in zip(procs, cpus, strict=True)
    }
    cpu_dict = {
        cpu: topology.Cpu(name=cpu, processes=sorted(proc.name for proc in process_dict.values() if proc.cpu == cpu))
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
        processes=process_dict,
    )


def test_validate_expected_system(expected_system: topology.System) -> None:
    topology.validate_system(expected_system)


def test_summarize(expected_system: topology.System) -> None:
    path = fix_clockwork_path(Path("clockwork/tools/topology/tests/test_system_multi_node_topology_summary.pkl"))
    with path.open("rb") as f:
        loaded_system = topology.load_system(f)
    assert expected_system == loaded_system


def test_multi_route_channel() -> None:
    """Test that a channel with multiple subscribers on different CPUs has multiple routes."""
    path = fix_clockwork_path(Path("clockwork/tools/topology/tests/test_system_multi_route_topology_summary.pkl"))
    with path.open("rb") as f:
        loaded_system = topology.load_system(f)

    # Validate the loaded system
    topology.validate_system(loaded_system)

    # Check that MultiRouteChan exists and has subscribers on multiple CPUs
    assert "MultiRouteChan" in loaded_system.channels
    chan = loaded_system.channels["MultiRouteChan"]

    # The channel should have subscribers on both CPU2 and CPU3
    assert len(chan.subscribers) == 2

    # Should have 2 routes: one to each subscriber CPU
    assert len(chan.routes) == 2

    # Sort routes by destination CPU for deterministic testing
    routes_sorted = sorted(chan.routes, key=lambda r: r.dest_cpu)

    # Route to CPU2
    assert routes_sorted[0].source_cpu == "MultiRouteCpu1"
    assert routes_sorted[0].dest_cpu == "MultiRouteCpu2"
    assert routes_sorted[0].bridge_type == "tcp"
    assert routes_sorted[0].endpoint is not None  # Should have port info

    # Route to CPU3
    assert routes_sorted[1].source_cpu == "MultiRouteCpu1"
    assert routes_sorted[1].dest_cpu == "MultiRouteCpu3"
    assert routes_sorted[1].bridge_type == "tcp"
    assert routes_sorted[1].endpoint is not None  # Should have port info
