# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to channel spy configurations."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PublishedChannelMetadata(Tachyon["PublishedChannelMetadata"]):
# fmt: on
    """Channel metadata."""

    uuid: UUID
    num_slots: int
    message_size: int
    channel_name: str
    schema_name: str
    schema_definition: list[int]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ChannelSpyConfig(Tachyon["ChannelSpyConfig"]):
# fmt: on
    """Channel spy configuration."""

    channels: list[PublishedChannelMetadata]
