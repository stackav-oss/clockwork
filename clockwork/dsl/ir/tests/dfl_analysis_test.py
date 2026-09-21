# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for DFL static analysis utilities."""

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl import clockwork_parser as parser
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, dfl, dfl_analysis, node, parse, primitive
from clockwork.dsl.ir.module_id import ModuleID
from fltk.fegen.pyrt import terminalsrc


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
    result = parse.parse_rule(clk_parser, "dfl_expr", cst.DflExpr)
    assert result is not None, f"Parse failed: {source}"
    assert result.pos == len(source), f"Parse incomplete: {source}"
    return result.result, terminals


def _parse_and_convert(source: str, scope: node.Scope | None = None) -> dfl.Expr:
    """Parse a DFL expression and convert it to IR."""
    cst_expr, terminals = _parse_dfl_expr(source)
    module = _make_test_module(terminals, scope)
    ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
    return dfl.expr_from_cst(cst_expr, ctx, module)


class TestFreeVars:
    """Tests for the free_vars function."""

    def test_single_ref(self) -> None:
        """Test free_vars with a single reference."""
        expr = _parse_and_convert("x")
        assert dfl_analysis.free_vars(expr) == frozenset({"x"})

    def test_multiple_refs(self) -> None:
        """Test free_vars with multiple references."""
        expr = _parse_and_convert("a + b * c")
        assert dfl_analysis.free_vars(expr) == frozenset({"a", "b", "c"})

    def test_duplicate_refs(self) -> None:
        """Test that duplicate refs are deduplicated."""
        expr = _parse_and_convert("x + x")
        assert dfl_analysis.free_vars(expr) == frozenset({"x"})

    def test_literal_only(self) -> None:
        """Test free_vars with only literals."""
        expr = _parse_and_convert("1 + 2")
        assert dfl_analysis.free_vars(expr) == frozenset()

    def test_nested_expression(self) -> None:
        """Test free_vars in nested expressions."""
        expr = _parse_and_convert("if a > 0 then b else c")
        assert dfl_analysis.free_vars(expr) == frozenset({"a", "b", "c"})

    def test_cond_expression(self) -> None:
        """Test free_vars in cond expressions."""
        expr = _parse_and_convert("cond { x > 0 => y, else => z }")
        assert dfl_analysis.free_vars(expr) == frozenset({"x", "y", "z"})

    def test_array_literal(self) -> None:
        """Test free_vars in array literals."""
        expr = _parse_and_convert("[a, b, c]")
        assert dfl_analysis.free_vars(expr) == frozenset({"a", "b", "c"})

    def test_function_call(self) -> None:
        """Test free_vars in function calls."""
        expr = _parse_and_convert("max(a, b)")
        # "max" is the function name, a and b are arguments
        assert dfl_analysis.free_vars(expr) == frozenset({"max", "a", "b"})


class TestFindCalls:
    """Tests for the find_calls function."""

    def test_single_call(self) -> None:
        """Test finding a single function call."""
        expr = _parse_and_convert("max(a, b)")
        calls = dfl_analysis.find_calls(expr)
        assert len(calls) == 1
        assert isinstance(calls[0], dfl.Call)
        assert isinstance(calls[0].func, dfl.Ref)
        assert calls[0].func.path == ("max",)

    def test_nested_calls(self) -> None:
        """Test finding nested function calls."""
        expr = _parse_and_convert("max(a, min(b, c))")
        calls = dfl_analysis.find_calls(expr)
        assert len(calls) == 2
        func_names = {c.func.path[0] for c in calls if isinstance(c.func, dfl.Ref)}
        assert func_names == {"max", "min"}

    def test_filter_by_name(self) -> None:
        """Test filtering calls by function name."""
        expr = _parse_and_convert("max(a, min(b, c))")
        calls = dfl_analysis.find_calls(expr, {"min"})
        assert len(calls) == 1
        assert isinstance(calls[0].func, dfl.Ref)
        assert calls[0].func.path == ("min",)

    def test_no_calls(self) -> None:
        """Test expression with no calls."""
        expr = _parse_and_convert("a + b")
        calls = dfl_analysis.find_calls(expr)
        assert calls == []

    def test_call_in_conditional(self) -> None:
        """Test finding calls inside conditionals."""
        expr = _parse_and_convert("if x then f(a) else g(b)")
        calls = dfl_analysis.find_calls(expr)
        assert len(calls) == 2
        func_names = {c.func.path[0] for c in calls if isinstance(c.func, dfl.Ref)}
        assert func_names == {"f", "g"}


