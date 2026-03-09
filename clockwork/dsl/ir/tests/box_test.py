# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for box."""

from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
from clockwork.dsl.ir import box, clkbuiltins, cog, compiler, hardware, policy, pubsub, signal, signal_registry, udp
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from clockwork.dsl.ir.node import Module


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_box(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    assert box_ir.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box"

    assert len(box_ir.instances) == 10

    (
        mem_box,
        hello_cog,
        hello_config_file,
        _rw_hello_init,
        _ro_hello_init,
        ro_hello,
        _rw_hello,
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

    assert len(box_ir.connections) == 18
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
    multi_out_conn1 = box_ir.connections[11]
    assert multi_out_conn1.source is hello_cog.attribute("out_multi1")
    assert (
        multi_out_conn1.source.value_key()
        == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog.out_multi1"
    )
    assert multi_out_conn1.target is multi_chan
    multi_out_conn2 = box_ir.connections[12]
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

    assert len(box_ir.instances) == 10

    (
        mem_box,
        hello_cog,
        hello_config_file,
        _rw_hello_init,
        _ro_hello_init,
        ro_hello,
        _rw_hello,
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
    assert len(process_policies) == 50
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
    assert all(x == 10 for x in process_counts.values())
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
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}

policy ReportGroupPolicy for InstanceCog.group_level
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::telemetry;
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
    assert {"default", "group_level"} == {
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
    log_type = ReportGroupLogType::telemetry;
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
    log_type = ReportGroupLogType::telemetry;
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
