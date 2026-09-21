# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from clockwork.dsl.bazel.simple_launch_targets import MergeSimplelaunchConfig
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.composition import journal_topology, journal_topology_file_config, system, systemgen
from clockwork.dsl.ir import compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.journal import journal_topology_pb2
from clockwork.serialization.py import protocol
from google.protobuf import text_format


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hello_system(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    unresolved_system_ir = module.inner_scope.lookup("system1")
    assert isinstance(unresolved_system_ir, system_target.UnresolvedSystemTarget)
    system_ir = unresolved_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)
    generated_files, output_targets, physical_system = systemgen.gen_system(
        root_dir=tmp_path,
        system_target_ir=system_ir,
        write_files=True,
        write_json_files=True,
    )
    assert {x.name for x in generated_files.process_description_files} == {
        "clockwork.clockwork.dsl.composition.tests.support.simplesys.system1.proc1.tachyon",
        "clockwork.clockwork.dsl.composition.tests.support.simplesys.system1.proc2.tachyon",
    }
    assert {x.name for x in generated_files.event_logger_config_files} == {
        "simplesys.system1.Cpu1_event_logger_config.tachyon",
        "simplesys.system1.Cpu2_event_logger_config.tachyon",
    }
    assert {x.name for x in generated_files.telemetry_logger_config_files} == {
        "simplesys.system1.Cpu1_telemetry_logger_config.tachyon",
        "simplesys.system1.Cpu2_telemetry_logger_config.tachyon",
    }
    assert {x.name for x in generated_files.bridge_config_files} == {
        "simplesys.system1.Cpu1_bridge_config.tachyon",
        "simplesys.system1.Cpu2_bridge_config.tachyon",
    }
    assert {x.name for x in generated_files.simplelaunch_config_files} == {
        "simplesys.system1.Cpu1_simplelaunch_config.textproto",
        "simplesys.system1.Cpu2_simplelaunch_config.textproto",
    }
    assert {x.name for x in generated_files.channel_allocation_report_files} == {
        "simplesys.system1.Cpu1_channel_allocations.csv",
        "simplesys.system1.Cpu2_channel_allocations.csv",
    }
    assert {x.name for x in generated_files.channel_spy_config_files} == {
        "simplesys.system1.Cpu1_channel_spy_config.tachyon",
        "simplesys.system1.Cpu2_channel_spy_config.tachyon",
    }
    assert {x.name for x in generated_files.diagnostics_database_config_files} == {
        "simplesys.system1.diagnostics_database_config.tachyon",
    }
    assert {x.name for x in generated_files.metrics_channel_metadata_files} == {
        "simplesys.system1.Cpu1_metrics_channel_metadata_config.tachyon",
        "simplesys.system1.Cpu2_metrics_channel_metadata_config.tachyon",
    }
    assert {x.name for x in generated_files.journal_topology_files} == {
        "simplesys.system1.Cpu1_journal_topology.tachyon",
        "simplesys.system1.Cpu2_journal_topology.tachyon",
    }
    topology_contents = {
        protocol.read_tachyon_from_file(journal_topology_file_config.JournalTopologyFileConfig, path).contents
        for path in generated_files.journal_topology_files
    }
    assert len(topology_contents) == 1
    topology_content = topology_contents.pop()
    assert topology_content.startswith(
        "# proto-file: clockwork/journal/journal_topology.proto\n# proto-message: JournalTopology\n"
    )
    topology = text_format.Parse(topology_content, journal_topology_pb2.JournalTopology())
    logical_system = physical_system.system
    expected_channel_maps = journal_topology.make_cog_channel_maps(logical_system)
    assert topology == journal_topology_pb2.JournalTopology(
        format_version=1,
        system_target_name=system_ir.fqn,
        cogs=[
            journal_topology_pb2.CogTopology(
                cog_instance_path=channel_map.cog_instance_path,
                input_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.input_channels
                ],
                output_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.output_channels
                ],
                has_clockwork_state=channel_map.has_clockwork_state,
                snapshot_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.snapshot_channels
                ],
            )
            for channel_map in expected_channel_maps.values()
        ],
    )

    cpu_1, cpu_2 = output_targets.keys()

    simple_launch_config_1 = MergeSimplelaunchConfig(
        name="cpu1.textproto",
        srcs=[
            Label(value=f"@{CLK_REPO}//clockwork/launch:clockwork_prelaunch.textproto"),
            Label(value="//clockwork/dsl/tests/support:host_a_simplelaunch_config.textproto"),
            Label(value="//clockwork/dsl/tests/support:host_b_simplelaunch_config.textproto"),
            Path("simplesys.system1.Cpu1_simplelaunch_config.textproto"),
        ],
        data=[
            Label(value=f"@{CLK_REPO}//clockwork/pinion:tcp_bridge_main"),
            Path("clockwork.clockwork.dsl.composition.tests.support.simplesys.system1.proc1.tachyon"),
            Label(value="//clockwork/dsl/composition/tests/support:exe"),
            Path("simplesys.system1.Cpu1_event_logger_config.tachyon"),
            Path("simplesys.system1.Cpu1_telemetry_logger_config.tachyon"),
            Path("simplesys.system1.Cpu1_bridge_config.tachyon"),
            Path("simplesys.system1.Cpu1_simplelaunch_runner_config.tachyon"),
            Path(
                "simplesys.system1.Cpu1.clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan2_config.tachyon"
            ),
            Path("simplesys.system1.diagnostics_database_config.tachyon"),
            Path("simplesys.system1.Cpu1_channel_spy_config.tachyon"),
            Path("simplesys.system1.Cpu1_channel_allocations.csv"),
            Path("simplesys.system1.Cpu1_metrics_channel_metadata_config.tachyon"),
            Path("simplesys.system1.Cpu1_signal_metadata_config.tachyon"),
            Path("simplesys.system1.Cpu1_system_metadata.tachyon"),
            Path("simplesys.system1.Cpu1_journal_topology.tachyon"),
        ],
    )

    simple_launch_config_2 = MergeSimplelaunchConfig(
        name="cpu2.textproto",
        srcs=[
            Label(value=f"@{CLK_REPO}//clockwork/launch:clockwork_prelaunch.textproto"),
            Path("simplesys.system1.Cpu2_simplelaunch_config.textproto"),
        ],
        data=[
            Label(value=f"@{CLK_REPO}//clockwork/pinion:tcp_bridge_main"),
            Path("clockwork.clockwork.dsl.composition.tests.support.simplesys.system1.proc2.tachyon"),
            Label(value="//clockwork/dsl/composition/tests/support:exe"),
            Path("simplesys.system1.Cpu2_event_logger_config.tachyon"),
            Path("simplesys.system1.Cpu2_telemetry_logger_config.tachyon"),
            Path("simplesys.system1.Cpu2_bridge_config.tachyon"),
            Path("simplesys.system1.Cpu2_simplelaunch_runner_config.tachyon"),
            Path(
                "simplesys.system1.Cpu2.clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan1_config.tachyon"
            ),
            Path("simplesys.system1.diagnostics_database_config.tachyon"),
            Path("simplesys.system1.Cpu2_channel_spy_config.tachyon"),
            Path("simplesys.system1.Cpu2_channel_allocations.csv"),
            Path("simplesys.system1.Cpu2_metrics_channel_metadata_config.tachyon"),
            Path("simplesys.system1.Cpu2_signal_metadata_config.tachyon"),
            Path("simplesys.system1.Cpu2_system_metadata.tachyon"),
            Path("simplesys.system1.Cpu2_journal_topology.tachyon"),
        ],
    )

    assert output_targets == {
        cpu_1: systemgen.DomainOutputTargets(
            domain=logical_system.cpu_domains[cpu_1],
            simple_launch_config=simple_launch_config_1,
        ),
        cpu_2: systemgen.DomainOutputTargets(
            domain=logical_system.cpu_domains[cpu_2],
            simple_launch_config=simple_launch_config_2,
        ),
    }


