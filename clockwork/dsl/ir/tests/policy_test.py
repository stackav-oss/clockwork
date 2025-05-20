# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for policy."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.composition import logger_config
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, policy, primitive, pubsub
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_policy(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), fs_importer
    )
    policy_def = module.inner_scope.lookup("TestPolicy", recursive=False)
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    test_chan = module.inner_scope.lookup("TestChan", recursive=False)
    assert isinstance(test_chan, pubsub.Channel)

    policy_inst = policy.lookup_policy(module, policy_class, test_chan)
    assert isinstance(policy_inst, policy.PolicyData)
    assert policy_inst.policy_class is policy_class
    assert policy_inst.target is test_chan

    policies = list(policy.lookup_all_policies(module, policy_class))
    assert len(policies) == 1
    assert policies[0] is policy_inst

    call_result = policy_class.evaluate_call(ir_node=None, module=module, args=list(policy_inst.data.data.items()))
    assert isinstance(call_result, policy.UnboundPolicyData)
    assert call_result.policy_class is policy_class
    assert call_result.data.data == policy_inst.data.data

    with pytest.raises(ValueError, match=re.escape("Only named arguments supported for policies")):
        policy_class.evaluate_call(ir_node=None, module=module, args=[(None, None)])  # type: ignore[list-item]

    with pytest.raises(
        AttributeError,
        match=re.escape("No such field wrong in schema TestPolicySchema, or field specified more than once"),
    ):
        policy_class.evaluate_call(ir_node=None, module=module, args=[("wrong", None)])  # type: ignore[list-item]

    with pytest.raises(ValueError, match=re.escape("Missing value for field do_stuff of schema TestPolicySchema")):
        policy_class.evaluate_call(ir_node=None, module=module, args=[])


def test_policy_bad_target(fs_importer: FilesystemImporter) -> None:
    source_text = """
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
    binds_to: 0;
    schema: Thing;
}
"""
    with pytest.raises(
        TypeError, match=re.escape("Attempt to unify NumericType.INTEGER type with TypeDef(name='Type')")
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_bad_schema(fs_importer: FilesystemImporter) -> None:
    source_text = """
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
    binds_to: Type;
    schema: Type;
}
"""
    with pytest.raises(TypeError, match=re.escape("Expected schema type, got TypeDef(name='Type')")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_bad_schema_instantiation(fs_importer: FilesystemImporter) -> None:
    source_text = """
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
    binds_to: Type;
    schema: FixedArray<Byte, 1>;
}
"""
    with pytest.raises(TypeError, match=re.escape("Only schema instantiations allowed here")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_inst_bad_policy(fs_importer: FilesystemImporter) -> None:
    source_text = """
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
    binds_to: Type;
    schema: Thing;
}

policy Thing for Thing
{
}
"""
    with pytest.raises(TypeError, match=re.escape("Expected a policy class, but got Schema")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_inst_bad_target(fs_importer: FilesystemImporter) -> None:
    source_text = """
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
    binds_to: Type;
    schema: Thing;
}

policy TestPolicy for false
{
}
"""
    with pytest.raises(
        TypeError, match=re.escape("Expected TypeDef(name='Type') but got PrimitiveBuiltinSerializable")
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_inst_missing_fields(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::testpolicy

policy testpolicy::TestPolicy for testpolicy::TestChan
{
}
"""
    with pytest.raises(ValueError, match=re.escape("Missing value for field do_stuff of schema TestPolicySchema")):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_inst_bad_value(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::testpolicy

policy testpolicy::TestPolicy for testpolicy::TestChan
{
    do_stuff = 0;
    stuff_type = 0;
}
"""
    with pytest.raises(
        TypeError,
        match=re.escape("Attempt to unify NumericType.INTEGER type with PrimitiveBuiltinSerializable(name='Bool',"),
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_policy_inst_duplicate(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::testpolicy

policy testpolicy::TestPolicy for testpolicy::TestChan
{
    do_stuff = true;
    stuff_type = Bool;
}
"""
    with pytest.raises(
        ValueError,
        match=re.escape(
            "Policy @clockwork::clockwork::dsl::tests::support::testpolicy::TestPolicy already specified for Channel(policy_test)"
        ),
    ):
        compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), importer=fs_importer)


def test_logging_policy(fs_importer: FilesystemImporter) -> None:
    system_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/test_logging_policy.clk")), fs_importer
    )
    hellomod = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    channel_policy = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/logging/channel_policy.clk")), fs_importer
    )

    hello_chan = hellomod.inner_scope.lookup("HelloChan")
    assert isinstance(hello_chan, pubsub.Channel)
    another_chan = hellomod.inner_scope.lookup("AnotherChan")
    assert isinstance(another_chan, pubsub.Channel)

    policy_def = channel_policy.inner_scope.lookup("ChannelLoggingPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()
    log_type = channel_policy.inner_scope.lookup("LogType")
    assert isinstance(log_type, clkenum.ClkEnum)

    hello_policy = policy.lookup_policy(system_module, policy_class, hello_chan)
    assert isinstance(hello_policy, policy.PolicyData)
    assert hello_policy.data.data["log_type"] is log_type.lookup("event")  # type: ignore[comparison-overlap]

    another_policy = policy.lookup_policy(system_module, policy_class, another_chan)
    assert isinstance(another_policy, policy.PolicyData)
    assert another_policy.data.data["log_type"] is log_type.lookup("telemetry")  # type: ignore[comparison-overlap]


def test_log_reader_policy(fs_importer: FilesystemImporter) -> None:
    system_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/test_logging_policy.clk")), fs_importer
    )
    hellomod = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )

    hello_chan = hellomod.inner_scope.lookup("HelloChan")
    assert isinstance(hello_chan, pubsub.Channel)
    another_chan = hellomod.inner_scope.lookup("AnotherChan")
    assert isinstance(another_chan, pubsub.Channel)

    reader_policy_class = logger_config.get_log_reader_policy()
    hello_reader_policy = policy.lookup_policy(system_module, reader_policy_class, hello_chan)
    assert isinstance(hello_reader_policy, policy.PolicyData)
    assert isinstance(hello_reader_policy.data.data["source_name"], clkbuiltins.Nullopt)

    another_reader_policy = policy.lookup_policy(system_module, reader_policy_class, another_chan)
    assert isinstance(another_reader_policy, policy.PolicyData)
    another_source_name = another_reader_policy.data.data["source_name"]
    assert isinstance(another_source_name, primitive.StringValue)
    assert another_source_name.value == "/another/chan"
