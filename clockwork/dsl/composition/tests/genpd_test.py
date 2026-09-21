# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl.composition import genpd, pdf, pdfproto, system
from clockwork.dsl.composition.channel_config import ChannelType
from clockwork.dsl.composition.pdf import NotConnectedEndpointType
from clockwork.dsl.ir import compiler, cpp_target, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import protocol


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hellomod(fs_importer: FilesystemImporter, tmp_path: Path) -> None:  # noqa: PLR0915 the PDF has a lot to check
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("hellomod", recursive=False)
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    sys_ir = module.inner_scope.lookup("helloworld", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance],
        sys_ir.module,
        sys_ir.require_logging_policies,
        sys_ir.use_simplelaunch,
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()
    tmp_file: Final = tmp_path / "HelloWorld.tachyon"
    protocol.write_tachyon_to_file(pd, tmp_file)
    buffer = bytearray(pdf.ProcessDescription.get_tachyon_constraint().size)
    pd.serialize_tachyon(memoryview(buffer))
    pd2 = pdf.ProcessDescription.deserialize_tachyon(memoryview(bytes(buffer)))
    assert pd2 == pd
    pd3 = protocol.read_tachyon_from_file(pdf.ProcessDescription, tmp_file)
    assert pd3 == pd

    assert len(pd.config_graph.config_instances) == 1
    (hello_config,) = pd.config_graph.config_instances
    # Config now references a data source instead of directly having a file path
    data_source_idx = hello_config.init_data_source
    assert len(pd.data_sources) > data_source_idx
    data_source = pd.data_sources[data_source_idx]
    assert data_source.source_path_or_name == "foo/bar.txtpb"
    assert data_source.data_source_type == pdf.DataSourceType.file
    assert len(pd.config_graph.connections) == 1
    (hello_config_conn,) = pd.config_graph.connections
    assert hello_config_conn.config_id == hello_config.config_instance_id

    assert len(pd.state_graph.state_instances) == 3
    (ro_hello, rw_hello, extern_hello) = pd.state_graph.state_instances
    assert (
        ro_hello.instance_path_name == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.ro_hello"
    )
    assert (
        rw_hello.instance_path_name == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.rw_hello"
    )
    assert (
        extern_hello.instance_path_name
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.extern_hello"
    )
    assert extern_hello.maybe_buffer_layout is None
    assert extern_hello.maybe_memory_resource is not None
    assert extern_hello.snapshot_representation_id is not None
    assert extern_hello.representation_id != extern_hello.snapshot_representation_id
    assert ro_hello.snapshot_representation_id is None
    assert rw_hello.snapshot_representation_id is None
    (
        ro_hello_conn,
        ro_hello_to_init2_conn,
        ro_hello_init_conn,
        rw_hello_conn,
        rw_hello_init_conn,
        extern_hello_conn,
    ) = pd.state_graph.connections
    assert ro_hello_conn.state_id == ro_hello.state_instance_id
    assert ro_hello_init_conn.state_id == ro_hello.state_instance_id
    assert rw_hello_conn.state_id == rw_hello.state_instance_id
    assert rw_hello_init_conn.state_id == rw_hello.state_instance_id
    assert ro_hello_to_init2_conn.state_id == ro_hello.state_instance_id
    assert extern_hello_conn.state_id == extern_hello.state_instance_id

    hello_cog, rw_hello_init, ro_hello_init = pd.cog_instances
    assert (
        hello_cog.instance_path_name
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.hello_cog"
    )
    assert (
        rw_hello_init.instance_path_name
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.rw_hello_init"
    )
    assert (
        ro_hello_init.instance_path_name
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.helloworld.box.ro_hello_init"
    )
    assert pd.init_cogs == [ro_hello_init.cog_instance_id, rw_hello_init.cog_instance_id]

    resources = pd.memory_resource_graph.memory_resources
    mem_hello = next(mem for mem in resources if mem.instance_path_name.endswith("mem_hello"))
    extern_hello_memory = next(mem for mem in resources if mem.instance_path_name.endswith("extern_hello_memory"))
    (mem_hello_conn,) = pd.memory_resource_graph.connections
    assert mem_hello_conn.memory_resource_id == mem_hello.memory_resource_id
    assert extern_hello.maybe_memory_resource == extern_hello_memory.memory_resource_id

    def get_channel(name: str) -> pdfproto.PublishEndpoint:
        return next(pub for pub in pd.pubsub_graph.publish_endpoints if pub.channel_name == name)

    assert get_channel("HelloChan").channel_type == ChannelType.unspecified
    assert get_channel("Name that doesn't follow reasonable conventions!").channel_type == ChannelType.shared_memory


