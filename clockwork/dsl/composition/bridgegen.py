# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate ProcessDescriptions from Process IR."""

from __future__ import annotations

from uuid import UUID
from clockwork.dsl.composition import (
    graphir,
    logger_config,
    pdf,
    platform_diagnostics_config,
    system,
    tcp_bridge_config,
    tcp_bridge_config_proto,
)
from clockwork.dsl.composition.channel_config import ChannelType
from clockwork.dsl.composition.str_manip import snake_from_camel
from clockwork.dsl.ir import uuid_reg
from clockwork.serialization.metadata import tachyon as tachyon_metadata


def gen_tcp_bridge_config(
    physical_system: system.PhysicalSystem,
) -> dict[UUID, tcp_bridge_config_proto.TcpBridgeConfig]:
    """Generate per-domain bridge configuration files for a system."""
    result = {}
    for domain_uuid, domain in physical_system.cpu_domains.items():
        ctx = domain.system.system.module.context
        logger_config_entities = logger_config.get_entities(ctx)
        config = tcp_bridge_config.TcpBridgeConfig(
            bridge_clients=[],
            bridge_servers=[],
            diagnostics_config=platform_diagnostics_config.PlatformDiagnosticsConfig(
                reporter_id=UUID(int=0),
                group_id="",
                instance_id="",
                publish_endpoint=pdf.PublishEndpoint(
                    process_id=UUID(int=0),
                    publisher_id=UUID(int=0),
                    buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0, is_published_once=False),
                    num_subscribers=0,
                    channel_name="",
                    is_bulk_data=False,
                    channel_type=ChannelType.unspecified,
                ),
            ),
            host_name=snake_from_camel(domain.logical.name),
            status_publish_endpoint=pdf.PublishEndpoint(
                process_id=UUID(int=0),
                publisher_id=UUID(int=0),
                buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0, is_published_once=False),
                num_subscribers=0,
                channel_name="",
                is_bulk_data=False,
                channel_type=ChannelType.unspecified,
            ),
        )
        bridge_process_uuid = uuid_reg.uuid_from_name(f"{domain.logical.value_key()}.__CLOCKWORK_BRIDGE__")
        port_to_channel: dict[int, graphir.Channel] = {}
        for observer_uuid, observer in domain.bridge_observers.items():
            if not isinstance(observer, system.TcpBridgeObserver):
                continue

            source_buffer = domain.buffers[observer.source_pinion_buffer]
            assert source_buffer.uuid == observer.source_pinion_buffer

            remote_domain = physical_system.cpu_domains[observer.dest_domain]
            # All of the following are invariants enforced during system construction
            assert domain.lan_connection is not None
            assert remote_domain.lan_connection is not None
            assert domain.lan_connection.lan is remote_domain.lan_connection.lan

            message_repr = source_buffer.channel.channel.message_repr
            message_repr_typespec = message_repr.typespec
            # Only Tachyon encoding is supported currently
            assert message_repr_typespec.instantiates.value_key() == "::Tachyon"
            message_schema = message_repr.get_schema()
            schema_definition = tachyon_metadata.get_serialized_metadata(ctx, message_schema)

            config.bridge_servers.append(
                tcp_bridge_config.TcpBridgeServerConfig(
                    publisher_id=observer.source_pinion_buffer,
                    buffer_layout=pdf.PinionBufferLayout(
                        num_slots=source_buffer.layout.num_slots,
                        message_size=source_buffer.layout.message_size,
                        is_published_once=source_buffer.layout.is_published_once,
                        max_msgs_per_exec=source_buffer.layout.max_msgs_per_exec,
                    ),
                    listen_address=domain.lan_connection.address,
                    listen_port=observer.lan_port,
                    num_clients=len(observer.remote_producers),
                    channel_name=source_buffer.channel.channel.channel_name,
                    is_bulk_data=source_buffer.channel.is_bulk_data(),
                    schema_encoding=logger_config_entities.schema_encoding.clockwork_tachyon,
                    schema_definition=list(schema_definition),
                    channel_type=source_buffer.channel.channel_type(),
                    subscriber_key=domain.channel_link_keys[observer_uuid][1],
                )
            )
            if port_to_channel.get(observer.lan_port) not in (None, source_buffer.channel.channel):
                msg = f"Bridge server port collision on {domain.uuid} ({domain.logical.name}) between {source_buffer.channel.channel} and {port_to_channel[observer.lan_port]} over {observer.lan_port}"
                raise ValueError(msg)

            port_to_channel[observer.lan_port] = source_buffer.channel.channel

        for producer in domain.bridge_producers.values():
            if not isinstance(producer, system.TcpBridgeProducer):
                continue

            dest_buffer = domain.buffers[producer.dest_pinion_buffer]
            assert domain.lan_connection is not None
            assert remote_domain.lan_connection is not None  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            remote_domain = physical_system.cpu_domains[producer.source_domain]
            remote_observer = remote_domain.bridge_observers[producer.remote_source]
            assert isinstance(remote_observer, system.TcpBridgeObserver)
            publisher_keys = {
                key for key in (domain.channel_link_keys[link][0] for link in dest_buffer.observers) if key
            }
            config.bridge_clients.append(
                tcp_bridge_config.TcpBridgeClientConfig(
                    publisher_endpoint=pdf.PublishEndpoint(
                        process_id=bridge_process_uuid,
                        publisher_id=dest_buffer.uuid,
                        buffer_layout=pdf.PinionBufferLayout(
                            num_slots=dest_buffer.layout.num_slots,
                            message_size=dest_buffer.layout.message_size,
                            is_published_once=dest_buffer.layout.is_published_once,
                            max_msgs_per_exec=dest_buffer.layout.max_msgs_per_exec,
                        ),
                        num_subscribers=dest_buffer.num_subscribers,
                        channel_name=dest_buffer.channel.channel.channel_name,
                        is_bulk_data=dest_buffer.channel.is_bulk_data(),
                        channel_type=dest_buffer.channel.channel_type(),
                    ),
                    server_address=remote_domain.lan_connection.address,  # pyright: ignore[reportOptionalMemberAccess] # Linter doesn't know lan_connection is not None
                    server_port=remote_observer.lan_port,
                    publisher_keys=list(publisher_keys),
                )
            )

        if domain.bridge_diagnostics_producer:
            diagnostics_producer = domain.platform_diagnostics_producers[domain.bridge_diagnostics_producer]
            diagnostics_buffer = domain.buffers[diagnostics_producer.pinion_buffer]
            config.diagnostics_config.reporter_id = diagnostics_producer.uuid
            config.diagnostics_config.group_id = diagnostics_producer.group_id
            config.diagnostics_config.instance_id = diagnostics_producer.instance_id
            config.diagnostics_config.publish_endpoint = pdf.PublishEndpoint(
                process_id=bridge_process_uuid,
                publisher_id=diagnostics_buffer.uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=diagnostics_buffer.layout.num_slots,
                    message_size=diagnostics_buffer.layout.message_size,
                    is_published_once=diagnostics_buffer.layout.is_published_once,
                    max_msgs_per_exec=diagnostics_buffer.layout.max_msgs_per_exec,
                ),
                num_subscribers=diagnostics_buffer.num_subscribers,
                channel_name=diagnostics_buffer.channel.channel.channel_name,
                is_bulk_data=diagnostics_buffer.channel.is_bulk_data(),
                channel_type=diagnostics_buffer.channel.channel_type(),
            )

        if domain.bridge_status_producer:
            bridge_status_producer = domain.platform_status_producers[domain.bridge_status_producer]
            bridge_status_buffer = domain.buffers[bridge_status_producer.pinion_buffer]
            config.status_publish_endpoint = pdf.PublishEndpoint(
                process_id=bridge_process_uuid,
                publisher_id=bridge_status_buffer.uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=bridge_status_buffer.layout.num_slots,
                    message_size=bridge_status_buffer.layout.message_size,
                    is_published_once=bridge_status_buffer.layout.is_published_once,
                    max_msgs_per_exec=bridge_status_buffer.layout.max_msgs_per_exec,
                ),
                num_subscribers=bridge_status_buffer.num_subscribers,
                channel_name=bridge_status_buffer.channel.channel.channel_name,
                is_bulk_data=bridge_status_buffer.channel.is_bulk_data(),
                channel_type=bridge_status_buffer.channel.channel_type(),
            )

        if config.bridge_clients or config.bridge_servers:
            result[domain_uuid] = config

    return result
