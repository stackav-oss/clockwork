"""Cog unit test helpers."""

# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import os
from pathlib import Path

RESOURCES_DIR = Path(__file__).parent.parent / "resources"
UPDATE_EXPECTATIONS = False


def _get_workspace_root() -> Path:
    """Return the Bazel workspace root (requires ``bazel run``)."""
    if "BUILD_WORKSPACE_DIRECTORY" in os.environ:
        return Path(os.environ["BUILD_WORKSPACE_DIRECTORY"])

    path = Path(__file__).absolute()
    while path != Path("/"):
        if (path / "MODULE.bazel").is_file():
            return path
        path = path.parent

    msg = "Could not find workspace root. Run with 'bazel run' to update expectation files."
    raise RuntimeError(msg)


def load_expected(name: str) -> str:
    """Load expected output from a resource file."""
    filepath = RESOURCES_DIR / f"{name}.txt"
    return filepath.read_text().strip()


def write_expected(name: str, content: str) -> None:
    """Write expected output to the *source* resource file (not the sandbox)."""
    if not UPDATE_EXPECTATIONS:
        return
    workspace_root = _get_workspace_root()
    resources_dir = workspace_root / "clockwork/dsl/cog/tests/resources"
    resources_dir.mkdir(parents=True, exist_ok=True)
    filepath = resources_dir / f"{name}.txt"
    filepath.write_text(content.strip() + "\n")