def test_signal_metadata_generation(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    """Test that signal metadata config files are generated correctly for systems with signals."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system_target.clk")),
        fs_importer,
    )
    unresolved_system_ir = module.inner_scope.lookup("SignalTestSystem")
    assert isinstance(unresolved_system_ir, system_target.UnresolvedSystemTarget)
    system_ir = unresolved_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    # Generate system with write_json_files=True for validation
    generated_files, output_targets, physical_system = systemgen.gen_system(
        root_dir=tmp_path,
        system_target_ir=system_ir,
        write_files=True,
        write_json_files=True,
    )

    # Verify signal metadata files are generated
    assert len(generated_files.signal_metadata_files) == 1
    assert {x.name for x in generated_files.signal_metadata_files} == {
        "signals_test_system_target.SignalTestSystem.TestCpu_signal_metadata_config.tachyon",
    }

    # Verify the file exists and is readable
    signal_metadata_file = generated_files.signal_metadata_files[0]
    assert signal_metadata_file.exists()

    # Verify JSON file was also created
    json_file = signal_metadata_file.with_suffix(".json")
    assert json_file.exists()

    # Verify the JSON content is valid and contains expected structure
    with json_file.open() as f:
        signal_metadata = json.load(f)

    # Check top-level structure
    assert "signal_instance_names" in signal_metadata
    assert "signals" in signal_metadata
    assert "cogs" in signal_metadata
    assert "cog_instances" in signal_metadata

    # Verify signal_instance_names is a list with 2 unique instance names (one per cog instance)
    assert isinstance(signal_metadata["signal_instance_names"], list)
    assert len(signal_metadata["signal_instance_names"]) == 2

    # Verify signals is a list with 4 signals (2 module-level + 2 cog-scope)
    assert isinstance(signal_metadata["signals"], list)
    assert len(signal_metadata["signals"]) == 4
    signal_names = {sig["name"] for sig in signal_metadata["signals"]}
    assert any("module_signal" in name for name in signal_names)
    assert any("multi_instance_signal" in name for name in signal_names)
    assert any("cog_signal" in name for name in signal_names)
    assert any("cog_private_signal" in name for name in signal_names)

    # Verify cogs is a list with 1 cog class (SignalTestCog)
    assert isinstance(signal_metadata["cogs"], list)
    assert len(signal_metadata["cogs"]) == 1
    # Verify each cog has a cog_class_id
    for cog in signal_metadata["cogs"]:
        assert "cog_path" in cog

    # Verify cog_instances is a list with test_cog1 and test_cog2
    assert isinstance(signal_metadata["cog_instances"], list)
    assert len(signal_metadata["cog_instances"]) == 2

    # Verify signal metadata file is included in simplelaunch config data
    logical_system = physical_system.system
    for domain_uuid in output_targets:
        domain_targets = output_targets[domain_uuid]
        data_paths = [
            str(item) if isinstance(item, Path) else item for item in domain_targets.simple_launch_config.data
        ]
        # Check that signal metadata config is in the data list
        assert any("signal_metadata_config.tachyon" in str(path) for path in data_paths), (
            f"Signal metadata config not found in simplelaunch data for domain {logical_system.cpu_domains[domain_uuid].name}"
        )


def test_system_metadata_generation(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    """Test that system metadata files are generated."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system_target.clk")),
        fs_importer,
    )
    unresolved_system_ir = module.inner_scope.lookup("SignalTestSystem")
    assert isinstance(unresolved_system_ir, system_target.UnresolvedSystemTarget)
    system_ir = unresolved_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    # Generate system with write_json_files=True for validation
    generated_files, _, _ = systemgen.gen_system(
        root_dir=tmp_path,
        system_target_ir=system_ir,
        write_files=True,
        write_json_files=True,
    )

    # Verify system metadata files are generated
    assert len(generated_files.system_metadata_files) == 1
    assert {x.name for x in generated_files.system_metadata_files} == {
        "signals_test_system_target.SignalTestSystem.TestCpu_system_metadata.tachyon",
    }

    # Verify the file exists and is readable
    system_metadata_file = generated_files.system_metadata_files[0]
    assert system_metadata_file.exists()

    # Verify JSON file was also created
    json_file = system_metadata_file.with_suffix(".json")
    assert json_file.exists()

    # Verify the JSON content is valid and contains expected structure
    with json_file.open() as f:
        system_metadata = json.load(f)

    # Check the metadata
    assert "system_target_name" in system_metadata
    assert isinstance(system_metadata["system_target_name"], str)
    assert system_metadata["system_target_name"] == "signals_test_system_target"


def test_memory_resource_multiple_connections_system(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    """Test that a memory resource can't be connected to multiple endpoints."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/memory_resource_test_system.clk")),
        fs_importer,
    )
    unresolved_system_ir = module.inner_scope.lookup("MemoryResourceTestSystem")
    assert isinstance(unresolved_system_ir, system_target.UnresolvedSystemTarget)
    system_ir = unresolved_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    with pytest.raises(
        RuntimeError,
        match=r"Memory resource (.*) already connected to an endpoint, cannot connect to (.*)",
    ):
        systemgen.gen_system(
            root_dir=tmp_path,
            system_target_ir=system_ir,
            write_files=False,
            write_json_files=False,
        )
