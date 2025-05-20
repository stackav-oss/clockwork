# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel spy configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
class PublishedChannelMetadata(Tachyon["PublishedChannelMetadata"]):
    """Channel metadata."""

    uuid: UUID
    num_slots: int
    message_size: int
    channel_name: str
    schema_name: str
    schema_definition: list[int]


@dataclass(kw_only=True)
class ChannelSpyConfig(Tachyon["ChannelSpyConfig"]):
    """Channel spy configuration."""

    channels: list[PublishedChannelMetadata]
