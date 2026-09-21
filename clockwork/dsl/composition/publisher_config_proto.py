# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel publisher configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PublishedChannelConfig(Tachyon["PublishedChannelConfig"]):
# fmt: on
    """Published channel configuration."""

    uuid: UUID
    num_slots: int
    message_size: int
    channel_name: str
    schema_definition: list[int]
    module_name: str
    source_file_name: str
    class_name: str
    is_published_once: bool


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ChannelPublisherConfig(Tachyon["ChannelPublisherConfig"]):
# fmt: on
    """Channel publisher configuration."""

    channels: list[PublishedChannelConfig]
