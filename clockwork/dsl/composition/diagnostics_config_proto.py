# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to configuration for diagnostics database."""

from dataclasses import dataclass
from uuid import UUID

from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
class ReporterInfo(Tachyon["ReporterInfo"]):
    """Configuration information for a diagnostic reporter."""

    id: UUID
    name: str
    instance: str


@dataclass(kw_only=True)
class DatabaseInfo(Tachyon["DatabaseInfo"]):
    """Configuration for the diagnostics system."""

    reporters: list[ReporterInfo]
