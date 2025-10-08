# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.bazel.simple_launch_targets import MergeSimplelaunchConfig
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.composition import systemgen
from clockwork.dsl.ir import compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver


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

    logical_system = physical_system.system
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
            Path(
                "simplesys.system1.Cpu1.clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan2_config.tachyon"
            ),
            Path("simplesys.system1.diagnostics_database_config.tachyon"),
            Path("simplesys.system1.Cpu1_channel_spy_config.tachyon"),
            Path("simplesys.system1.Cpu1_channel_allocations.csv"),
            Path("simplesys.system1.Cpu1_metrics_channel_metadata_config.tachyon"),
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
            Path(
                "simplesys.system1.Cpu2.clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan1_config.tachyon"
            ),
            Path("simplesys.system1.diagnostics_database_config.tachyon"),
            Path("simplesys.system1.Cpu2_channel_spy_config.tachyon"),
            Path("simplesys.system1.Cpu2_channel_allocations.csv"),
            Path("simplesys.system1.Cpu2_metrics_channel_metadata_config.tachyon"),
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
