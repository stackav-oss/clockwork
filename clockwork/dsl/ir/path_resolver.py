# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""File path resolution."""

from __future__ import annotations

import os
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

import runfiles
from clockwork.dsl.ir.module_id import ROOT_REPO, ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable
    from typing import Final


class PathResolver(ABC):
    """Path resolution for Module IDs."""

    @abstractmethod
    def search_paths(self, module_id: ModuleID) -> Iterable[Path]:
        """Enumerate search paths for a module ID."""

    @abstractmethod
    def to_buildtime_path(self, module_id: ModuleID) -> Path:
        """To a buildtime path."""

    @abstractmethod
    def to_runtime_path(self, module_id: ModuleID) -> Path:
        """To a runtime path."""

    @abstractmethod
    def find_path(self, module_id: ModuleID) -> Path | None:
        """Find the path for the module ID."""


_BAZEL_RUNFILES: Final = runfiles.Create()
"""A common instance to use because creating a runfiles manifest is expensive."""


@dataclass(frozen=True)
class BazelPathResolver(PathResolver):
    """File path resolution for Bazel."""

    prefix_paths: list[Path] | None = None

    @override
    def search_paths(self, module_id: ModuleID) -> Iterable[Path]:
        """Enumerate search paths for a module ID."""
        base_path = module_id.get_base_path()
        yield base_path
        if module_id.repo != ROOT_REPO:
            yield Path("external") / f"{module_id.repo}+" / base_path
            yield Path("..") / f"{module_id.repo}+" / base_path
        if (
            _BAZEL_RUNFILES
            and os.getenv("RUNFILES_DIR")
            and (runfiles_path := _BAZEL_RUNFILES.Rlocation(f"_main/{base_path}", source_repo=""))
        ):
            yield Path(runfiles_path)
        if (
            _BAZEL_RUNFILES
            and module_id.repo != ROOT_REPO
            and os.getenv("RUNFILES_DIR")
            and (runfiles_path := _BAZEL_RUNFILES.Rlocation(f"{module_id.repo}+/{base_path}", source_repo=""))
        ):
            yield Path(runfiles_path)

        yield from Path().glob(f"bazel-out/*/bin/{base_path}")
        if module_id.repo != ROOT_REPO:
            yield from Path().glob(f"bazel-out/*/bin/external/{module_id.repo}+/{base_path}")

        if self.prefix_paths:
            for prefix in self.prefix_paths:
                yield prefix / base_path

    @override
    def to_buildtime_path(self, module_id: ModuleID) -> Path:
        """To a buildtime path."""
        if module_id.repo != ROOT_REPO:
            return Path("external", f"{module_id.repo}+", *module_id.get_base_path().parts)
        return module_id.get_base_path()

    @override
    def to_runtime_path(self, module_id: ModuleID) -> Path:
        """To a runtime path."""
        if module_id.repo != ROOT_REPO:
            return Path("../", f"{module_id.repo}+", *module_id.get_base_path().parts)
        return module_id.get_base_path()

    @override
    def find_path(self, module_id: ModuleID) -> Path | None:
        """Find the path for the module."""
        for path in self.search_paths(module_id):
            if path.is_file():
                return path
        return None
