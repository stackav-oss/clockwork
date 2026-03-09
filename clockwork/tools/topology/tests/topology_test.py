# Copyright 2025 Stack AV Co.
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
    entities = [
        topology.Entity(
            name="source_socket",
            inputs=[],
            outputs=["source_chan"],
            process=proc_a,
        ),
        topology.Entity(
            name="source_cog",
            inputs=["source_chan"],
            outputs=["multi_node_chan"],
            process=proc_a,
        ),
        topology.Entity(
            name="sink_cog",
            inputs=["multi_node_chan"],
            outputs=["sink_chan"],
            process=proc_b,
        ),
        topology.Entity(
            name="sink_socket",
            inputs=["sink_chan"],
            outputs=[],
            process=proc_b,
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

    return topology.System(
        cpus=cpu_dict,
        entities=entity_dict,
        channels=channel_dict,
        processes=processes,
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
