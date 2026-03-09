# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for hardware."""

from __future__ import annotations

import re
from pathlib import Path
from textwrap import dedent

import pytest
from clockwork.dsl.ir import compiler, hardware, policy, pubsub
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hardware(fs_importer: FilesystemImporter) -> None:
    hellomod = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    hellocog = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellocog.clk")), fs_importer
    )
    assert hardware._CPU_DOMAIN_CONNECTION_KEY not in hellocog.context._contexts
    assert hardware._CPU_DOMAIN_CONNECTION_KEY in hellomod.context._contexts
    lan_nodes = hellomod.context[hardware._CPU_DOMAIN_CONNECTION_KEY].lan_nodes
    assert len(lan_nodes) == 2
    host_a = hellomod.inner_scope.lookup("HostA", recursive=False)
    host_b = hellomod.inner_scope.lookup("HostB", recursive=False)
    lan1 = hellomod.inner_scope.lookup("Lan1", recursive=False)
    assert isinstance(host_a, hardware.CpuDomain)
    assert host_a.simplelaunch_node == "a"
    assert host_a.simplelaunch_node_name() == "a"
    assert host_a.simplelaunch_srcs == [
        "//clockwork/dsl/tests/support:host_a_simplelaunch_config.textproto",
        "//clockwork/dsl/tests/support:host_b_simplelaunch_config.textproto",
    ]
    assert host_a.bridge_cpus == [0]
    assert host_a.default_cpus == [1, 2, 3]
    assert isinstance(host_b, hardware.CpuDomain)
    assert not host_b.simplelaunch_node
    assert host_b.simplelaunch_node_name() == "hostb"
    assert not host_b.simplelaunch_srcs
    assert not host_b.bridge_cpus
    assert not host_b.default_cpus
    assert isinstance(lan1, hardware.EthernetLan)
    host_a_lan = lan_nodes[host_a.value_key()]
    assert host_a_lan.lan is lan1
    assert host_a_lan.address == "1.2.3.4"
    assert host_a_lan.bridge_ports == (16000, 18000)
    assert host_a_lan.cpu_domain is host_a
    host_b_lan = lan_nodes[host_b.value_key()]
    assert host_b_lan.lan is lan1
    assert host_b_lan.address == "1.2.3.5"
    assert host_b_lan.bridge_ports == (16000, 18000)
    assert host_b_lan.cpu_domain is host_b


def test_clk_hardware(fs_importer: FilesystemImporter) -> None:
    clk_cpu_domains = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_cpu_domains.clk")), fs_importer
    )
    assert hardware._CPU_DOMAIN_CONNECTION_KEY in clk_cpu_domains.context._contexts
    lan_nodes = clk_cpu_domains.context[hardware._CPU_DOMAIN_CONNECTION_KEY].lan_nodes
    assert len(lan_nodes) == 2
    host_a = clk_cpu_domains.inner_scope.lookup("HostA", recursive=False)
    host_b = clk_cpu_domains.inner_scope.lookup("HostB", recursive=False)
    lan1 = clk_cpu_domains.inner_scope.lookup("Lan1", recursive=False)
    assert isinstance(host_a, hardware.CpuDomain)
    assert host_a.simplelaunch_node == "a"
    assert host_a.simplelaunch_node_name() == "a"
    assert host_a.simplelaunch_srcs == [
        "//clockwork/dsl/tests/support:host_a_simplelaunch_config.textproto",
        "//clockwork/dsl/tests/support:host_b_simplelaunch_config.textproto",
    ]
    assert host_a.bridge_cpus == [0]
    assert host_a.default_cpus == [1, 2, 3]
    assert isinstance(host_b, hardware.CpuDomain)
    assert not host_b.simplelaunch_node
    assert host_b.simplelaunch_node_name() == "hostb"
    assert not host_b.simplelaunch_srcs
    assert not host_b.bridge_cpus
    assert not host_b.default_cpus
    assert isinstance(lan1, hardware.EthernetLan)
    host_a_lan = lan_nodes[host_a.value_key()]
    assert host_a_lan.lan is lan1
    assert host_a_lan.address == "1.2.3.4"
    assert host_a_lan.bridge_ports == (16000, 18000)
    assert host_a_lan.cpu_domain is host_a
    host_b_lan = lan_nodes[host_b.value_key()]
    assert host_b_lan.lan is lan1
    assert host_b_lan.address == "1.2.3.5"
    assert host_b_lan.bridge_ports == (16000, 18000)
    assert host_b_lan.cpu_domain is host_b


def test_invalid_pcie_link(fs_importer: FilesystemImporter) -> None:
    source = dedent(
        """
        cpu_domain HostA;

        cpu_domain HostB;

        pcie_link HostA HostB
        {
            a: field;
        }
        """,
    )
    with pytest.raises(
        ValueError,
        match=re.escape("Missing required field 'type'"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "pcie_link_missing_type"), importer=fs_importer)

    source = dedent(
        """
        cpu_domain HostA;

        cpu_domain HostB;

        pcie_link HostA HostB
        {
            type: foo;
        }
        """,
    )
    with pytest.raises(
        ValueError,
        match=re.escape("Got invalid pcie_link type: foo"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "pcie_link_unknown_type"), importer=fs_importer)

    source = dedent(
        """
        cpu_domain HostA;

        cpu_domain HostB;

        pcie_link HostA HostB
        {
            type: 123;
        }
        """,
    )
    with pytest.raises(
        TypeError,
        match=re.escape("pcie_link type must be an identifier"),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "pcie_link_bad_type"), importer=fs_importer)
