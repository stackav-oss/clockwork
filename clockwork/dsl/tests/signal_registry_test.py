# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for signal registry."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import box, clkbuiltins, cog, compiler, expr, node, primitive, signal
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.signal_registry import (
    get_all_signal_instances,
    get_all_signals,
    get_signal_instances,
    get_signal_instances_from_spec,
    get_signals_by_cog_class,
    get_unique_signal_instance_names,
    lookup_signal,
    lookup_signal_instance,
    register_signal,
    register_signal_instance,
)


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def test_signals_module(fs_importer: FilesystemImporter) -> node.Module:
    return compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/test_signals.clk")),
        importer=fs_importer,
    )


def _get_signal(module: node.Module, name: str) -> signal.Signal:
    """Helper to get a signal from a module."""
    sig = module.inner_scope.lookup(name)
    assert isinstance(sig, signal.Signal)
    return sig


def test_resolved_signal_names(test_signals_module: node.Module) -> None:
    """Test that resolved signals have correct signal names (fqn or custom name)."""
    simple_signal = _get_signal(test_signals_module, "simple_signal")
    resolved = simple_signal.get_resolved()
    assert resolved.signal_name == "@clockwork::clockwork::dsl::tests::support::test_signals.simple_signal"

    named_signal = _get_signal(test_signals_module, "named_signal")
    resolved_custom = named_signal.get_resolved()
    assert resolved_custom.signal_name == "custom_signal_name"


def test_register_signal_duplicate_name(fs_importer: FilesystemImporter) -> None:
    """Test that registering signals with duplicate custom names raises ValueError during compilation."""
    source = """
// Doc.
signal first_signal: UInt32 {
    name: "duplicate_name";
}
// Doc.
signal second_signal: UInt64 {
    name: "duplicate_name";
}
"""
    with pytest.raises(ValueError, match="Signal name 'duplicate_name' is already registered"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "duplicate_test"), importer=fs_importer)


def test_register_signal_instance_signal_not_registered(test_signals_module: node.Module) -> None:
    """Test that registering instance for unregistered signal raises RuntimeError."""
    # Create a fresh context without the signal registered
    context = CompilerContext()
    multi_signal = _get_signal(test_signals_module, "multi_signal")
    resolved_signal = multi_signal.get_resolved()

    # Trying to register an instance without first registering the signal should fail
    with pytest.raises(RuntimeError, match="must be registered before registering instances"):
        register_signal_instance(context, resolved_signal, "instance1")


def test_register_signal_instance_not_multi_instance(test_signals_module: node.Module) -> None:
    """Test that non-multi-instance signals can have one instance, but not multiple."""
    # Use the module's context where signals are already registered
    context = test_signals_module.context
    simple_signal = _get_signal(test_signals_module, "simple_signal")
    resolved_signal = simple_signal.get_resolved()

    # First instance should succeed
    register_signal_instance(context, resolved_signal, "instance1")

    # Verify the instance was registered
    instance = lookup_signal_instance(context, resolved_signal.signal_name, "instance1")
    assert instance is not None
    assert instance.instance_name == "instance1"

    # Second instance should fail
    with pytest.raises(ValueError, match="is not marked as multi_instance and already has an instance"):
        register_signal_instance(context, resolved_signal, "instance2")


def test_register_and_query_signal_instances(test_signals_module: node.Module) -> None:
    """Test registering and querying signal instances, including edge cases and multiple signals."""
    # Use the module's context where signals are already registered
    context = test_signals_module.context
    multi_signal = _get_signal(test_signals_module, "multi_signal")
    complex_signal = _get_signal(test_signals_module, "complex_signal")
    resolved_multi = multi_signal.get_resolved()
    resolved_complex = complex_signal.get_resolved()
    multi_signal_name = "@clockwork::clockwork::dsl::tests::support::test_signals.multi_signal"

    # Test with no instances (empty list)
    instances = get_signal_instances(context, multi_signal_name)
    assert instances == []

    # Test lookup on non-existent signal and instance (returns None)
    assert lookup_signal(context, "nonexistent_signal") is None
    assert lookup_signal_instance(context, multi_signal_name, "nonexistent_instance") is None
    assert get_signal_instances(context, "nonexistent_signal") == []

    # Test automatic signal registration and lookup
    simple_signal = _get_signal(test_signals_module, "simple_signal")
    simple_signal_name = "@clockwork::clockwork::dsl::tests::support::test_signals.simple_signal"
    result = lookup_signal(context, simple_signal_name)
    assert result is not None
    assert result.source is simple_signal

    # Test registering multiple instances for first signal
    register_signal_instance(context, resolved_multi, "instance1")
    register_signal_instance(context, resolved_multi, "instance2")
    register_signal_instance(context, resolved_multi, "instance3")

    # Test registering instances for second signal
    register_signal_instance(context, resolved_complex, "complex_instance1")

    # Test duplicate instance name raises ValueError
    with pytest.raises(ValueError, match="Instance name 'instance1' is already registered"):
        register_signal_instance(context, resolved_multi, "instance1")

    # Test get_signal_instances returns all instances for the correct signal
    instances1 = get_signal_instances(context, multi_signal_name)
    assert len(instances1) == 3
    instance_names = {inst.instance_name for inst in instances1}
    assert instance_names == {"instance1", "instance2", "instance3"}
    for inst in instances1:
        assert inst.signal_ir is resolved_multi

    # Test get_signal_instances returns only instances for the specified signal
    instances2 = get_signal_instances(context, "complex_name")  # complex_signal has custom name
    assert len(instances2) == 1
    assert instances2[0].instance_name == "complex_instance1"

    # Test lookup_signal_instance
    result = lookup_signal_instance(context, multi_signal_name, "instance1")
    assert result is not None
    assert result.signal_ir is resolved_multi
    assert result.instance_name == "instance1"


