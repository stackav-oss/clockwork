# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate channel spy configurations."""

from itertools import chain
from uuid import UUID

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.composition import channel_spy_config_proto, system
from clockwork.dsl.composition.channel_spy_config import (
    ChannelSpyConfig,
    PublishedChannelMetadata,
)
from clockwork.serialization.metadata import tachyon as tachyon_metadata


def _generate_channel_spy_config_domain(
    compiler_context: CompilerContext, domain: system.PhysicalCpuDomain
) -> channel_spy_config_proto.ChannelSpyConfig:
    all_metadata = []
    for buffer_uuid, buffer in chain(domain.buffers.items(), domain.metrics_buffers.items()):
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        # Only supporting Tachyon representation
        assert message_repr_typespec.instantiates.value_key() == "::Tachyon"
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(compiler_context, message_schema)
        all_metadata.append(
            PublishedChannelMetadata(
                uuid=buffer_uuid,
                num_slots=buffer.layout.num_slots,
                message_size=buffer.layout.message_size,
                channel_name=buffer.channel.channel.channel_name,
                schema_name=message_schema.value_key(),
                schema_definition=list(schema_definition),
            )
        )
    return ChannelSpyConfig(channels=all_metadata)


def gen_channel_spy_configs(
    compiler_context: CompilerContext, sys: system.PhysicalSystem
) -> dict[UUID, channel_spy_config_proto.ChannelSpyConfig]:
    """Generate channel spy configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        result[domain_uuid] = _generate_channel_spy_config_domain(compiler_context, domain)
    return result
