# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests the LogicalSystemInterface functionality."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.composition.system import Channel
from clockwork.dsl.ir.compiler import compile_source_file
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.logical_system_interface import CogInterface, LogicalSystemInterface


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compile_source_file)


@pytest.fixture(scope="module")
def test_system_multi_node() -> LogicalSystemInterface:
    return LogicalSystemInterface(
        ModuleID.from_path(CLK_REPO, Path("clockwork/tests/support/test_system_multi_node.clk"))
    )


def check_publisher_cog_expectations(logical_system: LogicalSystemInterface, cog: CogInterface) -> None:
    """Check the expectations of the publisher cog."""
    state_fqn = f"@{CLK_REPO}::clockwork::tests::support::test_system_description.test_system.test_cogs_box.state"
    publisher_cog_connectables = cog.get_connectables()
    assert len(publisher_cog_connectables) == 1
    publisher_cog_connectable = publisher_cog_connectables[0]
    assert state_fqn == publisher_cog_connectable.get_name()
    endpoints = publisher_cog_connectable.get_endpoints()
    # same state is connected to both cogs
    assert len(endpoints) == 3
    found_connected_endpoint = False
    found_disconnected_endpoint = False
    for endpoint in endpoints:
        if logical_system.is_endpoint_connected_to_cog(endpoint, cog):
            found_connected_endpoint = True
        else:
            found_disconnected_endpoint = True
    assert found_connected_endpoint
    assert found_disconnected_endpoint
    for endpoint in endpoints:
        assert endpoint.is_udp_socket() is False
        assert endpoint.is_log_producer() is False


