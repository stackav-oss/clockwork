# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate ProcessDescriptions from Process IR."""

from __future__ import annotations

from uuid import UUID

from clockwork.dsl.composition import (
    graphir,
    pdf,
    platform_diagnostics_config,
    system,
    tcp_bridge_config,
    tcp_bridge_config_proto,
)
from clockwork.dsl.composition.str_manip import snake_from_camel
from clockwork.dsl.ir import uuid_reg


def gen_bridge_config(physical_system: system.PhysicalSystem) -> dict[UUID, tcp_bridge_config_proto.TcpBridgeConfig]:
    """Generate per-domain bridge configuration files for a system."""
    result = {}
    for domain_uuid, domain in physical_system.cpu_domains.items():
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
                    buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0),
                    num_subscribers=0,
                    channel_name="",
                ),
            ),
            host_name=snake_from_camel(domain.logical.name),
            status_publish_endpoint=pdf.PublishEndpoint(
                process_id=UUID(int=0),
                publisher_id=UUID(int=0),
                buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0),
                num_subscribers=0,
                channel_name="",
            ),
        )
        bridge_process_uuid = uuid_reg.uuid_from_name(f"{domain.logical.value_key()}.__CLOCKWORK_BRIDGE__")
        port_to_channel: dict[int, graphir.Channel] = {}
        for observer in domain.bridge_observers.values():
            source_buffer = domain.buffers[observer.source_pinion_buffer]
            assert source_buffer.uuid == observer.source_pinion_buffer

            remote_domain = physical_system.cpu_domains[observer.dest_domain]
            # All of the following are invariants enforced during system construction
            assert domain.lan_connection is not None
            assert remote_domain.lan_connection is not None
            assert domain.lan_connection.lan is remote_domain.lan_connection.lan

            config.bridge_servers.append(
                tcp_bridge_config.TcpBridgeServerConfig(
                    publisher_id=observer.source_pinion_buffer,
                    buffer_layout=pdf.PinionBufferLayout(
                        num_slots=source_buffer.layout.num_slots, message_size=source_buffer.layout.message_size
                    ),
                    listen_address=domain.lan_connection.address,
                    listen_port=observer.lan_port,
                    num_clients=len(observer.remote_producers),
                    channel_name=source_buffer.channel.channel.channel_name,
                )
            )
            if port_to_channel.get(observer.lan_port) not in (None, source_buffer.channel.channel):
                msg = f"Bridge server port collision on {domain.uuid} ({domain.logical.name}) between {source_buffer.channel.channel} and {port_to_channel[observer.lan_port]} over {observer.lan_port}"
                raise ValueError(msg)

            port_to_channel[observer.lan_port] = source_buffer.channel.channel

        for producer in domain.bridge_producers.values():
            dest_buffer = domain.buffers[producer.dest_pinion_buffer]
            assert domain.lan_connection is not None
            assert remote_domain.lan_connection is not None  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            remote_domain = physical_system.cpu_domains[producer.source_domain]
            remote_observer = remote_domain.bridge_observers[producer.remote_source]
            config.bridge_clients.append(
                tcp_bridge_config.TcpBridgeClientConfig(
                    publisher_endpoint=pdf.PublishEndpoint(
                        process_id=bridge_process_uuid,
                        publisher_id=dest_buffer.uuid,
                        buffer_layout=pdf.PinionBufferLayout(
                            num_slots=dest_buffer.layout.num_slots, message_size=dest_buffer.layout.message_size
                        ),
                        num_subscribers=dest_buffer.num_subscribers,
                        channel_name=dest_buffer.channel.channel.channel_name,
                    ),
                    server_address=remote_domain.lan_connection.address,  # pyright: ignore[reportOptionalMemberAccess] # Linter doesn't know lan_connection is not None
                    server_port=remote_observer.lan_port,
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
                    num_slots=diagnostics_buffer.layout.num_slots, message_size=diagnostics_buffer.layout.message_size
                ),
                num_subscribers=diagnostics_buffer.num_subscribers,
                channel_name=diagnostics_buffer.channel.channel.channel_name,
            )

        if domain.bridge_status_producer:
            bridge_status_producer = domain.platform_bridge_status_producers[domain.bridge_status_producer]
            bridge_status_buffer = domain.buffers[bridge_status_producer.pinion_buffer]
            config.status_publish_endpoint = pdf.PublishEndpoint(
                process_id=bridge_process_uuid,
                publisher_id=bridge_status_buffer.uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=bridge_status_buffer.layout.num_slots,
                    message_size=bridge_status_buffer.layout.message_size,
                ),
                num_subscribers=bridge_status_buffer.num_subscribers,
                channel_name=bridge_status_buffer.channel.channel.channel_name,
            )

        if config.bridge_clients or config.bridge_servers:
            result[domain_uuid] = config

    return result
