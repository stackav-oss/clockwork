# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to metrics channel metadata configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
class MetricsChannelMetadata(Tachyon["MetricsChannelMetadata"]):
    """Metrics channel metadata."""

    metrics_channel_name: str
    metrics_channel_uuid: UUID
    cog_path: str
    cog_instance_path: str


@dataclass(kw_only=True)
class MetricsChannelMetadataReport(Tachyon["MetricsChannelMetadataReport"]):
    """Metrics channel metadata report."""

    metrics_channels: list[MetricsChannelMetadata]


@dataclass(kw_only=True)
class MetricsChannelMetadataConfig(Tachyon["MetricsChannelMetadataConfig"]):
    """Metrics channel metadata configuration."""

    metrics_channels: list[MetricsChannelMetadata]
    metrics_metadata_report_schema_name: str
    metrics_metadata_report_schema_definition: list[int]
