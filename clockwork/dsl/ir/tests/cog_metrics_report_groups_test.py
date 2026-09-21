# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test cog metrics report group generation."""

from __future__ import annotations

import re
from decimal import Decimal
from pathlib import Path
from types import SimpleNamespace
from typing import Final, cast
from unittest.mock import MagicMock

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, node, policy, primitive, schema, signal, typesys, units
from clockwork.dsl.ir import cog as cog_ir
from clockwork.dsl.ir import schema as schema_mod
from clockwork.dsl.ir.cog_metrics_report_groups import (
    _DEFAULT_EVENT_BATCH_SIZE,
    EVENT_METRICS_GROUP_NAME,
    TELEMETRY_METRICS_GROUP_NAME,
    build_event_config,
    build_event_metrics_report_group,
    build_telemetry_config,
    build_telemetry_metrics_report_group,
    generate_cog_metrics_report_groups,
    load_signals,
    resolve_policy_config,
)
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.report_group import ReportGroupConfig, ReportGroupEntry, ReportGroupLogType, ReportingStrategy

# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

_COG_MODULE_TEMPLATE: Final = """\
use std::cog_metrics_policy::{{CogEventMetricsPolicy, CogTelemetryMetricsPolicy}};

// A minimal test cog.
cog {name}
{{
    execution
    {{
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }}
}}
{policies}
"""


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def compiled_module(fs_importer: FilesystemImporter) -> node.Module:
    return compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/cog/cog_execution_metrics_signals.clk")),
        importer=fs_importer,
    )


@pytest.fixture()
def signals(compiled_module: node.Module) -> dict[str, signal.Signal]:
    return load_signals(compiled_module)


def _mock_cog(value_key: str = "test::TestCog") -> typesys.NamedValue:
    mock = MagicMock(spec=typesys.NamedValue)
    mock.value_key.return_value = value_key
    mock.name = value_key.rsplit("::", 1)[-1]
    return mock


def _uint64_value(value: int) -> primitive.DecimalValue:
    return primitive.DecimalValue(type_info=clkbuiltins.UINT64, value=Decimal(value))


def _duration_value(seconds: str) -> primitive.UnitValue:
    return primitive.UnitValue.make(Decimal(seconds), units.SECONDS)


def _policy_data(schema_data: dict[str, typesys.Value]) -> policy.PolicyData:
    return policy.PolicyData(
        policy_class=cast("policy.PolicyClass", MagicMock(spec=policy.PolicyClass)),
        data=cast("schema.SchemaInstance", SimpleNamespace(data=schema_data)),
        target=cast("typesys.Value", MagicMock(spec=typesys.Value)),
        source=None,
    )


def _compile_cog_with_policies(
    fs_importer: FilesystemImporter,
    cog_name: str,
    policies: str,
    *,
    module_name: str | None = None,
) -> node.Module:
    source = _COG_MODULE_TEMPLATE.format(name=cog_name, policies=policies)
    return compiler.compile_source_text(
        source,
        ModuleID(CLK_REPO, module_name or f"cog_metrics_policy_integration_{cog_name}"),
        importer=fs_importer,
    )


def test_resolve_policy_config_defaults_to_enabled_without_config() -> None:
    config_builder = MagicMock()

    enabled, config = resolve_policy_config(None, config_builder)

    assert enabled is True
    assert config is None
    config_builder.assert_not_called()


def test_resolve_policy_config_disabled_skips_config_builder() -> None:
    schema_data: dict[str, typesys.Value] = {"enabled": clkbuiltins.FALSE_VALUE}
    config_builder = MagicMock()

    enabled, config = resolve_policy_config(
        _policy_data(schema_data),
        config_builder,
    )

    assert enabled is False
    assert config is None
    config_builder.assert_not_called()


def test_resolve_policy_config_enabled_uses_config_builder() -> None:
    schema_data: dict[str, typesys.Value] = {"enabled": clkbuiltins.TRUE_VALUE}
    expected_config = ReportGroupConfig(
        reporting_strategy=ReportingStrategy.BATCHED,
        log_type=ReportGroupLogType.EVENT,
        min_observations=None,
        max_observations=7,
        min_duration=None,
        max_duration=None,
    )
    config_builder = MagicMock(return_value=expected_config)

    enabled, config = resolve_policy_config(_policy_data(schema_data), config_builder)

    assert enabled is True
    assert config is expected_config
    config_builder.assert_called_once_with(schema_data)


def test_build_event_config_uses_event_policy_values() -> None:
    min_duration = _duration_value("0.25")
    max_duration = _duration_value("2")
    schema_data: dict[str, typesys.Value] = {
        "batch_size": _uint64_value(17),
        "min_duration": min_duration,
        "max_duration": max_duration,
    }

    config = build_event_config(schema_data)

    assert config.reporting_strategy == ReportingStrategy.BATCHED
    assert config.log_type == ReportGroupLogType.EVENT
    assert config.min_observations is None
    assert config.max_observations == 17
    assert config.min_duration is min_duration
    assert config.max_duration is max_duration


