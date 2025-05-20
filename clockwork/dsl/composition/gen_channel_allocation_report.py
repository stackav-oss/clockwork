# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate a channel memory allocation for a CPU domain."""

from typing import Any

from clockwork.dsl.composition import system


def gen_channel_allocation_report(domain: system.PhysicalCpuDomain) -> tuple[list[str], list[dict[str, Any]]]:
    """Generate a channel allocation report for a CPU domain."""
    rows: list[dict[str, Any]] = []
    fieldnames = ["channel_name", "num_slots", "message_size", "buffer_size"]
    total_size = 0
    for buffer in domain.buffers.values():
        message_size = 512 * round((buffer.layout.message_size + 511) / 512)
        buffer_size = buffer.layout.num_slots * message_size
        total_size += buffer_size
        rows.append(
            {
                "channel_name": buffer.channel.channel.channel_name,
                "num_slots": buffer.layout.num_slots,
                "message_size": message_size,
                "buffer_size": buffer_size,
            }
        )
    rows.append({"channel_name": "ALL_CHANNELS", "buffer_size": total_size})
    return (fieldnames, rows)
