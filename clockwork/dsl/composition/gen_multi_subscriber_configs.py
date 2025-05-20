# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

from uuid import UUID

from clockwork.dsl.composition import multi_subscriber_config, multi_subscriber_config_proto, pdf, system


def _gen_multi_subscriber_configs_domain(
    domain: system.PhysicalCpuDomain,
) -> dict[str, multi_subscriber_config_proto.MultiSubscriberConfig]:
    result: dict[str, multi_subscriber_config_proto.MultiSubscriberConfig] = {}
    for buffer_uuid, buffer in domain.buffers.items():
        if buffer.channel.is_multi_producer() and buffer.has_local_endpoints():
            channel_str = buffer.channel.channel.ir_node.fqn.lstrip(":").replace("::", ".").replace("@", "")
            if channel_str not in result:
                result[channel_str] = multi_subscriber_config.MultiSubscriberConfig(
                    buffer_layout=pdf.PinionBufferLayout(
                        message_size=buffer.layout.message_size,
                        num_slots=buffer.layout.num_slots,
                    ),
                    publisher_ids=[],
                )
            result[channel_str].publisher_ids.append(buffer_uuid)
    return result


def gen_multi_subscriber_configs(
    sys: system.PhysicalSystem,
) -> dict[UUID, dict[str, multi_subscriber_config_proto.MultiSubscriberConfig]]:
    """Generate configurations for the multi-publisher channel subscribers in a system.

    Returns:
        Dictionary from CPU domain to dictionary from channel ID string to MultiSubscriberConfig.
    """
    result: dict[UUID, dict[str, multi_subscriber_config_proto.MultiSubscriberConfig]] = {}
    for domain_uuid, domain in sys.cpu_domains.items():
        result[domain_uuid] = _gen_multi_subscriber_configs_domain(domain)
    return result
