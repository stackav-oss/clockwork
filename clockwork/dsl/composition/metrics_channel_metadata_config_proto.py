# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to metrics channel metadata configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MetricsChannelMetadata(Tachyon["MetricsChannelMetadata"]):
# fmt: on
    """Metrics channel metadata."""

    metrics_channel_name: str
    metrics_channel_uuid: UUID
    cog_path: str
    cog_instance_path: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MetricsChannelMetadataReport(Tachyon["MetricsChannelMetadataReport"]):
# fmt: on
    """Metrics channel metadata report."""

    metrics_channels: list[MetricsChannelMetadata]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MetricsChannelMetadataConfig(Tachyon["MetricsChannelMetadataConfig"]):
# fmt: on
    """Metrics channel metadata configuration."""

    metrics_channels: list[MetricsChannelMetadata]
    metrics_metadata_report_schema_name: str
    metrics_metadata_report_schema_definition: list[int]
