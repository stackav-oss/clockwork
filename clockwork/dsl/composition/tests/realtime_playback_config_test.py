# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for real-time playback policy registration."""

from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace
from typing import cast

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.composition import realtime_playback_config, system
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    compiler,
    hardware,
    importer,
    importer_registry,
    node,
    policy,
    primitive,
    pubsub,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.uuid_reg import lookup_uuid


def _get_test_context() -> CompilerContext:
    context = CompilerContext(name="realtime_playback_config_test")
    context[importer_registry.IMPORTER_REGISTRY_KEY].importer = importer.FilesystemImporter(
        compile_fn=compiler.compile_source_file
    )
    return context


def _compile_source(context: CompilerContext, source: str, module_name: str) -> node.Module:
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    return compiler.compile_source_text(source, ModuleID(CLK_REPO, module_name), source_importer)


def _lookup_domain(module: node.Module, name: str) -> hardware.CpuDomain:
    domain = module.inner_scope.lookup(name)
    assert isinstance(domain, hardware.CpuDomain)
    return domain


def _make_channel(*, producers: object, observers: object = ()) -> system.Channel:
    fake_channel: object = SimpleNamespace(producers=producers, observers=observers)
    return cast("system.Channel", fake_channel)


def _make_logical_system(
    module: node.Module,
    domains: tuple[hardware.CpuDomain, ...],
    channels: dict[str, system.Channel] | None = None,
) -> system.LogicalSystem:
    logical_system = system.LogicalSystem(module=module, require_logging_policies=False, use_simplelaunch=False)
    logical_system.cpu_domains = {lookup_uuid(module.context, domain): domain for domain in domains}
    if channels is not None:
        logical_system.channels = channels
    return logical_system


def _resolve_assignments(
    logical_system: system.LogicalSystem,
) -> tuple[realtime_playback_config.RealtimePlaybackAssignment, ...]:
    return realtime_playback_config.resolve_assignments(
        logical_system.module,
        logical_system.channels,
        logical_system.cpu_domains,
    )


def _compile_playback_source(
    context: CompilerContext, module_name: str, policies: str, *, second_node: str = "c2"
) -> node.Module:
    source = f"""
#![generate()]

use clockwork::dsl::tests::support::hellomsg;
use clockwork::logging::realtime_playback::realtime_playback_policy::{{RealtimePlaybackPolicy}};

// First playback channel.
channel FirstChan
{{
  name: "first";
  message_type: Tachyon<hellomsg::HelloMsg>;
  max_num_messages: 1;
}}

// Second playback channel.
channel SecondChan
{{
  name: "second";
  message_type: Tachyon<hellomsg::HelloMsg>;
  max_num_messages: 1;
}}

// First playback domain.
cpu_domain DomainOne
{{
  simplelaunch_node: "c1";
}}

// Second playback domain.
cpu_domain DomainTwo
{{
  simplelaunch_node: "{second_node}";
}}

{policies}
"""
    return _compile_source(context, source, module_name)


def test_realtime_playback_policy_requires_playback_domain() -> None:
    context = _get_test_context()
    policy_class = realtime_playback_config.get_realtime_playback_policy(context)
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  playback_cpu_domain = testpolicy::TestDomain;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    policy_module = compiler.compile_source_text(
        source,
        ModuleID(CLK_REPO, "realtime_playback_policy_defaults_test"),
        source_importer,
    )
    test_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), source_importer
    )
    channel = test_policy_module.inner_scope.lookup("TestChan")
    assert isinstance(channel, pubsub.Channel)
    policy_data = policy.lookup_policy(policy_module, policy_class, channel)

    assert isinstance(policy_data, policy.PolicyData)
    assert isinstance(policy_data.data.data["source_name"], clkbuiltins.Nullopt)
    assert policy_data.data.data["playback_cpu_domain"] is test_policy_module.inner_scope.lookup("TestDomain")
    assert isinstance(policy_data.data.data["stream_kind"], clkenum.ValueRef)
    assert policy_data.data.data["stream_kind"].name == "regular"


def test_realtime_playback_policy_values() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::logging::realtime_playback::realtime_playback_stream_kind::{RealtimePlaybackStreamKind};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  source_name = '/source';
  playback_cpu_domain = testpolicy::TestDomain;
  stream_kind = RealtimePlaybackStreamKind::camera;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "realtime_playback_policy_test"), source_importer)
    test_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), source_importer
    )
    channel = test_policy_module.inner_scope.lookup("TestChan")
    assert isinstance(channel, pubsub.Channel)

    policy_data = policy.lookup_policy(module, realtime_playback_config.get_realtime_playback_policy(context), channel)
    assert isinstance(policy_data, policy.PolicyData)
    assert isinstance(policy_data.data.data["source_name"], primitive.StringValue)
    assert policy_data.data.data["source_name"].value == "/source"
    assert isinstance(policy_data.data.data["stream_kind"], clkenum.ValueRef)
    assert policy_data.data.data["stream_kind"].name == "camera"
    assert policy_data.data.data["playback_cpu_domain"] is test_policy_module.inner_scope.lookup("TestDomain")


def test_realtime_playback_policy_rejects_invalid_field_type() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  source_name = 1;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    with pytest.raises(TypeError, match=r"Attempt to unify NumericType.INTEGER type with TypeDef\(name='String'\)"):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "realtime_playback_policy_invalid_test"), source_importer
        )


