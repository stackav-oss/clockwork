# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for topology library."""

# pyright: reportPrivateUsage=false
import re
from pathlib import Path

import pytest
from clockwork.tools.topology import topology


@pytest.fixture()
def fake_system() -> topology.System:
    cpu_a = "cpu_a"
    cpu_b = "cpu_b"
    proc_a = "proc_a"
    proc_b = "proc_b"
    cpus = [cpu_a, cpu_b]

    states = [
        topology.State(
            name="source_state",
            uuid="",
            is_extern=False,
            type="StateSchema",
            memory_resource="",
            entities=["source_cog"],
        ),
        topology.State(
            name="sink_state",
            uuid="",
            is_extern=True,
            type="StateClass",
            memory_resource="state_resource",
            entities=["sink_cog"],
        ),
    ]

    memory_resources = {
        "state_resource": topology.Memory(
            name="state_resource", uuid="", type="HeapMemory", size_bytes=3200, entities=[], states=["sink_state"]
        ),
        "sink_resource": topology.Memory(
            name="sink_resource", uuid="", type="HeapMemory", size_bytes=1600, entities=["sink_cog"], states=[]
        ),
    }

    entities = [
        topology.Entity(
            name="source_socket",
            uuid="",
            inputs=[],
            outputs=["source_chan"],
            process=proc_a,
            states=[],
            memory_resources=[],
        ),
        topology.Entity(
            name="source_cog",
            uuid="",
            inputs=["source_chan"],
            outputs=["multi_node_chan"],
            process=proc_a,
            states=[topology.Endpoint(name="state", entity="source_state")],
            memory_resources=[],
        ),
        topology.Entity(
            name="sink_cog",
            uuid="",
            inputs=["multi_node_chan"],
            outputs=["sink_chan"],
            process=proc_b,
            states=[topology.Endpoint(name="state", entity="sink_state")],
            memory_resources=[topology.Endpoint(name="memory", entity="sink_resource")],
        ),
        topology.Entity(
            name="sink_socket",
            uuid="",
            inputs=["sink_chan"],
            outputs=[],
            process=proc_b,
            states=[],
            memory_resources=[],
        ),
    ]
    channels = [
        topology.Channel(
            name="source_chan",
            size=10,
            publishers=[],
            subscribers=[],
            message_type="SourceMessage",
            message_size=1,
        ),
        topology.Channel(
            name="multi_node_chan",
            size=10,
            publishers=[],
            subscribers=[],
            message_type="MultiNodeMessage",
            message_size=1,
        ),
        topology.Channel(
            name="sink_chan",
            size=10,
            publishers=[],
            subscribers=[],
            message_type="SinkMessage",
            message_size=1,
        ),
    ]
    processes = {
        proc: topology.Process(
            name=proc,
            cpu=cpu,
            entities=[entity.name for entity in entities if entity.process == proc],
        )
        for proc, cpu in zip([proc_a, proc_b], [cpu_a, cpu_b], strict=True)
    }
    entity_dict = {entity.name: entity for entity in entities}
    cpu_dict = {
        cpu: topology.Cpu(name=cpu, processes=[proc.name for proc in processes.values() if proc.cpu == cpu])
        for cpu in cpus
    }
    for channel in channels:
        channel.publishers = sorted(entity.name for entity in entities if channel.name in entity.outputs)
        channel.subscribers = sorted(entity.name for entity in entities if channel.name in entity.inputs)
    channel_dict = {channel.name: channel for channel in channels}

    memory_dict = {memory.name: memory for memory in memory_resources.values()}

    state_dict = {state.name: state for state in states}

    return topology.System(
        cpus=cpu_dict,
        entities=entity_dict,
        channels=channel_dict,
        processes=processes,
        memory_resources=memory_dict,
        states=state_dict,
    )


def test_save_load(fake_system: topology.System, tmp_path: Path) -> None:
    test_file = tmp_path / "fake_system.pkl"
    with test_file.open("wb") as f:
        topology.save_system(fake_system, f)
    with test_file.open("rb") as f:
        assert topology.load_system(f) == fake_system


def test_channel_to_publisher_cpu_mapping(fake_system: topology.System) -> None:
    assert topology.channel_to_publisher_cpu_mapping(fake_system) == {
        "source_chan": "cpu_a",
        "multi_node_chan": "cpu_a",
        "sink_chan": "cpu_b",
    }

    fake_system.entities["source_socket"].outputs.append("sink_chan")
    with pytest.raises(RuntimeError, match='A publisher already exists for channel: "sink_chan"'):
        topology.channel_to_publisher_cpu_mapping(fake_system)


def test_channel_to_subscriber_cpu_mapping(fake_system: topology.System) -> None:
    assert topology.channel_to_subscriber_cpu_mapping(fake_system) == {
        "source_chan": {"cpu_a"},
        "multi_node_chan": {"cpu_b"},
        "sink_chan": {"cpu_b"},
    }


