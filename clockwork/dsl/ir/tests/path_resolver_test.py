# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for module_id library."""

import os
from pathlib import Path

from clockwork.dsl.ir.module_id import ROOT_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver


def test_bazel_search_paths() -> None:
    paths = list(BazelPathResolver().search_paths(ModuleID("repo", "a::b::c")))
    runfiles_dir = os.getenv("RUNFILES_DIR")
    assert runfiles_dir
    assert paths == [
        Path("a/b/c.clk"),
        Path("external/repo+/a/b/c.clk"),
        Path("../repo+/a/b/c.clk"),
        Path(runfiles_dir) / "_main" / "a/b/c.clk",
        Path(runfiles_dir) / "repo+" / "a/b/c.clk",
    ]

    paths = list(BazelPathResolver().search_paths(ModuleID(ROOT_REPO, "a::b::c")))
    runfiles_dir = os.getenv("RUNFILES_DIR")
    assert runfiles_dir
    assert paths == [
        Path("a/b/c.clk"),
        Path(runfiles_dir) / "_main" / "a/b/c.clk",
    ]


def test_bazel_buildtime_path() -> None:
    path = BazelPathResolver().to_buildtime_path(ModuleID(ROOT_REPO, "a::b::c"))
    assert path == Path("a/b/c.clk")
    path = BazelPathResolver().to_buildtime_path(ModuleID("repo", "a::b::c"))
    assert path == Path("external/repo+/a/b/c.clk")


def test_bazel_runtime_path() -> None:
    path = BazelPathResolver().to_runtime_path(ModuleID(ROOT_REPO, "a::b::c"))
    assert path == Path("a/b/c.clk")
    path = BazelPathResolver().to_runtime_path(ModuleID("repo", "a::b::c"))
    assert path == Path("../repo+/a/b/c.clk")


def test_find_path() -> None:
    module_id = ModuleID("repo", "a::b::c")
    assert not BazelPathResolver().find_path(ModuleID("repo", "a::b::c"))
    module_id.get_base_path().parent.mkdir(parents=True)
    module_id.get_base_path().touch()
    assert BazelPathResolver().find_path(ModuleID("repo", "a::b::c")) == module_id.get_base_path()
