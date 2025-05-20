# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

from dataclasses import dataclass
from typing import Final
from uuid import UUID

from clockwork.dsl.composition import logger_config, logger_config_proto, system
from clockwork.dsl.composition.logger_config import (
    LoggedChannelConfig,
    MessageEncoding,
    SchemaEncoding,
)
from clockwork.serialization.metadata import tachyon as tachyon_metadata

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
        buffer = domain.buffers[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        message_encoding = _REPRESENTATION_TO_MESSAGE_ENCODING[message_repr_typespec.instantiates.value_key()]
        assert message_encoding == MessageEncoding.tachyon  # noqa: S101 (invariant)
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
        if log_observer.log_type == logger_config.LogType.telemetry:
            result.telemetry_config.channels.append(config)
        if log_observer.log_type in (logger_config.LogType.event, logger_config.LogType.telemetry):
            result.events_config.channels.append(config)
    return result


def gen_logger_configs(sys: system.PhysicalSystem) -> dict[UUID, GeneratedLogWriterConfigs]:
    """Generate logger configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        result[domain_uuid] = _gen_logger_configs_domain(domain)
    return result


def _gen_log_reader_configs_domain(domain: system.PhysicalCpuDomain) -> logger_config_proto.LogWriterConfig:
    result = logger_config.LogWriterConfig(channels=[])
    for log_producer, buffer_uuid in domain.log_producers.values():
        buffer = domain.buffers[buffer_uuid]
        message_repr = buffer.channel.channel.message_repr
        message_repr_typespec = message_repr.typespec
        message_encoding = _REPRESENTATION_TO_MESSAGE_ENCODING[message_repr_typespec.instantiates.value_key()]
        assert message_encoding == MessageEncoding.tachyon  # noqa: S101 (invariant)
        message_schema = message_repr.get_schema()
        schema_definition = tachyon_metadata.get_serialized_metadata(domain.logical.module.context, message_schema)
        config = LoggedChannelConfig(
            uuid=buffer_uuid,
            num_slots=buffer.layout.num_slots,
            message_size=buffer.layout.message_size,
            channel_name=log_producer.source_name,
            message_encoding=message_encoding,
            schema_name=message_schema.value_key(),
            schema_encoding=SchemaEncoding.clockwork_tachyon,
            schema_definition=list(schema_definition),
            channel_type=logger_config.ChannelType.regular,
        )
        result.channels.append(config)
    return result


def gen_log_reader_configs(sys: system.PhysicalSystem) -> dict[UUID, logger_config_proto.LogWriterConfig]:
    """Generate log reader configs for a system."""
    result = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        if not domain.log_producers:
            continue
        result[domain_uuid] = _gen_log_reader_configs_domain(domain)
    return result
