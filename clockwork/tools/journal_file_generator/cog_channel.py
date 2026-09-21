# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Cog input, output, and snapshot channel metadata."""

from __future__ import annotations

from dataclasses import dataclass
from typing import final


@final
@dataclass(frozen=True, kw_only=True)
class CogChannelRef:
    """Mapping from a cog input or output member to the connected channel name."""

    cog_member_name: str
    """Cog input or output member name used in generated metric field names."""

    channel_name: str
    """Concrete Clockwork channel name stored in journal records."""


@final
@dataclass(frozen=True, kw_only=True)
class DiscoveredSnapshotChannelRef:
    """Mapping from a cog state member to a snapshot channel."""

    cog_member_name: str
    """Cog state member name."""

    channel_name: str
    """Concrete Clockwork snapshot channel name."""


@final
@dataclass(frozen=True, kw_only=True)
class CogChannelMap:
    """Input, output, and snapshot channel refs for one runtime cog instance."""

    cog_instance_path: str
    """Fully resolved runtime cog instance path."""

    input_channels: tuple[CogChannelRef, ...]
    """Cog input member names and their connected channel names."""

    output_channels: tuple[CogChannelRef, ...]
    """Cog output member names and their connected channel names."""

    has_clockwork_state: bool = False
    """Whether the cog has at least one Clockwork state endpoint."""

    snapshot_channels: tuple[DiscoveredSnapshotChannelRef, ...] = ()
    """Cog state member names and their snapshot channel names."""