def test_build_event_config_defaults_batch_size_and_max_duration() -> None:
    config = build_event_config({})

    assert config.max_observations == _DEFAULT_EVENT_BATCH_SIZE
    assert config.max_duration is not None
    assert config.max_duration.as_unit(units.SECONDS).value == Decimal(1)


def test_build_telemetry_config_uses_telemetry_policy_values() -> None:
    min_duration = _duration_value("0.5")
    max_duration = _duration_value("3")
    schema_data: dict[str, typesys.Value] = {
        "min_observations": _uint64_value(4),
        "max_observations": _uint64_value(40),
        "min_duration": min_duration,
        "max_duration": max_duration,
    }

    config = build_telemetry_config(schema_data)

    assert config.reporting_strategy == ReportingStrategy.POST_AGGREGATED
    assert config.log_type == ReportGroupLogType.NON_REDUNDANT_TELEMETRY
    assert config.min_observations == 4
    assert config.max_observations == 40
    assert config.min_duration is min_duration
    assert config.max_duration is max_duration


def test_build_telemetry_config_defaults_max_duration() -> None:
    config = build_telemetry_config({})

    assert config.min_observations is None
    assert config.max_observations is None
    assert config.max_duration is not None
    assert config.max_duration.as_unit(units.SECONDS).value == Decimal(1)


def test_generate_returns_both_groups_by_default(compiled_module: node.Module) -> None:
    """With no policy applied, both groups are generated using default configuration."""
    result = generate_cog_metrics_report_groups(
        is_init=False,
        module=compiled_module,
        parent_scope=compiled_module.inner_scope,
        cog=_mock_cog(),
        input_names=[],
        output_names=[],
        condition_names=[],
        memory_resource_names=[],
    )
    assert EVENT_METRICS_GROUP_NAME in result
    assert TELEMETRY_METRICS_GROUP_NAME in result
    assert len(result) == 2

    event_cfg = result[EVENT_METRICS_GROUP_NAME].report_group_config
    assert event_cfg is not None
    assert event_cfg.reporting_strategy == ReportingStrategy.BATCHED
    assert event_cfg.log_type == ReportGroupLogType.EVENT
    assert event_cfg.max_observations == _DEFAULT_EVENT_BATCH_SIZE
    assert event_cfg.max_duration is not None
    assert event_cfg.max_duration.value == Decimal(1)

    telemetry_cfg = result[TELEMETRY_METRICS_GROUP_NAME].report_group_config
    assert telemetry_cfg is not None
    assert telemetry_cfg.reporting_strategy == ReportingStrategy.POST_AGGREGATED
    assert telemetry_cfg.log_type == ReportGroupLogType.NON_REDUNDANT_TELEMETRY
    assert telemetry_cfg.max_duration is not None
    assert telemetry_cfg.max_duration.value == Decimal(1)


def test_generate_returns_empty_for_init_cog(compiled_module: node.Module) -> None:
    """Init cogs never receive cog metrics report groups."""
    result = generate_cog_metrics_report_groups(
        is_init=True,
        module=compiled_module,
        parent_scope=compiled_module.inner_scope,
        cog=_mock_cog(),
        input_names=[],
        output_names=[],
        condition_names=[],
        memory_resource_names=[],
    )
    assert result == {}


