# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

from dataclasses import dataclass
from typing import Final
from uuid import UUID

from clockwork.dsl.composition import logger_config, logger_config_proto, publisher_config_proto, system
from clockwork.dsl.composition.logger_config import (
    LoggedChannelConfig,
    MessageEncoding,
    SchemaEncoding,
)
from clockwork.dsl.composition.publisher_config import ChannelPublisherConfig, PublishedChannelConfig
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2

_REPRESENTATION_TO_MESSAGE_ENCODING: Final = {"::Tachyon": MessageEncoding.tachyon}


@dataclass(kw_only=True)
class GeneratedLogWriterConfigs:
    """Hold generated LogWriterConfigs."""

    events_config: logger_config_proto.LogWriterConfig
    telemetry_config: logger_config_proto.LogWriterConfig


def _gen_logger_configs_domain(domain: system.PhysicalCpuDomain) -> GeneratedLogWriterConfigs:
    result = GeneratedLogWriterConfigs(
        events_config=logger_config.LogWriterConfig(channels=[]),
        telemetry_config=logger_config.LogWriterConfig(channels=[]),
    )
    for log_observer in domain.log_observers.values():
        buffer_uuid = log_observer.pinion_buffer
        buffer = (domain.buffers | domain.metrics_buffers)[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        message_encoding = _REPRESENTATION_TO_MESSAGE_ENCODING[message_repr_typespec.instantiates.value_key()]
        assert message_encoding == MessageEncoding.tachyon
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(
            domain.system.system.module.context, message_schema
        )
        config = LoggedChannelConfig(
            uuid=buffer_uuid,
            num_slots=buffer.layout.num_slots,
            message_size=buffer.layout.message_size,
            channel_name=buffer.channel.channel.channel_name,
            message_encoding=message_encoding,
            schema_name=message_schema.value_key(),
            schema_encoding=SchemaEncoding.clockwork_tachyon,
            schema_definition=list(schema_definition),
            channel_type=log_observer.channel_type,
        )
        if log_observer.log_type in (logger_config.LogType.redundant_telemetry, logger_config.LogType.telemetry):
            result.telemetry_config.channels.append(config)
        if log_observer.log_type in (
            logger_config.LogType.redundant_telemetry,
            logger_config.LogType.telemetry,
            logger_config.LogType.event,
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
        for log_observer in domain.log_observers.values():
            buffer_uuid = log_observer.pinion_buffer
            buffer = (domain.buffers | domain.metrics_buffers)[buffer_uuid]
            if buffer.channel.enforce_backwards_compatibility():
                message_repr = buffer.channel.channel.message_repr
                message_repr_typespec = message_repr.typespec
                message_encoding = _REPRESENTATION_TO_MESSAGE_ENCODING[message_repr_typespec.instantiates.value_key()]
                assert message_encoding == MessageEncoding.tachyon
                message_schema = message_repr.get_schema()
                schema_metadata = tachyon_metadata.get_metadata(domain.system.system.module.context, message_schema)
                result.channel_metadata[buffer.channel.channel.channel_name].CopyFrom(
                    tachyon_metadata.to_protobuf(schema_metadata)
                )
    return result


def _gen_channel_publisher_configs_domain(
    domain: system.PhysicalCpuDomain,
) -> publisher_config_proto.ChannelPublisherConfig:
    result = ChannelPublisherConfig(channels=[])
    for log_producer, buffer_uuid in domain.log_producers.values():
        buffer = domain.buffers[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        message_encoding = _REPRESENTATION_TO_MESSAGE_ENCODING[message_repr_typespec.instantiates.value_key()]
        assert message_encoding == MessageEncoding.tachyon
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(domain.logical.module.context, message_schema)
        config = PublishedChannelConfig(
            uuid=buffer_uuid,
            num_slots=buffer.layout.num_slots,
            message_size=buffer.layout.message_size,
            channel_name=log_producer.source_name,
            schema_definition=list(schema_definition),
            module_name=message_repr.schema_ir.schema.module.module_id.repo,
            source_file_name=str(message_repr.schema_ir.schema.module.module_id.get_base_path()),
            class_name=message_repr.schema_ir.schema_name,
        )
        result.channels.append(config)
    return result


def gen_channel_publisher_configs(
    sys: system.PhysicalSystem,
) -> dict[UUID, publisher_config_proto.ChannelPublisherConfig]:
    """Generate channel publisher configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        if not domain.log_producers:
            continue
        result[domain_uuid] = _gen_channel_publisher_configs_domain(domain)
    return result
