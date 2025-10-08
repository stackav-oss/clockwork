# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel publisher configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
class PublishedChannelConfig(Tachyon["PublishedChannelConfig"]):
    """Published channel configuration."""

    uuid: UUID
    num_slots: int
    message_size: int
    channel_name: str
    schema_definition: list[int]
    module_name: str
    source_file_name: str
    class_name: str


@dataclass(kw_only=True)
class ChannelPublisherConfig(Tachyon["ChannelPublisherConfig"]):
    """Channel publisher configuration."""

    channels: list[PublishedChannelConfig]