def test_channel_flow_for_cpu(fake_system: topology.System) -> None:
    assert topology.channel_flow_for_cpu(
        "cpu_a",
        fake_system,
        topology.channel_to_publisher_cpu_mapping(fake_system),
        topology.channel_to_subscriber_cpu_mapping(fake_system),
    ) == topology.ChannelFlow(
        local_channels={"source_chan"},
        outbound_channels={"multi_node_chan"},
        inbound_channels=set(),
    )

    assert topology.channel_flow_for_cpu(
        "cpu_b",
        fake_system,
        topology.channel_to_publisher_cpu_mapping(fake_system),
        topology.channel_to_subscriber_cpu_mapping(fake_system),
    ) == topology.ChannelFlow(
        local_channels={"sink_chan"},
        outbound_channels=set(),
        inbound_channels={"multi_node_chan"},
    )


def test_channel_logging_classification(fake_system: topology.System) -> None:
    channel = fake_system.channels["multi_node_chan"]
    channel.telemetry_log_locations = [
        topology.LogLocation(cpu="cpu_a"),
        topology.LogLocation(cpu="cpu_b", is_redundant=True),
    ]

    assert channel.is_telemetry_logged
    assert channel.is_redundant_telemetry_logged
    assert not channel.is_non_redundant_telemetry_logged
    assert not channel.is_event_logged

    channel.event_log_locations = [topology.LogLocation(cpu="cpu_a")]
    assert channel.is_event_logged


def test_mismatched_cpu_name(fake_system: topology.System) -> None:
    fake_system.cpus["cpu_a"].name = "cpu_x"
    with pytest.raises(ValueError, match=r"Entity cpu_x has unexpected key cpu_a"):
        topology.validate_system(fake_system)


def test_mismatched_entity_name(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].name = "some_socket"
    with pytest.raises(ValueError, match=r"Entity some_socket has unexpected key source_socket"):
        topology.validate_system(fake_system)


def test_mismatched_channel_name(fake_system: topology.System) -> None:
    fake_system.channels["source_chan"].name = "another_chan"
    with pytest.raises(ValueError, match=r"Entity another_chan has unexpected key source_chan"):
        topology.validate_system(fake_system)


def test_mismatched_memory_resource_name(fake_system: topology.System) -> None:
    fake_system.memory_resources["state_resource"].name = "another_resource"
    with pytest.raises(ValueError, match=r"Entity another_resource has unexpected key state_resource"):
        topology.validate_system(fake_system)


def test_mismatched_state_name(fake_system: topology.System) -> None:
    fake_system.states["source_state"].name = "another_state"
    with pytest.raises(ValueError, match=r"Entity another_state has unexpected key source_state"):
        topology.validate_system(fake_system)


def test_invalid_process_for_entity(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].process = "unknown_process"
    with pytest.raises(
        ValueError,
        match=re.escape(r"Entity source_socket is expected to be in process unknown_process, but found in ['proc_a']"),
    ):
        topology.validate_system(fake_system)


def test_process_missing_entity(fake_system: topology.System) -> None:
    fake_system.processes["proc_a"].entities.remove("source_socket")
    with pytest.raises(
        ValueError, match=re.escape(r"Entity source_socket is expected to be in process proc_a, but found in []")
    ):
        topology.validate_system(fake_system)


def test_multiple_processes_containe_entity(fake_system: topology.System) -> None:
    fake_system.processes["proc_b"].entities.append("source_socket")
    with pytest.raises(
        ValueError,
        match=re.escape(r"Entity source_socket is expected to be in process proc_a, but found in ['proc_a', 'proc_b']"),
    ):
        topology.validate_system(fake_system)


def test_invalid_cpu_for_process(fake_system: topology.System) -> None:
    fake_system.processes["proc_a"].cpu = "unknown_cpu"
    with pytest.raises(
        ValueError, match=re.escape(r"Process proc_a is expected to be in cpu unknown_cpu, but found in ['cpu_a']")
    ):
        topology.validate_system(fake_system)


def test_cpu_missing_process(fake_system: topology.System) -> None:
    fake_system.cpus["cpu_a"].processes.remove("proc_a")
    with pytest.raises(ValueError, match=re.escape(r"Process proc_a is expected to be in cpu cpu_a, but found in []")):
        topology.validate_system(fake_system)


def test_multiple_cpus_contain_process(fake_system: topology.System) -> None:
    fake_system.cpus["cpu_b"].processes.append("proc_a")
    with pytest.raises(
        ValueError, match=re.escape(r"Process proc_a is expected to be in cpu cpu_a, but found in ['cpu_a', 'cpu_b']")
    ):
        topology.validate_system(fake_system)


