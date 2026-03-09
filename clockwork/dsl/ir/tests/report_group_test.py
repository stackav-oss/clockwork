# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test report group functionality."""

import re
from typing import Any

import pytest
from clockwork.dsl.ir import clkbuiltins, clkenum, cog, cog_components, compiler, policy, signal
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.report_group import ReportGroupLogType, ReportingStrategy


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for tests."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_report_groups_basic_functionality(fs_importer: FilesystemImporter) -> None:
    """Test report groups with signal definitions, references, and multiple groups."""
    source = """
use clockwork::dsl::tests::support::test_signals::{simple_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog ReportCog
{
    signals
    {
        // Default report group signal
        default_signal: signal UInt64
        {
            post_aggregation: ["min"];
        }
    }

    signals status_group
    {
        // Status signal
        status_signal: signal Int32
        {
            name: "custom_status_signal_name";
            multi_instance: true;
            metadata: SyncTime;
            pre_aggregation: ["min", "max", "mean"];
            post_aggregation: ["min"];
        }
        // Reference to imported signal with post-aggregation
        simple_signal
        {
            post_aggregation: ["sum", "count"];
        }
    }

    signals metrics_group
    {
        // Mixed definitions and references
        latency: signal Float64
        {
            post_aggregation: ["mean"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for ReportCog.default
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}

policy ReportGroupPolicy for ReportCog.status_group
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}

policy ReportGroupPolicy for ReportCog.metrics_group
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 100;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "report_groups_test"), importer=fs_importer)
    report_cog = module.inner_scope.lookup("ReportCog")
    assert isinstance(report_cog, cog.Cog)

    # Check that we have 3 report groups
    assert len(report_cog.report_groups) == 3

    # Check the default (unnamed) group
    default_group = report_cog.report_groups["default"]
    assert default_group.name == "default"
    assert len(default_group.entries) == 1
    assert "default_signal" in default_group.entries

    # Check the named status group (definitions + references)
    status_group = report_cog.report_groups["status_group"]
    assert status_group.name == "status_group"
    assert len(status_group.entries) == 2
    assert "status_signal" in status_group.entries
    assert "simple_signal" in status_group.entries

    # Verify signal options are parsed correctly
    status_signal_entry = status_group.entries["status_signal"]
    assert isinstance(status_signal_entry.signal, signal.Signal)
    status_signal_ir = status_signal_entry.signal
    # Check resolved signal for evaluated values
    resolved_signal = status_signal_ir.resolved
    assert resolved_signal is not None
    assert resolved_signal.signal_name == "custom_status_signal_name"
    assert status_signal_ir.multi_instance is True
    assert status_signal_ir.metadata is not None
    assert len(status_signal_ir.pre_aggregation) == 3
    assert signal.AggregationType.MIN in status_signal_ir.pre_aggregation
    assert signal.AggregationType.MAX in status_signal_ir.pre_aggregation
    assert signal.AggregationType.MEAN in status_signal_ir.pre_aggregation

    # Verify post_aggregation on signal reference
    simple_signal_entry = status_group.entries["simple_signal"]
    assert simple_signal_entry.post_aggregation is not None
    assert len(simple_signal_entry.post_aggregation) == 2
    assert signal.AggregationType.SUM in simple_signal_entry.post_aggregation
    assert signal.AggregationType.COUNT in simple_signal_entry.post_aggregation

    # Check the metrics group
    metrics_group = report_cog.report_groups["metrics_group"]
    assert metrics_group.name == "metrics_group"
    assert len(metrics_group.entries) == 1
    assert "latency" in metrics_group.entries


def test_report_groups_error_cases(fs_importer: FilesystemImporter) -> None:
    """Test report group error cases: invalid references and duplicates."""
    # Test invalid signal reference
    source = """
