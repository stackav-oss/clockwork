# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for DFL trait system using real std/traits.clk."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, dfl_types
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture(scope="module")
def std_traits_registry(fs_importer: FilesystemImporter) -> dfl_types.TraitRegistry:
    """Load std/traits.clk and create a registry from its contents."""
    traits_path = fix_clockwork_path(Path("std/traits.clk"))
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, traits_path),
        fs_importer,
    )
    return dfl_types.get_trait_registry(module)


# ---------------------------------------------------------------------------
# Trait Definition Tests
# ---------------------------------------------------------------------------


def test_std_traits_add_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Add trait is defined in std/traits.clk."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None
    assert add.name == "Add"
    assert len(add.type_params) == 1
    assert add.type_params[0].name == "Rhs"
    assert add.type_params[0].default_is_self is True
    assert "Output" in add.associated_types


def test_std_traits_sub_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Sub trait is defined in std/traits.clk."""
    sub = std_traits_registry.get_trait("@clockwork::std::traits.Sub")
    assert sub is not None
    assert sub.name == "Sub"
    assert len(sub.type_params) == 1
    assert "Output" in sub.associated_types


def test_std_traits_mul_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Mul trait is defined in std/traits.clk."""
    mul = std_traits_registry.get_trait("@clockwork::std::traits.Mul")
    assert mul is not None
    assert mul.name == "Mul"


def test_std_traits_div_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Div trait is defined in std/traits.clk."""
    div = std_traits_registry.get_trait("@clockwork::std::traits.Div")
    assert div is not None
    assert div.name == "Div"


def test_std_traits_neg_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Neg trait is defined (unary, no type params)."""
    neg = std_traits_registry.get_trait("@clockwork::std::traits.Neg")
    assert neg is not None
    assert neg.name == "Neg"
    assert neg.type_params == ()
    assert "Output" in neg.associated_types


def test_std_traits_ord_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Ord trait is defined with Output type."""
    ord_trait = std_traits_registry.get_trait("@clockwork::std::traits.Ord")
    assert ord_trait is not None
    assert ord_trait.name == "Ord"
    assert len(ord_trait.type_params) == 1
    # Ord now has Output (returns Bool for comparisons)
    assert "Output" in ord_trait.associated_types


def test_std_traits_eq_defined(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that Eq trait is defined with Output type."""
    eq = std_traits_registry.get_trait("@clockwork::std::traits.Eq")
    assert eq is not None
    assert eq.name == "Eq"
    # Eq now has Output (returns Bool for comparisons)
    assert "Output" in eq.associated_types


# ---------------------------------------------------------------------------
# Tests for Standard Trait Implementations
# ---------------------------------------------------------------------------


def test_std_int64_add(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Int64 + Int64 -> Int64 is implemented."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None
    impl = std_traits_registry.find_impl(add, clkbuiltins.INT64, clkbuiltins.INT64)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.INT64


def test_std_int64_sub(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Int64 - Int64 -> Int64 is implemented."""
    sub = std_traits_registry.get_trait("@clockwork::std::traits.Sub")
    assert sub is not None
    impl = std_traits_registry.find_impl(sub, clkbuiltins.INT64, clkbuiltins.INT64)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.INT64


def test_std_float64_add(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Float64 + Float64 -> Float64 is implemented."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None
    impl = std_traits_registry.find_impl(add, clkbuiltins.FLOAT64, clkbuiltins.FLOAT64)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.FLOAT64


def test_std_duration_add(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Duration + Duration -> Duration is implemented."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None
    impl = std_traits_registry.find_impl(add, clkbuiltins.DURATION, clkbuiltins.DURATION)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.DURATION


def test_std_duration_mul_float64(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Duration * Float64 -> Duration is implemented."""
    mul = std_traits_registry.get_trait("@clockwork::std::traits.Mul")
    assert mul is not None
    impl = std_traits_registry.find_impl(mul, clkbuiltins.DURATION, clkbuiltins.FLOAT64)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.DURATION


def test_std_duration_div_duration(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Duration / Duration -> Float64 is implemented."""
    div = std_traits_registry.get_trait("@clockwork::std::traits.Div")
    assert div is not None
    impl = std_traits_registry.find_impl(div, clkbuiltins.DURATION, clkbuiltins.DURATION)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.FLOAT64


def test_std_synctime_sub_synctime(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test SyncTime - SyncTime -> Duration is implemented."""
    sub = std_traits_registry.get_trait("@clockwork::std::traits.Sub")
    assert sub is not None
    impl = std_traits_registry.find_impl(sub, clkbuiltins.SYNC_TIME, clkbuiltins.SYNC_TIME)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.DURATION


def test_std_synctime_add_duration(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test SyncTime + Duration -> SyncTime is implemented."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None
    impl = std_traits_registry.find_impl(add, clkbuiltins.SYNC_TIME, clkbuiltins.DURATION)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.SYNC_TIME


def test_std_bool_not(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test not Bool -> Bool is implemented."""
    not_trait = std_traits_registry.get_trait("@clockwork::std::traits.Not")
    assert not_trait is not None
    impl = std_traits_registry.find_impl(not_trait, clkbuiltins.BOOL, None)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.BOOL


def test_std_bool_and(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Bool and Bool -> Bool is implemented."""
    and_trait = std_traits_registry.get_trait("@clockwork::std::traits.And")
    assert and_trait is not None
    impl = std_traits_registry.find_impl(and_trait, clkbuiltins.BOOL, clkbuiltins.BOOL)
    assert impl is not None
    assert std_traits_registry.output_type(impl) is clkbuiltins.BOOL


def test_std_int64_ord(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test Int64 comparison is implemented (Ord trait)."""
    ord_trait = std_traits_registry.get_trait("@clockwork::std::traits.Ord")
    assert ord_trait is not None
    impl = std_traits_registry.find_impl(ord_trait, clkbuiltins.INT64, clkbuiltins.INT64)
    assert impl is not None
    # Ord now has Output = Bool
    assert std_traits_registry.output_type(impl) is clkbuiltins.BOOL


def test_std_no_impl_for_invalid_op(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that invalid operations have no impl."""
    add = std_traits_registry.get_trait("@clockwork::std::traits.Add")
    assert add is not None

    # Int64 + Bool is not valid
    impl = std_traits_registry.find_impl(add, clkbuiltins.INT64, clkbuiltins.BOOL)
    assert impl is None

    # Bool + Int64 is not valid
    impl = std_traits_registry.find_impl(add, clkbuiltins.BOOL, clkbuiltins.INT64)
    assert impl is None
