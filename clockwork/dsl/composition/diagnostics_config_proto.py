# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for diagnostics database."""

from dataclasses import dataclass
from uuid import UUID

# pyrefly: ignore[implicit-reexport] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
from clockwork.serialization.py.protocol import Protocol, Tachyon


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ReporterInfo(Tachyon["ReporterInfo"]):
# fmt: on
    """Configuration information for a diagnostic reporter."""

    id: UUID
    name: str
    instance: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class DatabaseInfo(Tachyon["DatabaseInfo"]):
# fmt: on
    """Configuration for the diagnostics system."""

    reporters: list[ReporterInfo]
