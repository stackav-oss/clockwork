# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""MemoryResource IR node."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl.ir import typesys


@dataclass
class MemoryResource(typesys.NamedAttribute):
    """IR Node representing a pub/sub channel declaration."""
