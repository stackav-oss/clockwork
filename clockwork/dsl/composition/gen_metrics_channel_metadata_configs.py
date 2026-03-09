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
from clockwork.dsl.ir import clkbuiltins, node, representation, schema, typesys
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

    rep_schema = CSC_MODULE.inner_scope.lookup("MetricsChannelMetadataReport")
    # Only way these asserts can fail is if a change to metrics_channel_metadata_config.clk removes/modifies
    # MetricsChannelMetadataReport.
    assert isinstance(rep_schema, schema.Schema), node.enrich_error_if_possible(
        rep_schema, "Expected MetricsChannelMetadataReport to refer to a Schema"
    )
    rep_params = rep_schema.generic_parameters()
    assert rep_params, node.enrich_error_if_possible(
        rep_schema, "Expected MetricsChannelMetadataReport to refer to a parameterized Schema"
    )
    rep_args: dict[str, typesys.Value] = {}
    for param in rep_params:
        assert param.default, node.enrich_error_if_possible(
            rep_schema, "Expected MetricsChannelMetadataReport parameters to have default values"
        )
        rep_args[param.name] = param.default
    rep_inst = typesys.Instantiation(type_info=clkbuiltins.TYPE_TYPE, instantiates=rep_schema, arguments=rep_args)
    rep = representation.ResolvedReprInstantiation.from_schema(schema_ir=rep_inst, module=CSC_MODULE)
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
