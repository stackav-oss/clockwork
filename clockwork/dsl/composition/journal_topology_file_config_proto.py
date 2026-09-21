# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Protocol stub for the journal topology file configuration."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.serialization.py.protocol import Tachyon


@dataclass(kw_only=True)
# pyrefly: ignore [implicit-abstract-class] We just want a minimal version of the class for type checking.
class JournalTopologyFileConfig(Tachyon["JournalTopologyFileConfig"]):
    """Rendered journal topology carried in SimpleLaunch data."""

    contents: str
    """Complete JournalTopology protobuf text including its header."""