def test_realtime_playback_policy_rejects_missing_playback_domain() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    with pytest.raises(ValueError, match="playback_cpu_domain"):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "realtime_playback_policy_missing_domain_test"), source_importer
        )


def test_resolve_assignments_uses_explicit_simplelaunch_node() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::logging::realtime_playback::realtime_playback_stream_kind::{RealtimePlaybackStreamKind};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  source_name = '/recorded';
  playback_cpu_domain = testpolicy::TestDomain;
  stream_kind = RealtimePlaybackStreamKind::camera;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, "realtime_playback_assignment_test"), source_importer
    )
    test_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), source_importer
    )
    domain = test_policy_module.inner_scope.lookup("TestDomain")
    assert isinstance(domain, hardware.CpuDomain)
    domain.simplelaunch_node = "c1"
    logical_system = _make_logical_system(module, (domain,))

    (assignment,) = _resolve_assignments(logical_system)

    assert assignment.destination_channel_name == "policy_test"
    assert assignment.source_channel_name == "/recorded"
    assert assignment.stream_kind_name == "camera"
    assert assignment.simplelaunch_node_name == "c1"


def test_resolve_assignments_rejects_missing_simplelaunch_node() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  playback_cpu_domain = testpolicy::TestDomain;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "missing_simplelaunch_node_test"), source_importer)
    test_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), source_importer
    )
    domain = test_policy_module.inner_scope.lookup("TestDomain")
    assert isinstance(domain, hardware.CpuDomain)
    logical_system = _make_logical_system(module, (domain,))

    with pytest.raises(TypeError, match="explicit simplelaunch node"):
        _resolve_assignments(logical_system)


def test_resolve_assignments_rejects_live_publisher() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  playback_cpu_domain = testpolicy::TestDomain;
}
"""
    source_importer = context[importer_registry.IMPORTER_REGISTRY_KEY].importer
    assert source_importer is not None
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "live_publisher_test"), source_importer)
    test_policy_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/testpolicy.clk")), source_importer
    )
    domain = test_policy_module.inner_scope.lookup("TestDomain")
    assert isinstance(domain, hardware.CpuDomain)
    domain.simplelaunch_node = "c1"
    logical_system = _make_logical_system(
        module, (domain,), {"policy_test": _make_channel(producers={"publisher": object()})}
    )

    with pytest.raises(ValueError, match="live publisher"):
        _resolve_assignments(logical_system)


def test_realtime_playback_policy_rejects_duplicate_destination() -> None:
    context = _get_test_context()
    source = """
use clockwork::logging::realtime_playback::realtime_playback_policy::{RealtimePlaybackPolicy};
use clockwork::dsl::tests::support::testpolicy;

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  playback_cpu_domain = testpolicy::TestDomain;
}

policy RealtimePlaybackPolicy for testpolicy::TestChan
{
  playback_cpu_domain = testpolicy::TestDomain;
}
"""

    with pytest.raises(ValueError, match="already specified"):
        _compile_source(context, source, "realtime_playback_duplicate_destination_test")


def test_resolve_assignments_allows_same_domain_duplicate_source_and_ignores_consumers() -> None:
    context = _get_test_context()
    module = _compile_playback_source(
        context,
        "realtime_playback_same_domain_test",
        """
policy RealtimePlaybackPolicy for FirstChan
{
  source_name = '/recorded';
  playback_cpu_domain = DomainOne;
}

policy RealtimePlaybackPolicy for SecondChan
{
  source_name = '/recorded';
  playback_cpu_domain = DomainOne;
}
""",
    )
    domain = _lookup_domain(module, "DomainOne")
    logical_system = _make_logical_system(
        module,
        (domain,),
        {
            "first": _make_channel(producers={}, observers={"other_domain": object()}),
            "second": _make_channel(
                producers={}, observers={"other_domain_one": object(), "other_domain_two": object()}
            ),
        },
    )

    assignments = _resolve_assignments(logical_system)

    assert [(assignment.destination_channel_name, assignment.source_channel_name) for assignment in assignments] == [
        ("first", "/recorded"),
        ("second", "/recorded"),
    ]
    assert {assignment.simplelaunch_node_name for assignment in assignments} == {"c1"}


def test_resolve_assignments_rejects_domain_absent_from_system() -> None:
    context = _get_test_context()
    module = _compile_playback_source(
        context,
        "realtime_playback_missing_domain_test",
        """
policy RealtimePlaybackPolicy for FirstChan
{
  playback_cpu_domain = DomainOne;
}
""",
    )

    with pytest.raises(ValueError, match="not in the system"):
        _resolve_assignments(_make_logical_system(module, ()))


def test_resolve_assignments_allows_duplicate_simplelaunch_nodes() -> None:
    context = _get_test_context()
    module = _compile_playback_source(
        context,
        "realtime_playback_duplicate_node_test",
        """
policy RealtimePlaybackPolicy for FirstChan
{
  playback_cpu_domain = DomainOne;
}

policy RealtimePlaybackPolicy for SecondChan
{
  playback_cpu_domain = DomainTwo;
}
""",
        second_node="c1",
    )
    domains = (_lookup_domain(module, "DomainOne"), _lookup_domain(module, "DomainTwo"))

    assignments = _resolve_assignments(_make_logical_system(module, domains))

    assert {assignment.destination_channel_name: assignment.source_channel_name for assignment in assignments} == {
        "first": "first",
        "second": "second",
    }
    assert {assignment.simplelaunch_node_name for assignment in assignments} == {"c1"}