def test_logical_system_interface(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915  For testing only
    """Test the LogicalSystemInterface.

    Note:
        This test can't be easily split into multiple because LogicalSystem policies are accessed globally..
    """
    # load a system with some cogs
    # input_log -> Chan1 -> subscriber_cog -> Chan2 -> publisher_cog -> Chan3 -> output_log
    logical_system = LogicalSystemInterface(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/tests/support/test_system_description.clk"),
        ),
        fs_importer,
    )

    # load an empty system
    logical_system_2 = LogicalSystemInterface(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/tests/support/test_system_description_2.clk"),
        ),
        fs_importer,
        compatible_with=logical_system,
    )
    logical_system_2.import_context_from(logical_system)

    # test getters
    assert len(logical_system.get_channels()) == 10
    channel_1 = logical_system.get_channel("Chan1")
    assert channel_1 is not None
    assert channel_1.get_name() == "Chan1"
    channel_1_producers = channel_1.get_producers()
    assert len(channel_1_producers) == 1
    channel_1_observers = channel_1.get_observers()
    assert len(channel_1_observers) == 1
    channel_3 = logical_system.get_channel("Chan3")
    assert channel_3 is not None
    assert channel_3.has_log_writer_policy() is True
    assert channel_3.is_persistent() is False
    channel_4 = logical_system.get_channel("Chan4")
    assert channel_4 is not None
    assert channel_4.has_log_writer_policy() is True
    assert channel_4.is_persistent() is True
    assert channel_1.get_message_size_bytes() > 0
    assert channel_1.get_message_size_bytes() == channel_3.get_message_size_bytes()
    assert channel_1.get_queue_size() == 12
    channel_1.set_queue_size(2)
    assert channel_1.get_queue_size() == 2
    channel_1_producer = channel_1_producers[0]
    assert channel_1_producer.is_udp_socket() is False
    assert channel_1_producer.is_log_producer() is True
    processes = logical_system.get_processes()
    assert len(processes) == 1
    system_cogs = logical_system.get_cogs()
    assert len(system_cogs) == 3
    cog_names = {cog.get_name() for cog in system_cogs}
    publisher_cog_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::test_system_description.test_system.test_cogs_box.publisher_cog"
    )
    assert publisher_cog_fqn in cog_names
    assert (
        f"@{CLK_REPO}::clockwork::tests::support::test_system_description.test_system.test_cogs_box.subscriber_cog"
        in cog_names
    )
    for cog in system_cogs:
        if cog.get_name() == publisher_cog_fqn:
            check_publisher_cog_expectations(logical_system, cog)
    cpu_domains = logical_system.get_cpu_domains()
    assert len(cpu_domains) == 1
    assert cpu_domains[0].get_name() == "TestSystemCpu"
    logical_system.bind_cpu_domain_policy_to_process(cpu_domains[0], processes[0])

    # test modifiers
    logical_system.assign_all_entities_to_process(processes[0])

    new_output_channel_name = "some_new_output_channel"
    new_input_channel_name = "some_new_input_channel"
    channel_1.add_log_producer(alternative_output_channel_name=new_output_channel_name)
    assert len(logical_system.get_channels()) == 11
    new_channel = logical_system.get_channel(new_output_channel_name)
    assert new_channel is not None
    new_channel_producers = new_channel.get_producers()
    assert len(new_channel_producers) == 1
    assert new_channel_producers[0].is_log_producer() is True

    channel_1_producers = channel_1.get_producers()
    assert len(channel_1_producers) == 1
    logical_system.add_log_producer(channel_1, alternative_input_channel_name=new_input_channel_name)
    channel_1_producers = channel_1.get_producers()
    # we don't allow two log producers for the same channel
    assert len(channel_1_producers) == 1

    assert channel_1.has_log_writer_policy() is False
    logical_system.add_telemetry_log_observers([channel_1])
    assert channel_1.has_log_writer_policy() is True

    # test adding entities to a different system
    logical_system_2.clear_policies()
    logical_system_2.bind_cpu_domain_policy_to_process(cpu_domains[0], processes[0])
    new_cpu_domain = logical_system_2.add_cpu_domain(cpu_domains[0])
    logical_system_2.add_process(processes[0], new_cpu_domain)
    for cog in logical_system.get_cogs():
        logical_system_2.add_cog(cog)
        for channel in logical_system.get_channels():
            channel_name = channel.get_name()
            for observer in channel.get_observers():
                if logical_system.is_endpoint_connected_to_cog(observer, cog) and isinstance(channel.channel, Channel):
                    logical_system_2.add_channel(channel)
                    new_channel = logical_system_2.get_channel(channel_name)
                    assert new_channel is not None
                    assert new_channel.get_name() == channel.get_name()
                    assert new_channel.get_message_size_bytes() == channel.get_message_size_bytes()
                    logical_system_2.connect_channel_observer(new_channel, observer)
            for producer in channel.get_producers():
                if isinstance(channel.channel, Channel) and logical_system.is_endpoint_connected_to_cog(producer, cog):
                    logical_system_2.add_channel(channel)
                    new_channel = logical_system_2.get_channel(channel_name)
                    assert new_channel is not None
                    logical_system_2.connect_channel_producer(new_channel, producer)
    assert (  # `some_new_channel` wasn't connected to a cog so didn't get brought over
        len(logical_system_2.get_channels()) == 10
    )
    assert len(logical_system_2.get_processes()) == 1
    assert len(logical_system_2.get_cogs()) == 3
    assert len(logical_system_2.get_cpu_domains()) == 1

    renamed_channel_1_name = "renamed_channel_1"
    renamed_channel_1_in_system_1 = logical_system_2.add_channel(
        channel_1, alternative_channel_name=renamed_channel_1_name
    )
    assert len(renamed_channel_1_in_system_1.get_observers()) == 0
    assert len(renamed_channel_1_in_system_1.get_producers()) == 0
    assert renamed_channel_1_in_system_1.get_name() == renamed_channel_1_name

    # test removals
    channel_2 = logical_system.get_channel("Chan2")
    assert channel_2 is not None
    logical_system.remove_channel(channel_2)
    assert logical_system.get_channel("Chan2") is None
    assert len(logical_system.get_channels()) == 10
    logical_system.remove_cog(system_cogs[0])
    assert len(logical_system.get_cogs()) == 2
    logical_system.remove_process(processes[0])
    assert len(logical_system.get_processes()) == 0
    assert channel_1.has_log_writer_policy() is True
    logical_system.clear_policies()
    assert channel_1.has_log_writer_policy() is False


