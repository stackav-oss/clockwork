# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for box."""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from clockwork.dsl.ir import (
    box,
    clkbuiltins,
    cog,
    compiler,
    hardware,
    policy,
    pubsub,
    signal,
    signal_registry,
    system_target,
    typesys,
    udp,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from clockwork.dsl.ir.node import Module


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_box(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 (test code)
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    assert box_ir.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box"

    assert len(box_ir.instances) == 11

    (
        mem_box,
        hello_cog,
        hello_config_file,
        _rw_hello_init,
        _ro_hello_init,
        ro_hello,
        _rw_hello,
        _extern_hello_memory,
        _extern_hello,
        _in_udp,
        _out_udp,
    ) = box_ir.instances
    assert isinstance(mem_box, box.Box)
    (mem_hello,) = mem_box.instances
    assert isinstance(mem_hello, box.MemoryResourceInstance)
    assert mem_hello.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.mem_box.mem_hello"
    assert isinstance(hello_cog, cog.CogInstance)
    assert hello_cog.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog"
    assert (
        hello_cog.cog_class.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellocog::HelloCogWithMetrics"
    )

    assert isinstance(hello_config_file, box.SerializedDataFileInstance)
    assert hello_config_file.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_config"
    assert (
        hello_config_file.repr_typespec.value_key()
        == "::Protobuf<schema=@clockwork::clockwork::dsl::tests::support::hellomsg::HelloMsg>"
    )
    assert hello_config_file.file_path == Path("foo/bar.txtpb")

    assert isinstance(ro_hello, box.StateInstance)
    assert ro_hello.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.ro_hello"
    assert (
        ro_hello.repr_typespec.value_key()
        == "::Tachyon<schema=@clockwork::clockwork::dsl::tests::support::hellomsg::HelloMsg>"
    )

    assert len(box_ir.connections) == 19
    conn = box_ir.connections[6]
    chan = module.inner_scope.lookup("HelloChan")
    assert isinstance(chan, pubsub.Channel)
    assert chan.publishers_option == pubsub.ChannelPublishersOption.single
    assert conn.source is chan
    assert conn.target is hello_cog.attribute("latest_hello")
    assert (
        conn.target.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.latest_hello"
    )
    assert isinstance(conn.target, cog.CogInstanceMember)
    assert isinstance(conn.target.member, cog.InputDef)
    multi_chan = module.inner_scope.lookup("MultiPublisherChannel")
    assert isinstance(multi_chan, pubsub.Channel)
    assert multi_chan.publishers_option == pubsub.ChannelPublishersOption.multiple
    multi_in_conn = box_ir.connections[8]
    assert multi_in_conn.source is multi_chan
    assert multi_in_conn.target is hello_cog.attribute("multi_publisher_hello")
    assert (
        multi_in_conn.target.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.multi_publisher_hello"
    )
    multi_connect_in_conn = box_ir.connections[9]
    assert isinstance(multi_connect_in_conn.source, typesys.Values)
    assert len(multi_connect_in_conn.source.elements) == 2
    assert multi_connect_in_conn.target is hello_cog.attribute("multi_connect_hello")
    assert (
        multi_connect_in_conn.target.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.multi_connect_hello"
    )
    multi_out_conn1 = box_ir.connections[12]
    assert multi_out_conn1.source is hello_cog.attribute("out_multi1")
    assert (
        multi_out_conn1.source.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.out_multi1"
    )
    assert multi_out_conn1.target is multi_chan
    multi_out_conn2 = box_ir.connections[13]
    assert multi_out_conn2.source is hello_cog.attribute("out_multi2")
    assert (
        multi_out_conn2.source.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.out_multi2"
    )
    assert multi_out_conn2.target is multi_chan


def test_clk_box(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/clk_hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    assert box_ir.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box"

    assert len(box_ir.instances) == 11

    (
        mem_box,
        hello_cog,
        hello_config_file,
        _rw_hello_init,
        _ro_hello_init,
        ro_hello,
        _rw_hello,
        _extern_hello_memory,
        _extern_hello,
        _in_udp,
        _out_udp,
    ) = box_ir.instances
    assert isinstance(mem_box, box.Box)
    (mem_hello,) = mem_box.instances
    assert isinstance(mem_hello, box.MemoryResourceInstance)
    assert mem_hello.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.mem_box.mem_hello"
    assert isinstance(hello_cog, cog.CogInstance)
    assert hello_cog.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_cog"
    assert (
        hello_cog.cog_class.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellocog::HelloCogWithMetrics"
    )

    assert isinstance(hello_config_file, box.SerializedDataFileInstance)
    assert (
        hello_config_file.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_config"
    )
    assert (
        hello_config_file.repr_typespec.value_key()
        == "::Protobuf<schema=@clockwork::clockwork::dsl::tests::support::clk_hellomsg::HelloMsg>"
    )
    assert hello_config_file.file_path == Path("foo/bar.txtpb")

    assert isinstance(ro_hello, box.StateInstance)
    assert ro_hello.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.ro_hello"
    assert (
        ro_hello.repr_typespec.value_key()
        == "::Tachyon<schema=@clockwork::clockwork::dsl::tests::support::clk_hellomsg::HelloMsg>"
    )

    assert len(box_ir.connections) == 18
    conn = box_ir.connections[6]
    chan = module.inner_scope.lookup("HelloChan")
    assert isinstance(chan, pubsub.Channel)
    assert chan.publishers_option == pubsub.ChannelPublishersOption.single
    assert conn.source is chan
    assert conn.target is hello_cog.attribute("latest_hello")
    assert (
        conn.target.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_cog.latest_hello"
    )
    assert isinstance(conn.target, cog.CogInstanceMember)
    assert isinstance(conn.target.member, cog.InputDef)
    multi_chan = module.inner_scope.lookup("MultiPublisherChannel")
    assert isinstance(multi_chan, pubsub.Channel)
    assert multi_chan.publishers_option == pubsub.ChannelPublishersOption.multiple
    multi_in_conn = box_ir.connections[8]
    assert multi_in_conn.source is multi_chan
    assert multi_in_conn.target is hello_cog.attribute("multi_publisher_hello")
    assert (
        multi_in_conn.target.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_cog.multi_publisher_hello"
    )
    multi_out_conn1 = box_ir.connections[11]
    assert multi_out_conn1.source is hello_cog.attribute("out_multi1")
    assert (
        multi_out_conn1.source.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_cog.out_multi1"
    )
    assert multi_out_conn1.target is multi_chan
    multi_out_conn2 = box_ir.connections[12]
    assert multi_out_conn2.source is hello_cog.attribute("out_multi2")
    assert (
        multi_out_conn2.source.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::clk_hellomod.box.hello_cog.out_multi2"
    )
    assert multi_out_conn2.target is multi_chan


def test_apply_policy_single(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
// Thing
schema Thing
{
    fields
    {
        // Do a thing?
        #0 do_thing: Bool = true;
    }
}

// A test policy
def policy TestPolicy
{
    binds_to: CogInstance;
    schema: Thing;
}

box SubBox
{
    new sub_cog: hellocog::HelloCog;
    apply TestPolicy(do_thing=false) to sub_cog;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)
    sub_box = module.inner_scope.lookup("SubBox")
    assert isinstance(sub_box, box.BoxTemplate)
    sub_box.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="sub_box", doc=None)
    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()
    (policy_data,) = policy.lookup_all_policies(module, policy_class)
    assert policy_data.policy_class is policy_class
    assert policy_data.data.data["do_thing"] is clkbuiltins.FALSE_VALUE


def test_apply_policy_recursive(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
// Thing
schema Thing
{
    fields
    {
        // Do a thing?
        #0 do_thing: Bool = true;
    }
}

// A test policy
def policy TestPolicy
{
    binds_to: CogInstance;
    schema: Thing;
}

box SubBox
{
    new sub_cog: hellocog::HelloCog;
}

box SuperBox
{
    new sub_box: SubBox;
}

box UltraBox
{
    new super_box: SuperBox;
    apply TestPolicy(do_thing=false) in super_box;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)
    ultra_box = module.inner_scope.lookup("UltraBox")
    assert isinstance(ultra_box, box.BoxTemplate)
    ultra_box.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="ultra_box", doc=None)
    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()
    (policy_data,) = policy.lookup_all_policies(module, policy_class)
    assert policy_data.policy_class is policy_class
    assert policy_data.data.data["do_thing"] is clkbuiltins.FALSE_VALUE
    assert isinstance(policy_data.target, cog.CogInstance)


def test_composition_policies(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloSystem")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(
        cst_node=None, module=module, scope=box_template_ir.scope, name="test", doc=None
    )
    compiler._register_box_instance_uuids(module.context, box_ir)
    process_policies = list(policy.lookup_all_policies(module, box.HOST_PROCESS_POLICY))
    # 10 in each HelloBox, two HelloBoxes per HelloProcs, two HelloProcs in
    # system, another HelloBox for hello_world system
    assert len(process_policies) == 55
    process_counts: dict[str, int] = defaultdict(int)
    for process_policy in process_policies:
        assert isinstance(
            process_policy.target,
            cog.CogInstance
            | box.StateInstance
            | box.SerializedDataFileInstance
            | box.MemoryResourceInstance
            | udp.UdpSocketInstance,
        )
        process = process_policy.data.data["process"]
        assert isinstance(process, box.ProcessInstance)
        process_counts[process.value_key()] += 1
    assert len(process_counts) == 5
    assert all(x == 11 for x in process_counts.values())
    cpu_policies = list(policy.lookup_all_policies(module, box.HOST_CPU_DOMAIN_POLICY))
    assert len(cpu_policies) == 5
    cpu_counts: dict[str, int] = defaultdict(int)
    for cpu_policy in cpu_policies:
        assert isinstance(cpu_policy.target, box.ProcessInstance)
        assert cpu_policy.target.value_key() in process_counts
        cpu_domain = cpu_policy.data.data["cpu_domain"]
        assert isinstance(cpu_domain, hardware.CpuDomain)
        cpu_counts[cpu_domain.value_key()] += 1
    assert cpu_counts == {
        "CpuDomain(HostA)": 3,
        "CpuDomain(HostB)": 2,
    }



def test_unsatisfiable_safety_margin(fs_importer: FilesystemImporter) -> None:
    # Step 1: Define a cog with an input with a six-message view and connect a ten-message channel to it.
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Doc.
cog SomeCog
{
    inputs
    {
        // Doc.
        hellos: Tappy<hellomsg::HelloMsg>
        {
            max_msgs: 6;
        }
    }

    execution
    {
        condition new_msg: new_message(max=2, input=hellos);
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic or new_msg;
    }
}

// Channel for HelloMsg messages
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box CogBox
{
    new cog1: SomeCog;
    connect Chan1 to cog1.hellos;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "unsatisfiable_safety_margin"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    # Step 2: Confirm that the compiler raises an error about the channel being too small to satisfy the view and the default safety margin.
    # view size (6) + default margin (max(6, Chan1.max_num_message/2)) should be too large
    with pytest.raises(ValueError, match=r"Channel Chan1 is not large enough"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)

    # Step 3: Define a cog with an input with an eleven-message safety margin and connect a ten-message channel to it.
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Doc.
cog SomeCog
{
    inputs
    {
        // Doc.
        hellos: Tappy<hellomsg::HelloMsg>
        {
            safety_margin: 11;
        }
    }

    execution
    {
        condition new_msg: new_message(max=2, input=hellos);
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic or new_msg;
    }
}

// Channel for HelloMsg messages
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box CogBox
{
    new cog1: SomeCog;
    connect Chan1 to cog1.hellos;
}
"""
    # Step 4: Confirm that the compiler raises an error about the channel being too small to satisfy the eleven-message margin.
    # user-specified margin (11) should be flagged as unsatisifable
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "unsatisfiable_safety_margin"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match=r"Channel Chan1 is not large enough"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)

    # Step 5: Define a cog with an input with a one-message view and a safety
    # margin of one, and connect a ten-message channel to it.

    # Check that we can't set the safety margin to a value that's less than the
    # emergency margin (the one that causes the runner to terminate itself when
    # breached).
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Doc.
cog SomeCog
{
    inputs
    {
        // Doc.
        hellos: Tappy<hellomsg::HelloMsg>
        {
            safety_margin: 1;
        }
    }

    execution
    {
        condition new_msg: new_message(max=2, input=hellos);
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic or new_msg;
    }
}

// Channel for HelloMsg messages
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box CogBox
{
    new cog1: SomeCog;
    connect Chan1 to cog1.hellos;
}
"""
    # Step 6: Confirm that the compiler raises an error about how the specified margin is less than the emergency margin.
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "unsatisfiable_safety_margin"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match=r"is less than the emergency margin"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)

    # Step 7: Define a cog with an input with a one-message view and connect a one-message channel to it.
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Doc.
cog SomeCog
{
    inputs
    {
        // Doc.
        hellos: Tappy<hellomsg::HelloMsg>;
    }

    execution
    {
        condition new_msg: new_message(input=hellos);
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic or new_msg;
    }
}

// Channel for HelloMsg messages
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 1;
}

box CogBox
{
    new cog1: SomeCog;
    connect Chan1 to cog1.hellos;
}
"""
    # Step 8: Confirm that the compiler raises an error about how the input view is larger than the emergency margin.
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "view_too_large"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match=r"to accommodate emergency margin"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)


def test_invalid_skip_threshold(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Doc.
cog SomeCog
{
    inputs
    {
        // Doc.
        hellos: Tappy<hellomsg::HelloMsg>
        {
            skip_threshold: 9;
            max_msgs: 3;
        }
    }

    execution
    {
        condition new_msg: new_message(max=2, input=hellos);
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic or new_msg;
    }
}

// Channel for HelloMsg messages
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box CogBox
{
    new cog1: SomeCog;
    connect Chan1 to cog1.hellos;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "invalid_skip_and_view"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match=r"sum to more than the capacity of Chan1"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)


def test_first_message(fs_importer: FilesystemImporter) -> None:
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
    new fallback_file: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="fallback.txtpb");
    connect fallback_file to first_msg.fallback;
    new first_msg_with_default: FirstMessage(channel=TestChan, allow_default=true);
}
"""
    module: Module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_first_message"), fs_importer)
    box_template_ir = module.inner_scope.lookup("TestBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    test_chan = module.inner_scope.lookup("TestChan")
    first_msg, fallback_file, first_msg_with_default = box_ir.instances
    assert isinstance(first_msg, box.FirstMessageInstance)
    assert first_msg.channel is test_chan
    assert first_msg.allow_default is False
    assert isinstance(fallback_file, box.SerializedDataFileInstance)
    assert isinstance(first_msg_with_default, box.FirstMessageInstance)
    assert first_msg_with_default.channel is test_chan
    assert first_msg_with_default.allow_default is True

    assert len(box_ir.connections) == 1
    conn = box_ir.connections[0]
    assert conn.source is fallback_file
    assert conn.target is first_msg.fallback_endpoint


def test_signal_instantiation(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::test_signals::{multi_signal, simple_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog InstanceCog
{
    signals
    {
        // Simple signal reference
        simple_signal;
        // Entry-level instance name
        entry_level: multi_signal
        {
            instance_name: "entry_inst";
        }
        // Multiple instances of same signal
        instance_one: multi_signal
        {
            instance_name: "one_instance";
        }
        instance_two: multi_signal
        {
            instance_name: "another_instance";
        }
        inline_multi: signal UInt64
        {
            multi_instance: true;
        }
    }

    signals group_level
    {
        instance_name: "group_inst";
        // Entry inherits group-level instance name
        inherits_group: multi_signal;
        // Entry overrides group-level instance name
        overrides_group: multi_signal
        {
            instance_name: "override_inst";
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for InstanceCog.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

policy ReportGroupPolicy for InstanceCog.group_level
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

box CogBox
{
    new cog1: InstanceCog;
}
"""

    module: Module = compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_signal_instantiation"), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    simple_signal = module.inner_scope.lookup("simple_signal")
    assert isinstance(simple_signal, signal.Signal)
    multi_signal = module.inner_scope.lookup("multi_signal")
    assert isinstance(multi_signal, signal.Signal)

    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    (instance_cog,) = box_ir.instances
    assert isinstance(instance_cog, cog.CogInstance)
    assert {"default", "group_level", "cog_event_metrics_group", "cog_telemetry_metrics_group"} == {
        report_group.group_def.name for report_group in instance_cog.report_group_instances
    }
    for report_group in instance_cog.report_group_instances:
        if report_group.group_def.name == "default":
            assert len(report_group.entries) == 5
            signal_names = {"simple_signal", "entry_level", "instance_one", "instance_two", "inline_multi"}
            report_group_signal_names = set(report_group.entries.keys())
            assert signal_names == report_group_signal_names
            # Default instance name is the cog fqn
            assert report_group.entries["simple_signal"].instance_name == instance_cog.fqn
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, simple_signal.get_resolved().signal_name, instance_cog.fqn
                )
                is not None
            )
            assert report_group.entries["entry_level"].instance_name == "entry_inst"
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, multi_signal.get_resolved().signal_name, "entry_inst"
                )
                is not None
            )
            assert report_group.entries["instance_one"].instance_name == "one_instance"
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, multi_signal.get_resolved().signal_name, "one_instance"
                )
                is not None
            )
            assert report_group.entries["instance_two"].instance_name == "another_instance"
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, multi_signal.get_resolved().signal_name, "another_instance"
                )
                is not None
            )
            assert report_group.entries["inline_multi"].instance_name == instance_cog.fqn
            inline_multi = report_group.entries["inline_multi"].signal
            assert (
                signal_registry.lookup_signal_instance(module.context, inline_multi.signal_name, instance_cog.fqn)
                is not None
            )
        if report_group.group_def.name == "group_level":
            assert len(report_group.entries) == 2
            signal_names = {"inherits_group", "overrides_group"}
            report_group_signal_names = set(report_group.entries.keys())
            assert signal_names == report_group_signal_names
            assert report_group.entries["inherits_group"].instance_name == "group_inst"
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, multi_signal.get_resolved().signal_name, "group_inst"
                )
                is not None
            )
            assert report_group.entries["overrides_group"].instance_name == "override_inst"
            assert (
                signal_registry.lookup_signal_instance(
                    module.context, multi_signal.get_resolved().signal_name, "override_inst"
                )
                is not None
            )


def test_two_cog_conflicting_cogs(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::test_signals::{simple_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog InstanceCog
{
    signals
    {
        // Simple signal reference
        simple_signal;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for InstanceCog.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

box CogBox
{
    new cog1: InstanceCog;
    new cog2: InstanceCog;
}
"""

    module: Module = compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_signal_instantiation"), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match="Cannot register additional instance"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)


def test_non_multi_instance_signal(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::test_signals::{simple_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog NonMultiInstanceCog
{
    signals
    {
        // Simple signal reference
        simple_signal
        {
            instance_name: "instance1";
        }

        another_simple_one: simple_signal
        {
            instance_name: "instance2";
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for NonMultiInstanceCog.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

box CogBox
{
    new cog1: NonMultiInstanceCog;
}
"""

    module: Module = compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_non_multi_instance_signal"), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match="Cannot register additional instance"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)


def test_box_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomod;

box TestBox
{
    new test_box: clk_hellomod::HelloBox;
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for test_box, require [cpp], have []")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_box_without_use_cpp"), fs_importer)


def test_box_in_box_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use clockwork::dsl::tests::support::clk_hellobox;

box TestBox
{
    new test_box: clk_hellobox::HelloBox;
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for hello_box, require [cpp], have []")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_box_in_box_without_use_cpp"), fs_importer)


def test_cog_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellocog;

box TestBox
{
    new test_cog: clk_hellocog::HelloCogWithMetrics;
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for test_cog, require [cpp], have []")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_cog_without_use_cpp"), fs_importer)


def test_udp_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomod;

box TestBox
{
    new test_udp: clk_hellomod::OutgoingUdpSocket;
}
"""
    with pytest.raises(
        ValueError, match=re.escape("Missing use targets for OutgoingUdpSocket, require [cpp], have []")
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_udp_without_use_cpp"), fs_importer)


def test_first_msg_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomod;

box TestBox
{
    new test_first_msg:  FirstMessage(channel=clk_hellomod::HelloChan);
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use statement for HelloMsg, require [cpp]")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_first_msg_without_use_cpp"), fs_importer)


def test_proto_config_without_use_proto(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[cpp, proto] clockwork::dsl::tests::support::clk_hellomsg;

box TestBox
{
    new test_config: SerializedDataFile(representation=Protobuf<clk_hellomsg::HelloMsg>, path="foo/bar.txtpb");
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape("Missing use targets for HelloMsg, require [cpp, proto, proto_conv], have [cpp, proto]"),
    ):
        compiler.compile_source_text(
            source_text, ModuleID(CLK_REPO, "test_proto_config_without_use_proto"), fs_importer
        )


def test_tachyon_config_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomsg;

box TestBox
{
    new test_config: SerializedDataFile(representation=Tachyon<clk_hellomsg::HelloMsg>, path="foo/bar.txtpb");
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for HelloMsg, require [cpp], have []")):
        compiler.compile_source_text(
            source_text, ModuleID(CLK_REPO, "test_tachyon_config_without_use_cpp"), fs_importer
        )


def test_tachyon_state_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomsg;

box TestBox
{
    new test_state: State(representation=Tachyon<clk_hellomsg::HelloMsg>);
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for HelloMsg, require [cpp], have []")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_tachyon_state_without_use_cpp"), fs_importer)


def test_extern_state_without_use_cpp(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellocog;

box TestBox
{
    new test_mem: HeapMemory(max_size=1'000'000);
    new test_state: State(representation=clk_hellocog::CxxState, memory_resource=test_mem);
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing use targets for CxxState, require [cpp], have []")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_extern_state_without_use_cpp"), fs_importer)


def test_tachyon_state_with_memory_resource(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomsg;

box TestBox
{
    new test_memory: HeapMemory(max_size=1024);
    new test_state: State(representation=Tachyon<clk_hellomsg::HelloMsg>, memory_resource=test_memory);
}
"""
    with pytest.raises(
        ValueError, match=re.escape("States with Tachyon representations can not specify a memory_resource")
    ):
        compiler.compile_source_text(
            source_text, ModuleID(CLK_REPO, "test_tachyon_state_with_memory_resource"), fs_importer
        )


def test_cpp_state_without_memory_resource(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellocog;

box TestBox
{
    new test_state: State(representation=clk_hellocog::CxxState);
}
"""
    with pytest.raises(ValueError, match=re.escape("Extern type state must have a memory_resource parameter")):
        compiler.compile_source_text(
            source_text, ModuleID(CLK_REPO, "test_cpp_state_without_memory_resource"), fs_importer
        )


def test_state_without_representation(fs_importer: FilesystemImporter) -> None:
    source_text = """
#![generate(cpp_exe)]

box TestBox
{
    new test_memory: HeapMemory(max_size=1024);
    new test_state: State(memory_resource=test_memory);
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing parameter 'representation'")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_state_without_representation"), fs_importer)


def test_state_with_unsupported_representation(fs_importer: FilesystemImporter) -> None:
    source_text = """

#![generate(cpp_exe)]
use[] clockwork::dsl::tests::support::clk_hellomsg;

box TestBox
{
    new test_state: State(representation=Tappy<clk_hellomsg::HelloMsg>);
}
"""
    with pytest.raises(
        TypeError, match=re.escape("Representation must be a Tachyon instantiation or extern type, not")
    ):
        compiler.compile_source_text(
            source_text, ModuleID(CLK_REPO, "test_state_with_unsupported_representation"), fs_importer
        )


def test_non_multi_instance_signal_in_nested_box(fs_importer: FilesystemImporter) -> None:
    """Non-multi-instance signals should work in cogs within nested boxes.

    When a box containing a cog with a non-multi-instance signal is nested inside another box,
    compile_source_text with cpp_exe generation should not raise a duplicate signal instance error.
    The cog is only instantiated once, so the signal should be registered exactly once.
    """
    source_text = """
#![generate(cpp, cpp_cog, cpp_exe)]
#![cpp(namespace=test::nested_box_signals)]

use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Cog with a non-multi-instance signal
cog InnerCog
{
    signals perf_metrics
    {
        // Non-multi-instance signal
        total_runtime: signal Float32
        {
            post_aggregation: ["min", "max", "mean"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for InnerCog.perf_metrics
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 50;
    max_duration = 5s;
}

// Inner box containing the cog
box InnerBox
{
    new inner_cog: InnerCog;
}

// Outer box nesting the inner box
box OuterBox
{
    new inner_box: InnerBox;
}
"""
    # This should compile successfully without raising a duplicate signal instance error
    compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_non_multi_instance_signal_in_nested_box"), fs_importer
    )


def test_connecting_instantiated_channels(fs_importer: FilesystemImporter) -> None:
    """Test that instantiated generic channels can be used in box connections."""
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Cog with input and output endpoints
cog SomeCog
{
    inputs
    {
        // Input endpoint
        input: Tappy<hellomsg::HelloMsg>
        {
            max_msgs: 6;
        }
    }

    outputs
    {
        // Output endpoint
        output: Tappy<hellomsg::HelloMsg>;
    }

    execution
    {
        condition periodic: time_since_last_exec(10ms);
        execute when: periodic;
    }
}

// Generic channel with a string parameter
channel Chan
{
    parameters
    {
        // Channel name suffix
        some_string: String;
    }
    name: fmt!("/{some_string}");
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box CogBox
{
    new cog: SomeCog;
    connect Chan<"input"> to cog.input;
    connect cog.output to Chan<"output">;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "connection_generic_channels"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)

    # Instantiate the box to verify connections resolve properly
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    resolved = box_ir.get_resolved()

    # Verify we have two connections
    assert len(resolved.connections) == 2

    # Verify the first connection: Chan<"input"> -> cog.input
    input_conn = resolved.connections[0]
    assert isinstance(input_conn.source, pubsub.InstantiatedChannel)
    assert input_conn.source.channel_name.value == "/input"
    assert isinstance(input_conn.target, cog.CogInstanceMember)
    assert input_conn.target.member.name == "input"

    # Verify the second connection: cog.output -> Chan<"output">
    output_conn = resolved.connections[1]
    assert isinstance(output_conn.source, cog.CogInstanceMember)
    assert output_conn.source.member.name == "output"
    assert isinstance(output_conn.target, pubsub.InstantiatedChannel)
    assert output_conn.target.channel_name.value == "/output"


def test_parameterized_boxes(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 (test code)
    source_text = """
use clockwork::dsl::tests::support::clk_parameterized_box::{CxxState, TestBoxImpl, clk_parameterized_box_clk_exe};
use clockwork::dsl::tests::support::parameterized_types::{TestSchema};

cpu_domain Cpu1;
cpu_domain Cpu2;

box ParamTestBox
{
  parameters
  {
    cpu1: CpuDomain;
    cpu2: CpuDomain;
    proc: ProcessType;
  }

  new box_impl1 : TestBoxImpl<channel_prefix=fmt!("{snake_case_name(cpu1)}"), group_id="fault_injector_b", state_type=CxxState, input_type=UInt8, output_type=UInt8>;
  new box_impl2 : TestBoxImpl<channel_prefix=fmt!("{snake_case_name(cpu2)}"), group_id="fault_injector_b", state_type=Tappy<TestSchema<Bool>>, input_type=UInt8, output_type=Bool>;

  apply HostProcess(process=proc) in box_impl1;
  apply HostProcess(process=proc) in box_impl2;

  apply HostCpuDomain(cpu_domain=cpu1) in box_impl1;
  apply HostCpuDomain(cpu_domain=cpu2) in box_impl2;
}

box TestSystemBox
{
  new proc : Process(executable=clk_parameterized_box_clk_exe);
  new param_box: ParamTestBox<cpu1=Cpu1, cpu2=Cpu2, proc=proc>;
}

system_target target_sys
{
  box: TestSystemBox;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "parameterized_boxes"), fs_importer)

    target = module.inner_scope.lookup("target_sys")
    assert isinstance(target, system_target.UnresolvedSystemTarget)
    assert isinstance(target.resolved, system_target.SystemTarget)

    target_box = target.resolved.box_instance
    assert isinstance(target_box, box.ResolvedBox)
    assert target_box.name == "target_sys"
    assert len(target_box.instances) == 2

    param_box = target_box.instances[1]
    assert isinstance(param_box, box.Box)
    assert param_box.name == "param_box"
    assert param_box.resolved
    assert len(param_box.resolved.instances) == 2

    box_impl1 = param_box.resolved.instances[0]
    assert isinstance(box_impl1, box.Box)
    assert box_impl1.name == "box_impl1"
    assert box_impl1.resolved
    assert len(box_impl1.instances) == 4

    socket_1 = box_impl1.resolved.instances[0]
    assert socket_1.name == "socket"
    assert isinstance(socket_1, udp.UdpSocketInstance)

    cog_1 = box_impl1.resolved.instances[1]
    assert cog_1.name == "cog"
    assert isinstance(cog_1, cog.CogInstance)

    param_cog1_1 = box_impl1.resolved.instances[2]
    assert param_cog1_1.name == "param_cog1"
    assert isinstance(param_cog1_1, cog.CogInstance)
    assert (
        param_cog1_1.cog_class.name
        == "ParamTestCog_config_type_::Bool_group_id_fault_injector_b_input_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_instance_id_a_output_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_state_type_@clockwork::clockwork::dsl::tests::support::clk_parameterized_box::CxxState"
    )

    param_cog2_1 = box_impl1.resolved.instances[3]
    assert param_cog2_1.name == "param_cog2"
    assert isinstance(param_cog2_1, cog.CogInstance)
    assert (
        param_cog2_1.cog_class.name
        == "ParamTestCog_config_type_::UInt8_group_id_fault_injector_b_input_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_instance_id_b_output_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_state_type_@clockwork::clockwork::dsl::tests::support::clk_parameterized_box::CxxState"
    )

    box_impl2 = param_box.resolved.instances[1]
    assert isinstance(box_impl2, box.Box)
    assert box_impl2.name == "box_impl2"
    assert box_impl2.resolved
    assert len(box_impl2.instances) == 4

    socket_2 = box_impl2.resolved.instances[0]
    assert socket_2.name == "socket"
    assert isinstance(socket_2, udp.UdpSocketInstance)

    cog_2 = box_impl2.resolved.instances[1]
    assert cog_2.name == "cog"
    assert isinstance(cog_2, cog.CogInstance)

    param_cog1_2 = box_impl2.resolved.instances[2]
    assert param_cog1_2.name == "param_cog1"
    assert isinstance(param_cog1_2, cog.CogInstance)
    assert (
        param_cog1_2.cog_class.name
        == "ParamTestCog_config_type_::Bool_group_id_fault_injector_b_input_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_instance_id_a_output_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::Bool>_state_type_::Tappy<schema=@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::Bool>>"
    )

    param_cog2_2 = box_impl2.resolved.instances[3]
    assert param_cog2_2.name == "param_cog2"
    assert isinstance(param_cog2_2, cog.CogInstance)
    assert (
        param_cog2_2.cog_class.name
        == "ParamTestCog_config_type_::UInt8_group_id_fault_injector_b_input_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::UInt8>_instance_id_b_output_type_@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::Bool>_state_type_::Tappy<schema=@clockwork::clockwork::dsl::tests::support::parameterized_types::TestSchema<param=::Bool>>"
    )


def test_parameterized_box_instantiations(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::clk_parameterized_box::{CxxState, TestBoxImpl, clk_parameterized_box_clk_exe};

cpu_domain Cpu1;

box TestBox
{
  new test_box : TestBoxImpl<channel_prefix="cpu22", group_id="fault_injector_b", state_type=CxxState, input_type=UInt8, output_type=UInt8>;
  new proc : Process(executable=clk_parameterized_box_clk_exe);
  apply HostProcess(process=proc) in test_box;
  apply HostCpuDomain(cpu_domain=Cpu1) in test_box;
}

system_target TargetSys
{
  box: TestBox;
}
"""
    with pytest.raises(ValueError, match="Box has not been instantiated"):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "parameterized_boxes"), fs_importer)


def test_executable_type_in_builtins_scope() -> None:
    """EXECUTABLE_TYPE is registered in the builtins scope and usable as a parameter type bound."""
    executable = clkbuiltins.BUILTINS_SCOPE.lookup("Executable")
    assert executable is clkbuiltins.EXECUTABLE_TYPE


def test_state_instance_type_in_builtins_scope() -> None:
    """STATE_INSTANCE_TYPE is registered in the builtins scope and usable as a parameter type bound."""
    state_instance = clkbuiltins.BUILTINS_SCOPE.lookup("StateInstance")
    assert state_instance is clkbuiltins.STATE_INSTANCE_TYPE


def test_box_executable_type_parameter(fs_importer: FilesystemImporter) -> None:
    """A box parameter typed as Executable accepts a CppExecutable value."""
    source = """
use clockwork::dsl::tests::support::clk_parameterized_box::{clk_parameterized_box_clk_exe};

box ExeBox {
    parameters {
        exe: Executable;
    }
    new proc : Process(executable=exe);
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)
    exe_box = module.inner_scope.lookup("ExeBox")
    assert isinstance(exe_box, box.BoxTemplate)
    params = exe_box.generic_parameters()
    assert params is not None
    assert len(params) == 1
    assert params[0].name == "exe"
    assert params[0].type_bound is clkbuiltins.EXECUTABLE_TYPE


def test_box_state_instance_type_parameter(fs_importer: FilesystemImporter) -> None:
    """A box parameter typed as StateInstance accepts a StateInstance value."""
    source = """
use clockwork::dsl::tests::support::hellomsg;

box StateBox {
    parameters {
        state: StateInstance;
    }
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)
    state_box = module.inner_scope.lookup("StateBox")
    assert isinstance(state_box, box.BoxTemplate)
    params = state_box.generic_parameters()
    assert params is not None
    assert len(params) == 1
    assert params[0].name == "state"
    assert params[0].type_bound is clkbuiltins.STATE_INSTANCE_TYPE


def test_apply_host_process_with_process_parameter(fs_importer: FilesystemImporter) -> None:
    """Apply HostProcess(process=proc) works when proc is a ProcessInstance parameter."""
    source = """
use clockwork::dsl::tests::support::clk_parameterized_box::{TestBoxImpl, clk_parameterized_box_clk_exe, CxxState};

cpu_domain Cpu1;

box WrapperBox {
    new proc : Process(executable=clk_parameterized_box_clk_exe);
    new inner : TestBoxImpl<channel_prefix="cpu1", group_id="fault_injector_b", state_type=CxxState, input_type=UInt8, output_type=UInt8>;
    apply HostProcess(process=proc) in inner;
    apply HostCpuDomain(cpu_domain=Cpu1) in inner;
}

system_target TargetSys
{
    box: WrapperBox;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test"), importer=fs_importer)
    wrapper = module.inner_scope.lookup("WrapperBox")
    assert isinstance(wrapper, box.BoxTemplate)
    wrapper.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="wrapper", doc=None)
    process_policies = list(policy.lookup_all_policies(module, box.HOST_PROCESS_POLICY))
    assert len(process_policies) > 0
    process = process_policies[0].data.data["process"]
    assert isinstance(process, box.ProcessInstance)


def test_match_statement(fs_importer: FilesystemImporter) -> None:
    source_text = """
// Modes the box can be configured with.
enum Mode
{
    values
    {
        // A
        #0 mode_a default;
        // B
        #1 mode_b;
        // C
        #2 mode_c;
    }
}

box NestedA
{
}

box NestedB
{
}

box ConditionalBox
{
  parameters
  {
    mode: Mode;
  }

  match mode {
    Mode::mode_a => {
      new box_a: NestedA;
    },
    Mode::mode_b => {
      new box_b: NestedB;
    },
  }
}

box SystemBox
{
    new conditional_box_a: ConditionalBox<Mode::mode_a>;
    new conditional_box_b: ConditionalBox<Mode::mode_b>;
}

system_target the_system
{
  box: SystemBox;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_match_box"), fs_importer)

    system = module.inner_scope.lookup("the_system")
    assert isinstance(system, system_target.UnresolvedSystemTarget)
    assert isinstance(system.resolved, system_target.SystemTarget)

    system_box = system.resolved.box_instance
    assert isinstance(system_box, box.ResolvedBox)
    assert len(system_box.instances) == 2

    conditional_a = system_box.instances[0]
    assert isinstance(conditional_a, box.Box)
    assert conditional_a.name == "conditional_box_a"
    assert len(conditional_a.instances) == 1
    nested_a = conditional_a.instances[0]
    assert isinstance(nested_a, box.Box)
    assert nested_a.name == "box_a"
    assert nested_a.template is not None
    assert nested_a.template.name == "NestedA"

    conditional_b = system_box.instances[1]
    assert isinstance(conditional_b, box.Box)
    assert conditional_b.name == "conditional_box_b"
    assert len(conditional_b.instances) == 1
    nested_b = conditional_b.instances[0]
    assert isinstance(nested_b, box.Box)
    assert nested_b.name == "box_b"
    assert nested_b.template is not None
    assert nested_b.template.name == "NestedB"


def test_conditional_statement(fs_importer: FilesystemImporter) -> None:
    source_text = """
use std::traits;

box NestedA
{
}

box NestedB
{
}

box ConditionalBox
{
  parameters
  {
    mode: String;
  }

  cond {
    mode == "a" => {
      new box_a: NestedA;
    },
    mode == "b" => {
      new box_b: NestedB;
    },
    else => {}
  }
}

box SystemBox
{
    new conditional_box_a: ConditionalBox<\"a\">;
    new conditional_box_b: ConditionalBox<\"b\">;
    new defaulted_box: ConditionalBox<\"c\">;
}

system_target the_system
{
  box: SystemBox;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_cond_box"), fs_importer)

    system = module.inner_scope.lookup("the_system")
    assert isinstance(system, system_target.UnresolvedSystemTarget)
    assert isinstance(system.resolved, system_target.SystemTarget)

    system_box = system.resolved.box_instance
    assert isinstance(system_box, box.ResolvedBox)
    assert len(system_box.instances) == 3

    conditional_a = system_box.instances[0]
    assert isinstance(conditional_a, box.Box)
    assert conditional_a.name == "conditional_box_a"
    assert len(conditional_a.instances) == 1
    nested_a = conditional_a.instances[0]
    assert isinstance(nested_a, box.Box)
    assert nested_a.name == "box_a"
    assert nested_a.template is not None
    assert nested_a.template.name == "NestedA"

    conditional_b = system_box.instances[1]
    assert isinstance(conditional_b, box.Box)
    assert conditional_b.name == "conditional_box_b"
    assert len(conditional_b.instances) == 1
    nested_b = conditional_b.instances[0]
    assert isinstance(nested_b, box.Box)
    assert nested_b.name == "box_b"
    assert nested_b.template is not None
    assert nested_b.template.name == "NestedB"

    defaulted = system_box.instances[2]
    assert isinstance(defaulted, box.Box)
    assert defaulted.name == "defaulted_box"
    assert len(defaulted.instances) == 0


def test_if_statement(fs_importer: FilesystemImporter) -> None:
    source_text = """
box NestedA
{
}

box NestedB
{
}

box ConditionalBox
{
  parameters
  {
    flag: Bool;
  }

  if flag then {
      new box_a: NestedA;
  } else {
    new box_b: NestedB;
  }
}

box SystemBox
{
    new conditional_box_a: ConditionalBox<true>;
    new conditional_box_b: ConditionalBox<false>;
}

system_target the_system
{
  box: SystemBox;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_if_box"), fs_importer)

    system = module.inner_scope.lookup("the_system")
    assert isinstance(system, system_target.UnresolvedSystemTarget)
    assert isinstance(system.resolved, system_target.SystemTarget)

    system_box = system.resolved.box_instance
    assert isinstance(system_box, box.ResolvedBox)
    assert len(system_box.instances) == 2

    conditional_a = system_box.instances[0]
    assert isinstance(conditional_a, box.Box)
    assert conditional_a.name == "conditional_box_a"
    assert len(conditional_a.instances) == 1
    nested_a = conditional_a.instances[0]
    assert isinstance(nested_a, box.Box)
    assert nested_a.name == "box_a"
    assert nested_a.template is not None
    assert nested_a.template.name == "NestedA"

    conditional_b = system_box.instances[1]
    assert isinstance(conditional_b, box.Box)
    assert conditional_b.name == "conditional_box_b"
    assert len(conditional_b.instances) == 1
    nested_b = conditional_b.instances[0]
    assert isinstance(nested_b, box.Box)
    assert nested_b.name == "box_b"
    assert nested_b.template is not None
    assert nested_b.template.name == "NestedB"


def test_invalid_conditional_statement(fs_importer: FilesystemImporter) -> None:
    source_text = """
box NestedA
{
}

box NestedB
{
}

box ConditionalBox
{
  parameters
  {
    flag: Bool;
  }

  if flag then "foo" else "bar"
}

box SystemBox
{
    new conditional_box_a: ConditionalBox<true>;
    new conditional_box_b: ConditionalBox<false>;
}

system_target the_system
{
  box: SystemBox;
}
"""
    with pytest.raises(TypeError, match=r", but expected a Block"):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_invalid_conditional_box"), fs_importer)