def test_event_report_group(compiled_module: node.Module, signals: dict[str, signal.Signal]) -> None:
    """Test event report group structure, defaults, entries, and no aggregation."""
    rg = build_event_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=["cam", "lidar"],
        output_names=["trajectory"],
        condition_names=["ready"],
        memory_resource_names=["memory"],
    )
    assert rg.name == "cog_event_metrics_group"
    assert rg.parent_cog_name == "MyCog"
    assert rg.type_info is clkbuiltins.REPORT_GROUP_TYPE
    assert rg.report_group_config is not None
    assert rg.report_group_config.reporting_strategy == ReportingStrategy.BATCHED
    assert rg.report_group_config.log_type == ReportGroupLogType.EVENT
    assert rg.report_group_config.max_observations == _DEFAULT_EVENT_BATCH_SIZE
    assert rg.report_group_config.max_duration
    assert rg.report_group_config.max_duration.value == Decimal(1)

    # 8 global + 2 event metrics only + 2*3 input + 2 output + 1 condition + 4 memory resource = 23
    assert len(rg.entries) == 23
    assert {
        "cog_exec_start_time",
        "cog_dial_start_time",
        "cog_exec_duration",
        "execute_cog_wall_duration",
        "execute_cog_thread_cpu_duration",
        "execute_cog_thread_user_duration",
        "execute_cog_thread_system_duration",
        "cog_ready_to_exec_latency",
        "cog_attempt_to_exec_latency",
        "cog_requeue_count",
        "cam_unseen_messages",
        "cam_staleness",
        "cam_dropped_messages",
        "lidar_unseen_messages",
        "trajectory_num_messages",
        "trajectory_first_sequence_number",
        "ready_active",
        "memory_peak_allocated",
        "memory_current_allocated",
        "memory_total_allocated",
        "memory_total_deallocated",
    } <= set(rg.entries.keys())
    for entry in rg.entries.values():
        assert entry.post_aggregation == set()

    # Verify instance_name is set for indexed entries and None for global/event-only signals
    for name in (
        "cog_exec_duration",
        "execute_cog_wall_duration",
        "execute_cog_thread_cpu_duration",
        "execute_cog_thread_user_duration",
        "execute_cog_thread_system_duration",
        "cog_ready_to_exec_latency",
        "cog_attempt_to_exec_latency",
        "cog_requeue_count",
        "cog_dial_start_time",
        "cog_exec_start_time",
    ):
        assert rg.entries[name].instance_name is None, f"{name} should have instance_name=None"
    for name in ("cam_unseen_messages", "cam_staleness", "cam_dropped_messages"):
        assert rg.entries[name].instance_name == "cam", f"{name} should have instance_name='cam'"
    for name in ("lidar_unseen_messages", "lidar_staleness", "lidar_dropped_messages"):
        assert rg.entries[name].instance_name == "lidar", f"{name} should have instance_name='lidar'"
    assert rg.entries["trajectory_num_messages"].instance_name == "trajectory"
    assert rg.entries["trajectory_first_sequence_number"].instance_name == "trajectory"
    assert rg.entries["ready_active"].instance_name == "ready"
    assert "ready_active_count" not in rg.entries
    assert rg.entries["memory_peak_allocated"].instance_name == "memory"
    assert rg.entries["memory_current_allocated"].instance_name == "memory"
    assert rg.entries["memory_total_allocated"].instance_name == "memory"
    assert rg.entries["memory_total_deallocated"].instance_name == "memory"


def test_event_report_group_config_override(compiled_module: node.Module, signals: dict[str, signal.Signal]) -> None:
    """Event report group uses custom config when provided."""
    custom_config = ReportGroupConfig(
        reporting_strategy=ReportingStrategy.BATCHED,
        log_type=ReportGroupLogType.EVENT,
        max_observations=25,
        max_duration=None,
        min_duration=None,
        min_observations=None,
    )
    rg = build_event_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=[],
        output_names=[],
        condition_names=[],
        memory_resource_names=[],
        config=custom_config,
    )
    assert rg.report_group_config
    assert rg.report_group_config is custom_config
    assert rg.report_group_config.max_observations == 25


def test_telemetry_report_group(compiled_module: node.Module, signals: dict[str, signal.Signal]) -> None:
    """Test telemetry report group structure, defaults, entries, and aggregation."""
    rg = build_telemetry_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=["a", "b"],
        output_names=["c"],
        condition_names=["d"],
        memory_resource_names=["e"],
    )
    assert rg.name == "cog_telemetry_metrics_group"
    assert rg.parent_cog_name == "MyCog"
    assert rg.report_group_config is not None
    assert rg.report_group_config.reporting_strategy == ReportingStrategy.POST_AGGREGATED
    assert rg.report_group_config.log_type == ReportGroupLogType.NON_REDUNDANT_TELEMETRY
    assert rg.report_group_config.max_observations is None
    assert rg.report_group_config.max_duration
    assert rg.report_group_config.max_duration.value == Decimal(1)

    assert len(rg.entries) == 18
    expected_agg = {signal.AggregationType.MIN, signal.AggregationType.MAX, signal.AggregationType.MEAN}
    condition_agg = {signal.AggregationType.VALUE}
    condition_entry_names = {f"{cond}_active_count" for cond in ["d"]} | {
        f"agg_{cond}_peak_allocated" for cond in ["e"]
    }
    for entry in rg.entries.values():
        if entry.name in condition_entry_names:
            assert entry.post_aggregation == condition_agg, f"Signal {entry.name} has incorrect post_aggregation"
        else:
            assert entry.post_aggregation == expected_agg, f"Signal {entry.name} has incorrect post_aggregation"

    # Verify telemetry uses execution_condition_active_count per condition
    assert "d_active_count" in rg.entries

    # Verify instance_name is set for indexed entries
    for name in ("agg_a_unseen_messages", "agg_a_staleness", "agg_a_dropped_messages"):
        assert rg.entries[name].instance_name == "a", f"{name} should have instance_name='a'"
    for name in ("agg_b_unseen_messages", "agg_b_staleness", "agg_b_dropped_messages"):
        assert rg.entries[name].instance_name == "b", f"{name} should have instance_name='b'"
    assert rg.entries["agg_c_num_messages"].instance_name == "c"
    assert rg.entries["d_active_count"].instance_name == "d"
    assert rg.entries["agg_e_peak_allocated"].instance_name == "e"

    # Verify instance_name is None for global and telemetry-only signals
    for name in (
        "agg_cog_exec_duration",
        "agg_execute_cog_wall_duration",
        "agg_execute_cog_thread_cpu_duration",
        "agg_execute_cog_thread_user_duration",
        "agg_execute_cog_thread_system_duration",
        "agg_cog_ready_to_exec_latency",
        "agg_cog_attempt_to_exec_latency",
        "agg_cog_requeue_count",
        "cog_exec_period",
    ):
        assert rg.entries[name].instance_name is None, f"{name} should have instance_name=None"