def test_cog_interface_multi_node(test_system_multi_node: LogicalSystemInterface) -> None:
    cogs = {cog.get_name(): cog for cog in test_system_multi_node.get_cogs()}
    assert len(cogs) == 3
    source_cog_name = f"@{CLK_REPO}::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_cog"
    sink_cog_name = f"@{CLK_REPO}::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_cog"
    init_cog_name = f"@{CLK_REPO}::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.init_cog"
    assert {cog_name: cog.is_init() for cog_name, cog in cogs.items()} == {
        source_cog_name: False,
        sink_cog_name: False,
        init_cog_name: True,
    }

    io_conns = {io_conn.get_name(): io_conn for io_conn in test_system_multi_node.get_io_connections()}
    assert len(io_conns) == 2
    source_socket_name = f"@{CLK_REPO}::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_1.source_socket"
    sink_socket_name = f"@{CLK_REPO}::clockwork::tests::support::test_system_multi_node.test_system_multi_node.test_cogs_box_2.sink_socket"
    assert list(io_conns.keys()) == [source_socket_name, sink_socket_name]

    cpu_1 = "TestSystemCpu1"
    cpu_2 = "TestSystemCpu2"
    assert test_system_multi_node.get_cpu_domain_for_entity(io_conns[source_socket_name].get_uuid()).get_name() == cpu_1
    assert test_system_multi_node.get_cpu_domain_for_entity(cogs[source_cog_name].get_uuid()).get_name() == cpu_1

    assert test_system_multi_node.get_cpu_domain_for_entity(io_conns[sink_socket_name].get_uuid()).get_name() == cpu_2
    assert test_system_multi_node.get_cpu_domain_for_entity(cogs[sink_cog_name].get_uuid()).get_name() == cpu_2
    assert test_system_multi_node.get_cpu_domain_for_entity(cogs[init_cog_name].get_uuid()).get_name() == cpu_2

    channels = {channel.get_name(): channel for channel in test_system_multi_node.get_channels()}
    assert all(not channel.is_multi_producer() for channel in channels.values())

    source_chan_name = "SourceChan"
    sink_chan_name = "SinkChan"
    multi_node_chan_name = "MultiNodeChan"
    assert (
        channels[source_chan_name].get_message_repr_name()
        == f"@{CLK_REPO}::clockwork::tests::support::test_messages_multi_node.SourceMessage"
    )
    assert (
        channels[multi_node_chan_name].get_message_repr_name()
        == f"@{CLK_REPO}::clockwork::tests::support::test_messages_multi_node.MultiNodeMessage"
    )
    assert (
        channels[sink_chan_name].get_message_repr_name()
        == f"@{CLK_REPO}::clockwork::tests::support::test_messages_multi_node.SinkMessage"
    )

    assert io_conns[source_socket_name].get_input_channel_names() == []
    assert io_conns[source_socket_name].get_output_channel_names() == [source_chan_name]
    assert cogs[source_cog_name].get_input_channel_names() == [source_chan_name]
    assert cogs[source_cog_name].get_output_channel_names() == [multi_node_chan_name]
    assert cogs[sink_cog_name].get_input_channel_names() == [multi_node_chan_name]
    assert cogs[sink_cog_name].get_output_channel_names() == [sink_chan_name]
    assert io_conns[sink_socket_name].get_input_channel_names() == [sink_chan_name]
    assert io_conns[sink_socket_name].get_output_channel_names() == []

    source_cog = cogs[source_cog_name]
    assert source_cog.get_endpoint_by_input_name("message_output") is None
    assert (in_endpoint := source_cog.get_endpoint_by_input_name("message_input")) is not None
    assert (in_channel := in_endpoint.get_connected_channel()) is not None
    assert in_channel.get_name() == source_chan_name

    assert source_cog.get_endpoint_by_output_name("message_input") is None
    assert (out_endpoint := source_cog.get_endpoint_by_output_name("message_output")) is not None
    assert (out_channel := out_endpoint.get_connected_channel()) is not None
    assert out_channel.get_name() == multi_node_chan_name
    assert test_system_multi_node.is_endpoint_connected_to_cog(in_endpoint, source_cog)
    assert test_system_multi_node.is_endpoint_connected_to_cog(out_endpoint, source_cog)


