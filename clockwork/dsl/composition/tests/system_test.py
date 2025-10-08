# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.composition import system, systemgen
from clockwork.dsl.ir import box, cog, compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.uuid_reg import lookup_uuid


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hellomod(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloSystem")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(
        cst_node=None, module=module, scope=box_template_ir.scope, name="test", doc=None
    )
    compiler._register_box_instance_uuids(module.context, box_ir)
    sys = system.make_system([box_ir.get_resolved()], module, False)
    assert sys.module is module
    assert len(sys.channels) == 5
    assert sys.channels.keys() == {
        "HelloChan",
        "Name that doesn't follow reasonable conventions!",
        "many_publishers",
        "NetworkData",
        "/diagnostics",
    }
    assert len(sys.metrics_channels) == 24  # 6 channels per HelloBox, 4 HelloBox instances
    assert len(sys.cogs) == 12  # 3 cogs per HelloBox, 4 HelloBox instances


def test_simplesys(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 For testing only
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    sys = system.make_system([box_ir.get_resolved()], module, False)
    assert sys.module is module
    assert len(sys.channels) == 5
    assert sys.channels.keys() == {"Chan1", "Chan2", "MultiChan1", "MultiChan2", "/diagnostics"}
    assert len(sys.metrics_channels) == 4  # 2 channels per cog, 2 cogs
    psys = system.make_physical_system(sys)
    cpu1 = None
    cpu2 = None
    for domain in psys.cpu_domains.values():
        if domain.logical.name == "Cpu1":
            cpu1 = domain
        else:
            assert domain.logical.name == "Cpu2"
            cpu2 = domain
    assert cpu1 is not None
    assert cpu2 is not None
    ((obs1_1_uuid, obs1_1), (obs1_2_uuid, obs1_2), (obs1_3_uuid, obs1_3)) = cpu1.bridge_observers.items()
    ((log_obs1_1_uuid, log_obs1_1), (log_obs1_2_uuid, log_obs1_2), (log_obs1_3_uuid, log_obs1_3)) = (
        cpu1.log_observers.items()
    )
    ((pro1_1_uuid, pro1_1), (pro1_2_uuid, pro1_2), (pro1_3_uuid, pro1_3)) = cpu1.bridge_producers.items()
    ((obs2_1_uuid, obs2_1), (obs2_2_uuid, obs2_2), (obs2_3_uuid, obs2_3)) = cpu2.bridge_observers.items()
    ((log_obs2_1_uuid, log_obs2_1), (log_obs2_2_uuid, log_obs2_2), (log_obs2_3_uuid, log_obs2_3)) = (
        cpu2.log_observers.items()
    )
    ((pro2_1_uuid, pro2_1), (pro2_2_uuid, pro2_2), (pro2_3_uuid, pro2_3)) = cpu2.bridge_producers.items()

    assert obs1_1.dest_domain == cpu2.uuid
    assert len(obs1_1.remote_producers) == 1
    assert obs1_1.remote_producers[0] == pro2_1_uuid
    assert obs1_1.source_domain == cpu1.uuid
    assert cpu1.buffers[obs1_1.source_pinion_buffer].observers == {obs1_1_uuid: obs1_1, log_obs1_1_uuid: log_obs1_1}
    assert cpu1.buffers[obs1_1.source_pinion_buffer].uuid == obs1_1.source_pinion_buffer
    assert cpu1.buffers[obs1_1.source_pinion_buffer].num_subscribers == 3
    assert pro1_1.source_domain == cpu2.uuid
    assert pro1_1.dest_domain == cpu1.uuid
    assert len(cpu1.metrics_buffers) == 2

    assert obs1_2.dest_domain == cpu2.uuid
    assert len(obs1_2.remote_producers) == 1
    assert obs1_2.remote_producers[0] == pro2_2_uuid
    assert obs1_2.source_domain == cpu1.uuid
    assert cpu1.buffers[obs1_2.source_pinion_buffer].observers == {obs1_2_uuid: obs1_2, log_obs1_2_uuid: log_obs1_2}
    assert cpu1.buffers[obs1_2.source_pinion_buffer].uuid == obs1_2.source_pinion_buffer
    assert cpu1.buffers[obs1_2.source_pinion_buffer].num_subscribers == 2
    assert pro1_2.source_domain == cpu2.uuid
    assert pro1_2.dest_domain == cpu1.uuid

    assert obs1_3.dest_domain == cpu2.uuid
    assert len(obs1_3.remote_producers) == 1
    assert obs1_3.remote_producers[0] == pro2_3_uuid
    assert obs1_3.source_domain == cpu1.uuid
    assert cpu1.buffers[obs1_3.source_pinion_buffer].observers == {obs1_3_uuid: obs1_3, log_obs1_3_uuid: log_obs1_3}
    assert cpu1.buffers[obs1_3.source_pinion_buffer].uuid == obs1_3.source_pinion_buffer
    assert cpu1.buffers[obs1_3.source_pinion_buffer].num_subscribers == 2
    assert pro1_3.source_domain == cpu2.uuid
    assert pro1_3.dest_domain == cpu1.uuid

    assert cpu1.buffers[pro1_1.dest_pinion_buffer].uuid == pro1_1_uuid
    assert cpu2.buffers[pro2_1.dest_pinion_buffer].uuid == pro2_1_uuid
    assert cpu2.buffers[obs2_1.source_pinion_buffer].observers == {obs2_1_uuid: obs2_1, log_obs2_1_uuid: log_obs2_1}
    assert cpu2.buffers[obs2_1.source_pinion_buffer].num_subscribers == 3

    assert cpu1.buffers[pro1_2.dest_pinion_buffer].uuid == pro1_2_uuid
    assert cpu2.buffers[pro2_2.dest_pinion_buffer].uuid == pro2_2_uuid
    assert cpu2.buffers[obs2_2.source_pinion_buffer].observers == {obs2_2_uuid: obs2_2, log_obs2_2_uuid: log_obs2_2}
    assert cpu2.buffers[obs2_2.source_pinion_buffer].num_subscribers == 2

    assert cpu1.buffers[pro1_3.dest_pinion_buffer].uuid == pro1_3_uuid
    assert cpu2.buffers[pro2_3.dest_pinion_buffer].uuid == pro2_3_uuid
    assert cpu2.buffers[obs2_3.source_pinion_buffer].observers == {obs2_3_uuid: obs2_3, log_obs2_3_uuid: log_obs2_3}
    assert cpu2.buffers[obs2_3.source_pinion_buffer].num_subscribers == 2

    assert len(cpu2.metrics_buffers) == 2

    assert len(sys.channels) == 5
    chan1, multichan1, diag_chan, chan2, multichan2 = sys.channels.values()
    assert chan1.channel.channel_name == "Chan1"
    assert multichan1.channel.channel_name == "MultiChan1"
    assert diag_chan.channel.channel_name == "/diagnostics"
    assert chan2.channel.channel_name == "Chan2"
    assert multichan2.channel.channel_name == "MultiChan2"

    # 2 cogs * 3 inputs per cog
    assert len(sys.observer_endpoints) == 6
    # 2 cogs * (4 outputs per cog + 1 diagnostics per cog + 2 metrics per cog)
    assert len(sys.producer_endpoints) == 14

    assert len(sys.cogs) == 2
    cog1, cog2 = sys.cogs.values()
    assert cog1.name == "cog1"
    cog1_latest_hello, cog1_multi_hello, cog1_history_hello = [
        member for member in cog1.members if isinstance(member.member, cog.InputDef)
    ]
    assert cog1_latest_hello.member.name == "latest_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_latest_hello)].entity == cog1_latest_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_latest_hello)].connected_to is None
    assert cog1_multi_hello.member.name == "multi_publisher_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_hello)].entity == cog1_multi_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_hello)].connected_to == multichan2
    assert cog1_history_hello.member.name == "history_of_hellos"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_history_hello)].entity == cog1_history_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_history_hello)].connected_to == chan2

    cog1_out_world, cog1_out_goodbye, cog1_out_multi1, cog1_out_multi2 = [
        member for member in cog1.members if isinstance(member.member, cog.OutputDef)
    ]
    assert cog1_out_world.member.name == "out_world"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_world)].entity == cog1_out_world
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_world)].connected_to == chan1
    assert cog1_out_goodbye.member.name == "out_goodbye"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_goodbye)].entity == cog1_out_goodbye
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_goodbye)].connected_to is None
    assert cog1_out_multi1.member.name == "out_multi1"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_multi1)].entity == cog1_out_multi1
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_multi1)].connected_to == multichan1
    assert cog1_out_multi2.member.name == "out_multi2"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_multi2)].entity == cog1_out_multi2
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_out_multi2)].connected_to == multichan1

    cog2_latest_hello, cog2_multi_hello, cog2_history_hello = [
        member for member in cog2.members if isinstance(member.member, cog.InputDef)
    ]
    assert cog2_latest_hello.member.name == "latest_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_latest_hello)].entity == cog2_latest_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_latest_hello)].connected_to is None
    assert cog2_multi_hello.member.name == "multi_publisher_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_hello)].entity == cog2_multi_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_hello)].connected_to == multichan1
    assert cog2_history_hello.member.name == "history_of_hellos"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_history_hello)].entity == cog2_history_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_history_hello)].connected_to == chan1

    cog2_out_world, cog2_out_goodbye, cog2_out_multi1, cog2_out_multi2 = [
        member for member in cog2.members if isinstance(member.member, cog.OutputDef)
    ]
    assert cog2_out_world.member.name == "out_world"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_world)].entity == cog2_out_world
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_world)].connected_to == chan2
    assert cog2_out_goodbye.member.name == "out_goodbye"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_goodbye)].entity == cog2_out_goodbye
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_goodbye)].connected_to is None
    assert cog2_out_multi1.member.name == "out_multi1"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_multi1)].entity == cog2_out_multi1
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_multi1)].connected_to == multichan2
    assert cog2_out_multi2.member.name == "out_multi2"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_multi2)].entity == cog2_out_multi2
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_out_multi2)].connected_to == multichan2

    cog1_event_metrics, cog1_telemetry_metrics = [
        member for member in cog1.members if isinstance(member.member, cog.MetricsOutputDef)
    ]

    assert cog1_event_metrics.member.name == "cog_event_metrics"
    assert cog1_telemetry_metrics.member.name == "cog_telemetry_metrics"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_event_metrics)].entity == cog1_event_metrics
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_event_metrics)].connected_to is None
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_telemetry_metrics)].entity == cog1_telemetry_metrics
    assert sys.producer_endpoints[lookup_uuid(module.context, cog1_telemetry_metrics)].connected_to is None

    cog2_event_metrics, cog2_telemetry_metrics = [
        member for member in cog2.members if isinstance(member.member, cog.MetricsOutputDef)
    ]
    assert cog2_event_metrics.member.name == "cog_event_metrics"
    assert cog2_telemetry_metrics.member.name == "cog_telemetry_metrics"
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_event_metrics)].entity == cog2_event_metrics
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_event_metrics)].connected_to is None
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_telemetry_metrics)].entity == cog2_telemetry_metrics
    assert sys.producer_endpoints[lookup_uuid(module.context, cog2_telemetry_metrics)].connected_to is None