def test_telemetry_report_group_config_override(
    compiled_module: node.Module, signals: dict[str, signal.Signal]
) -> None:
    """Telemetry report group uses custom config when provided."""
    custom_config = ReportGroupConfig(
        reporting_strategy=ReportingStrategy.POST_AGGREGATED,
        log_type=ReportGroupLogType.NON_REDUNDANT_TELEMETRY,
        min_observations=5,
        max_observations=50,
        min_duration=primitive.UnitValue.make(Decimal("0.5"), units.SECONDS),
        max_duration=primitive.UnitValue.make(Decimal(2), units.SECONDS),
    )
    rg = build_telemetry_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=[],
        output_names=[],
        condition_names=[],
        memory_resource_names=[],
        config=custom_config,
    )
    assert rg.report_group_config
    assert rg.report_group_config is custom_config
    assert rg.report_group_config.min_observations == 5
    assert rg.report_group_config.max_observations == 50


def test_telemetry_report_group_entries_and_aggregation(
    compiled_module: node.Module, signals: dict[str, signal.Signal]
) -> None:
    rg = build_telemetry_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=["a", "b"],
        output_names=["c"],
        condition_names=["d"],
        memory_resource_names=["e"],
    )
    assert len(rg.entries) == 18
    expected_agg = {signal.AggregationType.MIN, signal.AggregationType.MAX, signal.AggregationType.MEAN}
    condition_agg = {signal.AggregationType.VALUE}
    condition_entry_names = {f"{cond}_active_count" for cond in ["d"]} | {
        f"agg_{cond}_peak_allocated" for cond in ["e"]
    }
    for entry in rg.entries.values():
        if entry.name in condition_entry_names:
            assert entry.post_aggregation == condition_agg
        else:
            assert entry.post_aggregation == expected_agg


def test_make_instance_prepends_cog_fqn(compiled_module: node.Module, signals: dict[str, signal.Signal]) -> None:
    """make_instance qualifies instance names with report group name and cog FQN."""
    rg = build_event_metrics_report_group(
        cog_name="MyCog",
        module=compiled_module,
        scope=compiled_module.inner_scope,
        signals=signals,
        input_names=["cam"],
        output_names=["trajectory"],
        condition_names=["ready"],
        memory_resource_names=["cog_memory"],
    )
    cog_fqn = "my_system::MyCog"
    instance = rg.make_instance(cog_fqn)

    group_name = EVENT_METRICS_GROUP_NAME

    # Cog-level signals: group_name/cog_instance_fqn
    assert instance.entries["cog_exec_duration"].instance_name == f"{group_name}/{cog_fqn}"
    assert instance.entries["cog_exec_start_time"].instance_name == f"{group_name}/{cog_fqn}"

    # Per-input: group_name/cog_instance_fqn/endpoint_name
    assert instance.entries["cam_unseen_messages"].instance_name == f"{group_name}/{cog_fqn}/cam"
    assert instance.entries["cam_staleness"].instance_name == f"{group_name}/{cog_fqn}/cam"

    # Per-output: group_name/cog_instance_fqn/endpoint_name
    assert instance.entries["trajectory_num_messages"].instance_name == f"{group_name}/{cog_fqn}/trajectory"
    assert instance.entries["trajectory_first_sequence_number"].instance_name == f"{group_name}/{cog_fqn}/trajectory"

    # Per-condition: group_name/cog_instance_fqn/condition_name
    assert instance.entries["ready_active"].instance_name == f"{group_name}/{cog_fqn}/ready"

    # Per-memory resource: group_name/cog_instance_fqn/resource_name
    assert instance.entries["cog_memory_peak_allocated"].instance_name == f"{group_name}/{cog_fqn}/cog_memory"
    assert instance.entries["cog_memory_current_allocated"].instance_name == f"{group_name}/{cog_fqn}/cog_memory"
    assert instance.entries["cog_memory_total_allocated"].instance_name == f"{group_name}/{cog_fqn}/cog_memory"
    assert instance.entries["cog_memory_total_deallocated"].instance_name == f"{group_name}/{cog_fqn}/cog_memory"


