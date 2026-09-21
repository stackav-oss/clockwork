# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to TCP Bridge Configuration schemas."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.serialization.py.protocol import Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SystemMetadata(Tachyon["SystemMetadata"]):
# fmt: on
    """Metadata about the clockwork system."""

    system_target_name: str