def test_system_with_non_connected_cogs(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::HelloMsg;

        // Doc
        cog OptionalInputCog
        {
            inputs
            {
                message_in: Tappy<HelloMsg>
                {
                    connect_optional: true;
                }
            }
            outputs
            {
                message_out: Tappy<HelloMsg>;
            }
            execution
            {
                condition periodic: time_since_last_exec(100ms);
                execute when: periodic;
            }
        }

        // Doc
        cog OptionalOutputCog
        {
            inputs
            {
                message_in: Tappy<HelloMsg>;
            }
            outputs
            {
                message_out: Tappy<HelloMsg>
                {
                    connect_optional: true;
                }
            }
            execution
            {
                condition periodic: time_since_last_exec(100ms);
                execute when: periodic;
            }
        }

        box MissingInputConnectionBox
        {
            new optional_output_cog: OptionalOutputCog;
        }

        box MissingOutputConnectionBox
        {
            new optional_input_cog: OptionalInputCog;
        }

        box MissingOutputSystem
        {
            new missing_output_connection_box: MissingOutputConnectionBox;
            new proc2: Process(executable=exe2);
            apply HostProcess(process=proc2) in missing_output_connection_box;
            apply HostCpuDomain(cpu_domain=Cpu2) to proc2;
        }

        cpp_executable exe2
        {
            casing
            {
            }
        }

        system_target missing_output_system
        {
            box: MissingOutputSystem;
        }

        box MissingInputSystem
        {
            new missing_input_connection_box: MissingInputConnectionBox;
            new proc1: Process(executable=exe1);
            apply HostProcess(process=proc1) in missing_input_connection_box;
            apply HostCpuDomain(cpu_domain=Cpu1) to proc1;
        }

        cpp_executable exe1
        {
            casing
            {
            }
        }

        system_target missing_input_system
        {
            box: MissingInputSystem;
        }

        cpu_domain Cpu1;
        cpu_domain Cpu2;
"""

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_input_system"), fs_importer)
    unresolved_missing_input_system_ir = module.inner_scope.lookup("missing_input_system")
    assert isinstance(unresolved_missing_input_system_ir, system_target.UnresolvedSystemTarget)
    missing_input_system_ir = unresolved_missing_input_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    with pytest.raises(ValueError, match=re.escape("Observer endpoint not connected to channel: message_in")):
        systemgen.gen_system(
            root_dir=tmp_path, system_target_ir=missing_input_system_ir, write_files=True, write_json_files=True
        )

    unresolved_missing_output_system_ir = module.inner_scope.lookup("missing_output_system")
    assert isinstance(unresolved_missing_output_system_ir, system_target.UnresolvedSystemTarget)
    missing_output_system_ir = unresolved_missing_output_system_ir.get_resolved()

    with pytest.raises(ValueError, match=re.escape("Producer endpoint not connected to channel: message_out")):
        systemgen.gen_system(
            root_dir=tmp_path, system_target_ir=missing_output_system_ir, write_files=True, write_json_files=True
        )