class TestSubstitute:
    """Tests for the substitute function."""

    def test_substitute_single(self) -> None:
        """Test substituting a single reference."""
        expr = _parse_and_convert("x + 1")
        replacement = _parse_and_convert("5")
        result = dfl_analysis.substitute(expr, {"x": replacement})

        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert result.left.value == 5

    def test_substitute_multiple(self) -> None:
        """Test substituting multiple references."""
        expr = _parse_and_convert("a + b")
        result = dfl_analysis.substitute(
            expr,
            {
                "a": _parse_and_convert("1"),
                "b": _parse_and_convert("2"),
            },
        )

        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert result.left.value == 1
        assert isinstance(result.right, primitive.DecimalLiteral)
        assert result.right.value == 2

    def test_substitute_preserves_unbound(self) -> None:
        """Test that unbound references are preserved."""
        expr = _parse_and_convert("a + b")
        result = dfl_analysis.substitute(expr, {"a": _parse_and_convert("1")})

        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert isinstance(result.right, dfl.Ref)
        assert result.right.path == ("b",)

    def test_substitute_nested(self) -> None:
        """Test substituting in nested expressions."""
        expr = _parse_and_convert("if x then y else z")
        result = dfl_analysis.substitute(
            expr,
            {
                "x": _parse_and_convert("a > 0"),
                "y": _parse_and_convert("1"),
                "z": _parse_and_convert("2"),
            },
        )

        assert isinstance(result, dfl.IfElse)
        assert isinstance(result.test, dfl.Binary)
        assert isinstance(result.then_, primitive.DecimalLiteral)
        assert isinstance(result.else_, primitive.DecimalLiteral)

    def test_substitute_in_array(self) -> None:
        """Test substituting in array literals."""
        expr = _parse_and_convert("[a, b]")
        result = dfl_analysis.substitute(
            expr,
            {
                "a": _parse_and_convert("1"),
                "b": _parse_and_convert("2"),
            },
        )

        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 2
        assert isinstance(result.elements[0], primitive.DecimalLiteral)
        assert isinstance(result.elements[1], primitive.DecimalLiteral)


class TestRefsInExpr:
    """Tests for the refs_in_expr function."""

    def test_finds_all_refs(self) -> None:
        """Test that refs_in_expr finds all Ref nodes."""
        expr = _parse_and_convert("a + b * c")
        refs = dfl_analysis.refs_in_expr(expr)
        assert len(refs) == 3
        paths = {r.path for r in refs}
        assert paths == {("a",), ("b",), ("c",)}

    def test_empty_on_literals(self) -> None:
        """Test that refs_in_expr returns empty list for literals."""
        expr = _parse_and_convert("1 + 2")
        refs = dfl_analysis.refs_in_expr(expr)
        assert refs == []


def test_flatten_blocks() -> None:
    """Test that flatten_blocks() works as expected."""
    block_source = """{
      foo: Tappy<MySchema> {
            max_msgs: 1;
      }
      {
          bar: Tappy<MySchema> {
            max_msgs: 2;
          }
          baz: Tappy<MySchema>;
      }
      {
          {
              upstream_override {
                  {
                      param: value;
                  }
              }
          }
      }
    }
    """
    expr = _parse_and_convert(block_source)
    assert isinstance(expr, dfl.Block)
    assert len(expr.statements) == 3
    assert isinstance(expr.statements[0], dfl.Definition)
    assert isinstance(expr.statements[1], dfl.Block)
    assert isinstance(expr.statements[2], dfl.Block)
    flattened = dfl_analysis.flatten_blocks(expr)
    assert isinstance(flattened, dfl.Block)
    assert len(flattened.statements) == 4

    # Blocks attached to definitions should remain attached to them.
    assert isinstance(flattened.statements[0], dfl.Definition)
    assert flattened.statements[0].name == "foo"
    assert flattened.statements[0].options is not None

    # Statements within anonymous blocks should be absorbed by the parent.
    assert isinstance(flattened.statements[1], dfl.Definition)
    assert flattened.statements[1].name == "bar"
    assert flattened.statements[1].options is not None
    assert isinstance(flattened.statements[2], dfl.Definition)
    assert flattened.statements[2].name == "baz"
    assert flattened.statements[2].options is None

    # Named blocks should be lifted out of anonymous ones. The statements
    # they contain should also be flattened, but remain inside.
    assert isinstance(flattened.statements[3], dfl.Block)
    assert flattened.statements[3].name == "upstream_override"
    assert len(flattened.statements[3].statements) == 1
    assert isinstance(flattened.statements[3].statements[0], dfl.Definition)
    assert flattened.statements[3].statements[0].name == "param"


def test_find_with_pred() -> None:
    """Tests for the find_with_pred function."""
    expr = _parse_and_convert("2 * (a + 3)")
    assert isinstance(expr, dfl.Binary)
    assert isinstance(expr.right, dfl.Binary)

    # Trivially empty.
    results = dfl_analysis.find_with_pred(expr, lambda _: False)
    assert results == []

    # Match everything. Results should come back in depth-first order.
    results = dfl_analysis.find_with_pred(expr, lambda _: True)
    assert results == [expr.left, expr.right.left, expr.right.right, expr.right, expr]

    results = dfl_analysis.find_with_pred(expr, lambda e: isinstance(e, dfl.Binary))
    assert results == [expr.right, expr]

    results = dfl_analysis.find_with_pred(expr, lambda e: isinstance(e, dfl.Value))
    assert results == [expr.left, expr.right.right]

    results = dfl_analysis.find_with_pred(expr, lambda e: isinstance(e, dfl.Ref))
    assert results == [expr.right.left]
