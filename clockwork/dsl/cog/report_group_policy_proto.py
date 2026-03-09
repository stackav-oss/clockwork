# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to report group policy configurations."""

from dataclasses import dataclass
from typing import Protocol

from clockwork.serialization.py.protocol import Tachyon


class ReportingStrategy(Protocol):
    """Fake enum type for values of ReportingStrategy."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_reporting_strategy: None


class ReportingStrategyEnum(Protocol):
    """Reporting strategy enum."""

    batched: ReportingStrategy
    post_aggregated: ReportingStrategy


class ReportGroupLogType(Protocol):
    """Fake enum type for values of ReportGroupLogType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_report_group_log_type: None


class ReportGroupLogTypeEnum(Protocol):
    """Report group log type enum."""

    none: ReportGroupLogType
    event: ReportGroupLogType
    telemetry: ReportGroupLogType


@dataclass(kw_only=True)
class ReportGroupPolicyConfig(Tachyon["ReportGroupPolicyConfig"]):
    """Schema for report group policy."""

    reporting_strategy: ReportingStrategy
    log_type: ReportGroupLogType
    min_observations: int | None
    max_observations: int | None
    min_duration: int | None
    max_duration: int | None