// Doc.
cog ReportCog
{
    signals
    {
        nonexistent_signal;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(ValueError, match=re.escape("Undefined identifier nonexistent_signal")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "report_groups_invalid_ref"), importer=fs_importer)

    # Test duplicate signal names
    source = """
// Doc.
cog ReportCog
{
    signals
    {
        my_signal: signal UInt64;
        my_signal: signal Int32;
    }
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(ValueError, match=re.escape("Duplicate signal entry")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "report_groups_duplicate"), importer=fs_importer)


def test_signal_identifiers_explicit_syntax(fs_importer: FilesystemImporter) -> None:
    """Test explicit signal identifier syntax where identifier differs from signal name."""
    source = """
use clockwork::dsl::tests::support::test_signals::{simple_signal, named_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog IdentifierTestCog
{
    signals
    {
        // Shorthand: identifier is same as signal name
        simple_signal;
        // Explicit: local identifier is different from signal name
        my_local_id: simple_signal;
        // Another explicit reference with custom named signal
        custom_id: named_signal;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for IdentifierTestCog.default
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 50;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "signal_identifier_test"), importer=fs_importer)
    test_cog = module.inner_scope.lookup("IdentifierTestCog")
    assert isinstance(test_cog, cog.Cog)

    # Check that we have 1 report group with 3 entries
    assert len(test_cog.report_groups) == 1
    default_group = test_cog.report_groups["default"]
    assert default_group.name == "default"
    assert len(default_group.entries) == 3

    # Verify the identifiers
    assert "simple_signal" in default_group.entries
    assert "my_local_id" in default_group.entries
    assert "custom_id" in default_group.entries

    # Verify that all entries reference signals
    simple_entry = default_group.entries["simple_signal"]
    assert isinstance(simple_entry.signal, signal.Signal)

    my_local_entry = default_group.entries["my_local_id"]
    assert isinstance(my_local_entry.signal, signal.Signal)

    custom_entry = default_group.entries["custom_id"]
    assert isinstance(custom_entry.signal, signal.Signal)


def test_post_aggregation_in_report_group_instance(fs_importer: FilesystemImporter) -> None:
    """Test that post_aggregation is properly transferred to report group instances."""
    source = """
use clockwork::dsl::tests::support::test_signals::{simple_signal};
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog PostAggTestCog
{
    signals
    {
        // Signal reference with post-aggregation
        signal_with_post_agg: simple_signal
        {
            post_aggregation: ["min", "max", "final_value"];
        }
        // Signal with different post-aggregation
        signal_with_value_post_agg: simple_signal
        {
            post_aggregation: ["value"];
        }
        // Cog-scope signal definition with post-aggregation
        cog_signal_with_post_agg: signal Int64
        {
            post_aggregation: ["sum", "count"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for PostAggTestCog.default
{
   reporting_strategy = ReportingStrategy::post_aggregated;
   log_type = ReportGroupLogType::telemetry;
   max_observations = 10;
}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "post_agg_instance_test"), importer=fs_importer)
    test_cog = module.inner_scope.lookup("PostAggTestCog")
    assert isinstance(test_cog, cog.Cog)
    test_cog.resolve()

    # Get the report group
    assert len(test_cog.report_groups) == 1
    report_group = test_cog.report_groups["default"]

    # Verify that the policy is bound to the report group
    policy_class_def = module.inner_scope.lookup("ReportGroupPolicy")
    assert isinstance(policy_class_def, policy.PolicyDef)
    policy_class = policy_class_def.get_resolved()

    bound_policy = policy.lookup_policy(module, policy_class, report_group)
    assert bound_policy is not None, "Policy should be bound to the report group"
    assert bound_policy.target is report_group
    assert bound_policy.policy_class is policy_class

    # Verify the policy data
    reporting_strategy = bound_policy.data.data["reporting_strategy"]
    assert isinstance(reporting_strategy, clkenum.ValueRef)
    assert reporting_strategy.value_def.name == "post_aggregated"

    log_type = bound_policy.data.data["log_type"]
    assert isinstance(log_type, clkenum.ValueRef)
    assert log_type.value_def.name == "telemetry"

    # Create an instance
    cog_instance_fqn = "test::PostAggTestCog"
    group_instance = report_group.make_instance(cog_instance_fqn)

    # Verify post_aggregation_text on the report group definitions (not instances)
    signal_with_post_agg_def = report_group.entries["signal_with_post_agg"]
    assert signal_with_post_agg_def.post_aggregation_text == '["min", "max", "final_value"]'
    signal_with_value_post_agg_def = report_group.entries["signal_with_value_post_agg"]
    assert signal_with_value_post_agg_def.post_aggregation_text == '["value"]'
    cog_signal_def = report_group.entries["cog_signal_with_post_agg"]
    assert isinstance(cog_signal_def.signal, signal.Signal)
    assert cog_signal_def.signal.post_aggregation_text == '["sum", "count"]'

    # Verify post_aggregation in signal instances
    signal_with_post_agg_entry = group_instance.entries["signal_with_post_agg"]
    assert signal_with_post_agg_entry.post_aggregation is not None
    assert len(signal_with_post_agg_entry.post_aggregation) == 3
    assert signal.AggregationType.MIN in signal_with_post_agg_entry.post_aggregation
    assert signal.AggregationType.MAX in signal_with_post_agg_entry.post_aggregation
    assert signal.AggregationType.FINAL_VALUE in signal_with_post_agg_entry.post_aggregation

    # Verify post_aggregation for the signal with value aggregation
    signal_with_value_entry = group_instance.entries["signal_with_value_post_agg"]
    assert signal_with_value_entry.post_aggregation is not None
    assert len(signal_with_value_entry.post_aggregation) == 1
    assert signal.AggregationType.VALUE in signal_with_value_entry.post_aggregation

    # Verify post_aggregation from cog-scope signal definition
    cog_signal_entry = group_instance.entries["cog_signal_with_post_agg"]
    assert cog_signal_entry.post_aggregation is not None
    assert len(cog_signal_entry.post_aggregation) == 2
    assert signal.AggregationType.SUM in cog_signal_entry.post_aggregation
    assert signal.AggregationType.COUNT in cog_signal_entry.post_aggregation


def test_post_aggregation_module_scope_error(fs_importer: FilesystemImporter) -> None:
    """Test that post_aggregation is not allowed on module-scope signals."""
    source = """
// Module-scope signal with post_aggregation (should fail)
signal module_signal: UInt64
{
    post_aggregation: ["min", "max"];
}

// Doc.
cog TestCog
{
    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}
"""
    with pytest.raises(TypeError, match=re.escape("post_aggregation is only allowed on cog-scope signals")):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "post_agg_module_scope_error"), importer=fs_importer)


@pytest.mark.parametrize(
    (
        "policy_config",
        "expected_strategy",
        "expected_values",
        "needs_post_agg",
        "expected_log_type",
        "expected_metrics_log_type",
    ),
    [
        # Test batched strategy with observation counts
        (
            "reporting_strategy = ReportingStrategy::batched;\n    max_observations = 50;\n    min_observations = 10;\n    log_type = ReportGroupLogType::telemetry;",
            ReportingStrategy.BATCHED,
            {"max_observations": 50, "min_observations": 10, "min_duration": None, "max_duration": None},
            False,
            ReportGroupLogType.TELEMETRY,
            cog_components.MetricsLogType.telemetry,
        ),
        # Test post_aggregated strategy
        (
            "reporting_strategy = ReportingStrategy::post_aggregated;\n    max_observations = 100;\n    log_type = ReportGroupLogType::event;",
            ReportingStrategy.POST_AGGREGATED,
            {"max_observations": 100, "min_observations": None, "min_duration": None, "max_duration": None},
            True,
            ReportGroupLogType.EVENT,
            cog_components.MetricsLogType.event,
        ),
        # Test duration-based constraints with post_aggregated (batched requires max_observations)
        (
            "reporting_strategy = ReportingStrategy::post_aggregated;\n    min_duration = 1s;\n    max_duration = 10s;\n    log_type = ReportGroupLogType::none;",
            ReportingStrategy.POST_AGGREGATED,
            {
                "max_observations": None,
                "min_observations": None,
                "min_duration": clkbuiltins.DURATION,
                "max_duration": clkbuiltins.DURATION,
            },
            True,
            ReportGroupLogType.NONE,
            cog_components.MetricsLogType.none,
        ),
    ],
    ids=["batched_observations", "post_aggregated", "duration_constraints"],
)
def test_report_group_config_variants(  # noqa: PLR0913
    fs_importer: FilesystemImporter,
    policy_config: str,
    expected_strategy: ReportingStrategy,
    expected_values: dict[str, Any],
    needs_post_agg: bool,
    expected_log_type: ReportGroupLogType,
    expected_metrics_log_type: cog_components.MetricsLogType,
) -> None:
    """Test various ReportGroupConfig configurations with different strategies and constraints."""
    # Verify enum values inline
    assert ReportingStrategy.BATCHED.value == "batched"
    assert ReportingStrategy.POST_AGGREGATED.value == "post_aggregated"
    assert ReportGroupLogType.TELEMETRY.value == "telemetry"
    assert ReportGroupLogType.EVENT.value == "event"
    assert ReportGroupLogType.NONE.value == "none"

    # Build signal definition based on whether post_aggregation is needed
    if needs_post_agg:
        signal_def = """test_signal: signal UInt64
        {
            post_aggregation: ["min", "max"];
        }"""
    else:
        signal_def = "test_signal: signal UInt64;"

    source = f"""
use std::signals::{{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType}};

// Doc.
cog TestCog
{{
    signals test_group
    {{
        {signal_def}
    }}

    execution
    {{
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }}
}}

policy ReportGroupPolicy for TestCog.test_group
{{
    {policy_config}
}}
"""
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "config_test"), importer=fs_importer)
    test_cog = module.inner_scope.lookup("TestCog")
    assert isinstance(test_cog, cog.Cog)
    report_group = test_cog.report_groups["test_group"]
    report_group.resolve(module.inner_scope, parent_cog_name="TestCog")

    assert report_group.report_group_config is not None
    config = report_group.report_group_config
    assert config.reporting_strategy == expected_strategy
    assert config.log_type == expected_log_type

    assert report_group.log_type == expected_metrics_log_type

    # Check all expected values
    for field, expected in expected_values.items():
        actual = getattr(config, field)
        if expected is clkbuiltins.DURATION:
            # Special handling for duration type validation
            assert actual is not None
            assert actual.type_info is clkbuiltins.DURATION
        else:
            assert actual == expected


def test_report_group_config_validation_requires_max_constraint(fs_importer: FilesystemImporter) -> None:
    """Test that ReportGroupConfig validation requires at least one of max_observations or max_duration."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog TestCog
{
    signals test_group
    {
        test_signal: signal UInt64;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for TestCog.test_group
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::event;
    min_observations = 10;
}
"""
    with pytest.raises(
        ValueError, match="Report group policy must specify at least one of max_observations or max_duration"
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "config_validation_test"), importer=fs_importer)


def test_report_group_config_batched_requires_max_observations(fs_importer: FilesystemImporter) -> None:
    """Test that batched report groups require max_observations even if max_duration is specified."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog TestCog
{
    signals test_group
    {
        test_signal: signal UInt64;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for TestCog.test_group
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::event;
    min_duration = 1s;
    max_duration = 10s;
}
"""
    with pytest.raises(
        ValueError, match="Batched report group policy must specify max_observations to define the batch size"
    ):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "batched_requires_max_observations_test"), importer=fs_importer
        )


def test_report_group_config_invalid_value_type(fs_importer: FilesystemImporter) -> None:
    """Test that ReportGroupConfig validation catches invalid value types in policy fields."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog TestCog
{
    signals test_group
    {
        test_signal: signal UInt64;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for TestCog.test_group
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::event;
    // Using a duration value instead of integer for max_observations
    max_observations = 100ms;
}
"""
    # This should fail during compilation with a type error (Duration != UInt64)
    with pytest.raises(TypeError, match=r"Type inference failed.*Duration.*UInt64"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "config_invalid_type_test"), importer=fs_importer)


def test_report_group_batched_rejects_post_aggregation(fs_importer: FilesystemImporter) -> None:
    """Test that batched report groups reject signals with post-aggregation."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog TestCog
{
    signals test_group
    {
        test_signal: signal UInt64
        {
            post_aggregation: ["min", "max"];
        }
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for TestCog.test_group
{
    reporting_strategy = ReportingStrategy::batched;
    log_type = ReportGroupLogType::event;
    max_observations = 50;
}
"""
    with pytest.raises(
        ValueError, match=r"Batched report groups cannot specify post-aggregation.*test_signal.*post_aggregation"
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "batched_rejects_post_agg_test"), importer=fs_importer)


def test_report_group_post_aggregated_requires_post_aggregation(fs_importer: FilesystemImporter) -> None:
    """Test that post-aggregated report groups require post-aggregation on all signals."""
    source = """
use std::signals::{ReportGroupPolicy, ReportingStrategy, ReportGroupLogType};

// Doc.
cog TestCog
{
    signals test_group
    {
        // Signal without post-aggregation in a post-aggregated group
        test_signal: signal UInt64;
    }

    execution
    {
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }
}

policy ReportGroupPolicy for TestCog.test_group
{
    reporting_strategy = ReportingStrategy::post_aggregated;
    log_type = ReportGroupLogType::telemetry;
    max_observations = 50;
}
"""
    with pytest.raises(
        ValueError,
        match=r"Post-aggregated report groups require post_aggregation on all signals.*test_signal.*no post_aggregation",
    ):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "post_aggregated_requires_post_agg_test"), importer=fs_importer
        )
