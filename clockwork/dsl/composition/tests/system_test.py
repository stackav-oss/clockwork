# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.composition import system, systemgen
from clockwork.dsl.ir import box, cog, compiler, hardware, node, system_target
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
    sys = system.make_system([box_ir.get_resolved()], module, False, False)
    assert sys.module is module
    assert sys.use_simplelaunch is False
    assert len(sys.channels) == 5
    assert sys.channels.keys() == {
        "HelloChan",
        "Name that doesn't follow reasonable conventions!",
        "many_publishers",
        "NetworkData",
        "/diagnostics",
    }
    assert len(sys.metrics_channels) == 8  # 2 channels per HelloCogWithMetrics (explicit enabled), 4 HelloBox instances
    assert len(sys.cogs) == 12  # 3 cogs per HelloBox, 4 HelloBox instances
    assert len(sys.data_sources) == 4  # 1 data source per HelloBox instance
    assert sys.init_data_sources == {}
    assert sys.data_source_fallbacks == {}


def test_simplesys(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 For testing only
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    sys = system.make_system([box_ir.get_resolved()], module, False, True)
    assert sys.module is module
    assert sys.use_simplelaunch is True
    assert len(sys.channels) == 5
    assert sys.channels.keys() == {"Chan1", "Chan2", "MultiChan1", "MultiChan2", "/diagnostics"}
    assert len(sys.metrics_channels) == 4  # 2 channels per cog, 2 cogs
    psys = system.make_physical_system(sys)
    # New fields should be empty
    assert sys.data_sources == {}
    assert sys.init_data_sources == {}
    assert sys.data_source_fallbacks == {}
    assert sys.realtime_playback_assignments == ()
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
    assert cpu1.simplelaunch_diagnostics_producer is not None
    assert cpu1.simplelaunch_status_producer is not None
    assert cpu2.simplelaunch_diagnostics_producer is not None
    assert cpu2.simplelaunch_status_producer is not None
    ((obs1_1_uuid, obs1_1), (obs1_2_uuid, obs1_2), (obs1_3_uuid, obs1_3), (obs1_4_uuid, obs1_4)) = (
        cpu1.bridge_observers.items()
    )
    (
        (log_obs1_1_uuid, log_obs1_1),
        (log_obs1_2_uuid, log_obs1_2),
        (log_obs1_3_uuid, log_obs1_3),
        (log_obs1_4_uuid, log_obs1_4),
        (log_obs1_5_uuid, log_obs1_5),
    ) = cpu1.log_observers.items()
    ((pro1_1_uuid, pro1_1), (pro1_2_uuid, pro1_2), (pro1_3_uuid, pro1_3), (pro1_4_uuid, pro1_4)) = (
        cpu1.bridge_producers.items()
    )
    ((obs2_1_uuid, obs2_1), (obs2_2_uuid, obs2_2), (obs2_3_uuid, obs2_3), (obs2_4_uuid, obs2_4)) = (
        cpu2.bridge_observers.items()
    )
    (
        (_log_obs2_1_uuid, _log_obs2_1),
        (log_obs2_2_uuid, log_obs2_2),
        (log_obs2_3_uuid, log_obs2_3),
        (log_obs2_4_uuid, log_obs2_4),
        (log_obs2_5_uuid, log_obs2_5),
        (log_obs2_6_uuid, log_obs2_6),
    ) = cpu2.log_observers.items()
    ((pro2_1_uuid, pro2_1), (pro2_2_uuid, pro2_2), (pro2_3_uuid, pro2_3), (pro2_4_uuid, pro2_4)) = (
        cpu2.bridge_producers.items()
    )

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

    assert obs1_4.dest_domain == cpu2.uuid
    assert len(obs1_4.remote_producers) == 1
    assert obs1_4.remote_producers[0] == pro2_4_uuid
    assert obs1_4.source_domain == cpu1.uuid
    assert cpu1.buffers[obs1_4.source_pinion_buffer].observers == {obs1_4_uuid: obs1_4, log_obs1_4_uuid: log_obs1_4}
    assert cpu1.buffers[obs1_4.source_pinion_buffer].uuid == obs1_4.source_pinion_buffer
    assert cpu1.buffers[obs1_4.source_pinion_buffer].num_subscribers == 3
    assert pro1_4.source_domain == cpu2.uuid
    assert pro1_4.dest_domain == cpu1.uuid

    assert cpu1.buffers[pro1_1.dest_pinion_buffer].uuid == pro1_1_uuid
    assert cpu2.buffers[pro2_1.dest_pinion_buffer].uuid == pro2_1_uuid
    assert cpu2.buffers[obs2_1.source_pinion_buffer].observers == {obs2_1_uuid: obs2_1, log_obs2_2_uuid: log_obs2_2}
    assert cpu2.buffers[obs2_1.source_pinion_buffer].num_subscribers == 3

    assert cpu1.buffers[pro1_2.dest_pinion_buffer].uuid == pro1_2_uuid
    assert cpu2.buffers[pro2_2.dest_pinion_buffer].uuid == pro2_2_uuid
    assert cpu2.buffers[obs2_2.source_pinion_buffer].observers == {obs2_2_uuid: obs2_2, log_obs2_3_uuid: log_obs2_3}
    assert cpu2.buffers[obs2_2.source_pinion_buffer].num_subscribers == 2

    assert cpu1.buffers[pro1_3.dest_pinion_buffer].uuid == pro1_3_uuid
    assert cpu2.buffers[pro2_3.dest_pinion_buffer].uuid == pro2_3_uuid
    assert cpu2.buffers[obs2_3.source_pinion_buffer].observers == {obs2_3_uuid: obs2_3, log_obs2_4_uuid: log_obs2_4}
    assert cpu2.buffers[obs2_3.source_pinion_buffer].num_subscribers == 2

    assert cpu1.buffers[pro1_4.dest_pinion_buffer].uuid == pro1_4_uuid
    assert cpu1.buffers[pro1_4.dest_pinion_buffer].observers == {log_obs1_5_uuid: log_obs1_5}
    assert cpu2.buffers[pro2_4.dest_pinion_buffer].uuid == pro2_4_uuid
    assert cpu2.buffers[pro2_4.dest_pinion_buffer].observers == {log_obs2_5_uuid: log_obs2_5}
    assert cpu2.buffers[obs2_4.source_pinion_buffer].observers == {obs2_4_uuid: obs2_4, log_obs2_6_uuid: log_obs2_6}
    assert cpu2.buffers[obs2_4.source_pinion_buffer].num_subscribers == 3

    assert len(cpu2.metrics_buffers) == 2

    assert len(sys.channels) == 7
    diag_chan, chan1, multichan1, chan2, multichan2, simplelaunch_status1, simplelaunch_status2 = sys.channels.values()
    assert chan1.channel.channel_name == "Chan1"
    assert multichan1.channel.channel_name == "MultiChan1"
    assert diag_chan.channel.channel_name == "/diagnostics"
    assert chan2.channel.channel_name == "Chan2"
    assert multichan2.channel.channel_name == "MultiChan2"
    assert simplelaunch_status1.channel.channel_name == "/cpu1/simplelaunch_status"
    assert simplelaunch_status2.channel.channel_name == "/cpu2/simplelaunch_status"

    # 2 cogs * 5 inputs per cog
    assert len(sys.observer_endpoints) == 10
    # 2 cogs * (4 outputs per cog + 4 snapshotable + 1 diagnostics per cog + 1 infra diagnostics per cog + 2 metrics per cog)
    assert len(sys.producer_endpoints) == 24

    assert len(sys.cogs) == 2
    cog1, cog2 = sys.cogs.values()
    assert cog1.name == "cog1"
    cog1_latest_hello, cog1_multi_hello, cog1_multi_connect_hello, cog1_history_hello = [
        member for member in cog1.members if isinstance(member.member, cog.InputDef)
    ]
    assert cog1_latest_hello.member.name == "latest_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_latest_hello)].entity == cog1_latest_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_latest_hello)].connected_to is None
    assert cog1_multi_hello.member.name == "multi_publisher_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_hello)].entity == cog1_multi_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_hello)].connected_to == multichan2
    assert cog1_multi_connect_hello.member.name == "multi_connect_hello"
    assert isinstance(cog1_multi_connect_hello.elements, list)
    cog1_multi_connect_hello_1, cog1_multi_connect_hello_2 = cog1_multi_connect_hello.elements
    assert (
        sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_connect_hello_1)].entity
        == cog1_multi_connect_hello_1
    )
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_connect_hello_1)].connected_to == chan2
    assert (
        sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_connect_hello_2)].entity
        == cog1_multi_connect_hello_2
    )
    assert sys.observer_endpoints[lookup_uuid(module.context, cog1_multi_connect_hello_2)].connected_to is None
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

    cog2_latest_hello, cog2_multi_hello, cog2_multi_connect_hello, cog2_history_hello = [
        member for member in cog2.members if isinstance(member.member, cog.InputDef)
    ]
    assert cog2_latest_hello.member.name == "latest_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_latest_hello)].entity == cog2_latest_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_latest_hello)].connected_to is None
    assert cog2_multi_hello.member.name == "multi_publisher_hello"
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_hello)].entity == cog2_multi_hello
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_hello)].connected_to == multichan1
    assert cog2_multi_connect_hello.member.name == "multi_connect_hello"
    assert isinstance(cog2_multi_connect_hello.elements, list)
    cog2_multi_connect_hello_1, cog2_multi_connect_hello_2 = cog2_multi_connect_hello.elements
    assert (
        sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_connect_hello_1)].entity
        == cog2_multi_connect_hello_1
    )
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_connect_hello_1)].connected_to == chan1
    assert (
        sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_connect_hello_2)].entity
        == cog2_multi_connect_hello_2
    )
    assert sys.observer_endpoints[lookup_uuid(module.context, cog2_multi_connect_hello_2)].connected_to is None
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