def test_signal_registry_import_from(test_signals_module: node.Module) -> None:
    """Test importing signal registry from another context, including re-importing same signal."""
    # Create two contexts and manually register signals to test import_from
    context1 = CompilerContext()
    context2 = CompilerContext()

    signal1 = _get_signal(test_signals_module, "multi_signal")
    signal2 = _get_signal(test_signals_module, "simple_signal")
    resolved1 = signal1.get_resolved()
    resolved2 = signal2.get_resolved()

    # Manually register in separate contexts
    register_signal(context1, resolved1)
    register_signal_instance(context1, resolved1, "instance1")
    register_signal(context2, resolved2)

    # Import context1 into context2
    context2.import_from(context1)

    # context2 should now have both signals
    multi_signal_name = "@clockwork::clockwork::dsl::tests::support::test_signals.multi_signal"
    simple_signal_name = "@clockwork::clockwork::dsl::tests::support::test_signals.simple_signal"
    result1 = lookup_signal(context2, multi_signal_name)
    result2 = lookup_signal(context2, simple_signal_name)
    assert result1 is not None
    assert result2 is not None
    assert result1 is resolved1
    assert result2 is resolved2
    assert lookup_signal_instance(context2, multi_signal_name, "instance1") is not None

    # Test that re-importing the same signal (by identity) works correctly
    context3 = CompilerContext()
    register_signal(context3, resolved1)
    register_signal_instance(context3, resolved1, "instance1")

    # Should work since it's the same signal IR node (by identity)
    context2.import_from(context3)


def test_signal_registry_import_from_conflicting_signal_name(fs_importer: FilesystemImporter) -> None:
    """Test that importing signals with the same signal_name but different IR raises RuntimeError."""
    # Create two different signals that will have the same custom signal_name
    source1 = """
// Doc.
signal first_signal: UInt32 {
    name: "duplicate_name";
}
"""
    source2 = """
// Doc.
signal second_signal: UInt64 {
    name: "duplicate_name";
}
"""
    module1 = compiler.compile_source_text(source1, ModuleID(CLK_REPO, "module1"), importer=fs_importer)
    module2 = compiler.compile_source_text(source2, ModuleID(CLK_REPO, "module2"), importer=fs_importer)

    # Create separate contexts and register each signal
    context1 = CompilerContext()
    context2 = CompilerContext()

    signal1 = _get_signal(module1, "first_signal")
    signal2 = _get_signal(module2, "second_signal")
    resolved1 = signal1.get_resolved()
    resolved2 = signal2.get_resolved()

    register_signal(context1, resolved1)
    register_signal(context2, resolved2)

    # Attempting to import should raise RuntimeError because the same signal_name
    # maps to different signal IR nodes
    with pytest.raises(RuntimeError, match="Signal name 'duplicate_name' has conflicting signal info"):
        context2.import_from(context1)


