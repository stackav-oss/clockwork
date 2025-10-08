# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate channel spy configurations."""

from uuid import UUID

from clockwork.dsl.composition import metrics_channel_metadata_config_proto, system
from clockwork.dsl.composition.metrics_channel_metadata_config import (
    CSC_MODULE,
    MetricsChannelMetadata,
    MetricsChannelMetadataConfig,
)
from clockwork.dsl.ir import node, representation
from clockwork.serialization.metadata import tachyon as tachyon_metadata


def _generate_metrics_metadata_config_domain(
    domain: system.PhysicalCpuDomain,
) -> metrics_channel_metadata_config_proto.MetricsChannelMetadataConfig:
    all_metrics_channels_metadata = []
    for buffer in domain.metrics_buffers.values():
        metrics_channel_name = buffer.channel.channel.channel_name
        metrics_channel_uuid = buffer.channel.channel.uuid
        cog_path = buffer.channel.channel.cog_path

        cog_instance_path = buffer.channel.channel.cog_instance_path
        all_metrics_channels_metadata.append(
            MetricsChannelMetadata(
                metrics_channel_name=metrics_channel_name,
                metrics_channel_uuid=metrics_channel_uuid,
                cog_path=cog_path,
                cog_instance_path=cog_instance_path,
            )
        )

    rep = CSC_MODULE.inner_scope.lookup("MetricsChannelMetadataReportTach")
    # Only way this can fail is if a change to metrics_channel_metadata_config.clk removes/modifies MetricsChannelMetadataReportTach.
    assert isinstance(rep, representation.ReprInstantiation), node.enrich_error_if_possible(
        rep, "Expected MetricsChannelMetadataReportTach to refer to a ReprInstantiation"
    )
    message_schema = rep.get_resolved().get_schema()
    schema_definition = tachyon_metadata.get_serialized_metadata(CSC_MODULE.context, message_schema)
    return MetricsChannelMetadataConfig(
        metrics_channels=all_metrics_channels_metadata,
        metrics_metadata_report_schema_name=message_schema.value_key(),
        metrics_metadata_report_schema_definition=list(schema_definition),
    )


def gen_metrics_channel_metadata_configs(
    sys: system.PhysicalSystem,
) -> dict[UUID, metrics_channel_metadata_config_proto.MetricsChannelMetadataConfig]:
    """Generate metrics channel metadata configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        result[domain_uuid] = _generate_metrics_metadata_config_domain(domain)
    return result
