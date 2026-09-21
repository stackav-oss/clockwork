# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate signal metadata configurations for a physical system.

This module extracts comprehensive metadata about signals, report groups, and their
instances from a composed physical system and generates Tachyon configuration files.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from clockwork.dsl.composition import signal_metadata_config, signal_metadata_config_proto
from clockwork.dsl.ir import cog, report_group, signal_registry, units
from clockwork.dsl.ir.signal import AggregationType

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.composition import system


def _find_signal_index(all_signals: list[signal_registry.ResolvedSignal], signal_name: str) -> int:
    """Find the index of a signal by name.

    Signal names are unique globally, so we expect exactly one match.
    """
    try:
        return next(i for i, sig in enumerate(all_signals) if sig.signal_name == signal_name)
    except StopIteration as e:
        msg = f"Signal '{signal_name}' not found in signal registry"
        raise ValueError(msg) from e


def _find_report_group_index(report_groups: list[report_group.ReportGroupDef], group_name: str, cog_name: str) -> int:
    """Find the index of a report group by name within a cog class.

    Report group names are unique within a cog class, so we expect exactly one match.
    """
    try:
        return next(i for i, rg in enumerate(report_groups) if rg.name == group_name)
    except StopIteration as e:
        msg = f"Report group '{group_name}' not found in cog class '{cog_name}'"
        raise ValueError(msg) from e


def generate_signal_metadata_config(
    sys: system.PhysicalSystem,
) -> signal_metadata_config_proto.SignalMetadataConfig:
    """Generate signal metadata configuration for a physical system.

    Args:
        sys: The physical system to generate metadata for.

    Returns:
        Complete signal metadata configuration.
    """
    compiler_context = sys.system.module.context
    instance_names = signal_registry.get_unique_signal_instance_names(compiler_context)

    # Get enum entities for use in metadata generation
    entities = signal_metadata_config.get_entities(compiler_context)

    all_signals = signal_registry.get_all_signals(compiler_context)
    signals = [
        _get_signal_metadata(resolved_signal, compiler_context, instance_names, entities)
        for resolved_signal in all_signals
    ]
    cogs = _generate_cog_report_groups(sys, entities)
    cog_instances = [
        _generate_cog_instance_metadata(cog_instance, compiler_context, instance_names, entities)
        for cog_instance in sys.system.cogs.values()
    ]
    report_group_channels = _generate_report_group_channels(sys, entities)

    return entities.signal_metadata_config(
        signal_instance_names=instance_names,
        signals=signals,
        cogs=cogs,
        cog_instances=cog_instances,
        report_group_channels=report_group_channels,
    )