def test_cpu_contains_phantom_entity(fake_system: topology.System) -> None:
    fake_system.cpus["cpu_b"].processes.append("phantom_process")
    with pytest.raises(ValueError, match=r"CPU cpu_b expected additional processes that didn't exist: phantom_process"):
        topology.validate_system(fake_system)


def test_no_publishers(fake_system: topology.System) -> None:
    fake_system.channels["source_chan"].publishers = []
    with pytest.raises(ValueError, match=r"Channel source_chan has no publishers."):
        topology.validate_system(fake_system)


def test_entity_has_phantom_output(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].outputs.append("phantom_output")
    with pytest.raises(ValueError, match=r"Entity source_socket has an unknown output phantom_output."):
        topology.validate_system(fake_system)


def test_extraneous_publisher(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].outputs.append("sink_chan")
    with pytest.raises(ValueError, match=r"Channel sink_chan is not expecting source_socket to be a publisher."):
        topology.validate_system(fake_system)


def test_missing_publisher(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].outputs.remove("source_chan")
    with pytest.raises(ValueError, match=r"Channel source_chan is expecting publishers: source_socket"):
        topology.validate_system(fake_system)


def test_entity_has_phantom_input(fake_system: topology.System) -> None:
    fake_system.entities["sink_socket"].inputs.append("phantom_input")
    with pytest.raises(ValueError, match=r"Entity sink_socket has an unknown input phantom_input."):
        topology.validate_system(fake_system)


def test_extraneous_subscriber(fake_system: topology.System) -> None:
    fake_system.entities["sink_socket"].inputs.append("source_chan")
    with pytest.raises(ValueError, match=r"Channel source_chan is not expecting sink_socket to be a subscriber."):
        topology.validate_system(fake_system)


def test_missing_subscriber(fake_system: topology.System) -> None:
    fake_system.entities["sink_socket"].inputs.remove("sink_chan")
    with pytest.raises(ValueError, match=r"Channel sink_chan is expecting subscribers: sink_socket"):
        topology.validate_system(fake_system)


def test_entity_has_phantom_memory_resource(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].memory_resources.append(
        topology.Endpoint(name="memory", entity="unknown_memory_resource")
    )
    with pytest.raises(
        ValueError, match=r"Entity source_socket has an unknown memory resource unknown_memory_resource."
    ):
        topology.validate_system(fake_system)


def test_entity_has_unexpected_memory_resource(fake_system: topology.System) -> None:
    fake_system.memory_resources["sink_resource"].entities.clear()
    with pytest.raises(
        ValueError,
        match=r"Memory resource sink_resource is not expecting sink_cog to be an entity.",
    ):
        topology.validate_system(fake_system)


def test_memory_resource_missing_entity(fake_system: topology.System) -> None:
    fake_system.memory_resources["sink_resource"].entities.append("source_cog")
    with pytest.raises(
        ValueError,
        match=r"Memory resource sink_resource is expecting entities: source_cog",
    ):
        topology.validate_system(fake_system)


def test_state_has_phantom_memory_resource(fake_system: topology.System) -> None:
    fake_system.states["source_state"].memory_resource = "unknown_memory_resource"
    with pytest.raises(ValueError, match=r"State source_state has an unknown memory resource unknown_memory_resource."):
        topology.validate_system(fake_system)


def test_state_has_unexpected_memory_resource(fake_system: topology.System) -> None:
    fake_system.memory_resources["state_resource"].states.clear()
    with pytest.raises(
        ValueError,
        match=r"Memory resource state_resource is not expecting sink_state to be a state.",
    ):
        topology.validate_system(fake_system)


def test_memory_resource_missing_state(fake_system: topology.System) -> None:
    fake_system.memory_resources["state_resource"].states.append("source_state")
    with pytest.raises(
        ValueError,
        match=r"Memory resource state_resource is expecting states: source_state",
    ):
        topology.validate_system(fake_system)


def test_entity_has_phantom_state(fake_system: topology.System) -> None:
    fake_system.entities["source_socket"].states.append(topology.Endpoint(name="state", entity="unknown_state"))
    with pytest.raises(ValueError, match=r"Entity source_socket has an unknown state unknown_state."):
        topology.validate_system(fake_system)


def test_entity_has_unexpected_state(fake_system: topology.System) -> None:
    fake_system.states["source_state"].entities.clear()
    with pytest.raises(
        ValueError,
        match=r"State source_state is not expecting source_cog to be an entity.",
    ):
        topology.validate_system(fake_system)


def test_state_missing_entity(fake_system: topology.System) -> None:
    fake_system.states["source_state"].entities.append("sink_cog")
    with pytest.raises(
        ValueError,
        match=r"State source_state is expecting entities: sink_cog",
    ):
        topology.validate_system(fake_system)
