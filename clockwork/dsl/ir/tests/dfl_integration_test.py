# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Integration tests for the DFL language.

These tests exercise the full DFL pipeline from parsing through type checking,
demonstrating end-to-end usage patterns.
"""

from dataclasses import dataclass
from pathlib import Path

import pytest
from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl import clockwork_parser as parser
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, compiler, dfl, dfl_analysis, dfl_types, node, primitive, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path
from fltk.fegen.pyrt import terminalsrc
from typing_extensions import override


@dataclass
class _NamedTestEntity(typesys.NamedValue):
    """A simple named entity for testing."""

    @override
    def value_key(self) -> str:
        return f"test::{self.name}"


def _make_test_module(terminals: terminalsrc.TerminalSource, scope: node.Scope | None = None) -> node.Module:
    """Create a minimal test module for IR conversion."""
    if scope is None:
        scope = node.Scope(parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
    module_id = ModuleID("", "testmod")
    return node.Module(
        doc=None,
        module_id=module_id,
        inner_scope=scope,
        terminals=terminals,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
        generates=None,
        inner_attrs=None,
    )


def _parse_dfl_expr(source: str) -> tuple[cst.DflExpr, terminalsrc.TerminalSource]:
    """Parse a DFL expression string."""
    terminals = terminalsrc.TerminalSource(source)
    clk_parser = parser.Parser(terminalsrc=terminals)
    result = clk_parser.apply__parse_dfl_expr(0)
    assert result is not None, f"Parse failed: {source}"
    assert result.pos == len(source), f"Parse incomplete: {source}"
    assert isinstance(result.result, cst.DflExpr)
    return result.result, terminals


def _parse_and_convert(source: str, scope: node.Scope | None = None) -> dfl.Expr:
    """Parse a DFL expression and convert it to IR."""
    cst_expr, terminals = _parse_dfl_expr(source)
    module = _make_test_module(terminals, scope)
    ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
    return dfl.expr_from_cst(cst_expr, ctx, module)


def _make_typed_scope() -> node.Scope:
    """Create a scope with typed variables for testing."""
    scope = node.Scope(parent=None, uniq_path="test", module_id_for_errors=None)
    scope.names["i"] = _NamedTestEntity("i", scope, clkbuiltins.INT64)
    scope.names["j"] = _NamedTestEntity("j", scope, clkbuiltins.INT64)
    scope.names["f"] = _NamedTestEntity("f", scope, clkbuiltins.FLOAT64)
    scope.names["a"] = _NamedTestEntity("a", scope, clkbuiltins.BOOL)
    scope.names["b"] = _NamedTestEntity("b", scope, clkbuiltins.BOOL)
    scope.names["cond"] = _NamedTestEntity("cond", scope, clkbuiltins.BOOL)
    return scope


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


class TestFullPipeline:
    """Tests for the full DFL parse -> IR -> type check pipeline."""

    def test_arithmetic_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with simple arithmetic."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("i + j", scope)

        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert typed_expr.type_info is clkbuiltins.INT64

    def test_comparison_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with comparison operators."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("i > j", scope)

        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert typed_expr.type_info is clkbuiltins.BOOL

    def test_boolean_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with boolean logic."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("a and b", scope)
        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert typed_expr.type_info is clkbuiltins.BOOL

    def test_if_then_else(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with if-then-else."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("if cond then i else j", scope)
        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert typed_expr.type_info is clkbuiltins.INT64

    def test_cond_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with cond expression."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("cond { a => i, b => j, else => i + j }", scope)
        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert typed_expr.type_info is clkbuiltins.INT64

    def test_array_literal_type_checking(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test full pipeline with array literals."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("[i, j]", scope)

        typed_expr = dfl.type_check_expr(expr, std_traits_registry)
        assert isinstance(typed_expr.type_info, dfl_types.CollectionType)
        assert typed_expr.type_info.element_type is clkbuiltins.INT64


class TestErrorHandling:
    """Tests for error handling and error messages."""

    def test_undefined_variable_error(self) -> None:
        """Test that undefined variables produce clear errors."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("undefined_var", scope)

        assert isinstance(expr, dfl.Ref)
        with pytest.raises(dfl.NameValidationError, match="undefined_var"):
            expr.lookup()

    def test_type_mismatch_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test that type mismatches produce clear errors."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("if i then j else j", scope)

        with pytest.raises(dfl.TypeCheckError, match="must be Bool"):
            dfl.type_check_expr(expr, std_traits_registry)

    def test_branch_type_mismatch_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test that mismatched if-then-else branches produce errors."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("if cond then i else f", scope)

        with pytest.raises(dfl.TypeCheckError, match="Type mismatch"):
            dfl.type_check_expr(expr, std_traits_registry)


class TestAnalysisIntegration:
    """Tests for static analysis utilities with real expressions."""

    def test_free_vars_integration(self) -> None:
        """Test free_vars with parsed expressions."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("if i > 0 then j else i + j", scope)

        vars_ = dfl_analysis.free_vars(expr)
        assert vars_ == frozenset({"i", "j"})

    def test_find_calls_integration(self) -> None:
        """Test find_calls with nested function calls."""
        expr = _parse_and_convert("max(a, min(b, c))")
        calls = dfl_analysis.find_calls(expr)

        assert len(calls) == 2
        func_names = {c.func.path[0] for c in calls if isinstance(c.func, dfl.Ref)}
        assert func_names == {"max", "min"}

    def test_substitute_integration(self) -> None:
        """Test substitute with real expressions."""
        scope = _make_typed_scope()

        template = _parse_and_convert("x + y", scope)
        x_val = _parse_and_convert("1", scope)
        y_val = _parse_and_convert("2", scope)

        result = dfl_analysis.substitute(template, {"x": x_val, "y": y_val})

        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert result.left.value == 1


class TestContextConvenienceMethods:
    """Tests for Context convenience methods."""

    def test_for_testing_empty(self) -> None:
        """Test Context.for_testing() with no bindings."""
        ctx = dfl.Context.for_testing()
        assert ctx.scope is not None
        assert ctx.terminals is not None

    def test_for_testing_with_bindings(self) -> None:
        """Test Context.for_testing() with pre-populated bindings."""
        test_scope = node.Scope(parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
        x_entity = _NamedTestEntity("x", test_scope, clkbuiltins.INT64)

        ctx = dfl.Context.for_testing(bindings={"x": x_entity})

        result = ctx.scope.lookup("x")
        assert isinstance(result, _NamedTestEntity)
        assert result.type_info is clkbuiltins.INT64

    def test_bind(self) -> None:
        """Test Context.bind() adds to scope."""
        ctx = dfl.Context.for_testing()
        y_entity = _NamedTestEntity("y", ctx.scope, clkbuiltins.INT64)
        ctx.bind("y", y_entity)

        result = ctx.scope.lookup("y")
        assert isinstance(result, _NamedTestEntity)
        assert result.type_info is clkbuiltins.INT64
