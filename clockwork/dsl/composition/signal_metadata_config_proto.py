# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Protocol stubs for signal metadata configuration types.

This module provides type hints for the signal metadata configuration dataclasses.
These are protocol definitions that match the runtime dataclasses created by
signal_metadata_config.py.
"""

from dataclasses import dataclass
from typing import Protocol

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
    non_redundant_telemetry: LogType


class ReportGroupType(Protocol):
    """Fake enum type for values of ReportGroupType."""

    _please_never_define_a_class_with_this_attribute_report_group_type: None


class ReportGroupTypeEnum(Protocol):
    """Report group type."""

    Batched: ReportGroupType
    Aggregated: ReportGroupType


class SignalValiditySource(Protocol):
    """Fake enum type for values of SignalValiditySource."""

    _please_never_define_a_class_with_this_attribute_signal_validity_source: None
    value: int


class SignalValiditySourceEnum(Protocol):
    """Source used to determine whether a report-group signal is present."""

    legacy_assume_present: SignalValiditySource
    count_field: SignalValiditySource
    presence_bit: SignalValiditySource


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SignalMetadata(Tachyon["SignalMetadata"]):
# fmt: on
    """Signal definition with pre-aggregation configuration."""

    name: str
    pre_aggregation_type: list[AggregationType]
    pre_aggregation_definition: str
    signal_instance_indexes: list[int]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ReportGroupSignalMetadata(Tachyon["ReportGroupSignalMetadata"]):
# fmt: on
    """Signal entry within a report group."""

    signal_index: int
    post_aggregation_types: list[AggregationType]
    alias: str | None
    validity_source: SignalValiditySource
    validity_index: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ReportGroupMetadata(Tachyon["ReportGroupMetadata"]):
# fmt: on
    """Report group configuration."""

    name: str
    log_type: LogType
    report_group_type: ReportGroupType
    aggregation_size: int
    min_duration: int  # Duration in nanoseconds
    max_duration: int  # Duration in nanoseconds
    signals: list[ReportGroupSignalMetadata]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogReportGroupsMetadata(Tachyon["CogReportGroupsMetadata"]):
# fmt: on
    """Cog class with all its report groups."""

    report_groups: list[ReportGroupMetadata]
    cog_path: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SignalInstanceMetadata(Tachyon["SignalInstanceMetadata"]):
# fmt: on
    """Signal instance metadata."""

    signal_index: int
    signal_instance_index: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ReportGroupInstanceMetadata(Tachyon["ReportGroupInstanceMetadata"]):
# fmt: on
    """Report group instance metadata."""

    report_group_index: int
    channel_name: str
    signal_instances: list[SignalInstanceMetadata]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogInstanceMetadata(Tachyon["CogInstanceMetadata"]):
# fmt: on
    """Cog instance with report group instances."""

    report_group_instances: list[ReportGroupInstanceMetadata]
    cog_path: str
    cog_instance_path: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ReportGroupChannelMetadata(Tachyon["ReportGroupChannelMetadata"]):
# fmt: on
    """Channel mapping for report groups."""

    channel_name: str
    report_group_index: int
    cog_path: str
    cog_instance_path: str
    is_cog_metrics_channel: bool


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SignalMetadataConfig(Tachyon["SignalMetadataConfig"]):
# fmt: on
    """Top-level signal metadata configuration."""

    signal_instance_names: list[str]
    signals: list[SignalMetadata]
    cogs: list[CogReportGroupsMetadata]
    cog_instances: list[CogInstanceMetadata]
    report_group_channels: list[ReportGroupChannelMetadata]