def test_data_sources_and_state_restoration(fs_importer: FilesystemImporter) -> None:  # noqa: C901, PLR0912, PLR0915 # Test code only; we're testing a lot in this case
    """Test data source and state restoration interfaces added in the most recent commit.

    This test uses a dedicated test system with:
    - Multiple data sources (some connected, some not)
    - Multiple states (some connected, some not)
    - A cog to connect things to
    Then we test making new connections and setting fallbacks/init data sources.
    """
    logical_system = LogicalSystemInterface(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/tests/support/logical_system_interface_test_system.clk"),
        ),
        fs_importer,
    )

    data_sources = logical_system.get_data_sources()
    assert len(data_sources) == 3

    primary_config_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::logical_system_interface_test_system."
        "logical_system_interface_test_system.box.primary_config"
    )
    fallback_config_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::logical_system_interface_test_system."
        "logical_system_interface_test_system.box.fallback_config"
    )
    unconnected_config_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::logical_system_interface_test_system."
        "logical_system_interface_test_system.box.unconnected_config"
    )

    data_source_fqns = {ds.fqn for ds in data_sources}
    assert primary_config_fqn in data_source_fqns
    assert fallback_config_fqn in data_source_fqns
    assert unconnected_config_fqn in data_source_fqns

    primary_config_ds = None
    fallback_config_ds = None
    unconnected_config_ds = None
    for ds in data_sources:
        assert ds.uuid is not None
        assert ds.fqn is not None
        assert len(ds.fqn) > 0

        if ds.fqn == primary_config_fqn:
            primary_config_ds = ds
        elif ds.fqn == fallback_config_fqn:
            fallback_config_ds = ds
        elif ds.fqn == unconnected_config_fqn:
            unconnected_config_ds = ds

    assert primary_config_ds is not None
    assert fallback_config_ds is not None
    assert unconnected_config_ds is not None

    # Test DataSourceInterface.set_fallback() - set fallback for primary config
    primary_config_ds.set_fallback(fallback_config_ds)

    # Test get_state_instances() - should have 2 states
    state_instances = logical_system.get_state_instances()
    assert len(state_instances) == 2

    connected_state_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::logical_system_interface_test_system."
        "logical_system_interface_test_system.box.connected_state"
    )
    unconnected_state_fqn = (
        f"@{CLK_REPO}::clockwork::tests::support::logical_system_interface_test_system."
        "logical_system_interface_test_system.box.unconnected_state"
    )

    state_names = {state.get_name() for state in state_instances}
    assert connected_state_fqn in state_names
    assert unconnected_state_fqn in state_names

    connected_state = None
    unconnected_state = None
    for state in state_instances:
        if state.get_name() == connected_state_fqn:
            connected_state = state
        elif state.get_name() == unconnected_state_fqn:
            unconnected_state = state

    assert connected_state is not None
    assert unconnected_state is not None

    # Test ConnectableInterface.set_init_data_source() on the unconnected state
    unconnected_state.set_init_data_source(fallback_config_ds)

    cogs = logical_system.get_cogs()
    assert len(cogs) == 1
    test_cog = cogs[0]

    # Test get_config_endpoint_by_name() - cog should have a "config" endpoint
    config_endpoint = test_cog.get_config_endpoint_by_name("config")
    assert config_endpoint is not None

    # Test get_config_endpoint_by_name() with invalid name
    nonexistent_endpoint = test_cog.get_config_endpoint_by_name("nonexistent")
    assert nonexistent_endpoint is None

    # Test connect_config_by_name() - connect the unconnected_config to the cog
    # First, get the connectables before the connection to see what's already there
    connectables_before = test_cog.get_connectables()

    # Connect unconnected_config to the cog's config endpoint
    test_cog.connect_config_by_name("config", unconnected_config_ds)

    # After connecting, verify the connection appears in get_connectables()
    connectables_after = test_cog.get_connectables()
    # Should still have the same number of connectables (we're just changing which data source is connected)
    assert len(connectables_after) == len(connectables_before)

    # Verify that unconnected_config now appears in the connectables
    found_unconnected_config = False
    for connectable in connectables_after:
        endpoints = connectable.get_endpoints()
        for endpoint in endpoints:
            if (
                logical_system.is_endpoint_connected_to_cog(endpoint, test_cog)
                and connectable.connectable.entity == unconnected_config_ds.data_source
            ):
                found_unconnected_config = True
                break
        if found_unconnected_config:
            break
    assert found_unconnected_config, "unconnected_config should appear in connectables after connection"

    with pytest.raises(RuntimeError, match="Could not find config endpoint"):
        test_cog.connect_config_by_name("nonexistent_config", unconnected_config_ds)

    # Test connect_state() - connect the unconnected_state to the cog
    # We need to find a state endpoint in the cog to connect to
    # The cog has a state called "state" which should have endpoints
    # Get connectables and find the one that's a state
    connectables = test_cog.get_connectables()
    cog_state_connectable = None
    for connectable in connectables:
        # Look for the cog's internal state (not our unconnected_state)
        if "box.connected_state" in connectable.get_name():
            cog_state_connectable = connectable
            break

    # If we found the cog's state connectable, get its endpoints
    assert cog_state_connectable is not None
    state_endpoints = cog_state_connectable.get_endpoints()
    # Find an endpoint that belongs to the cog (not to our unconnected_state)
    for endpoint in state_endpoints:
        # Check if this endpoint is connected to our test_cog
        if logical_system.is_endpoint_connected_to_cog(endpoint, test_cog):
            # Get the count of connectables before connecting the state
            num_connectables_before = len(test_cog.get_connectables())

            # Now test connecting our unconnected_state to this endpoint
            logical_system.connect_state(unconnected_state, endpoint)

            # After connecting, verify the connection appears in get_connectables()
            connectables_after_state = test_cog.get_connectables()
            # We should have the same number of connectables (we're just changing which state is connected)
            assert len(connectables_after_state) == num_connectables_before

            # Verify that unconnected_state now appears in the connectables
            found_unconnected_state = False
            for connectable in connectables_after_state:
                if connectable.connectable.entity == unconnected_state.connectable.entity:
                    # Verify it has endpoints connected to our test_cog
                    endpoints = connectable.get_endpoints()
                    for ep in endpoints:
                        if logical_system.is_endpoint_connected_to_cog(ep, test_cog):
                            found_unconnected_state = True
                            break
                if found_unconnected_state:
                    break
            assert found_unconnected_state, "unconnected_state should appear in connectables after connection"
            break


def test_connect_state_type_validation(fs_importer: FilesystemImporter) -> None:
    """Test that connect_state validates types correctly."""
    logical_system = LogicalSystemInterface(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/tests/support/test_system_description.clk"),
        ),
        fs_importer,
    )

    state_instances = logical_system.get_state_instances()
    assert len(state_instances) > 0
    state = state_instances[0]

    cogs = logical_system.get_cogs()
    assert len(cogs) > 0
    cog = cogs[0]

    input_endpoint = cog.get_endpoint_by_input_name("message_input")
    assert input_endpoint is not None
    with pytest.raises(TypeError, match="Can only connect cog StateDef endpoints to state"):
        logical_system.connect_state(state, input_endpoint)