# ---------------------------------------------------------------------------
# Integration tests — compile cog with policies
# ---------------------------------------------------------------------------


class TestEventPolicyIntegration:
    """Test CogEventMetricsPolicy integration with cog compilation."""

    def test_event_policy_enabled_with_default_config(self, fs_importer: FilesystemImporter) -> None:
        """Enabled event policy generates an event metrics report group with default config.

        The telemetry group is also generated with its own defaults because it is
        enabled by default when no telemetry policy is applied.
        """
        module = _compile_cog_with_policies(
            fs_importer,
            "EventPolicyCog",
            "policy CogEventMetricsPolicy for EventPolicyCog { enabled = true; }",
        )
        cog = module.inner_scope.lookup("EventPolicyCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_telemetry_metrics_group" in cog.cog_metrics_report_groups
        cfg = cog.cog_metrics_report_groups["cog_event_metrics_group"].report_group_config
        assert cfg is not None
        assert cfg.reporting_strategy == ReportingStrategy.BATCHED
        assert cfg.log_type == ReportGroupLogType.EVENT
        assert cfg.max_observations == _DEFAULT_EVENT_BATCH_SIZE
        assert cfg.max_duration is not None
        assert cfg.max_duration.value == Decimal(1)

    def test_event_policy_disabled(self, fs_importer: FilesystemImporter) -> None:
        """Disabled event policy suppresses the event group; telemetry still defaults to enabled."""
        module = _compile_cog_with_policies(
            fs_importer,
            "DisabledEventCog",
            "policy CogEventMetricsPolicy for DisabledEventCog { enabled = false; }",
        )
        cog = module.inner_scope.lookup("DisabledEventCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_event_metrics_group" not in cog.cog_metrics_report_groups
        assert "cog_telemetry_metrics_group" in cog.cog_metrics_report_groups

    def test_event_policy_custom_config(self, fs_importer: FilesystemImporter) -> None:
        """Custom batch_size and duration settings are reflected in the event report group config."""
        module = _compile_cog_with_policies(
            fs_importer,
            "CustomEventCog",
            "policy CogEventMetricsPolicy for CustomEventCog { enabled = true; batch_size = 20; min_duration = 50ms; max_duration = 2s; }",
        )
        cog = module.inner_scope.lookup("CustomEventCog")
        assert isinstance(cog, cog_ir.Cog)
        cfg = cog.cog_metrics_report_groups["cog_event_metrics_group"].report_group_config
        assert cfg is not None
        assert cfg.max_observations == 20
        assert cfg.min_duration is not None
        assert cfg.min_duration.as_unit(units.SECONDS).value == Decimal("0.05")
        assert cfg.max_duration is not None
        assert cfg.max_duration.value == Decimal(2)


class TestTelemetryPolicyIntegration:
    """Test CogTelemetryMetricsPolicy integration with cog compilation."""

    def test_telemetry_policy_enabled_with_default_config(self, fs_importer: FilesystemImporter) -> None:
        """Enabled telemetry policy generates a telemetry metrics report group with default config.

        The event group is also generated with its own defaults because it is
        enabled by default when no event policy is applied.
        """
        module = _compile_cog_with_policies(
            fs_importer,
            "TelemetryPolicyCog",
            "policy CogTelemetryMetricsPolicy for TelemetryPolicyCog { enabled = true; }",
        )
        cog = module.inner_scope.lookup("TelemetryPolicyCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_event_metrics_group" in cog.cog_metrics_report_groups
        cfg = cog.cog_metrics_report_groups["cog_telemetry_metrics_group"].report_group_config
        assert cfg is not None
        assert cfg.reporting_strategy == ReportingStrategy.POST_AGGREGATED
        assert cfg.log_type == ReportGroupLogType.NON_REDUNDANT_TELEMETRY
        assert cfg.max_duration is not None
        assert cfg.max_duration.value == Decimal(1)
        assert cfg.min_observations is None
        assert cfg.max_observations is None

    def test_telemetry_policy_disabled(self, fs_importer: FilesystemImporter) -> None:
        """Disabled telemetry policy suppresses the telemetry group; event still defaults to enabled."""
        module = _compile_cog_with_policies(
            fs_importer,
            "DisabledTelemetryCog",
            "policy CogTelemetryMetricsPolicy for DisabledTelemetryCog { enabled = false; }",
        )
        cog = module.inner_scope.lookup("DisabledTelemetryCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_telemetry_metrics_group" not in cog.cog_metrics_report_groups
        assert "cog_event_metrics_group" in cog.cog_metrics_report_groups

    def test_telemetry_policy_custom_config(self, fs_importer: FilesystemImporter) -> None:
        """Custom duration and observation settings are reflected in the telemetry report group config."""
        module = _compile_cog_with_policies(
            fs_importer,
            "CustomTelemetryCog",
            "policy CogTelemetryMetricsPolicy for CustomTelemetryCog { enabled = true; max_duration = 500ms; min_observations = 5; max_observations = 50; }",
        )
        cog = module.inner_scope.lookup("CustomTelemetryCog")
        assert isinstance(cog, cog_ir.Cog)
        cfg = cog.cog_metrics_report_groups["cog_telemetry_metrics_group"].report_group_config
        assert cfg is not None
        assert cfg.max_duration is not None
        assert cfg.max_duration.as_unit(units.SECONDS).value == Decimal("0.5")
        assert cfg.min_observations == 5
        assert cfg.max_observations == 50


class TestBothPoliciesIntegration:
    """Test simultaneous application of both event and telemetry metrics policies."""

    def test_both_policies_generate_both_report_groups(self, fs_importer: FilesystemImporter) -> None:
        """Both policies enabled together generate both report groups."""
        module = _compile_cog_with_policies(
            fs_importer,
            "BothPoliciesCog",
            """policy CogEventMetricsPolicy for BothPoliciesCog { enabled = true; }
policy CogTelemetryMetricsPolicy for BothPoliciesCog { enabled = true; }""",
        )
        cog = module.inner_scope.lookup("BothPoliciesCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_event_metrics_group" in cog.cog_metrics_report_groups
        assert "cog_telemetry_metrics_group" in cog.cog_metrics_report_groups


class TestPolicyUniquenessIntegration:
    """Test that duplicate policies are rejected during compilation."""

    @pytest.mark.parametrize(
        ("policy_name", "cog_name", "module_name"),
        [
            ("CogEventMetricsPolicy", "DupEventCog", "dup_event_policy_test"),
            ("CogTelemetryMetricsPolicy", "DupTelemetryCog", "dup_telemetry_policy_test"),
        ],
    )
    def test_duplicate_policy_raises(
        self,
        fs_importer: FilesystemImporter,
        policy_name: str,
        cog_name: str,
        module_name: str,
    ) -> None:
        """Duplicate metrics policies for the same cog raise a ValueError."""
        source = _COG_MODULE_TEMPLATE.format(
            name=cog_name,
            policies=(
                f"policy {policy_name} for {cog_name} {{ enabled = true; }}\n"
                f"policy {policy_name} for {cog_name} {{ enabled = true; }}"
            ),
        )
        with pytest.raises(ValueError, match=re.escape(policy_name)):
            compiler.compile_source_text(
                source,
                ModuleID(CLK_REPO, module_name),
                importer=fs_importer,
            )


class TestInitCogIntegration:
    """Test that init-only cogs do not get metrics report groups."""

    def test_init_cog_gets_no_report_groups(self, fs_importer: FilesystemImporter) -> None:
        """Metrics policies on init-only cogs do not generate report groups."""
        source = """\
use std::cog_metrics_policy::{CogEventMetricsPolicy};

// An init cog.
cog InitOnlyCog
{
    execution
    {
        execute when: init;
    }
}

policy CogEventMetricsPolicy for InitOnlyCog { enabled = true; }
"""
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "init_cog_policy_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("InitOnlyCog")
        assert isinstance(cog, cog_ir.Cog)
        assert "cog_event_metrics_group" not in cog.cog_metrics_report_groups
        assert "cog_telemetry_metrics_group" not in cog.cog_metrics_report_groups


# ---------------------------------------------------------------------------
# Sequence numbers metadata tests
# ---------------------------------------------------------------------------

_COG_WITH_INPUTS_TEMPLATE: Final = """\
use std::cog_metrics_policy::{{CogEventMetricsPolicy, CogTelemetryMetricsPolicy}};
use clockwork::dsl::tests::support::hellomsg::{{HelloMsg}};

// A test cog with inputs
cog {name}
{{
    inputs
    {{
        sensor: Tappy<HelloMsg>{view_params}
    }}

    execution
    {{
        condition new_data: new_message(sensor);
        execute when: new_data;
    }}
}}
{policies}
"""

_COG_WITH_OUTPUT_TEMPLATE: Final = """\
use std::cog_metrics_policy::{{CogEventMetricsPolicy, CogTelemetryMetricsPolicy}};
use clockwork::dsl::tests::support::hellomsg::{{HelloMsg}};

// A test cog with outputs
cog {name}
{{
    outputs
    {{
        result: Tappy<HelloMsg>{output_params}
    }}

    execution
    {{
        condition periodic: time_since_last_exec(100ms);
        execute when: periodic;
    }}
}}
{policies}
"""


def get_view_size_from_metadata(entry: ReportGroupEntry) -> int | None:
    """Helper to extract max_size from sequence-numbers metadata."""
    if entry.metadata_override is None:
        return None
    assert isinstance(entry.metadata_override, schema_mod.InstantiatedSchema)
    field_type = entry.metadata_override.fields[1].type_info
    assert isinstance(field_type, typesys.Instantiation)
    max_size = field_type.arguments.get("max_size")
    assert isinstance(max_size, primitive.DecimalValue)
    return int(max_size.value)


class TestSequenceNumbersMetadata:
    """Test InputViewSequenceNumbers metadata on input_unseen_messages entries."""

    def test_event_group_with_input_view_sizes_has_metadata(
        self, compiled_module: node.Module, signals: dict[str, signal.Signal]
    ) -> None:
        """When input_view_sizes is provided, input_unseen_messages entries have metadata."""
        rg = build_event_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=["cam", "lidar"],
            output_names=[],
            condition_names=[],
            memory_resource_names=[],
            input_view_sizes={"cam": 3, "lidar": 5},
        )
        # Unseen messages entries should have metadata override
        cam_entry = rg.entries["cam_unseen_messages"]
        assert get_view_size_from_metadata(cam_entry) == 3

        lidar_entry = rg.entries["lidar_unseen_messages"]
        assert get_view_size_from_metadata(lidar_entry) == 5

        # Other per-input entries should NOT have metadata
        assert rg.entries["cam_staleness"].metadata_override is None
        assert rg.entries["cam_dropped_messages"].metadata_override is None

    def test_event_group_with_outputs_has_sequence_number_signal(
        self, compiled_module: node.Module, signals: dict[str, signal.Signal]
    ) -> None:
        """Event groups include output first sequence number entries."""
        rg = build_event_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=[],
            output_names=["result", "debug"],
            condition_names=[],
            memory_resource_names=[],
        )
        assert rg.entries["result_num_messages"].metadata_override is None
        assert rg.entries["debug_num_messages"].metadata_override is None
        assert rg.entries["result_first_sequence_number"].metadata_override is None
        assert rg.entries["debug_first_sequence_number"].metadata_override is None

    def test_event_group_without_input_view_sizes_has_no_metadata(
        self, compiled_module: node.Module, signals: dict[str, signal.Signal]
    ) -> None:
        """When input_view_sizes is None, no metadata override is set."""
        rg = build_event_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=["cam"],
            output_names=[],
            condition_names=[],
            memory_resource_names=[],
            input_view_sizes=None,
        )
        assert rg.entries["cam_unseen_messages"].metadata_override is None

    def test_event_group_defaults_to_view_size_1(
        self, compiled_module: node.Module, signals: dict[str, signal.Signal]
    ) -> None:
        """Inputs not in input_view_sizes map default to max_sequence_numbers=1."""
        rg = build_event_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=["cam", "lidar"],
            output_names=[],
            condition_names=[],
            memory_resource_names=[],
            input_view_sizes={"cam": 10},
        )
        cam_entry = rg.entries["cam_unseen_messages"]
        assert get_view_size_from_metadata(cam_entry) == 10

        # lidar not in map -> defaults to 1
        lidar_entry = rg.entries["lidar_unseen_messages"]
        assert get_view_size_from_metadata(lidar_entry) == 1

    def test_telemetry_group_has_no_seqno_metadata(
        self, compiled_module: node.Module, signals: dict[str, signal.Signal]
    ) -> None:
        """Telemetry group never has sequence numbers metadata."""
        rg = build_telemetry_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=["cam"],
            output_names=[],
            condition_names=[],
            memory_resource_names=[],
        )
        assert rg.entries["agg_cam_unseen_messages"].metadata_override is None

        rg = build_telemetry_metrics_report_group(
            cog_name="MyCog",
            module=compiled_module,
            scope=compiled_module.inner_scope,
            signals=signals,
            input_names=[],
            output_names=["result"],
            condition_names=[],
            memory_resource_names=[],
        )
        assert rg.entries["agg_result_num_messages"].metadata_override is None
        assert "agg_result_first_sequence_number" not in rg.entries


