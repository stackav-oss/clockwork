# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for subscribers to multi-publisher channels."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.dsl.composition.pdfproto import PinionBufferLayout
from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MultiSubscriberConfig(Tachyon["MultiSubscriberConfig"]):
# fmt: on
    """Configuration for a subscriber to a multi-publisher channel."""

    buffer_layout: PinionBufferLayout
    publisher_ids: list[UUID]
