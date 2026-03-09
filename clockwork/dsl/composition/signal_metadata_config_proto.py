# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Protocol stubs for signal metadata configuration types.

This module provides type hints for the signal metadata configuration dataclasses.
These are protocol definitions that match the runtime dataclasses created by
signal_metadata_config.py.
"""

from dataclasses import dataclass
from typing import Protocol
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


class AggregationType(Protocol):
    """Fake enum type for values of AggregationType."""

    _please_never_define_a_class_with_this_attribute_aggregation_type: None


class AggregationTypeEnum(Protocol):
    """Aggregation type for signals."""

    Value: AggregationType
    Min: AggregationType
    Max: AggregationType
    Sum: AggregationType
    Count: AggregationType
    Mean: AggregationType
    FinalValue: AggregationType
    FirstValue: AggregationType


class LogType(Protocol):
    """Fake enum type for values of LogType."""

    _please_never_define_a_class_with_this_attribute_log_type: None


class LogTypeEnum(Protocol):
    """Log type for report groups."""

    none: LogType
    event: LogType
    telemetry: LogType


class ReportGroupType(Protocol):
    """Fake enum type for values of ReportGroupType."""

    _please_never_define_a_class_with_this_attribute_report_group_type: None


class ReportGroupTypeEnum(Protocol):
    """Report group type."""

    Batched: ReportGroupType
    Aggregated: ReportGroupType


@dataclass(kw_only=True)
class SignalMetadata(Tachyon["SignalMetadata"]):
    """Signal definition with pre-aggregation configuration."""

    name: str
    pre_aggregation_type: list[AggregationType]
    pre_aggregation_definition: str
    signal_instance_indexes: list[int]


@dataclass(kw_only=True)
class ReportGroupSignalMetadata(Tachyon["ReportGroupSignalMetadata"]):
    """Signal entry within a report group."""

    signal_index: int
    post_aggregation_types: list[AggregationType]
    alias: str | None


@dataclass(kw_only=True)
class ReportGroupMetadata(Tachyon["ReportGroupMetadata"]):
    """Report group configuration."""

    name: str
    log_type: LogType
    report_group_type: ReportGroupType
    aggregation_size: int
    min_duration: int  # Duration in nanoseconds
    max_duration: int  # Duration in nanoseconds
    signals: list[ReportGroupSignalMetadata]


@dataclass(kw_only=True)
class CogReportGroupsMetadata(Tachyon["CogReportGroupsMetadata"]):
    """Cog class with all its report groups."""

    cog_class_id: UUID
    report_groups: list[ReportGroupMetadata]


@dataclass(kw_only=True)
class SignalInstanceMetadata(Tachyon["SignalInstanceMetadata"]):
    """Signal instance metadata."""

    signal_index: int
    signal_instance_index: int


@dataclass(kw_only=True)
class ReportGroupInstanceMetadata(Tachyon["ReportGroupInstanceMetadata"]):
    """Report group instance metadata."""

    report_group_index: int
    channel_name: str
    signal_instances: list[SignalInstanceMetadata]


@dataclass(kw_only=True)
class CogInstanceMetadata(Tachyon["CogInstanceMetadata"]):
    """Cog instance with report group instances."""

    cog_class_id: UUID
    cog_instance_id: UUID
    report_group_instances: list[ReportGroupInstanceMetadata]


@dataclass(kw_only=True)
class ReportGroupChannelMetadata(Tachyon["ReportGroupChannelMetadata"]):
    """Channel mapping for report groups."""

    channel_name: str
    cog_class_id: UUID
    cog_instance_id: UUID
    report_group_index: int


@dataclass(kw_only=True)
class SignalMetadataConfig(Tachyon["SignalMetadataConfig"]):
    """Top-level signal metadata configuration."""

    signal_instance_names: list[str]
    signals: list[SignalMetadata]
    cogs: list[CogReportGroupsMetadata]
    cog_instances: list[CogInstanceMetadata]
    report_group_channels: list[ReportGroupChannelMetadata]
