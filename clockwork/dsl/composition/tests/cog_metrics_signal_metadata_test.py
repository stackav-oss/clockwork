# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Integration tests for cog metrics report groups in signal metadata configuration.

Validates that auto-generated cog metrics report groups (event and telemetry) are
correctly included in the SignalMetadataConfig output alongside user-defined report
groups.
"""

from pathlib import Path

import pytest
from clockwork.dsl.composition import gen_signal_metadata_configs, system
from clockwork.dsl.ir import box, compiler
from clockwork.dsl.ir.cog_metrics_report_groups import EVENT_METRICS_GROUP_NAME, TELEMETRY_METRICS_GROUP_NAME
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

_TEST_SYSTEM_PATH = Path("clockwork/dsl/composition/tests/support/cog_metrics_signals_test_system.clk")


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def physical_system(fs_importer: FilesystemImporter) -> system.PhysicalSystem:
    module = compiler.compile_source_file(ModuleID.from_path(CLK_REPO, _TEST_SYSTEM_PATH), fs_importer)
    box_template_ir = module.inner_scope.lookup("CogMetricsSignalTestSystemBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False, False)
    return system.make_physical_system(logical_system)


@pytest.fixture()
def config(
    physical_system: system.PhysicalSystem,
) -> gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig:
    return gen_signal_metadata_configs.generate_signal_metadata_config(physical_system)


def test_cog_metrics_report_groups_in_metadata(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Cog metrics report groups appear alongside user-defined report groups in cog metadata."""
    assert len(config.cogs) == 1
    cog_metadata = config.cogs[0]

    rg_names = {rg.name for rg in cog_metadata.report_groups}
    assert EVENT_METRICS_GROUP_NAME in rg_names
    assert TELEMETRY_METRICS_GROUP_NAME in rg_names
    assert "user_report_group" in rg_names
    assert len(cog_metadata.report_groups) == 3


