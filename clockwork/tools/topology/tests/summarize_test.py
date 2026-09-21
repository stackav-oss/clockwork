# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for topology library."""

# pyright: reportPrivateUsage=false
from pathlib import Path

import pytest
from clockwork.tests.support.py_test_utils import fix_clockwork_path
from clockwork.tools.topology import summarize, topology


@pytest.fixture()
def expected_system() -> topology.System:
    cpu_1 = "TestSystemCpu1"
    cpu_2 = "TestSystemCpu2"
    cpus = [cpu_1, cpu_2]
    proc_1 = "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_process_1"
    proc_2 = "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_process_2"
    procs = [proc_1, proc_2]
    memory_resources = [
        topology.Memory(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_memory",
            uuid="dcdfff8c-8f74-58db-bc94-5eaa3af3d897",
            type="HeapMemory",
            size_bytes=1_000_000,
            entities=[
                "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog"
            ],
            states=[],
        ),
        topology.Memory(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_memory",
            uuid="33e2feb6-a005-50ff-b645-6b59d4ba676e",
            type="HeapMemory",
            size_bytes=1_000_000,
            entities=[
                "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog"
            ],
            states=[],
        ),
        topology.Memory(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.state_memory",
            uuid="c52a184a-1efa-534e-ac12-683d925e1fa8",
            type="HeapMemory",
            size_bytes=1_000_000,
            entities=[],
            states=[
                "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state"
            ],
        ),
    ]

    states = [
        topology.State(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_state",
            uuid="c4498425-8c96-5c75-96c6-92bc59c699f9",
            is_extern=False,
            type="@clockwork::clockwork::tests::support::test_messages_multi_node::SourceState",
            memory_resource="",
            entities=[
                "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog"
            ],
        ),
        topology.State(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state",
            uuid="ef9f24a0-1e61-5438-94f9-47a20ed88bd1",
            is_extern=True,
            type="SinkState",
            memory_resource="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.state_memory",
            entities=[
                "@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog"
            ],
        ),
    ]

    entities = [
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_socket",
            uuid="46d76a2a-a78e-552d-92cb-8e51e3f13539",
            inputs=[],
            outputs=["SourceChan"],
            process=proc_1,
            states=[],
            memory_resources=[],
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog",
            uuid="30d36ffe-fa36-5ebd-a191-1ab6bc7a2613",
            inputs=["SourceChan"],
            outputs=["MultiNodeChan"],
            process=proc_1,
            states=[
                topology.Endpoint(
                    name="source_state",
                    entity="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_state",
                )
            ],
            memory_resources=[
                topology.Endpoint(
                    name="source_memory",
                    entity="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_memory",
                )
            ],
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog",
            uuid="253672a0-4873-59cb-82d8-7fadc380caa2",
            inputs=["MultiNodeChan"],
            outputs=["SinkChan"],
            process=proc_2,
            states=[
                topology.Endpoint(
                    name="sink_state",
                    entity="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_state",
                )
            ],
            memory_resources=[
                topology.Endpoint(
                    name="sink_memory",
                    entity="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_memory",
                )
            ],
        ),
        topology.Entity(
            name="@clockwork::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_socket",
            uuid="2db31ac7-553a-5b5c-9b13-288c8df1398d",
            inputs=["SinkChan"],
            outputs=[],
            process=proc_2,
            states=[],
            memory_resources=[],
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

    memory_dict = {mem.name: mem for mem in memory_resources}

    state_dict = {state.name: state for state in states}

    return topology.System(
        cpus=cpu_dict,
        entities=entity_dict,
        channels=channel_dict,
        processes=process_dict,
        memory_resources=memory_dict,
        states=state_dict,
    )


def test_validate_expected_system(expected_system: topology.System) -> None:
    topology.validate_system(expected_system)


def test_summarize(expected_system: topology.System) -> None:
    path = fix_clockwork_path(Path("clockwork/tools/topology/tests/test_system_multi_node_topology_summary.pkl"))
    with path.open("rb") as f:
        loaded_system = topology.load_system(f)
    assert expected_system == loaded_system


def test_normalize_channel_names_dedupes_duplicate_entity_channel_inputs() -> None:
    assert summarize._normalize_channel_names(
        ["dup_chan", "dup_chan", "unknown_chan", "other_chan"],
        {"dup_chan", "other_chan"},
    ) == ["dup_chan", "other_chan"]


def test_logging_locations() -> None:
    path = fix_clockwork_path(Path("clockwork/tools/topology/tests/test_logging_topology_summary.pkl"))
    with path.open("rb") as f:
        loaded_system = topology.load_system(f)

    chan1 = loaded_system.channels["Chan1"]
    assert chan1.telemetry_log_locations == [
        topology.LogLocation(cpu="Cpu1"),
        topology.LogLocation(cpu="Cpu2", is_redundant=True),
    ]
    assert chan1.event_log_locations == chan1.telemetry_log_locations

    chan2 = loaded_system.channels["Chan2"]
    assert chan2.telemetry_log_locations == [topology.LogLocation(cpu="Cpu2")]
    assert chan2.event_log_locations == chan2.telemetry_log_locations

    multi_chan1 = loaded_system.unlisted_channels["MultiChan1"]
    assert multi_chan1.event_log_locations == [topology.LogLocation(cpu="Cpu1")]
    assert not multi_chan1.telemetry_log_locations


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
