# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Target types for Clockwork."""

from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from clockwork.dsl.bazel.targets import Label, Target
from clockwork.dsl.ir.module_id import ModuleID


@dataclass
class Clk(Target):
    """A clk target."""

    srcs: Sequence[Path]
    outs: Sequence[Path]
    deps: Sequence[Label]


def assert_path_is_clk(path: Path) -> None:
    """Assert that the path is a proto file."""
    if path.suffix != ".clk":
        msg = f"Unable to convert path with extension '{path.suffix}' to a clk label."
        raise ValueError(msg)


def module_to_clk(current_repo: str, module: ModuleID) -> Label:
    """Convert a clk module to a clk label."""
    clk_path = module.get_base_path()

    if current_repo == module.repo:
        return Label(value=f"//{clk_path.parent}:{clk_path.stem}_clk")
    return Label(value=f"@{module.repo}//{clk_path.parent}:{clk_path.stem}_clk")