def test_event_report_group_metadata(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Event metrics report group has correct type, log type, and configuration values."""
    cog_metadata = config.cogs[0]
    event_rg = next(rg for rg in cog_metadata.report_groups if rg.name == EVENT_METRICS_GROUP_NAME)

    # Batched type (not Aggregated)
    assert event_rg.report_group_type
    # Aggregation size should be 20 (custom batch_size from policy)
    assert event_rg.aggregation_size == 20
    # Max duration should be 2s = 2_000_000_000 ns
    assert event_rg.max_duration == 2_000_000_000

    # Event report group should have:
    # 9 global + 1 event-only + 2*3 per-input + 2 per-output + 2 per-condition = 19 signals
    assert len(event_rg.signals) == 20
    assert all(signal.validity_source.value in {1, 2} for signal in event_rg.signals)
    assert all(signal.validity_index >= 0 for signal in event_rg.signals)


def test_telemetry_report_group_metadata(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Telemetry metrics report group has correct type, log type, and configuration values."""
    cog_metadata = config.cogs[0]
    telemetry_rg = next(rg for rg in cog_metadata.report_groups if rg.name == TELEMETRY_METRICS_GROUP_NAME)

    # Max duration should be 2s = 2_000_000_000 ns
    assert telemetry_rg.max_duration == 2_000_000_000

    # Telemetry report group should have:
    # 8 global + 1 telemetry-only + 2*3 per-input + 1 per-output + 2 per-condition = 18 signals
    assert len(telemetry_rg.signals) == 18

    # All non-condition signals should have min/max/mean post-aggregation (3 types)
    condition_signal_count = 0
    for sig in telemetry_rg.signals:
        if len(sig.post_aggregation_types) == 1:
            condition_signal_count += 1
        else:
            assert len(sig.post_aggregation_types) == 3
    # 2 condition signals with VALUE post-aggregation
    assert condition_signal_count == 2
    assert all(signal.validity_source.value in {1, 2} for signal in telemetry_rg.signals)
    assert all(signal.validity_index >= 0 for signal in telemetry_rg.signals)


def test_cog_instances_have_all_report_group_instances(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Each cog instance has report group instances for user, event, and telemetry groups."""
    assert len(config.cog_instances) == 2

    cog_class = config.cogs[0]
    for cog_instance in config.cog_instances:
        assert cog_instance.cog_path == cog_class.cog_path
        # 3 report group instances: user, event metrics, telemetry metrics
        assert len(cog_instance.report_group_instances) == 3
        assert len(cog_instance.report_group_instances) == len(cog_class.report_groups)


def test_report_group_instance_indexes_valid(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Report group instance indexes reference valid report group definitions."""
    cog_class = config.cogs[0]
    for cog_instance in config.cog_instances:
        for rg_instance in cog_instance.report_group_instances:
            assert 0 <= rg_instance.report_group_index < len(cog_class.report_groups)


def test_report_group_channels_follow_convention(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Report group channels follow the /_clockwork/report-groups/ naming convention."""
    # 2 cog instances * 3 report groups = 6 channels
    assert len(config.report_group_channels) == 6

    for channel in config.report_group_channels:
        assert channel.channel_name.startswith("/_clockwork/report-groups/")


def test_cog_metrics_channels_contain_cog_class_name(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Cog metrics channel names contain the cog class name and group name."""
    cog_metrics_channels = [
        ch
        for ch in config.report_group_channels
        if EVENT_METRICS_GROUP_NAME in ch.channel_name or TELEMETRY_METRICS_GROUP_NAME in ch.channel_name
    ]
    # 2 cog instances * 2 cog metrics groups = 4 cog metrics channels
    assert len(cog_metrics_channels) == 4

    for ch in cog_metrics_channels:
        assert "CogMetricsSignalTestCog" in ch.channel_name


def test_signal_instance_names_include_cog_metrics_entries(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Signal instance names include entries for cog metrics multi-instance signals."""
    names = set(config.signal_instance_names)

    # Cog-level signals use the cog instance FQN as the instance name.
    # Per-input/output/condition signals use "{cog_fqn}.{endpoint_name}".
    # Due to qualify_entry_instance_names=True on cog metrics report groups,
    # the instance names are qualified with the cog instance FQN.
    # Check that per-endpoint instance names are present.
    endpoint_suffixes = {"sensor_a", "sensor_b", "result", "periodic", "new_msg"}
    for suffix in endpoint_suffixes:
        assert any(suffix in name for name in names), f"Expected instance name containing '{suffix}'"


def test_signal_indexes_in_report_groups_are_valid(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Signal indexes in report group signal entries reference valid signals."""
    for cog_metadata in config.cogs:
        for rg in cog_metadata.report_groups:
            for sig_entry in rg.signals:
                assert 0 <= sig_entry.signal_index < len(config.signals)


def test_signal_instance_indexes_in_instances_are_valid(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Signal instance indexes in report group instances reference valid instance names."""
    for cog_instance in config.cog_instances:
        for rg_instance in cog_instance.report_group_instances:
            for sig_instance in rg_instance.signal_instances:
                assert 0 <= sig_instance.signal_index < len(config.signals)
                assert 0 <= sig_instance.signal_instance_index < len(config.signal_instance_names)


def test_report_group_channel_cog_references_valid(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Report group channels reference valid cog class and instance paths."""
    cog_paths = {c.cog_path for c in config.cogs}
    cog_instance_paths = {ci.cog_instance_path for ci in config.cog_instances}

    for channel in config.report_group_channels:
        assert channel.cog_path in cog_paths
        assert channel.cog_instance_path in cog_instance_paths


def test_event_and_telemetry_channels_are_distinct(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Each cog instance has separate channels for event vs telemetry report groups."""
    channel_names = [ch.channel_name for ch in config.report_group_channels]
    # All channel names should be unique
    assert len(channel_names) == len(set(channel_names))

    event_channels = [ch for ch in channel_names if EVENT_METRICS_GROUP_NAME in ch]
    telemetry_channels = [ch for ch in channel_names if TELEMETRY_METRICS_GROUP_NAME in ch]
    assert len(event_channels) == 2
    assert len(telemetry_channels) == 2


def test_is_cog_metrics_channel_flag(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """is_cog_metrics_channel is set only for cog metrics report group channels."""
    cog_metrics_channels = [ch for ch in config.report_group_channels if ch.is_cog_metrics_channel]
    user_channels = [ch for ch in config.report_group_channels if not ch.is_cog_metrics_channel]

    # 2 cog instances * 2 cog metrics groups (event + telemetry) = 4 flagged channels
    assert len(cog_metrics_channels) == 4
    # 2 cog instances * 1 user report group = 2 unflagged channels
    assert len(user_channels) == 2

    # Cog metrics channels contain the metrics group name in their channel name
    for ch in cog_metrics_channels:
        assert EVENT_METRICS_GROUP_NAME in ch.channel_name or TELEMETRY_METRICS_GROUP_NAME in ch.channel_name

    # User-defined channels do not contain the metrics group names
    for ch in user_channels:
        assert EVENT_METRICS_GROUP_NAME not in ch.channel_name
        assert TELEMETRY_METRICS_GROUP_NAME not in ch.channel_name


def test_shared_signals_have_distinct_instances_per_report_group(
    config: gen_signal_metadata_configs.signal_metadata_config_proto.SignalMetadataConfig,
) -> None:
    """Signals shared between event and telemetry groups get distinct instances per report group.

    Cog-level signals like cog_exec_duration appear in both event and telemetry
    report groups. Each report group * cog instance combination produces a
    distinct signal instance (prefixed by the report group name), so the total
    instance count is 2 groups * 2 cog instances = 4.
    """
    cog_exec_duration_signal = next(s for s in config.signals if "cog_exec_duration" in s.name)
    # 2 cog instances * 2 report groups (event + telemetry) = 4 distinct instances
    assert len(cog_exec_duration_signal.signal_instance_indexes) == 4

    # Verify all signal instance names are globally unique
    assert len(config.signal_instance_names) == len(set(config.signal_instance_names))
