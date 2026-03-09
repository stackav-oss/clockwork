# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from pathlib import Path
from typing import Final

import pytest
from clockwork.dsl.composition import genpd, pdf, system
from clockwork.dsl.composition.pdf import NotConnectedEndpointType
from clockwork.dsl.ir import compiler, cpp_target, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import protocol


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hellomod(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    cpp_target_ir = module.inner_scope.lookup("hellomod", recursive=False)
    assert isinstance(cpp_target_ir, cpp_target.CppTarget)
    sys_ir = module.inner_scope.lookup("helloworld", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
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

    (mem_hello,) = pd.memory_resource_graph.memory_resources
    (mem_hello_conn,) = pd.memory_resource_graph.connections
    assert mem_hello_conn.memory_resource_id == mem_hello.memory_resource_id
    assert extern_hello.maybe_memory_resource == mem_hello.memory_resource_id


def test_gen_not_connected_endpoints(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")), fs_importer
    )

    sys_ir = module.inner_scope.lookup("system1", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    for pd in process_descs.values():
        assert len(pd.not_connected_endpoints) == 2
        assert {endpoint.endpoint_type for endpoint in pd.not_connected_endpoints} == {
            NotConnectedEndpointType.publisher,
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
