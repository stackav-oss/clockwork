# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python type stubs for cog metrics policy configurations."""

from dataclasses import dataclass

from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogEventMetricsPolicyConfig(Tachyon["CogEventMetricsPolicyConfig"]):
# fmt: on
    """Schema for cog event metrics policy."""

    enabled: bool
    batch_size: int | None
    min_duration: int | None  # Duration in nanoseconds
    max_duration: int | None  # Duration in nanoseconds


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogTelemetryMetricsPolicyConfig(Tachyon["CogTelemetryMetricsPolicyConfig"]):
# fmt: on
    """Schema for cog telemetry metrics policy."""

    enabled: bool
    min_duration: int | None  # Duration in nanoseconds
    max_duration: int | None  # Duration in nanoseconds
    min_observations: int | None
    max_observations: int | None
