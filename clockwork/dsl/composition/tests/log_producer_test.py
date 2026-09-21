# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.bazel.simple_launch_targets import MergeSimplelaunchConfig
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.composition import system, systemgen
from clockwork.dsl.ir import compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_logsim_system(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/logsimsys.clk")),
        fs_importer,
    )
    unresolved_system_ir = module.inner_scope.lookup("logsim_system1")
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
        "clockwork.clockwork.dsl.composition.tests.support.logsimsys.logsim_system1.proc1.tachyon",
    }
    assert {x.name for x in generated_files.event_logger_config_files} == {
        "logsimsys.logsim_system1.LogSimCpu_event_logger_config.tachyon",
    }
    assert {x.name for x in generated_files.telemetry_logger_config_files} == {
        "logsimsys.logsim_system1.LogSimCpu_telemetry_logger_config.tachyon",
    }
    assert {x.name for x in generated_files.bridge_config_files} == set()

    assert {x.name for x in generated_files.simplelaunch_config_files} == {
        "logsimsys.logsim_system1.LogSimCpu_simplelaunch_config.textproto",
    }
    logical_system = physical_system.system
    (cpu_1_uuid,) = output_targets.keys()
    cpu_1 = physical_system.cpu_domains[cpu_1_uuid]

    chan3 = logical_system.channels["Chan3"]
    (chan3_producer_uuid,) = chan3.producers
    chan3_producer = logical_system.log_producers[chan3_producer_uuid]
    assert isinstance(chan3_producer, system.LogProducer)
    _, chan3_buffer_uuid = cpu_1.log_producers[chan3_producer_uuid]
    chan3_buffer = cpu_1.buffers[chan3_buffer_uuid]
    (chan3_observer,) = chan3_buffer.observers.values()
    assert isinstance(chan3_observer, system.LogObserver)

    simple_launch_config_1 = MergeSimplelaunchConfig(
        name="logsimcpu.textproto",
        srcs=[
            Label(value=f"@{CLK_REPO}//clockwork/launch:clockwork_prelaunch.textproto"),
            Path("logsimsys.logsim_system1.LogSimCpu_simplelaunch_config.textproto"),
        ],
        data=[
            Label(value=f"@{CLK_REPO}//clockwork/pinion:tcp_bridge_main"),
            Path("clockwork.clockwork.dsl.composition.tests.support.logsimsys.logsim_system1.proc1.tachyon"),
            Label(value="//clockwork/dsl/composition/tests/support:logsim_exe"),
            Path("logsimsys.logsim_system1.LogSimCpu_event_logger_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_telemetry_logger_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_channel_publisher_config.tachyon"),
            Path(
                "logsimsys.logsim_system1.LogSimCpu.clockwork.clockwork.dsl.composition.tests.support.logsimsys.MultiChan2_config.tachyon"
            ),
            Path("logsimsys.logsim_system1.diagnostics_database_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_channel_spy_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_channel_allocations.csv"),
            Path("logsimsys.logsim_system1.LogSimCpu_metrics_channel_metadata_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_signal_metadata_config.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_system_metadata.tachyon"),
            Path("logsimsys.logsim_system1.LogSimCpu_journal_topology.tachyon"),
        ],
    )
    assert output_targets == {
        cpu_1_uuid: systemgen.DomainOutputTargets(
            domain=logical_system.cpu_domains[cpu_1_uuid],
            simple_launch_config=simple_launch_config_1,
        ),
    }
