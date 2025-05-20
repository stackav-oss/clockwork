# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for subscribers to multi-publisher channels."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.dsl.composition.pdfproto import PinionBufferLayout
from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
class MultiSubscriberConfig(Tachyon["MultiSubscriberConfig"]):
    """Configuration for a subscriber to a multi-publisher channel."""

    buffer_layout: PinionBufferLayout
    publisher_ids: list[UUID]
