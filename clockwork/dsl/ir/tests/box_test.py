# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for box."""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path

import pytest
from clockwork.dsl.ir import box, clkbuiltins, cog, compiler, hardware, policy, pubsub, udp
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


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
        rw_hello_init,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        ro_hello_init,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        ro_hello,
        rw_hello,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        extern_hello,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        in_udp,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        out_udp,  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    ) = box_ir.instances
    assert isinstance(mem_box, box.Box)
    (mem_hello,) = mem_box.instances
    assert isinstance(mem_hello, box.MemoryResourceInstance)
    assert mem_hello.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.mem_box.mem_hello"
    assert isinstance(hello_cog, cog.CogInstance)
    assert hello_cog.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellomod.box.hello_cog"
    assert hello_cog.cog_class.value_key() == f"@{CLK_REPO}::clockwork::dsl::tests::support::hellocog::HelloCog"

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

    assert len(box_ir.connections) == 17
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
    compiler._register_box_instance_uuids(module.context, box_ir)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
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
    source_text = """
use clockwork::dsl::tests::support::{hellocog, hellomsg};

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
        condition new_msg: new_message(min=1, max=2, input=hellos);
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
    # view size (6) + default margin (max(6, Chan1.max_num_message/2)) should be too large
    with pytest.raises(ValueError, match="Channel Chan1 is not large enough"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)

    source_text = """
use clockwork::dsl::tests::support::{hellocog, hellomsg};

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
        condition new_msg: new_message(min=1, max=2, input=hellos);
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
    # user-specified margin (11) should be flagged as unsatisifable
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "unsatisfiable_safety_margin"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    with pytest.raises(ValueError, match="Channel Chan1 is not large enough"):
        box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
