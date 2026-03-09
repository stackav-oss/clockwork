# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for signal subscript expressions and their use with policies."""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import box, clkbuiltins, cog, compiler, expr, policy, primitive, signal, signal_registry
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_signal_subscript_policy_operations(fs_importer: FilesystemImporter) -> None:
    """Test comprehensive policy operations with subscripted signals."""
    source = """
// Schema for policy
schema TestConfig
{
    fields
    {
        // Comment
        #0 enabled: Bool;
        // Comment
        #1 log_level: String;
    }
}

// Policy
def policy TestPolicy
{
    binds_to: Signal;
    schema: TestConfig;
}

signal sensor_data : Float32
{
    multi_instance: true;
}

// policy
policy TestPolicy for sensor_data["sensor1"]
{
    enabled = true;
    log_level = "DEBUG";
}

policy TestPolicy for sensor_data[*]
{
    enabled = false;
    log_level = "INFO";
}
"""
    module = compiler.compile_source_text(source, ModuleID(repo="", name="test_specific"), fs_importer)
    policy_def = module.inner_scope.lookup("TestPolicy")
    sensor_signal = module.inner_scope.lookup("sensor_data")
    assert isinstance(sensor_signal, signal.Signal)
    assert isinstance(policy_def, policy.PolicyDef)
    resolved_policy = policy_def.get_resolved()
    signal_spec = signal.SignalInstanceSpec(
        signal=sensor_signal, instance_key=primitive.StringValue.make("sensor1"), type_info=clkbuiltins.SIGNAL_TYPE
    )
    policy_resolved = policy.lookup_policy(module, resolved_policy, signal_spec)
    assert policy_resolved is not None

    wildcard_spec = signal.SignalInstanceSpec(
        signal=sensor_signal, instance_key=expr.WILDCARD, type_info=clkbuiltins.SIGNAL_TYPE
    )

    policy_resolved_wildcard = policy.lookup_policy(module, resolved_policy, wildcard_spec)
    assert policy_resolved_wildcard is not None

    assert isinstance(policy_resolved.target, signal.SignalInstanceSpec)
    assert isinstance(policy_resolved.target.instance_key, primitive.StringValue)
    assert policy_resolved.target.instance_key.value == "sensor1"
    assert isinstance(policy_resolved_wildcard.target, signal.SignalInstanceSpec)
    assert policy_resolved_wildcard.target.instance_key is expr.WILDCARD