class TestEmitSequenceNumbersPolicyIntegration:
    """Test emit_sequence_numbers policy parameter integration."""

    def test_emit_sequence_numbers_enabled_by_default(self, fs_importer: FilesystemImporter) -> None:
        """With default policy, sequence numbers metadata is present on input_unseen_messages."""
        source = _COG_WITH_INPUTS_TEMPLATE.format(
            name="SeqNoCog",
            view_params="\n        {\n            max_msgs: 5;\n        }",
            policies="policy CogEventMetricsPolicy for SeqNoCog { enabled = true; }",
        )
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "emit_seqno_default_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("SeqNoCog")
        assert isinstance(cog, cog_ir.Cog)
        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        entry = rg.entries["sensor_unseen_messages"]
        assert get_view_size_from_metadata(entry) == 5

    def test_emit_sequence_numbers_explicitly_true(self, fs_importer: FilesystemImporter) -> None:
        """Explicitly setting emit_sequence_numbers = true includes metadata."""
        source = _COG_WITH_INPUTS_TEMPLATE.format(
            name="SeqNoTrueCog",
            view_params="\n        {\n            max_msgs: 3;\n        }",
            policies="policy CogEventMetricsPolicy for SeqNoTrueCog { enabled = true; emit_sequence_numbers = true; }",
        )
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "emit_seqno_true_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("SeqNoTrueCog")
        assert isinstance(cog, cog_ir.Cog)
        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        entry = rg.entries["sensor_unseen_messages"]
        assert get_view_size_from_metadata(entry) == 3

    def test_emit_sequence_numbers_false_suppresses_metadata(self, fs_importer: FilesystemImporter) -> None:
        """Setting emit_sequence_numbers = false suppresses sequence numbers metadata."""
        source = _COG_WITH_INPUTS_TEMPLATE.format(
            name="NoSeqNoCog",
            view_params="\n        {\n            max_msgs: 5;\n        }",
            policies="policy CogEventMetricsPolicy for NoSeqNoCog { enabled = true; emit_sequence_numbers = false; }",
        )
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "emit_seqno_false_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("NoSeqNoCog")
        assert isinstance(cog, cog_ir.Cog)
        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        entry = rg.entries["sensor_unseen_messages"]
        assert entry.metadata_override is None

    def test_default_view_size_used_when_no_explicit_max_msgs(self, fs_importer: FilesystemImporter) -> None:
        """Default input view size (1) is used when max_msgs is not explicitly set."""
        source = _COG_WITH_INPUTS_TEMPLATE.format(
            name="DefaultViewCog",
            view_params=";",
            policies=(
                "policy CogEventMetricsPolicy for DefaultViewCog { enabled = true; emit_sequence_numbers = true; }"
            ),
        )
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "default_view_size_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("DefaultViewCog")
        assert isinstance(cog, cog_ir.Cog)
        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        entry = rg.entries["sensor_unseen_messages"]
        assert get_view_size_from_metadata(entry) == 1

    def test_output_sequence_numbers_add_first_sequence_number_signal(self, fs_importer: FilesystemImporter) -> None:
        """Output sequence numbers add a first sequence number signal."""
        source = _COG_WITH_OUTPUT_TEMPLATE.format(
            name="OutputSeqNoCog",
            output_params="\n        {\n            max_msgs_per_exec: 4;\n        }",
            policies=(
                "policy CogEventMetricsPolicy for OutputSeqNoCog { enabled = true; emit_sequence_numbers = true; }"
            ),
        )
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "output_seqno_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("OutputSeqNoCog")
        assert isinstance(cog, cog_ir.Cog)
        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        assert rg.entries["result_num_messages"].metadata_override is None
        assert rg.entries["result_first_sequence_number"].metadata_override is None

    def test_aligned_input_sequence_numbers_use_resolved_upstream_view_size(
        self, fs_importer: FilesystemImporter
    ) -> None:
        """Sequence metadata for expanded aligned inputs should use the resolved input view size."""
        source = """\
#![generate(cpp, cpp_cog)]
#![cpp(namespace=clockwork::aligned_seqno_view_size_test)]

use std::cog_metrics_policy::{CogEventMetricsPolicy, CogTelemetryMetricsPolicy};

// Sensor data.
schema SensorData
{
    uuid: aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa;
    fields
    {
        // Timestamp.
        #0 timestamp: SyncTime;
    }
}

// Sensor aligner.
aligner SensorAligner
{
    inputs
    {
        sensor: Tappy<SensorData>
        {
            max_msgs: 10;
            arbitrary_selection: true;
        }
    }
}

// Consumer cog.
cog AlignedSeqNoCog
{
    aligned_inputs
    {
        sensor_aligned: SensorAligner;
    }

    execution
    {
        condition s: new_message(sensor_aligned);
        execute when: s;
    }
}

policy CogEventMetricsPolicy for AlignedSeqNoCog
{
    enabled = true;
    emit_sequence_numbers = true;
}

policy CogTelemetryMetricsPolicy for AlignedSeqNoCog
{
    enabled = false;
}
"""
        module = compiler.compile_source_text(
            source,
            ModuleID(CLK_REPO, "aligned_seqno_view_size_test"),
            importer=fs_importer,
        )
        cog = module.inner_scope.lookup("AlignedSeqNoCog")
        assert isinstance(cog, cog_ir.Cog)
        # Consumer cog adds 20% to the original aligner view size of 10.
        assert cog.expanded_aligned_input_defs["sensor_aligned.sensor"].view_params.max_msgs == 12

        rg = cog.cog_metrics_report_groups["cog_event_metrics_group"]
        entry = rg.entries["sensor_unseen_messages"]
        assert get_view_size_from_metadata(entry) == 12