def test_get_signal_instances_from_spec(test_signals_module: node.Module) -> None:
    """Test getting signal instances from SignalInstanceSpec with specific instance, wildcard, and no match."""
    context = test_signals_module.context
    multi_signal = _get_signal(test_signals_module, "multi_signal")
    resolved_multi = multi_signal.get_resolved()

    # Register some instances
    register_signal_instance(context, resolved_multi, "instance1")
    register_signal_instance(context, resolved_multi, "instance2")
    register_signal_instance(context, resolved_multi, "instance3")

    # Test specific instance
    specific_spec = multi_signal.evaluate_subscript(
        index=primitive.StringValue.make("instance1"),
        cst_node=None,  # pyright: ignore[reportArgumentType]
        module=test_signals_module,
    )
    assert isinstance(specific_spec, signal.SignalInstanceSpec)
    assert not specific_spec.is_wildcard()
    specific_instances = get_signal_instances_from_spec(context, specific_spec)
    assert len(specific_instances) == 1
    assert specific_instances[0].instance_name == "instance1"
    assert specific_instances[0].signal_ir is resolved_multi

    # Test wildcard
    wildcard_spec = multi_signal.evaluate_subscript(
        index=expr.WILDCARD,
        cst_node=None,  # pyright: ignore[reportArgumentType]
        module=test_signals_module,
    )
    assert isinstance(wildcard_spec, signal.SignalInstanceSpec)
    assert wildcard_spec.is_wildcard()
    all_instances = get_signal_instances_from_spec(context, wildcard_spec)
    assert len(all_instances) == 3
    assert {inst.instance_name for inst in all_instances} == {"instance1", "instance2", "instance3"}

    # Test non-existent instance
    nonexistent_spec = multi_signal.evaluate_subscript(
        index=primitive.StringValue.make("nonexistent"),
        cst_node=None,  # pyright: ignore[reportArgumentType]
        module=test_signals_module,
    )
    assert isinstance(nonexistent_spec, signal.SignalInstanceSpec)
    assert get_signal_instances_from_spec(context, nonexistent_spec) == []


