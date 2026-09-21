# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to Platform Diagnostics Configuration schema."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl.composition import pdfproto


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PlatformDiagnosticsConfig(Tachyon["PlatformDiagnosticsConfig"]):
# fmt: on
    """Configuration for a diagnostics producer for a clockwork platform component."""

    reporter_id: UUID
    group_id: str
    instance_id: str
    publish_endpoint: pdfproto.PublishEndpoint
