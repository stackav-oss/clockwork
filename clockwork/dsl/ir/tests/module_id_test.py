# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for module_id library."""

from pathlib import Path

import pytest
from clockwork.dsl.ir.module_id import ModuleID


def test_from_path() -> None:
    module_id = ModuleID.from_path("repo", Path("a/b/c.clk"))
    assert module_id == ModuleID("repo", "a::b::c")


def test_get_base_path() -> None:
    module_id = ModuleID("repo", "a::b::c")
    assert module_id.get_base_path() == Path("a", "b", "c.clk")


def test_with_suffix() -> None:
    module_id = ModuleID("repo", "a::b::c")
    assert module_id.get_base_path() == Path("a", "b", "c.clk")
    assert module_id.with_suffix(".different").get_base_path() == Path("a", "b", "c.different")


def test_fqn() -> None:
    module_id = ModuleID("repo", "a::b::c")
    assert module_id.get_fqn() == "@repo::a::b::c"


def test_from_fqn() -> None:
    with pytest.raises(ValueError, match="Unable to create a ModuleID from a malformed FQN: a"):
        ModuleID.from_fqn("a")
    with pytest.raises(ValueError, match="Unable to create a ModuleID from a malformed FQN: a"):
        ModuleID.from_fqn("a::b::c")

    assert ModuleID.from_fqn("@a::b") == ModuleID("a", "b")
    assert ModuleID.from_fqn("@a::b::c") == ModuleID("a", "b::c")