def _get_signal_metadata(
    resolved_signal: signal_registry.ResolvedSignal,
    compiler_context: CompilerContext,
    instance_names: list[str],
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.SignalMetadata:
    """Generate metadata for a single signal.

    Args:
        resolved_signal: The resolved signal to generate metadata for.
        compiler_context: The compiler context.
        instance_names: List of all signal instance names.
        entities: Entity factory for creating metadata objects.

    Returns:
        Signal metadata for the resolved signal.
    """
    instances = signal_registry.get_signal_instances(compiler_context, resolved_signal.signal_name)
    signal_instance_indexes = [instance_names.index(instance.instance_name) for instance in instances]

    pre_aggregations = (
        [_aggregation_to_enum(agg, entities) for agg in resolved_signal.pre_aggregation]
        if resolved_signal.pre_aggregation
        else [_aggregation_to_enum(AggregationType.VALUE, entities)]
    )

    return entities.signal_metadata(
        name=resolved_signal.signal_name,
        pre_aggregation_type=pre_aggregations,
        pre_aggregation_definition=resolved_signal.pre_aggregation_text,
        signal_instance_indexes=signal_instance_indexes,
    )


def _generate_cog_report_groups(
    sys: system.PhysicalSystem,
    entities: signal_metadata_config.Entities,
) -> list[signal_metadata_config_proto.CogReportGroupsMetadata]:
    result = []
    compiler_context = sys.system.module.context
    processed_cog_classes: set[str] = set()

    for cog_instance in sys.system.cogs.values():
        cog_class = cog_instance.cog_class
        cog_path = cog_class.fqn

        if cog_path in processed_cog_classes:
            continue

        report_groups_defs = {**cog_class.report_groups, **cog_class.cog_metrics_report_groups}
        if not report_groups_defs:
            continue

        report_groups_metadata = [
            _generate_report_group_metadata(rg_def, compiler_context, entities)
            for rg_def in report_groups_defs.values()
        ]

        cog_report_groups = entities.cog_report_groups_metadata(
            cog_path=cog_path,
            report_groups=report_groups_metadata,
        )
        result.append(cog_report_groups)
        processed_cog_classes.add(cog_path)

    return result


def _generate_report_group_metadata(
    rg_def: report_group.ReportGroupDef,
    compiler_context: CompilerContext,
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.ReportGroupMetadata:
    config = rg_def.report_group_config

    if not config:
        msg = rg_def.append_error_line(
            f"Report group {rg_def.name} is missing its configuration. Make sure a ReportGroupPolicy is assigned to it."
        )
        raise ValueError(msg)
    rg_type = (
        entities.report_group_type.Aggregated
        if config.reporting_strategy == report_group.ReportingStrategy.POST_AGGREGATED
        else entities.report_group_type.Batched
    )

    if config.log_type == report_group.ReportGroupLogType.EVENT:
        log_type = entities.log_type.event
    elif config.log_type == report_group.ReportGroupLogType.NON_REDUNDANT_TELEMETRY:
        log_type = entities.log_type.non_redundant_telemetry
    else:
        log_type = entities.log_type.none

    min_duration = 0
    max_duration = 0
    aggregation_size = 0

    if config.min_duration:
        min_duration = int(config.min_duration.as_unit(units.NANOSECONDS).value)
    if config.max_duration:
        max_duration = int(config.max_duration.as_unit(units.NANOSECONDS).value)
    if config.max_observations is not None:
        aggregation_size = config.max_observations

    signals_metadata = [
        _generate_report_group_signal_metadata(rg_def, entry, compiler_context, entities)
        for entry in rg_def.entries.values()
    ]

    return entities.report_group_metadata(
        name=rg_def.name,
        log_type=log_type,
        report_group_type=rg_type,
        aggregation_size=aggregation_size,
        min_duration=min_duration,
        max_duration=max_duration,
        signals=signals_metadata,
    )


def _generate_report_group_signal_metadata(
    rg_def: report_group.ReportGroupDef,
    entry: report_group.ReportGroupEntry,
    compiler_context: CompilerContext,
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.ReportGroupSignalMetadata:
    resolved_signal = entry.resolved_signal()
    all_signals = signal_registry.get_all_signals(compiler_context)
    signal_index = _find_signal_index(all_signals, resolved_signal.signal_name)

    post_agg = entry.effective_post_aggregation()
    post_agg_types = [_aggregation_to_enum(agg_type, entities) for agg_type in post_agg] if post_agg else []

    alias = entry.name if entry.name != resolved_signal.signal_name else ""
    validity = rg_def.get_signal_validity(entry.name)

    return entities.report_group_signal_metadata(
        signal_index=signal_index,
        post_aggregation_types=post_agg_types,
        alias=alias,
        validity_source=_validity_source_to_enum(validity.source, entities),
        validity_index=validity.index,
    )


def _generate_cog_instance_metadata(
    cog_instance: cog.CogInstance,
    compiler_context: CompilerContext,
    instance_names: list[str],
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.CogInstanceMetadata:
    cog_class = cog_instance.cog_class

    report_group_instances = [
        _generate_report_group_instance_metadata(
            rg_instance,
            cog_class,
            compiler_context,
            instance_names,
            entities,
        )
        for rg_instance in cog_instance.report_group_instances
    ]

    return entities.cog_instance_metadata(
        cog_path=cog_class.fqn,
        cog_instance_path=cog_instance.fqn,
        report_group_instances=report_group_instances,
    )


def _generate_report_group_instance_metadata(
    rg_instance: report_group.ReportGroupInstance,
    cog_class: cog.Cog | cog.InstantiatedCog,
    compiler_context: CompilerContext,
    instance_names: list[str],
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.ReportGroupInstanceMetadata:
    report_group_index = _find_report_group_index(
        list({**cog_class.report_groups, **cog_class.cog_metrics_report_groups}.values()),
        rg_instance.group_def.name,
        cog_class.name,
    )

    channel_name_str = rg_instance.channel_name(compiler_context)

    all_signals = signal_registry.get_all_signals(compiler_context)
    signal_instances_metadata = [
        entities.signal_instance_metadata(
            signal_index=_find_signal_index(all_signals, entry_instance.signal.signal_name),
            signal_instance_index=instance_names.index(entry_instance.instance_name),
        )
        for entry_instance in rg_instance.entries.values()
    ]

    return entities.report_group_instance_metadata(
        report_group_index=report_group_index,
        channel_name=channel_name_str,
        signal_instances=signal_instances_metadata,
    )


def _generate_report_group_channels(
    sys: system.PhysicalSystem,
    entities: signal_metadata_config.Entities,
) -> list[signal_metadata_config_proto.ReportGroupChannelMetadata]:
    return [
        entities.report_group_channel_metadata(
            channel_name=rg_instance.channel_name(sys.system.module.context),
            cog_path=cog_instance.cog_class.fqn,
            cog_instance_path=cog_instance.fqn,
            is_cog_metrics_channel=rg_instance.group_def.name in cog_instance.cog_class.cog_metrics_report_groups,
            report_group_index=_find_report_group_index(
                list(
                    {
                        **cog_instance.cog_class.report_groups,
                        **cog_instance.cog_class.cog_metrics_report_groups,
                    }.values()
                ),
                rg_instance.group_def.name,
                cog_instance.cog_class.name,
            ),
        )
        for cog_instance in sys.system.cogs.values()
        for rg_instance in cog_instance.report_group_instances
    ]


def _aggregation_to_enum(
    agg_type: AggregationType,
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.AggregationType:
    """Convert AggregationType from signal.py to the .clk enum value."""
    mapping = {
        AggregationType.VALUE: entities.aggregation_type.Value,
        AggregationType.MIN: entities.aggregation_type.Min,
        AggregationType.MAX: entities.aggregation_type.Max,
        AggregationType.SUM: entities.aggregation_type.Sum,
        AggregationType.COUNT: entities.aggregation_type.Count,
        AggregationType.MEAN: entities.aggregation_type.Mean,
        AggregationType.FINAL_VALUE: entities.aggregation_type.FinalValue,
        AggregationType.FIRST_VALUE: entities.aggregation_type.FirstValue,
    }
    return mapping.get(agg_type, entities.aggregation_type.Value)


def _validity_source_to_enum(
    source: report_group.SignalValiditySource,
    entities: signal_metadata_config.Entities,
) -> signal_metadata_config_proto.SignalValiditySource:
    """Convert a generated report-group validity source to metadata enum data."""
    if source is report_group.SignalValiditySource.COUNT_FIELD:
        return entities.signal_validity_source.count_field
    if source is report_group.SignalValiditySource.PRESENCE_BIT:
        return entities.signal_validity_source.presence_bit
    msg = f"Unexpected signal validity source: {source}"
    raise ValueError(msg)