# This is an end to end test, so it is necessarily quite long to support all the features being tested.
def test_policy_target_lookup(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915
    """Test looking up policies by their target signal instance specifications."""
    source = """
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

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

// Doc
cog AnotherCog
{
    signals
    {
        multi_signal;
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

policy ReportGroupPolicy for AnotherCog.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}

// Schema for policy
schema PolicySchema
{
    fields
    {
        // Comment
        #0 enabled: Bool;
    }
}

// Policy
def policy TestPolicy
{
    binds_to: Signal;
    schema: PolicySchema;
}

// Another policy
def policy AnotherTestPolicy
{
    binds_to: Signal;
    schema: PolicySchema;
}

// cog_signalPolicy
def policy cog_signalPolicy
{
    binds_to: Signal;
    schema: PolicySchema;
}

// CogInstanceSignalPolicy
def policy CogInstanceSignalPolicy
{
    binds_to: Signal;
    schema: PolicySchema;
}

// policy
policy TestPolicy for multi_signal["entry_inst"]
{
    enabled = true;
}

// another policy
policy AnotherTestPolicy for multi_signal[*]
{
    enabled = false;
}

// cog signal policy
policy cog_signalPolicy for multi_signal[InstanceCog]
{
    enabled = true;
}

// cog signal policy
policy cog_signalPolicy for multi_signal[AnotherCog]
{
    enabled = true;
}
box CogBox
{
    new cog1: InstanceCog;
    apply CogInstanceSignalPolicy(enabled=true) to multi_signal[cog1];

    new cog2: AnotherCog;
    apply CogInstanceSignalPolicy(enabled=true) to multi_signal[cog2];
}
"""

    module = compiler.compile_source_text(source, ModuleID(repo=CLK_REPO, name="test_specific"), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)

    # Test using a policy that applies to a specific instance
    test_policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(test_policy_def, policy.PolicyDef)
    test_policy_instances = policy.lookup_all_policies(module, test_policy_def.get_resolved())
    policy_instances_list = list(test_policy_instances)
    assert len(policy_instances_list) == 1
    policy_instance = policy_instances_list[0]

    assert isinstance(policy_instance.target, signal.SignalInstanceSpec)
    assert isinstance(policy_instance.target.instance_key, primitive.StringValue)
    assert policy_instance.target.instance_key.value == "entry_inst"
    instance = signal_registry.get_signal_instances_from_spec(module.context, policy_instance.target)
    assert len(instance) == 1
    assert instance[0].instance_name == "entry_inst"
    assert isinstance(box_ir.instances[0], cog.CogInstance)
    assert instance[0].cog_instance is box_ir.instances[0]
    assert instance[0].cog_class is box_ir.instances[0].cog_class

    # Test using a policy that applies to all instances (wildcard) of a specific signal.
    another_policy_def = module.inner_scope.lookup("AnotherTestPolicy")
    assert isinstance(another_policy_def, policy.PolicyDef)
    resolved_another_policy = another_policy_def.get_resolved()
    another_policy_instances = policy.lookup_all_policies(module, resolved_another_policy)
    another_policy_instance = next(iter(another_policy_instances))

    assert isinstance(another_policy_instance.target, signal.SignalInstanceSpec)
    assert another_policy_instance.target.instance_key is expr.WILDCARD
    instances = signal_registry.get_signal_instances_from_spec(module.context, another_policy_instance.target)
    assert len(instances) == 4
    another_cog_instance = box_ir.instances[1]
    assert isinstance(another_cog_instance, cog.CogInstance)
    assert {inst.instance_name for inst in instances} == {
        "one_instance",
        "another_instance",
        "entry_inst",
        another_cog_instance.fqn,
    }

    # Test using a policy that applies to all instances produced by a specific cog class.
    cog_policy_def = module.inner_scope.lookup("cog_signalPolicy")
    assert isinstance(cog_policy_def, policy.PolicyDef)
    resolved_cog_policy = cog_policy_def.get_resolved()
    cog_policy_instances = policy.lookup_all_policies(module, resolved_cog_policy)

    instance_cog_policy_instance = next(iter(cog_policy_instances))
    assert isinstance(instance_cog_policy_instance.target, signal.SignalInstanceSpec)
    instances = signal_registry.get_signal_instances_from_spec(module.context, instance_cog_policy_instance.target)
    assert len(instances) == 3

    another_cog_policy_instance = list(cog_policy_instances)[1]
    assert isinstance(another_cog_policy_instance.target, signal.SignalInstanceSpec)
    instances = signal_registry.get_signal_instances_from_spec(module.context, another_cog_policy_instance.target)
    assert len(instances) == 1

    # Test using a policy that applies to all instances produced by specific cog instances.
    cog_instance_policy_def = module.inner_scope.lookup("CogInstanceSignalPolicy")
    assert isinstance(cog_instance_policy_def, policy.PolicyDef)
    resolved_cog_instance_policy = cog_instance_policy_def.get_resolved()
    cog_instance_policy_instances = policy.lookup_all_policies(module, resolved_cog_instance_policy)
    assert len(list(cog_instance_policy_instances)) == 2

    cog1_policy_instance = next(iter(cog_instance_policy_instances))
    assert isinstance(cog1_policy_instance.target, signal.SignalInstanceSpec)
    instances = signal_registry.get_signal_instances_from_spec(module.context, cog1_policy_instance.target)
    assert len(instances) == 3

    cog2_policy_instance = list(cog_instance_policy_instances)[1]
    assert isinstance(cog2_policy_instance.target, signal.SignalInstanceSpec)
    instances = signal_registry.get_signal_instances_from_spec(module.context, cog2_policy_instance.target)
    assert len(instances) == 1
