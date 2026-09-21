# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

from dataclasses import dataclass
from typing import cast
from uuid import UUID

from clockwork.dsl.composition import logger_config, logger_config_proto, publisher_config_proto, system
from clockwork.dsl.composition.publisher_config import (
    ChannelPublisherConfig,
    PublishedChannelConfig,
    make_unbuffered_published_channel_config,
)
from clockwork.dsl.ir import box, primitive
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2


@dataclass(kw_only=True)
class GeneratedLogWriterConfigs:
    """Hold generated LogWriterConfigs."""

    events_config: logger_config_proto.LogWriterConfig
    telemetry_config: logger_config_proto.LogWriterConfig


def _gen_logger_configs_domain(domain: system.PhysicalCpuDomain) -> GeneratedLogWriterConfigs:
    ctx = domain.system.system.module.context
    logger_config_entities = logger_config.get_entities(ctx)

    result = GeneratedLogWriterConfigs(
        events_config=logger_config_entities.log_writer_config(channels=[]),
        telemetry_config=logger_config_entities.log_writer_config(channels=[]),
    )
    for log_observer in domain.log_observers.values():
        buffer_uuid = log_observer.pinion_buffer
        buffer = (domain.buffers | domain.metrics_buffers)[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        # Only Tachyon encoding is supported currently
        assert message_repr_typespec.instantiates.value_key() == "::Tachyon"
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(ctx, message_schema)
        config = logger_config_entities.logged_channel_config(
            uuid=buffer_uuid,
            num_slots=buffer.layout.num_slots,
            message_size=buffer.layout.message_size,
            channel_name=buffer.channel.channel.channel_name,
            message_encoding=logger_config_entities.message_encoding.tachyon,
            schema_name=message_schema.value_key(),
            schema_encoding=logger_config_entities.schema_encoding.clockwork_tachyon,
            schema_definition=list(schema_definition),
            channel_type=log_observer.channel_type,
        )
        if log_observer.log_type in (
            logger_config_entities.log_type.redundant_telemetry,
            logger_config_entities.log_type.non_redundant_telemetry,
        ):
            result.telemetry_config.channels.append(config)
        if log_observer.log_type in (
            logger_config_entities.log_type.redundant_telemetry,
            logger_config_entities.log_type.non_redundant_telemetry,
            logger_config_entities.log_type.event,
        ):
            result.events_config.channels.append(config)
    return result


def gen_logger_configs(sys: system.PhysicalSystem) -> dict[UUID, GeneratedLogWriterConfigs]:
    """Generate logger configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        result[domain_uuid] = _gen_logger_configs_domain(domain)
    return result


def gen_logged_channel_metadata(sys: system.PhysicalSystem) -> model_pb2.LoggedChannelMetadata:
    """Generate logged channel metadata for a system."""
    result = model_pb2.LoggedChannelMetadata()
    for domain in sys.cpu_domains.values():
        ctx = domain.system.system.module.context
        for log_observer in domain.log_observers.values():
            buffer_uuid = log_observer.pinion_buffer
            buffer = (domain.buffers | domain.metrics_buffers)[buffer_uuid]
            if buffer.channel.enforce_backwards_compatibility():
                message_repr = buffer.channel.channel.message_repr
                message_repr_typespec = message_repr.typespec
                # Only Tachyon encoding is supported currently
                assert message_repr_typespec.instantiates.value_key() == "::Tachyon"
                message_schema = message_repr.get_schema()
                schema_metadata = tachyon_metadata.get_metadata(ctx, message_schema)
                result.channel_metadata[buffer.channel.channel.channel_name].CopyFrom(
                    tachyon_metadata.to_protobuf(schema_metadata)
                )
    return result


def _gen_channel_publisher_configs_domain(
    domain: system.PhysicalCpuDomain,
) -> publisher_config_proto.ChannelPublisherConfig:
    ctx = domain.logical.module.context

    result = ChannelPublisherConfig(channels=[])
    # Used to avoid adding duplicate configs for the same channel used by log producers and FirstMessage data sources
    covered_channels: set[str] = set()

    # Add configs for real log producers
    for log_producer, buffer_uuid in domain.log_producers.values():
        buffer = domain.buffers[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        # Only Tachyon encoding is supported currently
        assert message_repr_typespec.instantiates.value_key() == "::Tachyon"
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(ctx, message_schema)
        config = PublishedChannelConfig(
            uuid=buffer_uuid,
            num_slots=buffer.layout.num_slots,
            message_size=buffer.layout.message_size,
            channel_name=log_producer.source_name,
            schema_definition=list(schema_definition),
            module_name=message_repr.schema_ir.schema.module.module_id.repo,
            source_file_name=str(message_repr.schema_ir.schema.module.module_id.get_base_path()),
            class_name=message_repr.schema_ir.schema_name,
            is_published_once=buffer.layout.is_published_once,
        )
        result.channels.append(config)
        covered_channels.add(log_producer.source_name)

    # Add configs for FirstMessage channels not already covered
    for data_source in domain.system.system.data_sources.values():
        if (
            isinstance(data_source, box.FirstMessageInstance)
            and cast("primitive.StringValue", data_source.channel.channel_name).value not in covered_channels
        ):
            message_repr = data_source.channel.message_repr
            if message_repr is None:
                continue
            channel_name = data_source.channel.channel_name
            assert isinstance(channel_name, primitive.StringValue)
            assert message_repr is not None
            result.channels.append(make_unbuffered_published_channel_config(ctx, channel_name.value, message_repr))

    return result


def gen_channel_publisher_configs(
    sys: system.PhysicalSystem,
) -> dict[UUID, publisher_config_proto.ChannelPublisherConfig]:
    """Generate channel publisher configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        # Generate config if domain has log producers OR FirstMessage data sources
        has_first_message = any(
            isinstance(ds, box.FirstMessageInstance) for ds in domain.system.system.data_sources.values()
        )
        if not domain.log_producers and not has_first_message:
            continue
        result[domain_uuid] = _gen_channel_publisher_configs_domain(domain)
    return result