def test_serializable_external_state_snapshot_policy(fs_importer: FilesystemImporter) -> None:
    """Validate and generate a snapshot for an external state representation."""
    source = """
use clockwork::dsl::tests::support::clk_hellomod;
use clockwork::dsl::tests::support::clk_hellomsg;
use std::snapshot::{TakeSnapshots};

// Snapshot channel
channel SnapshotChannel
{
    message_type: Tachyon<clk_hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new hello_box: clk_hellomod::HelloBox;
}

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=clk_hellomod::clk_hellomod_clk_exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
    apply TakeSnapshots(channel=SnapshotChannel, cycles=1) to box.hello_box.hello_cog.extern_hello;
}

cpu_domain TestCpu;

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_serializable_snapshot_policy"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance],
        sys_ir.module,
        sys_ir.require_logging_policies,
        sys_ir.use_simplelaunch,
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()
    state_instance = next(
        state for state in pd.state_graph.state_instances if state.instance_path_name.endswith("extern_hello")
    )
    (snapshot_config,) = pd.snapshot_configs
    assert state_instance.snapshot_representation_id is not None
    assert snapshot_config.endpoint_id == next(
        connection.endpoint_id
        for connection in pd.state_graph.connections
        if connection.state_id == state_instance.state_instance_id
    )


def test_take_snapshots_rejects_external_state_without_serialized_form(fs_importer: FilesystemImporter) -> None:
    """Reject snapshot policies for external states without a serialized form."""
    source = """
use clockwork::dsl::tests::support::hellomsg;
use std::snapshot::{TakeSnapshots};

// External state
extern_type CxxState;

// Test cog
cog TestCog
{
    states
    {
        state: CxxState;
    }

    execution
    {
        condition periodic: time_since_last_exec(1ms);
        execute when: periodic;
    }
}

    // Snapshot channel
channel SnapshotChannel
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new cog: TestCog;
    new memory: HeapMemory(max_size=1'000'000);
    new state: State(representation=CxxState, memory_resource=memory);
    connect state to cog.state;
    apply TakeSnapshots(channel=SnapshotChannel, cycles=1) to cog.state;
}

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpu_domain TestCpu;

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_unsupported_snapshot_policy"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    with pytest.raises(ValueError, match="serialized form"):
        system.make_system(
            [sys_ir.get_resolved().box_instance],
            sys_ir.module,
            sys_ir.require_logging_policies,
            sys_ir.use_simplelaunch,
        )


def test_gen_not_connected_endpoints(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")), fs_importer
    )

    sys_ir = module.inner_scope.lookup("system1", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies, sys_ir.use_simplelaunch
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    for pd in process_descs.values():
        assert len(pd.not_connected_endpoints) == 3
        assert {endpoint.endpoint_type for endpoint in pd.not_connected_endpoints} == {
            NotConnectedEndpointType.publisher,
            NotConnectedEndpointType.subscriber,
            NotConnectedEndpointType.subscriber,
        }
        for endpoint in pd.not_connected_endpoints:
            if endpoint.endpoint_type == NotConnectedEndpointType.publisher:
                assert endpoint.endpoint_id in logical_system.producer_endpoints
                assert endpoint.endpoint_id in logical_system.ignored_producer_endpoints
                assert (
                    endpoint.buffer_layout.message_size
                    == logical_system.ignored_producer_endpoints[endpoint.endpoint_id]
                )
            if endpoint.endpoint_type == NotConnectedEndpointType.subscriber:
                assert endpoint.endpoint_id in logical_system.observer_endpoints
                assert endpoint.endpoint_id in logical_system.ignored_observer_endpoints
                assert (
                    endpoint.buffer_layout.message_size
                    == logical_system.ignored_observer_endpoints[endpoint.endpoint_id]
                )