def test_signal_instances_with_cogs(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915 Test code.
    """Test SignalInstanceInfo cog tracking, filtering by CogInstance/Cog class, and query methods."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

signal multi_signal: UInt32 { multi_instance: true; }

// CogA
cog CogA
{
    signals { sig_a: multi_signal; }
    execution { condition periodic: time_since_last_exec(100ms); execute when: periodic; }
}

// CogB
cog CogB
{
    signals { sig_b: multi_signal; }
    execution { condition periodic: time_since_last_exec(100ms); execute when: periodic; }
}

policy ReportGroupPolicy for CogA.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

policy ReportGroupPolicy for CogB.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::non_redundant_telemetry;
    max_observations = 100;
}

box TestBox
{
    new cog_a1: CogA;
    new cog_a2: CogA;
    new cog_b1: CogB;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "cog_test"), importer=fs_importer)
    box_template = module.inner_scope.lookup("TestBox")
    assert isinstance(box_template, box.BoxTemplate)
    box_inst = box_template.make_instance(
        cst_node=None, module=module, scope=module.inner_scope, name="test_box", doc=None
    )

    cog_a1, cog_a2, cog_b1 = box_inst.instances
    assert isinstance(cog_a1, cog.CogInstance)
    assert isinstance(cog_a2, cog.CogInstance)
    assert isinstance(cog_b1, cog.CogInstance)
    cog_a_class = module.inner_scope.lookup("CogA")
    cog_b_class = module.inner_scope.lookup("CogB")
    assert isinstance(cog_a_class, cog.Cog)
    assert isinstance(cog_b_class, cog.Cog)
    multi_signal = _get_signal(module, "multi_signal")
    resolved_signal = multi_signal.get_resolved()

    # Verify auto-registered instances have correct cog associations
    all_instances = get_signal_instances(module.context, resolved_signal.signal_name)
    assert len(all_instances) == 3
    assert all(inst.cog_class in [cog_a_class, cog_b_class] for inst in all_instances)

    # Test manual instance has no cog association
    register_signal_instance(module.context, resolved_signal, "manual_inst")
    manual_info = lookup_signal_instance(module.context, resolved_signal.signal_name, "manual_inst")
    assert manual_info is not None
    assert manual_info.cog_instance is None
    assert manual_info.cog_class is None

    # Test filtering by specific CogInstance returns exactly one match
    spec_a1 = signal.SignalInstanceSpec(signal=multi_signal, instance_key=cog_a1, type_info=clkbuiltins.SIGNAL_TYPE)
    instances_a1 = get_signal_instances_from_spec(module.context, spec_a1)
    assert len(instances_a1) == 1
    assert instances_a1[0].cog_instance is cog_a1

    # Test filtering by Cog class returns all instances of that class (using SignalInstanceSpec)
    spec_a_class = signal.SignalInstanceSpec(
        signal=multi_signal, instance_key=cog_a_class, type_info=clkbuiltins.SIGNAL_TYPE
    )
    instances_a = get_signal_instances_from_spec(module.context, spec_a_class)
    assert len(instances_a) == 2
    cog_a_instances = [inst.cog_instance for inst in instances_a]
    assert cog_a1 in cog_a_instances
    assert cog_a2 in cog_a_instances

    # Test get_signals_by_cog_class helper function (includes cog metrics signal instances)
    instances_a_direct = get_signals_by_cog_class(module.context, cog_a_class)
    # 2 cog_a instances * 22 signal instances each:
    #   1 user RG (sig_a) + 10 event metrics group + 10 telemetry metrics group
    #   Each metrics group: 8 global + 1 group-specific + 1 per-condition (periodic)
    assert len(instances_a_direct) == 44
    multi_signal_a_instances = [inst for inst in instances_a_direct if inst.signal_ir is resolved_signal]
    assert len(multi_signal_a_instances) == 2
    for inst in instances_a_direct:
        assert inst.cog_class is cog_a_class

    spec_b_class = signal.SignalInstanceSpec(
        signal=multi_signal, instance_key=cog_b_class, type_info=clkbuiltins.SIGNAL_TYPE
    )
    instances_b = get_signal_instances_from_spec(module.context, spec_b_class)
    assert len(instances_b) == 1
    assert instances_b[0].cog_instance is cog_b1

    instances_b_direct = get_signals_by_cog_class(module.context, cog_b_class)
    # 1 cog_b instance * 21 signal instances (same breakdown as CogA above)
    assert len(instances_b_direct) == 22
    multi_signal_b_instances = [inst for inst in instances_b_direct if inst.signal_ir is resolved_signal]
    assert len(multi_signal_b_instances) == 1
    for inst in instances_b_direct:
        assert inst.cog_class is cog_b_class

    # Verify manual instance excluded from cog-filtered queries
    assert all(inst.instance_name != "manual_inst" for inst in instances_a + instances_b)

    # Test get_unique_signal_instance_names includes cog-private and cog metrics instances
    unique_names = get_unique_signal_instance_names(module.context)
    # 3 user RG instance names (one FQN per cog instance)
    # + 12 cog metrics instance names (2 per group * 2 groups * 3 cog instances:
    #     base name and per-condition/periodic name, for both event and telemetry groups)
    # + 1 manual_inst
    assert len(unique_names) == 16
    assert "manual_inst" in unique_names


def test_get_all_signals(test_signals_module: node.Module) -> None:
    """Test getting all signals registered in the system."""
    context = test_signals_module.context

    # Get all signals
    all_signals = get_all_signals(context)

    # Verify all signals are returned and sorted by signal name
    # 6 signals defined in test_signals.clk:
    #   simple_signal, named_signal (custom_signal_name), multi_signal,  # noqa: ERA001 false positive
    #   metadata_signal, aggregated_signal, complex_signal (complex_name)  # noqa: ERA001 false positive
    assert len(all_signals) == 6
    signal_names = [sig.signal_name for sig in all_signals]

    # Check that some expected signals are present
    assert any("simple_signal" in name for name in signal_names)
    assert any("multi_signal" in name for name in signal_names)
    assert "complex_name" in signal_names  # complex_signal has custom name


def test_get_all_signal_instances(test_signals_module: node.Module) -> None:
    """Test getting all signal instances registered in the system."""
    context = test_signals_module.context
    multi_signal = _get_signal(test_signals_module, "multi_signal")
    simple_signal = _get_signal(test_signals_module, "simple_signal")
    resolved_multi = multi_signal.get_resolved()
    resolved_simple = simple_signal.get_resolved()

    # Register instances
    register_signal_instance(context, resolved_multi, "multi_inst_1")
    register_signal_instance(context, resolved_multi, "multi_inst_2")
    register_signal_instance(context, resolved_simple, "simple_inst_1")

    # Get all instances
    all_instances = get_all_signal_instances(context)

    # Verify all instances are returned
    assert len(all_instances) == 3
    instance_keys = [(inst.signal_ir.signal_name, inst.instance_name) for inst in all_instances]

    # Check that our registered instances are present
    assert any("multi_signal" in key[0] and key[1] == "multi_inst_1" for key in instance_keys)
    assert any("multi_signal" in key[0] and key[1] == "multi_inst_2" for key in instance_keys)
    assert any("simple_signal" in key[0] and key[1] == "simple_inst_1" for key in instance_keys)


def test_get_unique_signal_instance_names(test_signals_module: node.Module) -> None:
    """Test getting unique signal instance names across all signals, including deduplication."""
    context = test_signals_module.context
    multi_signal = _get_signal(test_signals_module, "multi_signal")
    simple_signal = _get_signal(test_signals_module, "simple_signal")
    complex_signal = _get_signal(test_signals_module, "complex_signal")
    resolved_multi = multi_signal.get_resolved()
    resolved_simple = simple_signal.get_resolved()
    resolved_complex = complex_signal.get_resolved()

    # Register instances with some overlapping names
    register_signal_instance(context, resolved_multi, "instance_a")
    register_signal_instance(context, resolved_multi, "instance_b")
    register_signal_instance(context, resolved_simple, "instance_c")
    register_signal_instance(context, resolved_complex, "instance_a")  # Duplicate name across signals

    # Get unique instance names
    unique_names = get_unique_signal_instance_names(context)

    # Verify we only get 3 unique names (not 4) due to deduplication
    assert len(unique_names) == 3
    assert set(unique_names) == {"instance_a", "instance_b", "instance_c"}