def test_data_source_connections(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// TestChan
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg: FirstMessage(channel=TestChan);
    new fallback_file: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="fallback.textproto");
    connect fallback_file to first_msg.fallback;
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect first_msg to state;
}

cpp_executable exe
{
    casing
    {
        box TestBox;
    }
}

cpu_domain Cpu1;

box TestSystem
{
    new test_box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in test_box;
    apply HostCpuDomain(cpu_domain=Cpu1) to proc;
}

system_target test_system
{
    box: TestSystem;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_data_source_connections"), fs_importer)
    system_target_ir = module.inner_scope.lookup("test_system")
    assert isinstance(system_target_ir, system_target.UnresolvedSystemTarget)
    resolved_system = system_target_ir.get_resolved()
    boxes = [resolved_system.box_instance]
    sys = system.make_system(boxes, module, False, False)
    # Check that data_sources has the instances
    assert len(sys.data_sources) == 2  # first_msg and fallback_file
    first_msg_uuid = None
    fallback_file_uuid = None
    for uuid, ds in sys.data_sources.items():
        if isinstance(ds, box.FirstMessageInstance):
            first_msg_uuid = uuid
        else:
            assert isinstance(ds, box.SerializedDataFileInstance)
            fallback_file_uuid = uuid
    assert first_msg_uuid is not None
    assert fallback_file_uuid is not None
    # Check init_data_sources
    assert len(sys.init_data_sources) == 1  # state connected to first_msg
    state_uuid = None
    for uuid in sys.states:
        state_uuid = uuid
        break
    assert state_uuid is not None
    assert sys.init_data_sources[state_uuid] == first_msg_uuid
    # Check data_source_fallbacks
    assert len(sys.data_source_fallbacks) == 1  # first_msg has fallback
    assert sys.data_source_fallbacks[first_msg_uuid] == fallback_file_uuid


def test_system_with_non_connected_cogs(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

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


def test_report_group_channels_created(fs_importer: FilesystemImporter) -> None:
    """Test that report group channels are created in metrics_channels."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("SignalTestSystemBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False, False)

    # Verify report group channels are present in metrics_channels
    # The signals_test_system.clk has 2 cog instances (test_cog1, test_cog2) each with 1 report group
    # Plus 2 cog metrics outputs per cog = 4 cog metrics channels + 2 report group channels = 6 total
    report_group_channels = [
        ch for ch in logical_system.metrics_channels.values() if "report-groups" in ch.channel.channel_name
    ]
    assert len(report_group_channels) == 2

    for rg_channel in report_group_channels:
        assert rg_channel.channel.message_size > 0
        assert rg_channel.channel.num_slots == 5
        assert "SignalTestCog" in rg_channel.channel.cog_path


def test_aligned_input_system(fs_importer: FilesystemImporter) -> None:
    """Test that aligned input connections expand into N+1 channel subscriptions."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/aligned_consumer_boxes.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("AlignedConsumerSystem")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(
        cst_node=None, module=module, scope=box_template_ir.scope, name="test", doc=None
    )
    compiler._register_box_instance_uuids(module.context, box_ir)
    sys = system.make_system([box_ir.get_resolved()], module, False, False)

    expected_channels = {"SensorChannel", "CameraChannel", "AlignmentChannel"}
    assert expected_channels.issubset(sys.channels.keys())

    consumer_cog = None
    for cog_inst in sys.cogs.values():
        if cog_inst.cog_class.name == "ConsumerCog":
            consumer_cog = cog_inst
            break
    assert consumer_cog is not None, "ConsumerCog not found in system"

    consumer_observers = [
        endpoint.entity.member.name
        for endpoint in sys.observer_endpoints.values()
        if isinstance(endpoint.entity, cog.CogInstanceMember) and endpoint.entity.cog_instance is consumer_cog
    ]
    assert sorted(consumer_observers) == sorted(
        ["raw_data", "aligned", "aligned.sensor", "aligned.camera", "aligned.lidar", "aligned.radar"]
    ), f"Unexpected consumer observer endpoints: {consumer_observers}"

    alignment_channel = sys.channels["AlignmentChannel"]
    alignment_observers = [
        obs
        for obs in alignment_channel.observers.values()
        if isinstance(obs.entity, cog.CogInstanceMember) and obs.entity.cog_instance is consumer_cog
    ]
    assert len(alignment_observers) == 1, "ConsumerCog should observe the AlignmentChannel"
    assert isinstance(alignment_observers[0].entity, cog.CogInstanceMember)
    assert alignment_observers[0].entity.member.name == "aligned"

    # Verify each upstream expanded member is connected to the correct upstream channel.
    upstream_mapping = {
        "aligned.sensor": "SensorChannel",
        "aligned.camera": "CameraChannel",
        "aligned.lidar": "LidarChannel",
        "aligned.radar": "RadarChannel",
    }
    for member_name, expected_channel_name in upstream_mapping.items():
        channel = sys.channels[expected_channel_name]
        consumer_upstream_observers = [
            obs
            for obs in channel.observers.values()
            if isinstance(obs.entity, cog.CogInstanceMember)
            and obs.entity.cog_instance is consumer_cog
            and obs.entity.member.name == member_name
        ]
        assert len(consumer_upstream_observers) == 1, f"Expected '{member_name}' to observe {expected_channel_name}"


def test_aligned_input_alignment_channel_requires_emergency_margin(fs_importer: FilesystemImporter) -> None:
    """Test that aligned input views cannot consume a channel's emergency margin."""
    source_text = """
use clockwork::dsl::composition::tests::support::aligned_consumer_aligner::{SimpleAligner, SimpleAlignerAlignmentMsg};

// A consumer with an aligned input view that leaves no emergency margin.
cog UnsafeAlignedConsumer
{
    aligned_inputs
    {
        aligned: SimpleAligner
        {
            max_msgs: 10;
        }
    }

    execution
    {
        condition new_alignment: new_message(aligned);
        execute when: new_alignment;
    }
}

// Alignment channel sized like max_num_messages: 11 gives 12 physical slots.
channel AlignmentChannel
{
    message_type: Tachyon<SimpleAlignerAlignmentMsg>;
    max_num_messages: 11;
}

box UnsafeAlignedConsumerBox
{
    new consumer: UnsafeAlignedConsumer;
    connect AlignmentChannel to consumer.aligned;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "unsafe_aligned_input_margin"), fs_importer)
    box_template_ir = module.inner_scope.lookup("UnsafeAlignedConsumerBox")
    assert isinstance(box_template_ir, box.BoxTemplate)

    with pytest.raises(ValueError, match=r"to accommodate emergency margin"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)


def test_system_direct_connect_cog_members(fs_importer: FilesystemImporter, tmp_path: Path) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

        // Doc
        channel HelloChan
        {
            message_type: Tachyon<HelloMsg>;
            max_num_messages: 10;
        }

        // Doc
        cog InitCog
        {
            states
            {
                state: Tappy<HelloMsg>
                {
                    mutable: true;
                }
            }
            execution
            {
                execute when: init;
            }
        }

        // Doc
        cog InputCog
        {
            configs
            {
              config: Tappy<HelloMsg>;
            }
            inputs
            {
                message_in: Tappy<HelloMsg>;
            }
            execution
            {
                condition periodic: time_since_last_exec(100ms);
                execute when: periodic;
            }
        }

        // Doc
        cog OutputCog
        {
            configs
            {
              config: Tappy<HelloMsg>;
            }
            states
            {
                state: Tappy<HelloMsg>
                {
                    mutable: true;
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

        box OutputBox
        {
            new output_cog: OutputCog;
            connect output_cog.message_out to HelloChan;
        }

        box InputBox
        {
            new input_cog: InputCog;
            new hello_config: SerializedDataFile(representation=Tachyon<HelloMsg>, path="path/to/file.tachyon");
            connect hello_config to input_cog.config;
        }


        box InitBox
        {
            new init_cog: InitCog;
            new hello_state: State(representation=Tachyon<HelloMsg>, init=init_cog.state);
        }

        box TestSystemBox
        {
            new init_box: InitBox;
            new input_box: InputBox;
            new output_box: OutputBox;
            connect init_box.init_cog.state to output_box.output_cog.state;
            connect input_box.input_cog.config to output_box.output_cog.config;
            connect output_box.output_cog.message_out to input_box.input_cog.message_in;
            new proc1: Process(executable=exe);
            apply HostProcess(process=proc1) in init_box;
            apply HostProcess(process=proc1) in input_box;
            apply HostProcess(process=proc1) in output_box;
            apply HostCpuDomain(cpu_domain=Cpu1) to proc1;
        }

        cpp_executable exe
        {
            casing
            {
            }
        }

        system_target test_system
        {
            box: TestSystemBox;
        }

        cpu_domain Cpu1;
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_sytem"), fs_importer)
    unresolved_test_system_ir = module.inner_scope.lookup("test_system")
    assert isinstance(unresolved_test_system_ir, system_target.UnresolvedSystemTarget)
    test_system_ir = unresolved_test_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    _, _, physical_sys = systemgen.gen_system(
        root_dir=tmp_path, system_target_ir=test_system_ir, write_files=True, write_json_files=True
    )
    assert isinstance(physical_sys, system.PhysicalSystem)
    logical_sys = physical_sys.system

    assert len(logical_sys.channels) == 1
    (hello_chan,) = logical_sys.channels.values()
    assert hello_chan.channel.channel_name == "HelloChan"

    assert len(logical_sys.configs) == 1
    (hello_config,) = logical_sys.configs.values()
    assert hello_config.entity.name == "hello_config"

    assert len(logical_sys.states) == 1
    (hello_state,) = logical_sys.states.values()
    assert hello_state.entity.name == "hello_state"

    assert len(logical_sys.cogs) == 3
    init_cog, input_cog, output_cog = logical_sys.cogs.values()

    assert init_cog.name == "init_cog"

    (init_cog_state,) = [member for member in init_cog.members if isinstance(member.member, cog.StateDef)]
    assert init_cog_state.member.name == "state"
    assert hello_state.endpoints[lookup_uuid(module.context, init_cog_state)].entity == init_cog_state

    assert input_cog.name == "input_cog"

    (input_cog_config,) = [member for member in input_cog.members if isinstance(member.member, cog.ConfigDef)]
    assert input_cog_config.member.name == "config"
    assert hello_config.endpoints[lookup_uuid(module.context, input_cog_config)].entity == input_cog_config

    (input_cog_message_in,) = [member for member in input_cog.members if isinstance(member.member, cog.InputDef)]
    assert input_cog_message_in.member.name == "message_in"
    assert (
        logical_sys.observer_endpoints[lookup_uuid(module.context, input_cog_message_in)].entity == input_cog_message_in
    )
    assert logical_sys.observer_endpoints[lookup_uuid(module.context, input_cog_message_in)].connected_to is hello_chan

    assert output_cog.name == "output_cog"

    (output_cog_config,) = [member for member in output_cog.members if isinstance(member.member, cog.ConfigDef)]
    assert output_cog_config.member.name == "config"
    assert hello_config.endpoints[lookup_uuid(module.context, output_cog_config)].entity == output_cog_config

    (output_cog_state,) = [member for member in output_cog.members if isinstance(member.member, cog.StateDef)]
    assert output_cog_state.member.name == "state"
    assert logical_sys.state_endpoints[lookup_uuid(module.context, output_cog_state)].entity == output_cog_state
    assert hello_state.endpoints[lookup_uuid(module.context, output_cog_state)].entity == output_cog_state

    (output_cog_message_out,) = [member for member in output_cog.members if isinstance(member.member, cog.OutputDef)]
    assert output_cog_message_out.member.name == "message_out"
    assert (
        logical_sys.producer_endpoints[lookup_uuid(module.context, output_cog_message_out)].entity
        == output_cog_message_out
    )
    assert (
        logical_sys.producer_endpoints[lookup_uuid(module.context, output_cog_message_out)].connected_to is hello_chan
    )


def test_system_direct_connect_unconnected_cog_output_to_cog_input(
    fs_importer: FilesystemImporter, tmp_path: Path
) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

        // Doc
        cog InputCog
        {
            inputs
            {
                message_in: Tappy<HelloMsg>;
            }
            execution
            {
                condition periodic: time_since_last_exec(100ms);
                execute when: periodic;
            }
        }

        // Doc
        cog OutputCog
        {
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

        box OutputBox
        {
            new output_cog: OutputCog;
        }

        box InputBox
        {
            new input_cog: InputCog;
        }

        box TestSystemBox
        {
            new input_box: InputBox;
            new output_box: OutputBox;
            connect output_box.output_cog.message_out to input_box.input_cog.message_in;
            new proc1: Process(executable=exe);
            apply HostProcess(process=proc1) in input_box;
            apply HostProcess(process=proc1) in output_box;
            apply HostCpuDomain(cpu_domain=Cpu1) to proc1;
        }

        cpp_executable exe
        {
            casing
            {
            }
        }

        system_target test_system
        {
            box: TestSystemBox;
        }

        cpu_domain Cpu1;
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_sytem"), fs_importer)
    unresolved_test_system_ir = module.inner_scope.lookup("test_system")
    assert isinstance(unresolved_test_system_ir, system_target.UnresolvedSystemTarget)
    test_system_ir = unresolved_test_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    with pytest.raises(RuntimeError, match=re.escape("output_cog.message_out is unconnected")):
        systemgen.gen_system(
            root_dir=tmp_path, system_target_ir=test_system_ir, write_files=True, write_json_files=True
        )


def test_system_direct_connect_unconnected_cog_state_to_cog_state(
    fs_importer: FilesystemImporter, tmp_path: Path
) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

        // Doc
        channel HelloChan
        {
            message_type: Tachyon<HelloMsg>;
            max_num_messages: 10;
        }

        // Doc
        cog InitCog
        {
            states
            {
                state: Tappy<HelloMsg>
                {
                    mutable: true;
                }
            }
            execution
            {
                execute when: init;
            }
        }

        // Doc
        cog OutputCog
        {
            states
            {
                state: Tappy<HelloMsg>
                {
                    mutable: true;
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

        box OutputBox
        {
            new output_cog: OutputCog;
            connect output_cog.message_out to HelloChan;
        }

        box InitBox
        {
            new init_cog: InitCog;
        }

        box TestSystemBox
        {
            new init_box: InitBox;
            new output_box: OutputBox;
            connect init_box.init_cog.state to output_box.output_cog.state;
            new proc1: Process(executable=exe);
            apply HostProcess(process=proc1) in init_box;
            apply HostProcess(process=proc1) in output_box;
            apply HostCpuDomain(cpu_domain=Cpu1) to proc1;
        }

        cpp_executable exe
        {
            casing
            {
            }
        }

        system_target test_system
        {
            box: TestSystemBox;
        }

        cpu_domain Cpu1;
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_sytem"), fs_importer)
    unresolved_test_system_ir = module.inner_scope.lookup("test_system")
    assert isinstance(unresolved_test_system_ir, system_target.UnresolvedSystemTarget)
    test_system_ir = unresolved_test_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    with pytest.raises(RuntimeError, match=re.escape("init_cog.state is unconnected")):
        systemgen.gen_system(
            root_dir=tmp_path, system_target_ir=test_system_ir, write_files=True, write_json_files=True
        )


def test_system_direct_connect_unconnected_cog_config_to_cog_config(
    fs_importer: FilesystemImporter, tmp_path: Path
) -> None:
    source = """
        use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

        // Doc
        channel HelloChan
        {
            message_type: Tachyon<HelloMsg>;
            max_num_messages: 10;
        }

        // Doc
        cog InputCog
        {
            configs
            {
              config: Tappy<HelloMsg>;
            }
            inputs
            {
                message_in: Tappy<HelloMsg>;
            }
            execution
            {
                condition periodic: time_since_last_exec(100ms);
                execute when: periodic;
            }
        }

        // Doc
        cog OutputCog
        {
            configs
            {
              config: Tappy<HelloMsg>;
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

        box OutputBox
        {
            new output_cog: OutputCog;
            connect output_cog.message_out to HelloChan;
        }

        box InputBox
        {
            new input_cog: InputCog;
            connect HelloChan to input_cog.message_in;
        }


        box TestSystemBox
        {
            new input_box: InputBox;
            new output_box: OutputBox;
            connect input_box.input_cog.config to output_box.output_cog.config;
            new proc1: Process(executable=exe);
            apply HostProcess(process=proc1) in input_box;
            apply HostProcess(process=proc1) in output_box;
            apply HostCpuDomain(cpu_domain=Cpu1) to proc1;
        }

        cpp_executable exe
        {
            casing
            {
            }
        }

        system_target test_system
        {
            box: TestSystemBox;
        }

        cpu_domain Cpu1;
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_sytem"), fs_importer)
    unresolved_test_system_ir = module.inner_scope.lookup("test_system")
    assert isinstance(unresolved_test_system_ir, system_target.UnresolvedSystemTarget)
    test_system_ir = unresolved_test_system_ir.get_resolved()
    (tmp_path / BazelPathResolver().to_buildtime_path(module.module_id)).mkdir(parents=True)

    with pytest.raises(RuntimeError, match=re.escape("input_cog.config is unconnected")):
        systemgen.gen_system(
            root_dir=tmp_path, system_target_ir=test_system_ir, write_files=True, write_json_files=True
        )
