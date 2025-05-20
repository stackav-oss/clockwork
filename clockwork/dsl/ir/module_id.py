# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Identifiers for clockwork modules."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Final

import root_repo_py


@dataclass(eq=True, frozen=True, order=True)
class ModuleID:
    """Uniquely identifies a clockwork module."""

    repo: str
    name: str

    @classmethod
    def from_path(cls, repo: str, path: Path) -> ModuleID:
        """Construct from a path."""
        parts = path.with_suffix("").parts
        if parts[0] in ["external", ".."]:
            return cls(repo, "::".join(parts[2:]))
        return cls(repo, "::".join(parts))

    @classmethod
    def from_fqn(cls, fqn: str) -> ModuleID:
        """Construct from an fqn."""
        parts = fqn.split("::")
        min_parts: Final = 2
        if len(parts) < min_parts or not parts[0].startswith("@"):
            msg = f"Unable to create a ModuleID from a malformed FQN: {fqn}"
            raise ValueError(msg)
        return cls(parts[0][1:], "::".join(parts[1:]))

    def get_base_path(self) -> Path:
        """Get the base path for the module ID."""
        return Path(*self.name.split("::")).with_suffix(".clk")

    def get_fqn(self) -> str:
        """Convert to a fully qualified name."""
        if self.repo:
            return f"@{self.repo}::{self.name}"
        return self.name


CLK_REPO: Final = "clockwork"
ROOT_REPO: Final = root_repo_py.ROOT_REPO
JEWELS_REPO: Final = "clockwork"
