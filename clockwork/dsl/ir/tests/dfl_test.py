# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for DFL IR module."""

from dataclasses import dataclass
from pathlib import Path
from typing import TypeVar

import pytest
from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl import clockwork_parser as parser
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, clkenum, compiler, dfl, dfl_types, node, primitive, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.tests.support.py_test_utils import fix_clockwork_path
from fltk.fegen.pyrt import errors, terminalsrc
from typing_extensions import override

# ---------------------------------------------------------------------------
# Parsing Helpers
# ---------------------------------------------------------------------------


def _make_test_module(terminals: terminalsrc.TerminalSource, scope: node.Scope | None = None) -> node.Module:
    """Create a minimal test module for IR conversion."""
    if scope is None:
        scope = node.Scope(parent=dfl.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
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


CstType = TypeVar("CstType")


def _parse(source: str, rule_name: str, cst_type: type[CstType]) -> tuple[CstType, terminalsrc.TerminalSource]:
    """Parse source using the specified parser rule.

    Args:
        source: The source string to parse.
        rule_name: The parser rule name (e.g., 'dfl_expr', 'dfl_pattern').
        cst_type: The expected type of the parsed CST node.

    Returns:
        Tuple of (parsed CST node, terminal source).

    Raises:
        AssertionError: If parsing fails.
    """
    terminals = terminalsrc.TerminalSource(source)
    clk_parser = parser.Parser(terminalsrc=terminals)
    parse_method = getattr(clk_parser, f"apply__parse_{rule_name}")
    result = parse_method(0)

    expected_length = len(source)
    if not result or result.pos != expected_length:
        longest_pos = clk_parser.error_tracker.longest_parse_len
        error_linecol = terminals.pos_to_line_col(longest_pos)
        msg = f"Parse failed at position {longest_pos} (line {error_linecol.line + 1}, col {error_linecol.col + 1})\n"
        msg += f"Input: {source!r}\n"
        msg += f"Parsed up to position: {result.pos if result else 0} of {expected_length}\n"
        msg += f"Longest parse reached: {longest_pos}\n"
        msg += f"Text at longest parse: {terminals.terminals[longest_pos : longest_pos + 20]!r}\n"
        msg += errors.format_error_message(
            clk_parser.error_tracker,
            terminals,
            lambda rule_id: clk_parser.rule_names[rule_id],
        )
        raise AssertionError(msg)

    assert isinstance(result.result, cst_type), f"Expected CST type {cst_type}, got {type(result.result)}"
    return result.result, terminals


def _parse_dfl_expr(source: str) -> tuple[cst.DflExpr, terminalsrc.TerminalSource]:
    """Parse a DFL expression string."""
    return _parse(source, "dfl_expr", cst.DflExpr)


def _parse_and_convert(source: str, scope: node.Scope | None = None) -> dfl.Expr:
    """Parse a DFL expression and convert it to IR."""
    cst_expr, terminals = _parse_dfl_expr(source)
    module = _make_test_module(terminals, scope)
    ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
    return dfl.expr_from_cst(cst_expr, ctx, module)


def _compile_fn_def(fn_source: str, fn_name: str) -> tuple[dfl.FnDef, node.Module]:
    """Compile a function definition and return the FnDef and module."""
    fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
    module = compiler.compile_source_text(fn_source, ModuleID(CLK_REPO, "test_fn"), fs_importer)

    module.inner_scope.parent = dfl.BUILTINS_SCOPE

    fn_def = module.inner_scope.lookup(fn_name)
    assert isinstance(fn_def, dfl.FnDef), f"Expected FnDef, got {type(fn_def)}"
    return fn_def, module


def test_parse_literals() -> None:
    """Test parsing various literal types."""
    expr = _parse_and_convert("42")
    assert isinstance(expr, primitive.DecimalLiteral)

    expr = _parse_and_convert("3.14")
    assert isinstance(expr, primitive.DecimalLiteral)

    expr = _parse_and_convert('"hello"')
    assert isinstance(expr, primitive.StringLiteral)
    assert expr.value == "hello"

    expr = _parse_and_convert("100ms")
    assert isinstance(expr, primitive.UnitLiteral)


def test_parse_identifier() -> None:
    """Test parsing a simple identifier."""
    expr = _parse_and_convert("foo")
    assert isinstance(expr, dfl.Ref)
    assert expr.path == ("foo",)
    assert not expr.is_namespaced


def test_ref_namespaced_identifier() -> None:
    """Test Ref.from_namespaced_identifier creates correct path."""
    cst_node, terminals, module = _parse_pattern("Status::active")
    simple_pat = cst_node.maybe_dfl_simple_pattern()
    assert simple_pat is not None
    ns_id_cst = simple_pat.child_namespaced_identifier()
    assert ns_id_cst is not None

    ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
    ref = dfl.Ref.from_namespaced_identifier(ns_id_cst, ctx)

    assert ref.path == ("Status", "active")
    assert ref.is_namespaced


def _check_left_right_a_b(expr: dfl.Binary) -> None:
    """Check that *expr* is a binary operation with left and right refs 'a' and 'b'."""
    assert isinstance(expr.left, dfl.Ref)
    assert expr.left.path == ("a",)
    assert isinstance(expr.right, dfl.Ref)
    assert expr.right.path == ("b",)


def test_parse_binary_operators() -> None:
    """Test parsing binary operators with correct precedence."""
    expr = _parse_and_convert("a + b")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.ADD
    _check_left_right_a_b(expr)

    # Precedence: a + b * c -> a + (b * c)
    expr = _parse_and_convert("a + b * c")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.ADD
    assert isinstance(expr.left, dfl.Ref)
    assert expr.left.path == ("a",)
    assert isinstance(expr.right, dfl.Binary)
    assert expr.right.op == dfl.BinaryOp.MUL
    assert isinstance(expr.right.left, dfl.Ref)
    assert expr.right.left.path == ("b",)
    assert isinstance(expr.right.right, dfl.Ref)
    assert expr.right.right.path == ("c",)


def test_parse_comparison_operators() -> None:
    """Test parsing all comparison operators."""
    ops = [
        ("a == b", dfl.BinaryOp.EQ),
        ("a != b", dfl.BinaryOp.NE),
        ("a < b", dfl.BinaryOp.LT),
        ("a <= b", dfl.BinaryOp.LE),
        ("a > b", dfl.BinaryOp.GT),
        ("a >= b", dfl.BinaryOp.GE),
    ]
    for source, expected_op in ops:
        expr = _parse_and_convert(source)
        assert isinstance(expr, dfl.Binary), f"Failed for {source}"
        assert expr.op == expected_op, f"Expected {expected_op} for {source}"
        _check_left_right_a_b(expr)


def test_parse_boolean_operators() -> None:
    """Test parsing boolean operators with correct precedence."""
    expr = _parse_and_convert("a and b")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.AND
    _check_left_right_a_b(expr)

    expr = _parse_and_convert("a or b")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.OR
    _check_left_right_a_b(expr)

    # Precedence: a or b and c -> a or (b and c)
    expr = _parse_and_convert("a or b and c")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.OR
    assert isinstance(expr.left, dfl.Ref)
    assert expr.left.path == ("a",)
    assert isinstance(expr.right, dfl.Binary)
    assert expr.right.op == dfl.BinaryOp.AND
    assert isinstance(expr.right.left, dfl.Ref)
    assert expr.right.left.path == ("b",)
    assert isinstance(expr.right.right, dfl.Ref)
    assert expr.right.right.path == ("c",)

    expr = _parse_and_convert("not x")
    assert isinstance(expr, dfl.Unary)
    assert expr.op == dfl.UnaryOp.NOT


def test_parse_unary_operators() -> None:
    """Test parsing unary operators."""
    expr = _parse_and_convert("-x")
    assert isinstance(expr, dfl.Unary)
    assert expr.op == dfl.UnaryOp.NEG

    expr = _parse_and_convert("+x")
    assert isinstance(expr, dfl.Unary)
    assert expr.op == dfl.UnaryOp.POS

    expr = _parse_and_convert("|x|")
    assert isinstance(expr, dfl.Unary)
    assert expr.op == dfl.UnaryOp.ABS


def test_parse_parentheses() -> None:
    """Test parsing parenthesized expressions."""
    expr = _parse_and_convert("(a + b) * c")
    assert isinstance(expr, dfl.Binary)
    assert expr.op == dfl.BinaryOp.MUL
    assert isinstance(expr.left, dfl.Binary)
    assert expr.left.op == dfl.BinaryOp.ADD
    _check_left_right_a_b(expr.left)


def test_parse_member_access() -> None:
    """Test parsing member access expressions."""
    expr = _parse_and_convert("msg.header")
    assert isinstance(expr, dfl.Member)
    assert expr.field_name == "header"
    assert isinstance(expr.base, dfl.Ref)
    assert expr.base.path == ("msg",)

    expr = _parse_and_convert("msg.header.timestamp")
    assert isinstance(expr, dfl.Member)
    assert expr.field_name == "timestamp"
    assert isinstance(expr.base, dfl.Member)
    assert expr.base.field_name == "header"
    assert isinstance(expr.base.base, dfl.Ref)
    assert expr.base.base.path == ("msg",)


def test_parse_if_else() -> None:
    """Test parsing if-then-else expressions."""
    expr = _parse_and_convert("if x > 0 then x else -x")
    assert isinstance(expr, dfl.IfElse)
    assert isinstance(expr.test, dfl.Binary)
    assert expr.test.op == dfl.BinaryOp.GT
    assert isinstance(expr.then_, dfl.Ref)
    assert expr.then_.path == ("x",)
    assert isinstance(expr.else_, dfl.Unary)
    assert expr.else_.op == dfl.UnaryOp.NEG


def test_parse_cond_expr() -> None:
    """Test parsing cond expressions."""
    expr = _parse_and_convert("cond { x < 0 => -1, x > 0 => 1, else => 0 }")
    assert isinstance(expr, dfl.CondExpr)
    assert len(expr.arms) == 3

    # First arm: x < 0 => -1
    assert expr.arms[0].guard is not None
    assert isinstance(expr.arms[0].guard, dfl.Binary)
    assert expr.arms[0].guard.op == dfl.BinaryOp.LT

    # Second arm: x > 0 => 1
    assert expr.arms[1].guard is not None
    assert isinstance(expr.arms[1].guard, dfl.Binary)
    assert expr.arms[1].guard.op == dfl.BinaryOp.GT

    # Third arm: else => 0 (guard is None)
    assert expr.arms[2].guard is None


@dataclass
class _NamedTestEntity(typesys.NamedValue):
    """A simple named entity for testing."""

    @override
    def value_key(self) -> str:
        return "test"


def test_validate_names_defined() -> None:
    """Test validate_names passes when all names are defined."""
    scope = node.Scope(parent=None, uniq_path="test", module_id_for_errors=None)
    scope.names["a"] = _NamedTestEntity("a", scope, clkbuiltins.INT64)
    scope.names["b"] = _NamedTestEntity("b", scope, clkbuiltins.INT64)

    expr = _parse_and_convert("a + b", scope)
    dfl.validate_names(expr)  # Should not raise


def test_validate_names_undefined() -> None:
    """Test validate_names raises error for undefined names."""
    expr = _parse_and_convert("undefined_var")
    with pytest.raises(dfl.NameValidationError, match="undefined_var"):
        dfl.validate_names(expr)


def test_find_refs() -> None:
    """Test find_refs extracts all variable references."""
    expr = _parse_and_convert("a + b * c")
    refs = dfl.find_refs(expr)
    names = {r.path for r in refs}
    assert names == {("a",), ("b",), ("c",)}


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


def _make_typed_scope() -> node.Scope:
    """Create a scope with typed variables for type checking tests."""
    scope = node.Scope(parent=dfl.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
    scope.names["i"] = _NamedTestEntity("i", scope, clkbuiltins.INT64)
    scope.names["j"] = _NamedTestEntity("j", scope, clkbuiltins.INT64)
    scope.names["f"] = _NamedTestEntity("f", scope, clkbuiltins.FLOAT64)
    scope.names["g"] = _NamedTestEntity("g", scope, clkbuiltins.FLOAT64)
    scope.names["b"] = _NamedTestEntity("b", scope, clkbuiltins.BOOL)
    scope.names["c"] = _NamedTestEntity("c", scope, clkbuiltins.BOOL)
    scope.names["d"] = _NamedTestEntity("d", scope, clkbuiltins.DURATION)
    scope.names["e"] = _NamedTestEntity("e", scope, clkbuiltins.DURATION)
    scope.names["coll_i"] = _NamedTestEntity("coll_i", scope, dfl_types.CollectionType(clkbuiltins.INT64))
    scope.names["coll_f"] = _NamedTestEntity("coll_f", scope, dfl_types.CollectionType(clkbuiltins.FLOAT64))
    scope.names["coll_b"] = _NamedTestEntity("coll_b", scope, dfl_types.CollectionType(clkbuiltins.BOOL))
    return scope


def _type_check(expr_str: str, registry: dfl_types.TraitRegistry) -> typesys.TypeVal | typesys.InferenceVar:
    """Parse, convert, and type-check an expression, returning the result type."""
    scope = _make_typed_scope()
    expr = _parse_and_convert(expr_str, scope)
    typed_expr = dfl.type_check_expr(expr, registry)
    return typed_expr.type_info


@pytest.mark.parametrize(
    ("expr", "expected_type"),
    [
        # Integer operations
        ("i + j", clkbuiltins.INT64),
        ("i - j", clkbuiltins.INT64),
        ("i * j", clkbuiltins.INT64),
        ("i / j", clkbuiltins.INT64),
        ("-i", clkbuiltins.INT64),
        ("|i|", clkbuiltins.INT64),
        # Float operations
        ("f + g", clkbuiltins.FLOAT64),
        ("f * g", clkbuiltins.FLOAT64),
        # Comparisons -> Bool
        ("i > j", clkbuiltins.BOOL),
        ("i == j", clkbuiltins.BOOL),
        ("i != j", clkbuiltins.BOOL),
        # Boolean operations
        ("b and c", clkbuiltins.BOOL),
        ("b or c", clkbuiltins.BOOL),
        ("not b", clkbuiltins.BOOL),
        # Duration operations
        ("d + e", clkbuiltins.DURATION),
        ("d - e", clkbuiltins.DURATION),
        ("d / e", clkbuiltins.FLOAT64),  # Duration / Duration -> Float64
        ("d * f", clkbuiltins.DURATION),  # Duration * Float64 -> Duration
        # Complex expressions
        ("(i + j) > (i * j)", clkbuiltins.BOOL),
        ("i > j and j > i", clkbuiltins.BOOL),
        # Conditional expressions
        ("if b then i else j", clkbuiltins.INT64),
        ("if i > 0 then f else g", clkbuiltins.FLOAT64),
        ("cond { b => i, else => j }", clkbuiltins.INT64),
        ("cond { i > j => i, j > i => j, else => i }", clkbuiltins.INT64),
    ],
)
def test_type_check_expressions(
    std_traits_registry: dfl_types.TraitRegistry,
    expr: str,
    expected_type: typesys.TypeVal,
) -> None:
    """Test type checking various expressions."""
    result = _type_check(expr, std_traits_registry)
    assert result is expected_type


def test_type_check_literals(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test that untyped literals have InferenceVar types."""
    result = _type_check("42", std_traits_registry)
    assert isinstance(result, typesys.InferenceVar)
    assert result.numeric_type == typesys.NumericType.INTEGER

    result = _type_check("3.14", std_traits_registry)
    assert isinstance(result, typesys.InferenceVar)
    assert result.numeric_type == typesys.NumericType.FLOAT


def test_type_check_errors(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test type checking produces appropriate errors."""
    scope = _make_typed_scope()

    # Int64 + Bool is invalid
    expr = _parse_and_convert("i + b", scope)
    with pytest.raises(dfl.TypeCheckError, match="No implementation of Add"):
        dfl.type_check_expr(expr, std_traits_registry)

    # Bool - Bool is invalid (Sub not implemented for Bool)
    expr = _parse_and_convert("b - c", scope)
    with pytest.raises(dfl.TypeCheckError, match="No implementation of Sub"):
        dfl.type_check_expr(expr, std_traits_registry)


def test_type_check_conditional_errors(std_traits_registry: dfl_types.TraitRegistry) -> None:
    """Test type checking produces appropriate errors for conditionals."""
    scope = _make_typed_scope()

    # Non-Bool condition in if-then-else
    expr = _parse_and_convert("if i then j else j", scope)
    with pytest.raises(dfl.TypeCheckError, match="must be Bool"):
        dfl.type_check_expr(expr, std_traits_registry)

    # Mismatched branches in if-then-else
    expr = _parse_and_convert("if b then i else f", scope)
    with pytest.raises(dfl.TypeCheckError, match="Type mismatch"):
        dfl.type_check_expr(expr, std_traits_registry)

    # Non-Bool guard in cond
    expr = _parse_and_convert("cond { i => j, else => j }", scope)
    with pytest.raises(dfl.TypeCheckError, match="must be Bool"):
        dfl.type_check_expr(expr, std_traits_registry)

    # Missing else arm in cond
    expr = _parse_and_convert("cond { b => i }", scope)
    with pytest.raises(dfl.TypeCheckError, match="must end with 'else'"):
        dfl.type_check_expr(expr, std_traits_registry)

    # Mismatched body types in cond
    expr = _parse_and_convert("cond { b => i, else => f }", scope)
    with pytest.raises(dfl.TypeCheckError, match="Type mismatch"):
        dfl.type_check_expr(expr, std_traits_registry)


def _parse_trait_def(source: str) -> tuple[cst.DflTraitDef, terminalsrc.TerminalSource]:
    """Parse a trait definition string."""
    return _parse(source, "dfl_trait_def", cst.DflTraitDef)


def _parse_trait_def_with_module(
    source: str,
    scope: node.Scope | None = None,
) -> tuple[cst.DflTraitDef, node.Module]:
    """Parse a trait definition and create a module for IR conversion."""
    cst_node, terminals = _parse_trait_def(source)
    module = _make_test_module(terminals, scope)
    return cst_node, module


class TestTraitCstToIr:
    """Tests for converting trait CST nodes to IR."""

    def test_trait_def_from_cst(self) -> None:
        """Test converting trait definitions from CST to IR."""
        cst_node, module = _parse_trait_def_with_module("trait Neg { type Output; }")
        trait = dfl_types.TraitDef.from_cst(cst_node, module, module.inner_scope)
        assert trait.name == "Neg"
        assert trait.type_params == ()
        assert trait.associated_types == ("Output",)

        cst_node, module = _parse_trait_def_with_module("trait Add<Rhs = Self> { type Output; }")
        trait = dfl_types.TraitDef.from_cst(cst_node, module, module.inner_scope)
        assert trait.name == "Add"
        assert len(trait.type_params) == 1
        assert trait.type_params[0].name == "Rhs"
        assert trait.type_params[0].default_is_self is True
        assert trait.associated_types == ("Output",)

        cst_node, module = _parse_trait_def_with_module("trait Ord<Rhs = Self>;")
        trait = dfl_types.TraitDef.from_cst(cst_node, module, module.inner_scope)
        assert trait.name == "Ord"
        assert trait.associated_types == ()

        cst_node, module = _parse_trait_def_with_module("trait Foo { type A; type B; }")
        trait = dfl_types.TraitDef.from_cst(cst_node, module, module.inner_scope)
        assert trait.associated_types == ("A", "B")


def _parse_pattern(source: str) -> tuple[cst.DflPattern, terminalsrc.TerminalSource, node.Module]:
    """Parse a DFL pattern string."""
    cst_node, terminals = _parse(source, "dfl_pattern", cst.DflPattern)
    module = _make_test_module(terminals)
    return cst_node, terminals, module


def _parse_match_pattern(source: str) -> tuple[cst.DflMatchPattern, terminalsrc.TerminalSource, node.Module]:
    """Parse a DFL match pattern string."""
    cst_node, terminals = _parse(source, "dfl_match_pattern", cst.DflMatchPattern)
    module = _make_test_module(terminals)
    return cst_node, terminals, module


class TestPatterns:
    """Tests for pattern parsing and type checking."""

    def test_parse_literal_pattern(self) -> None:
        """Test parsing a literal pattern."""
        cst_node, terminals, module = _parse_pattern("42")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        pattern = dfl.pattern_from_cst(cst_node, ctx, module)
        assert isinstance(pattern, dfl.LiteralPattern)
        assert isinstance(pattern.value, primitive.DecimalLiteral)

    def test_parse_range_pattern(self) -> None:
        """Test parsing a range pattern."""
        cst_node, terminals, module = _parse_pattern("1..10")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        pattern = dfl.pattern_from_cst(cst_node, ctx, module)
        assert isinstance(pattern, dfl.RangePattern)
        assert isinstance(pattern.lo, primitive.DecimalLiteral)
        assert isinstance(pattern.hi, primitive.DecimalLiteral)

    def test_parse_wildcard_pattern(self) -> None:
        """Test parsing wildcard patterns."""
        cst_node, terminals, module = _parse_match_pattern("else")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        patterns = dfl.match_pattern_from_cst(cst_node, ctx, module)
        assert len(patterns) == 1
        assert isinstance(patterns[0], dfl.WildcardPattern)

    def test_parse_union_pattern(self) -> None:
        """Test parsing union patterns with |."""
        cst_node, terminals, module = _parse_match_pattern("1 | 2 | 3")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        patterns = dfl.match_pattern_from_cst(cst_node, ctx, module)
        assert len(patterns) == 3
        assert all(isinstance(p, dfl.LiteralPattern) for p in patterns)

    def test_check_literal_pattern_type(self) -> None:
        """Test type checking literal patterns."""
        cst_node, terminals, module = _parse_pattern("42")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        pattern = dfl.pattern_from_cst(cst_node, ctx, module)

        dfl.check_pattern_type(pattern, clkbuiltins.INT64, ctx)

    def test_check_wildcard_always_matches(self) -> None:
        """Test that wildcard patterns match any type."""
        cst_node, terminals, module = _parse_match_pattern("else")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        patterns = dfl.match_pattern_from_cst(cst_node, ctx, module)

        dfl.check_pattern_type(patterns[0], clkbuiltins.INT64, ctx)
        dfl.check_pattern_type(patterns[0], clkbuiltins.FLOAT64, ctx)
        dfl.check_pattern_type(patterns[0], clkbuiltins.BOOL, ctx)

    def test_check_range_pattern_type(self) -> None:
        """Test type checking range patterns."""
        cst_node, terminals, module = _parse_pattern("1..10")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        pattern = dfl.pattern_from_cst(cst_node, ctx, module)

        dfl.check_pattern_type(pattern, clkbuiltins.INT64, ctx)

    def test_parse_enum_pattern(self) -> None:
        """Test parsing enum variant patterns."""
        enum_source = """
        // Test status enum
        enum Status
        {
            values
            {
                // Active status
                #0 active default;
                // Inactive status
                #1 inactive;
            }
        }
        """
        fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
        enum_module = compiler.compile_source_text(enum_source, ModuleID(CLK_REPO, "test_enum_pattern"), fs_importer)

        cst_node, terminals, module = _parse_pattern("Status::active")
        status_enum = enum_module.inner_scope.lookup("Status")
        assert status_enum is not None, "Status enum should exist"
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        pattern = dfl.pattern_from_cst(cst_node, ctx, module)

        assert isinstance(pattern, dfl.EnumPattern)
        assert isinstance(pattern.variant, clkenum.ValueDef)

    def test_enum_pattern_undefined_enum(self) -> None:
        """Test error handling when enum doesn't exist."""
        cst_node, terminals, module = _parse_pattern("UndefinedEnum::variant")
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        with pytest.raises(dfl.NameValidationError, match="Undefined name: UndefinedEnum"):
            dfl.pattern_from_cst(cst_node, ctx, module)

    def test_enum_pattern_undefined_variant(self) -> None:
        """Test error handling when variant doesn't exist."""
        enum_source = """
        // Test status enum
        enum Status
        {
            values
            {
                // Active status
                #0 active default;
                // Inactive status
                #1 inactive;
            }
        }
        """
        fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
        enum_module = compiler.compile_source_text(
            enum_source, ModuleID(CLK_REPO, "test_enum_undefined_variant"), fs_importer
        )

        cst_node, terminals, module = _parse_pattern("Status::unknown")
        status_enum = enum_module.inner_scope.lookup("Status")
        assert status_enum is not None, "Status enum should exist"
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        with pytest.raises(dfl.NameValidationError, match="Undefined name: Status::unknown"):
            dfl.pattern_from_cst(cst_node, ctx, module)

    def test_enum_pattern_wrong_type(self) -> None:
        """Test error when trying to use non-enum as enum."""
        schema_source = """
        // Test schema (not an enum)
        schema NotAnEnum
        {
            uuid: 12345678-1234-1234-1234-123456789012;
            fields
            {
                // A field
                #0 value: Int32;
            }
        }
        """
        fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
        schema_module = compiler.compile_source_text(
            schema_source, ModuleID(CLK_REPO, "test_enum_wrong_type"), fs_importer
        )

        cst_node, terminals, module = _parse_pattern("NotAnEnum::variant")
        not_an_enum = schema_module.inner_scope.lookup("NotAnEnum")
        assert not_an_enum is not None, "NotAnEnum schema should exist"
        module.inner_scope.define("NotAnEnum", not_an_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        with pytest.raises(dfl.NameValidationError, match="is not a namespace"):
            dfl.pattern_from_cst(cst_node, ctx, module)


class TestMatchExpressions:
    """Tests for match expression parsing and type checking."""

    def test_parse_basic_match(self) -> None:
        """Test parsing a basic match expression."""
        cst, terminals = _parse_dfl_expr("match x { 0 => 1, 1 => 2, else => 3 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        assert len(expr.arms) == 3

    def test_parse_match_with_else(self) -> None:
        """Test parsing a match with else arm."""
        cst, terminals = _parse_dfl_expr("match n { 0 => 1, else => 0 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        assert len(expr.arms) == 2
        # Last arm should have a wildcard pattern
        last_arm = expr.arms[-1]
        assert len(last_arm.patterns) == 1
        assert isinstance(last_arm.patterns[0], dfl.WildcardPattern)

    def test_parse_match_with_union_patterns(self) -> None:
        """Test parsing a match with union patterns."""
        cst, terminals = _parse_dfl_expr("match x { 0 | 1 | 2 => 1, else => 0 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        # First arm has 3 patterns in union
        assert len(expr.arms[0].patterns) == 3

    def test_parse_match_with_range_pattern(self) -> None:
        """Test parsing a match with range patterns."""
        cst, terminals = _parse_dfl_expr("match n { 0..9 => 1, else => 0 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        first_pattern = expr.arms[0].patterns[0]
        assert isinstance(first_pattern, dfl.RangePattern)

    def test_match_type_checking(self) -> None:
        """Test type checking a match expression."""
        cst, terminals = _parse_dfl_expr("match n { 0 => 1, 1 => 2, else => 0 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        def mock_check(_: dfl.Expr) -> typesys.TypeVal | typesys.InferenceVar:
            return clkbuiltins.INT64

        result_type = dfl.check_match_type(expr, clkbuiltins.INT64, mock_check)
        # Result type should be INT64 (unified type of all arm bodies)
        assert result_type is clkbuiltins.INT64


class TestExhaustivenessChecking:
    """Tests for exhaustiveness checking."""

    @pytest.fixture(scope="class")
    def status_enum_module(self) -> node.Module:
        """Create a module with a Status enum for testing."""
        enum_source = """
        // Test status enum for exhaustiveness checking
        enum Status
        {
            values
            {
                // Active status
                #0 active default;
                // Inactive status
                #1 inactive;
                // Pending status
                #2 pending;
            }
        }
        """
        fs_importer = FilesystemImporter(compile_fn=compiler.compile_source_file)
        return compiler.compile_source_text(enum_source, ModuleID(CLK_REPO, "test_exhaustiveness"), fs_importer)

    def test_exhaustive_enum_match(self, status_enum_module: node.Module) -> None:
        """Test that covering all enum variants passes exhaustiveness check."""
        cst, terminals = _parse_dfl_expr("match s { Status::active => 1, Status::inactive => 2, Status::pending => 3 }")
        module = _make_test_module(terminals)

        status_enum = status_enum_module.inner_scope.lookup("Status")
        assert status_enum is not None
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        # Should not raise - all variants covered
        assert isinstance(status_enum, clkenum.ClkEnum)
        dfl.check_match_exhaustiveness(expr, status_enum)

    def test_non_exhaustive_enum_match(self, status_enum_module: node.Module) -> None:
        """Test that missing enum variants fails exhaustiveness check."""
        cst, terminals = _parse_dfl_expr("match s { Status::active => 1, Status::inactive => 2 }")
        module = _make_test_module(terminals)

        status_enum = status_enum_module.inner_scope.lookup("Status")
        assert status_enum is not None
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        assert isinstance(status_enum, clkenum.ClkEnum)
        with pytest.raises(dfl.TypeCheckError, match="Non-exhaustive match: missing variants pending"):
            dfl.check_match_exhaustiveness(expr, status_enum)

    def test_wildcard_satisfies_exhaustiveness(self, status_enum_module: node.Module) -> None:
        """Test that wildcard patterns satisfy exhaustiveness."""
        cst, terminals = _parse_dfl_expr("match s { Status::active => 1, else => 0 }")
        module = _make_test_module(terminals)

        status_enum = status_enum_module.inner_scope.lookup("Status")
        assert status_enum is not None
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        # Should not raise - wildcard covers all remaining variants
        assert isinstance(status_enum, clkenum.ClkEnum)
        dfl.check_match_exhaustiveness(expr, status_enum)

    def test_non_enum_type_exempt_from_exhaustiveness(self) -> None:
        """Test that non-enum types don't require exhaustiveness."""
        cst, terminals = _parse_dfl_expr("match n { 0 => 1, 1 => 2 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        dfl.check_match_exhaustiveness(expr, clkbuiltins.INT64)  # Should not raise

    def test_has_wildcard_detection(self) -> None:
        """Test that has_wildcard correctly identifies wildcard patterns."""
        cst, terminals = _parse_dfl_expr("match n { 0 => 1, else => 2 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        assert dfl.has_wildcard(expr.arms) is True

        cst, terminals = _parse_dfl_expr("match n { 0 => 1, 1 => 2 }")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)
        assert dfl.has_wildcard(expr.arms) is False

    def test_covered_variants_extraction(self, status_enum_module: node.Module) -> None:
        """Test that covered_variants correctly extracts enum variant names."""
        cst, terminals = _parse_dfl_expr("match s { Status::active => 1, Status::inactive => 2 }")
        module = _make_test_module(terminals)

        # Add Status enum to scope
        status_enum = status_enum_module.inner_scope.lookup("Status")
        assert status_enum is not None
        module.inner_scope.define("Status", status_enum, terminals)

        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)
        expr = dfl.expr_from_cst(cst, ctx, module)
        assert isinstance(expr, dfl.Match)

        covered = dfl.covered_variants(expr.arms)
        assert covered == {"active", "inactive"}


class TestFunctions:
    """Tests for user-defined functions."""

    def test_fn_double_parsing_substitution_and_expansion(self) -> None:
        """Test parsing, substitution, expansion, and closure checking for fn double(x) { x + x }."""
        fn_def, module = _compile_fn_def("fn double(x) { x + x }", "double")

        # Verify parsing
        assert fn_def.name == "double"
        assert len(fn_def.params) == 1
        assert fn_def.params[0].name == "x"
        assert isinstance(fn_def.params[0], dfl.FnParam)
        assert isinstance(fn_def.body, dfl.Binary)

        # Test expand_call: parse "double(5)" and expand it
        call_expr = _parse_and_convert("double(5)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        assert isinstance(call_expr.func, dfl.Ref)
        assert call_expr.func.path == ("double",)
        assert len(call_expr.args) == 1
        assert isinstance(call_expr.args[0], dfl.CallArg)
        assert call_expr.args[0].name is None
        assert not call_expr.args[0].is_spread
        assert isinstance(call_expr.args[0].expr, primitive.DecimalLiteral)
        assert call_expr.args[0].expr.value == 5

        expanded = dfl.expand_call(call_expr)
        assert isinstance(expanded, dfl.Binary)
        assert expanded.op == dfl.BinaryOp.ADD
        assert isinstance(expanded.left, primitive.DecimalLiteral)
        assert expanded.left.value == 5
        assert isinstance(expanded.right, primitive.DecimalLiteral)
        assert expanded.right.value == 5

        # Test check_no_closures: referencing params is fine
        dfl.check_no_closures(fn_def)

    def test_expand_call_wrong_arg_count(self) -> None:
        """Test that expand_call raises error with wrong argument count."""
        _fn_def, module = _compile_fn_def("fn add(x, y) { x + y }", "add")

        # Parse a call with only 1 argument - should fail on expansion
        call_expr = _parse_and_convert("add(5)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(ValueError, match="Missing argument for parameter 'y'"):
            dfl.expand_call(call_expr)

    def test_parse_nested_function_call(self) -> None:
        """Test parsing nested function calls."""
        # Parsing-based tests are what we need - they cover the full path from source to IR
        expr = _parse_and_convert("max(a, min(b, c))")

        assert isinstance(expr, dfl.Call)
        assert isinstance(expr.func, dfl.Ref)
        assert expr.func.path == ("max",)
        assert len(expr.args) == 2

        # Second argument should be a CallArg containing a Call
        assert isinstance(expr.args[1], dfl.CallArg)
        assert isinstance(expr.args[1].expr, dfl.Call)
        nested_call = expr.args[1].expr
        assert isinstance(nested_call.func, dfl.Ref)
        assert nested_call.func.path == ("min",)
        assert len(nested_call.args) == 2

    def test_parse_chained_member_and_call(self) -> None:
        """Test parsing chained member access and function calls."""
        expr = _parse_and_convert("obj.method(arg).field")

        # Should be Member of a Call of a Member of a Ref
        assert isinstance(expr, dfl.Member)
        assert expr.field_name == "field"

        assert isinstance(expr.base, dfl.Call)
        call = expr.base
        assert len(call.args) == 1

        assert isinstance(call.func, dfl.Member)
        member = call.func
        assert member.field_name == "method"

        assert isinstance(member.base, dfl.Ref)
        assert member.base.path == ("obj",)

    def test_parse_function_definition_from_cst(self) -> None:
        """Test compiling function definition via full compiler pipeline."""
        fn_def, _module = _compile_fn_def("fn double(x) { x + x }", "double")

        assert fn_def.name == "double"
        assert len(fn_def.params) == 1
        assert fn_def.params[0].name == "x"
        assert fn_def.params[0].type_expr is None
        assert fn_def.params[0].is_variadic is False
        assert fn_def.return_type_expr is None
        assert fn_def.body is not None
        assert isinstance(fn_def.body, dfl.Binary)
        assert fn_def.body.op == dfl.BinaryOp.ADD

    def test_parse_function_with_type_annotations(self) -> None:
        """Test compiling function with type annotations and resolving them."""
        fn_def, _module = _compile_fn_def("fn add(x: Int64, y: Int64) -> Int64 { x + y }", "add")

        assert fn_def.name == "add"
        assert len(fn_def.params) == 2
        assert fn_def.params[0].name == "x"
        assert fn_def.params[0].type_expr is not None
        assert fn_def.params[1].name == "y"
        assert fn_def.params[1].type_expr is not None
        assert fn_def.return_type_expr is not None

        # Resolve (name resolution already done by compiler)
        resolved_fn = fn_def.resolve()

        assert resolved_fn.name == "add"
        assert len(resolved_fn.params) == 2
        assert resolved_fn.params[0].name == "x"
        assert resolved_fn.params[0].type_val == clkbuiltins.INT64
        assert resolved_fn.params[1].name == "y"
        assert resolved_fn.params[1].type_val == clkbuiltins.INT64
        assert resolved_fn.return_type == clkbuiltins.INT64

    def test_parse_call_with_named_args(self) -> None:
        """Test parsing function call with named arguments."""
        expr = _parse_and_convert("func(x, y=5)")

        assert isinstance(expr, dfl.Call)
        assert len(expr.args) == 2

        assert expr.args[0].name is None
        assert not expr.args[0].is_spread
        assert isinstance(expr.args[0].expr, dfl.Ref)

        assert expr.args[1].name == "y"
        assert not expr.args[1].is_spread
        assert isinstance(expr.args[1].expr, primitive.DecimalLiteral)
        assert expr.args[1].expr.value == 5

    def test_parse_call_with_spread_arg(self) -> None:
        """Test parsing function call with spread argument."""
        expr = _parse_and_convert("func(values...)")

        assert isinstance(expr, dfl.Call)
        assert len(expr.args) == 1
        assert expr.args[0].name is None
        assert expr.args[0].is_spread
        assert isinstance(expr.args[0].expr, dfl.Ref)
        assert expr.args[0].expr.path == ("values",)

    def test_parse_call_mixed_args(self) -> None:
        """Test parsing function call with mixed positional, named, and spread args."""
        expr = _parse_and_convert("func(a, b, name=c, items...)")

        assert isinstance(expr, dfl.Call)
        assert len(expr.args) == 4

        assert expr.args[0].name is None
        assert not expr.args[0].is_spread
        assert expr.args[1].name is None
        assert not expr.args[1].is_spread
        assert expr.args[2].name == "name"
        assert not expr.args[2].is_spread
        assert expr.args[3].name is None
        assert expr.args[3].is_spread

    def test_parse_variadic_function(self) -> None:
        """Test compiling function definition with variadic parameter."""
        fn_def, _module = _compile_fn_def("fn sum(values...) { values }", "sum")

        assert fn_def.name == "sum"
        assert len(fn_def.params) == 1
        assert fn_def.params[0].name == "values"
        assert fn_def.params[0].is_variadic is True
        assert fn_def.body is not None

    def test_parse_function_with_regular_and_variadic_params(self) -> None:
        """Test compiling function with regular parameters followed by variadic."""
        fn_def, _module = _compile_fn_def("fn format(template, args...) { template }", "format")

        assert fn_def.name == "format"
        assert len(fn_def.params) == 2
        assert fn_def.params[0].name == "template"
        assert fn_def.params[0].is_variadic is False
        assert fn_def.params[1].name == "args"
        assert fn_def.params[1].is_variadic is True

    def test_expand_call_with_named_args(self) -> None:
        """Test function expansion with named arguments."""
        _fn_def, module = _compile_fn_def("fn add(x, y) { x + y }", "add")

        call_expr = _parse_and_convert("add(y=10, x=5)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        assert isinstance(result, dfl.Binary)
        assert result.op == dfl.BinaryOp.ADD
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert result.left.value == 5
        assert isinstance(result.right, primitive.DecimalLiteral)
        assert result.right.value == 10

    def test_expand_call_unknown_named_arg(self) -> None:
        """Test that unknown named arguments raise an error."""
        _fn_def, module = _compile_fn_def("fn f(x) { x }", "f")

        call_expr = _parse_and_convert("f(unknown=5)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(ValueError, match="Missing argument for parameter 'x'"):
            dfl.expand_call(call_expr)

    def test_expand_call_unknown_named_arg_extra(self) -> None:
        """Test that unknown named arguments raise an error when all params are satisfied."""
        _fn_def, module = _compile_fn_def("fn f(x) { x }", "f")

        call_expr = _parse_and_convert("f(x=5, extra=6)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(ValueError, match="Unknown named argument"):
            dfl.expand_call(call_expr)

    def test_expand_call_duplicate_named_arg(self) -> None:
        """Test that duplicate named arguments raise an error."""
        _fn_def, module = _compile_fn_def("fn f(x) { x }", "f")

        call_expr = _parse_and_convert("f(x=5, x=6)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(ValueError, match="Duplicate named argument 'x'"):
            dfl.expand_call(call_expr)


class TestSpreadAndVariadic:
    """Tests for spread operator and variadic parameter expansion."""

    def test_expand_call_variadic_single_arg(self) -> None:
        """Variadic function with single argument collects it into ExprTuple."""
        _fn_def, module = _compile_fn_def("fn sum(values...) { values }", "sum")

        call_expr = _parse_and_convert("sum(5)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # Result should be ExprTuple([5])
        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 1
        assert isinstance(result.elements[0], primitive.DecimalLiteral)
        assert result.elements[0].value == 5

    def test_expand_call_variadic_multiple_args(self) -> None:
        """Variadic function with multiple arguments collects them into ExprTuple."""
        _fn_def, module = _compile_fn_def("fn sum(values...) { values }", "sum")

        call_expr = _parse_and_convert("sum(1, 2, 3)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 3
        for e in result.elements:
            assert isinstance(e, primitive.DecimalLiteral)
        assert [e.value for e in result.elements if isinstance(e, primitive.DecimalLiteral)] == [1, 2, 3]

    def test_expand_call_variadic_no_args(self) -> None:
        """Variadic function with no arguments creates empty ExprTuple."""
        _fn_def, module = _compile_fn_def("fn sum(values...) { values }", "sum")

        call_expr = _parse_and_convert("sum()", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 0

    def test_expand_call_mixed_regular_and_variadic(self) -> None:
        """Function with regular params followed by variadic."""
        _fn_def, module = _compile_fn_def("fn format(template, args...) { args }", "format")

        call_expr = _parse_and_convert("format(0, 1, 2, 3)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # Body is just `args`, so result should be ExprTuple([1, 2, 3])
        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 3
        for e in result.elements:
            assert isinstance(e, primitive.DecimalLiteral)
        assert [e.value for e in result.elements if isinstance(e, primitive.DecimalLiteral)] == [1, 2, 3]

    def test_expand_call_spread_array_literal(self) -> None:
        """Spread an ExprTuple into a function call."""
        _fn_def, module = _compile_fn_def("fn add(x, y, z) { x + y + z }", "add")

        call_expr = _parse_and_convert("add([1, 2, 3]...)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # Should expand to (1 + 2) + 3
        assert isinstance(result, dfl.Binary)
        assert result.op == dfl.BinaryOp.ADD

    def test_expand_call_spread_with_positional_before(self) -> None:
        """Spread with positional arg before it."""
        _fn_def, module = _compile_fn_def("fn f(a, b, c) { a + b + c }", "f")

        call_expr = _parse_and_convert("f(1, [2, 3]...)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # f(1, [2, 3]...) -> f(1, 2, 3) -> (1 + 2) + 3
        assert isinstance(result, dfl.Binary)

    def test_expand_call_spread_with_positional_after(self) -> None:
        """Spread with positional arg after it."""
        _fn_def, module = _compile_fn_def("fn f(a, b, c) { a + b + c }", "f")

        call_expr = _parse_and_convert("f([1, 2]..., 3)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # f([1, 2]..., 3) -> f(1, 2, 3) -> (1 + 2) + 3
        assert isinstance(result, dfl.Binary)

    def test_expand_call_spread_into_variadic(self) -> None:
        """Spread an ExprTuple into a variadic function."""
        _fn_def, module = _compile_fn_def("fn sum(values...) { values }", "sum")

        call_expr = _parse_and_convert("sum([1, 2, 3]...)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)
        result = dfl.expand_call(call_expr)

        # sum([1, 2, 3]...) -> values = [1, 2, 3]
        assert isinstance(result, dfl.ExprTuple)
        assert len(result.elements) == 3
        for e in result.elements:
            assert isinstance(e, primitive.DecimalLiteral)
        assert [e.value for e in result.elements if isinstance(e, primitive.DecimalLiteral)] == [1, 2, 3]

    def test_expand_call_spread_non_array_literal_error(self) -> None:
        """Spreading a non-ExprTuple raises TypeError."""
        _fn_def, module = _compile_fn_def("fn f(x, y) { x + y }", "f")

        # Create a reference to spread (not an ExprTuple)
        scope = module.inner_scope
        scope.names["arr"] = _NamedTestEntity("arr", scope, clkbuiltins.INT64)

        call_expr = _parse_and_convert("f(arr...)", scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(TypeError, match="Cannot spread expression of type Ref"):
            dfl.expand_call(call_expr)

    def test_expand_call_spread_after_named_error(self) -> None:
        """Spread after named arguments raises ValueError."""
        _fn_def, module = _compile_fn_def("fn f(x, y) { x + y }", "f")

        call_expr = _parse_and_convert("f(x=1, [2]...)", module.inner_scope)
        assert isinstance(call_expr, dfl.Call)

        with pytest.raises(ValueError, match="Spread arguments cannot appear after named arguments"):
            dfl.expand_call(call_expr)


class TestExprTuples:
    """Tests for ExprTuple expressions (array literals and variadic packs)."""

    def test_parse_empty_array(self) -> None:
        """Test parsing an empty array literal."""
        expr = _parse_and_convert("[]")
        assert isinstance(expr, dfl.ExprTuple)
        assert expr.elements == ()

    def test_parse_single_element_array(self) -> None:
        """Test parsing a single-element array literal."""
        expr = _parse_and_convert("[42]")
        assert isinstance(expr, dfl.ExprTuple)
        assert len(expr.elements) == 1
        assert isinstance(expr.elements[0], primitive.DecimalLiteral)
        assert expr.elements[0].value == 42

    def test_parse_multi_element_array(self) -> None:
        """Test parsing a multi-element array literal."""
        expr = _parse_and_convert("[1, 2, 3]")
        assert isinstance(expr, dfl.ExprTuple)
        assert len(expr.elements) == 3
        assert isinstance(expr.elements[0], primitive.DecimalLiteral)
        assert expr.elements[0].value == 1
        assert isinstance(expr.elements[1], primitive.DecimalLiteral)
        assert expr.elements[1].value == 2
        assert isinstance(expr.elements[2], primitive.DecimalLiteral)
        assert expr.elements[2].value == 3

    def test_parse_array_with_trailing_comma(self) -> None:
        """Test parsing an array with trailing comma."""
        expr = _parse_and_convert("[1, 2, 3,]")
        assert isinstance(expr, dfl.ExprTuple)
        assert len(expr.elements) == 3
        assert isinstance(expr.elements[0], primitive.DecimalLiteral)
        assert expr.elements[0].value == 1
        assert isinstance(expr.elements[1], primitive.DecimalLiteral)
        assert expr.elements[1].value == 2
        assert isinstance(expr.elements[2], primitive.DecimalLiteral)
        assert expr.elements[2].value == 3

    def test_parse_array_with_expressions(self) -> None:
        """Test parsing array with expression elements."""
        expr = _parse_and_convert("[a + b, c * d]")
        assert isinstance(expr, dfl.ExprTuple)
        assert len(expr.elements) == 2
        assert isinstance(expr.elements[0], dfl.Binary)
        assert isinstance(expr.elements[1], dfl.Binary)
        assert expr.elements[0].op == dfl.BinaryOp.ADD
        assert expr.elements[1].op == dfl.BinaryOp.MUL
        assert isinstance(expr.elements[0].left, dfl.Ref)
        assert expr.elements[0].left.path == ("a",)
        assert isinstance(expr.elements[0].right, dfl.Ref)
        assert expr.elements[0].right.path == ("b",)
        assert isinstance(expr.elements[1].left, dfl.Ref)
        assert expr.elements[1].left.path == ("c",)
        assert isinstance(expr.elements[1].right, dfl.Ref)
        assert expr.elements[1].right.path == ("d",)

    def test_type_check_array_literal(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type checking array literals with homogeneous types."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("[i, j]", scope)  # i and j are Int64
        typed_expr = dfl.type_check_expr(expr, std_traits_registry)

        assert isinstance(typed_expr.type_info, dfl_types.CollectionType)
        assert typed_expr.type_info.element_type is clkbuiltins.INT64

    def test_type_check_array_mismatched_types(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type error on mismatched element types."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("[i, f]", scope)  # i is Int64, f is Float64

        with pytest.raises(dfl.TypeCheckError, match="Type mismatch"):
            dfl.type_check_expr(expr, std_traits_registry)

    def test_type_check_empty_array_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type error on empty array literal."""
        expr = _parse_and_convert("[]")

        with pytest.raises(dfl.TypeCheckError, match="Empty array literals"):
            dfl.type_check_expr(expr, std_traits_registry)

    def test_map_expr_expr_tuple(self) -> None:
        """Test map_expr handles ExprTuple correctly."""
        scope = _make_typed_scope()
        expr = _parse_and_convert("[i, j]", scope)
        assert isinstance(expr, dfl.ExprTuple)

        def identity(e: dfl.Expr) -> dfl.Expr:
            return e

        mapped = dfl.map_expr(identity, expr)
        assert isinstance(mapped, dfl.ExprTuple)
        assert len(mapped.elements) == 2

    def test_fold_expr_expr_tuple(self) -> None:
        """Test fold_expr handles ExprTuple correctly."""
        expr = _parse_and_convert("[1, 2, 3]")
        size = dfl.expr_size(expr)
        # 1 ExprTuple + 3 literals = 4 nodes
        assert size == 4


class TestBuiltinFunctions:
    """Tests for built-in function infrastructure."""

    def test_builtin_registry(self) -> None:
        """Test that built-in functions are registered."""
        assert dfl.get_builtin("min") is not None
        assert dfl.get_builtin("max") is not None
        assert dfl.get_builtin("abs") is not None
        assert dfl.get_builtin("clamp") is not None
        assert dfl.get_builtin("sum") is not None
        assert dfl.get_builtin("count") is not None
        assert dfl.get_builtin("flatten") is not None
        assert dfl.get_builtin("nonexistent") is None

    def test_builtin_variadic(self) -> None:
        """Test built-in function variadic flag."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        assert min_fn.variadic is True
        assert min_fn.min_args == 1

        clamp_fn = dfl.get_builtin("clamp")
        assert clamp_fn is not None
        assert clamp_fn.variadic is False
        assert clamp_fn.min_args == 3

    def test_minmax_builtin_class(self) -> None:
        """Test min/max are MinMaxBuiltin instances."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        assert isinstance(min_fn, dfl.MinMaxBuiltin)

        max_fn = dfl.get_builtin("max")
        assert max_fn is not None
        assert isinstance(max_fn, dfl.MinMaxBuiltin)

    def test_minmax_builtin_collection_mode(self) -> None:
        """Test MinMaxBuiltin returns element type for collection arg."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        result = min_fn.infer_return_type([dfl_types.CollectionType(clkbuiltins.INT64)])
        assert result is clkbuiltins.INT64

    def test_minmax_builtin_scalar_mode(self) -> None:
        """Test MinMaxBuiltin returns unified type for multi-arg."""
        max_fn = dfl.get_builtin("max")
        assert max_fn is not None
        result = max_fn.infer_return_type([clkbuiltins.FLOAT64, clkbuiltins.FLOAT64])
        assert result is clkbuiltins.FLOAT64

    def test_minmax_builtin_scalar_type_mismatch(self) -> None:
        """Test MinMaxBuiltin rejects multi-arg with mismatched types."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        with pytest.raises(TypeError, match="Type inference failed"):
            min_fn.infer_return_type([clkbuiltins.INT64, clkbuiltins.FLOAT64])

    def test_minmax_builtin_single_scalar_error(self) -> None:
        """Test MinMaxBuiltin rejects single non-collection arg."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        with pytest.raises(dfl.TypeCheckError, match="requires a collection"):
            min_fn.infer_return_type([clkbuiltins.INT64])

    def test_minmax_builtin_no_args_error(self) -> None:
        """Test MinMaxBuiltin rejects zero args."""
        min_fn = dfl.get_builtin("min")
        assert min_fn is not None
        with pytest.raises(dfl.TypeCheckError, match="requires at least one argument"):
            min_fn.infer_return_type([])

    def test_preserves_input_type_builtin(self) -> None:
        """Test PreservesInputTypeBuiltin returns first argument type."""
        abs_fn = dfl.get_builtin("abs")
        assert abs_fn is not None
        assert isinstance(abs_fn, dfl.PreservesInputTypeBuiltin)
        result = abs_fn.infer_return_type([clkbuiltins.INT64])
        assert result is clkbuiltins.INT64

    def test_fixed_type_builtin(self) -> None:
        """Test FixedTypeBuiltin returns fixed type."""
        count_fn = dfl.get_builtin("count")
        assert count_fn is not None
        assert isinstance(count_fn, dfl.FixedTypeBuiltin)
        result = count_fn.infer_return_type([dfl_types.CollectionType(clkbuiltins.INT64)])
        assert result is clkbuiltins.UINT64

    def test_collection_element_type_builtin(self) -> None:
        """Test CollectionElementTypeBuiltin returns element type."""
        sum_fn = dfl.get_builtin("sum")
        assert sum_fn is not None
        assert isinstance(sum_fn, dfl.CollectionElementTypeBuiltin)
        result = sum_fn.infer_return_type([dfl_types.CollectionType(clkbuiltins.FLOAT64)])
        assert result is clkbuiltins.FLOAT64

    def test_flatten_builtin(self) -> None:
        """Test FlattenBuiltin unwraps nested collections."""
        flatten_fn = dfl.get_builtin("flatten")
        assert flatten_fn is not None
        assert isinstance(flatten_fn, dfl.FlattenBuiltin)
        nested = dfl_types.CollectionType(dfl_types.CollectionType(clkbuiltins.INT64))
        result = flatten_fn.infer_return_type([nested])
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.INT64

    def test_builtins_in_scope(self) -> None:
        """Test that builtins are accessible via BUILTINS_SCOPE."""
        abs_entity = dfl.BUILTINS_SCOPE.lookup("abs")
        assert abs_entity is not None
        assert isinstance(abs_entity, dfl.BuiltinFn)
        assert abs_entity.name == "abs"

    def test_make_typed_placeholder(self) -> None:
        """Test placeholder creation for type inference."""
        placeholder, _scope = dfl.make_typed_placeholder(clkbuiltins.FLOAT64)
        assert placeholder.name == "__placeholder__"
        assert isinstance(placeholder, dfl.FixedTypeBuiltin)
        result = placeholder.infer_return_type([])
        assert result is clkbuiltins.FLOAT64


class TestBuiltinTypeInference:
    """Tests for builtin function type inference from parsed source.

    All tests in this class parse expressions from source code and run full
    type inference to verify correct behavior.
    """

    def test_abs_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """abs(i) -> Int64."""
        result = _type_check("abs(i)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_abs_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """abs(f) -> Float64."""
        result = _type_check("abs(f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_min_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(i, j) -> Int64."""
        result = _type_check("min(i, j)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_min_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(f, g) -> Float64."""
        result = _type_check("min(f, g)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_max_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """max(i, j) -> Int64."""
        result = _type_check("max(i, j)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_max_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """max(f, g) -> Float64."""
        result = _type_check("max(f, g)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_min_variadic_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(i, j, i) with 3 args -> Int64."""
        result = _type_check("min(i, j, i)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_min_variadic_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(f, g, f) with 3 args -> Float64."""
        result = _type_check("min(f, g, f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_clamp_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """clamp(i, j, i) -> Int64."""
        result = _type_check("clamp(i, j, i)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_clamp_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """clamp(f, g, f) -> Float64."""
        result = _type_check("clamp(f, g, f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_sum_int64_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(coll_i) -> Int64."""
        result = _type_check("sum(coll_i)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_sum_float64_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(coll_f) -> Float64."""
        result = _type_check("sum(coll_f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_count_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """count(coll_i) -> UInt64."""
        result = _type_check("count(coll_i)", std_traits_registry)
        assert result is clkbuiltins.UINT64

    def test_mean_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """mean(coll_f) -> Float64."""
        result = _type_check("mean(coll_f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_is_positive(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """is_positive(i) -> Bool."""
        result = _type_check("is_positive(i)", std_traits_registry)
        assert result is clkbuiltins.BOOL

    def test_is_negative(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """is_negative(i) -> Bool."""
        result = _type_check("is_negative(i)", std_traits_registry)
        assert result is clkbuiltins.BOOL

    def test_is_zero(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """is_zero(i) -> Bool."""
        result = _type_check("is_zero(i)", std_traits_registry)
        assert result is clkbuiltins.BOOL

    def test_abs_of_min(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """abs(min(i, j)) -> Int64."""
        result = _type_check("abs(min(i, j))", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_sum_in_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(coll_i) + i -> Int64."""
        result = _type_check("sum(coll_i) + i", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_min_collection_int64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(coll_i) -> Int64."""
        result = _type_check("min(coll_i)", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_max_collection_float64(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """max(coll_f) -> Float64."""
        result = _type_check("max(coll_f)", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_min_expr_tuple(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min([i, j]) -> Int64 (ExprTuple as single collection arg)."""
        result = _type_check("min([i, j])", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_max_expr_tuple(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """max([f, g]) -> Float64 (ExprTuple as single collection arg)."""
        result = _type_check("max([f, g])", std_traits_registry)
        assert result is clkbuiltins.FLOAT64

    def test_min_collection_in_expression(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(coll_i) + i -> Int64."""
        result = _type_check("min(coll_i) + i", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_min_single_scalar_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(i) -> TypeError (single scalar, not a collection)."""
        with pytest.raises(dfl.TypeCheckError, match="requires a collection"):
            _type_check("min(i)", std_traits_registry)

    def test_max_single_scalar_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """max(f) -> TypeError (single scalar, not a collection)."""
        with pytest.raises(dfl.TypeCheckError, match="requires a collection"):
            _type_check("max(f)", std_traits_registry)

    def test_min_collection_bool_ord_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(coll_b) -> TypeCheckError (Bool does not implement Ord)."""
        with pytest.raises(dfl.TypeCheckError, match="implement Ord"):
            _type_check("min(coll_b)", std_traits_registry)

    def test_min_multi_arg_type_mismatch(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(i, f) -> TypeCheckError (Int64 vs Float64)."""
        with pytest.raises(dfl.TypeCheckError, match="Type mismatch"):
            _type_check("min(i, f)", std_traits_registry)

    def test_min_multi_arg_bool_ord_error(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """min(b, c) -> TypeCheckError (Bool does not implement Ord)."""
        with pytest.raises(dfl.TypeCheckError, match="implement Ord"):
            _type_check("min(b, c)", std_traits_registry)


class TestMapFilterBuiltins:
    """Tests for map and filter builtin functions.

    These tests verify that map and filter correctly infer return types
    based on the function argument.
    """

    def test_map_with_builtin_abs(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """map(abs, coll_i) -> Collection<Int64>."""
        result = _type_check("map(abs, coll_i)", std_traits_registry)
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.INT64

    def test_map_with_builtin_is_positive(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """map(is_positive, coll_i) -> Collection<Bool>."""
        result = _type_check("map(is_positive, coll_i)", std_traits_registry)
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.BOOL

    def test_filter_with_builtin_is_positive(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """filter(is_positive, coll_i) -> Collection<Int64>."""
        result = _type_check("filter(is_positive, coll_i)", std_traits_registry)
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.INT64

    def test_filter_with_builtin_is_negative(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """filter(is_negative, coll_f) -> Collection<Float64>."""
        result = _type_check("filter(is_negative, coll_f)", std_traits_registry)
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.FLOAT64

    def test_map_then_sum(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(map(abs, coll_i)) -> Int64."""
        result = _type_check("sum(map(abs, coll_i))", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_filter_then_count(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """count(filter(is_positive, coll_i)) -> UInt64."""
        result = _type_check("count(filter(is_positive, coll_i))", std_traits_registry)
        assert result is clkbuiltins.UINT64

    def test_filter_then_sum(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(filter(is_positive, coll_i)) -> Int64."""
        result = _type_check("sum(filter(is_positive, coll_i))", std_traits_registry)
        assert result is clkbuiltins.INT64

    def test_map_preserves_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Map returns a collection."""
        result = _type_check("map(is_zero, coll_f)", std_traits_registry)
        assert isinstance(result, dfl_types.CollectionType)
        assert result.element_type is clkbuiltins.BOOL


class TestMapFilterWithUserFunctions:
    """Tests for map and filter with user-defined functions.

    These tests compile complete .clk-style function definitions and verify
    that map/filter correctly infer return types.
    """

    def test_map_with_user_fn_double(self) -> None:
        """Map with user function that doubles values."""
        _fn_def, module = _compile_fn_def("fn double(x: Int64) -> Int64 { x * 2 }", "double")
        registry = dfl_types.get_trait_registry(module)

        # Create scope with the function and a collection
        scope = node.Scope(parent=module.inner_scope, uniq_path="test", module_id_for_errors=None)
        scope.names["coll"] = _NamedTestEntity("coll", scope, dfl_types.CollectionType(clkbuiltins.INT64))

        expr = _parse_and_convert("map(double, coll)", scope)
        typed = dfl.type_check_expr(expr, registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_map_with_inferred_return_type(self) -> None:
        """Map with user function where return type is inferred from explicit annotation."""
        # Note: Using explicit return type to avoid needing trait registry
        _fn_def, module = _compile_fn_def("fn negate(x: Int64) -> Int64 { -x }", "negate")
        registry = dfl_types.get_trait_registry(module)

        scope = node.Scope(parent=module.inner_scope, uniq_path="test", module_id_for_errors=None)
        scope.names["coll"] = _NamedTestEntity("coll", scope, dfl_types.CollectionType(clkbuiltins.INT64))

        expr = _parse_and_convert("map(negate, coll)", scope)
        typed = dfl.type_check_expr(expr, registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_filter_with_user_predicate(self) -> None:
        """Filter with user-defined predicate function."""
        _fn_def, module = _compile_fn_def("fn is_big(x: Int64) -> Bool { x > 100 }", "is_big")
        registry = dfl_types.get_trait_registry(module)

        scope = node.Scope(parent=module.inner_scope, uniq_path="test", module_id_for_errors=None)
        scope.names["coll"] = _NamedTestEntity("coll", scope, dfl_types.CollectionType(clkbuiltins.INT64))

        expr = _parse_and_convert("filter(is_big, coll)", scope)
        typed = dfl.type_check_expr(expr, registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_filter_with_inferred_predicate(self) -> None:
        """Filter with user predicate where return type is explicit."""
        # Note: Using explicit return type to avoid needing trait registry
        _fn_def, module = _compile_fn_def("fn is_small(x: Int64) -> Bool { x < 10 }", "is_small")
        registry = dfl_types.get_trait_registry(module)

        scope = node.Scope(parent=module.inner_scope, uniq_path="test", module_id_for_errors=None)
        scope.names["coll"] = _NamedTestEntity("coll", scope, dfl_types.CollectionType(clkbuiltins.INT64))

        expr = _parse_and_convert("filter(is_small, coll)", scope)
        typed = dfl.type_check_expr(expr, registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_map_changes_element_type(self) -> None:
        """Map with function that changes element type."""
        _fn_def, module = _compile_fn_def("fn to_bool(x: Int64) -> Bool { x > 0 }", "to_bool")
        registry = dfl_types.get_trait_registry(module)

        scope = node.Scope(parent=module.inner_scope, uniq_path="test", module_id_for_errors=None)
        scope.names["coll"] = _NamedTestEntity("coll", scope, dfl_types.CollectionType(clkbuiltins.INT64))

        expr = _parse_and_convert("map(to_bool, coll)", scope)
        typed = dfl.type_check_expr(expr, registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.BOOL


class TestFlattenBuiltin:
    """Tests for the flatten builtin function."""

    def test_flatten_nested_collection(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """flatten(Collection<Collection<T>>) -> Collection<T>."""
        scope = node.Scope(parent=dfl.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
        nested_type = dfl_types.CollectionType(dfl_types.CollectionType(clkbuiltins.INT64))
        scope.names["nested"] = _NamedTestEntity("nested", scope, nested_type)

        expr = _parse_and_convert("flatten(nested)", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_flatten_then_sum(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """sum(flatten(nested)) -> Int64."""
        scope = node.Scope(parent=dfl.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
        nested_type = dfl_types.CollectionType(dfl_types.CollectionType(clkbuiltins.INT64))
        scope.names["nested"] = _NamedTestEntity("nested", scope, nested_type)

        expr = _parse_and_convert("sum(flatten(nested))", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert typed.type_info is clkbuiltins.INT64


class TestDflLambdas:
    """Tests for DFL inline lambdas."""

    def test_parse_lambda_single_param(self) -> None:
        """Test parsing lambda with single parameter."""
        expr = _parse_and_convert("fn(x) x * 2")
        assert isinstance(expr, dfl.Lambda)
        assert expr.params == ("x",)
        assert isinstance(expr.body, dfl.Binary)
        assert expr.body.op == dfl.BinaryOp.MUL

    def test_parse_lambda_multiple_params(self) -> None:
        """Test parsing lambda with multiple parameters."""
        expr = _parse_and_convert("fn(x, y) x + y")
        assert isinstance(expr, dfl.Lambda)
        assert expr.params == ("x", "y")
        assert isinstance(expr.body, dfl.Binary)
        assert expr.body.op == dfl.BinaryOp.ADD

    def test_parse_lambda_no_params(self) -> None:
        """Test parsing lambda with no parameters."""
        expr = _parse_and_convert("fn() 42")
        assert isinstance(expr, dfl.Lambda)
        assert expr.params == ()
        assert isinstance(expr.body, primitive.DecimalLiteral)
        assert expr.body.value == 42

    def test_parse_lambda_in_map_call(self) -> None:
        """Test parsing lambda as first argument to map."""
        expr = _parse_and_convert("map(fn(x) x * 2, [1, 2, 3])")
        assert isinstance(expr, dfl.Call)
        assert isinstance(expr.args[0].expr, dfl.Lambda)
        assert isinstance(expr.args[1].expr, dfl.ExprTuple)

    def test_parse_lambda_in_filter_call(self) -> None:
        """Test parsing lambda as first argument to filter."""
        expr = _parse_and_convert("filter(fn(x) x > 0, [1, -2, 3])")
        assert isinstance(expr, dfl.Call)
        lambda_arg = expr.args[0].expr
        assert isinstance(lambda_arg, dfl.Lambda)
        assert lambda_arg.params == ("x",)
        # Body is: x > 0
        assert isinstance(lambda_arg.body, dfl.Binary)
        assert lambda_arg.body.op == dfl.BinaryOp.GT

    def test_substitute_lambda_param_simple(self) -> None:
        """Test substitute_lambda_param with simple replacement."""
        # Parse body: x + 1
        body = _parse_and_convert("x + 1")
        replacement = _parse_and_convert("5")
        result = dfl.substitute_lambda_param(body, "x", replacement)
        # Should be: 5 + 1
        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, primitive.DecimalLiteral)
        assert result.left.value == 5

    def test_substitute_lambda_param_nested(self) -> None:
        """Test substitute_lambda_param with nested occurrences of x * x + x."""
        body = _parse_and_convert("x * x + x")
        replacement = _parse_and_convert("3")
        result = dfl.substitute_lambda_param(body, "x", replacement)
        # Should be: 3 * 3 + 3
        assert isinstance(result, dfl.Binary)
        assert result.op == dfl.BinaryOp.ADD
        left_mult = result.left
        assert isinstance(left_mult, dfl.Binary)
        assert isinstance(left_mult.left, primitive.DecimalLiteral)
        assert left_mult.left.value == 3

    def test_substitute_lambda_param_no_match(self) -> None:
        """Test substitute_lambda_param when param name doesn't exist."""
        # Parse body: y + 1 (no x)
        body = _parse_and_convert("y + 1")
        replacement = _parse_and_convert("5")
        result = dfl.substitute_lambda_param(body, "x", replacement)
        # Body should be unchanged (still references y)
        assert isinstance(result, dfl.Binary)
        assert isinstance(result.left, dfl.Ref)
        assert result.left.path == ("y",)

    def test_map_expr_includes_lambda(self) -> None:
        """Test that map_expr handles Lambda nodes correctly."""
        expr = _parse_and_convert("fn(x) x + 1")
        assert isinstance(expr, dfl.Lambda)

        # map_expr only transforms immediate children (the body), not recursively.
        # To test Lambda handling, we just verify it returns a Lambda with transformed body.
        def wrap_binary(e: dfl.Expr) -> dfl.Expr:
            if isinstance(e, dfl.Binary):
                # Return a new Binary with swapped operands to prove transformation happened
                return dfl.Binary(e.op, e.right, e.left, e.span, e.ctx)
            return e

        mapped = dfl.map_expr(wrap_binary, expr)
        # map_expr should transform the body (a Binary)
        assert isinstance(mapped, dfl.Lambda)
        assert isinstance(mapped.body, dfl.Binary)
        # Operands should be swapped: was "x + 1", now right (1) and left (x) are reversed
        assert isinstance(mapped.body.left, primitive.DecimalLiteral)  # was right
        assert isinstance(mapped.body.right, dfl.Ref)  # was left

    def test_fold_expr_includes_lambda(self) -> None:
        """Test that fold_expr traverses Lambda nodes."""
        expr = _parse_and_convert("fn(x) x + 1")
        size = dfl.expr_size(expr)
        # Lambda(1) + Binary(1) + Ref(1) + Literal(1) = 4
        assert size == 4

    def test_find_refs_in_lambda(self) -> None:
        """Test that find_refs finds references in lambda body."""
        expr = _parse_and_convert("fn(x) x + y")
        refs = dfl.find_refs(expr)
        names = {r.path for r in refs}
        # Both 'x' (param) and 'y' (captured) should be found
        assert names == {("x",), ("y",)}

    def test_expand_map_with_lambda_single_element(self) -> None:
        """Test expanding map with lambda over single-element collection."""
        lambda_expr = _parse_and_convert("fn(x) x * 2")
        assert isinstance(lambda_expr, dfl.Lambda)

        collection_expr = _parse_and_convert("[5]")
        assert isinstance(collection_expr, dfl.ExprTuple)

        terminals = terminalsrc.TerminalSource("")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        expanded = dfl.expand_map_with_lambda(lambda_expr, collection_expr, ctx)

        # map(fn(x) x * 2, [5]) should expand to [5 * 2]
        assert isinstance(expanded, dfl.ExprTuple)
        assert len(expanded.elements) == 1
        elem = expanded.elements[0]
        assert isinstance(elem, dfl.Binary)
        assert elem.op == dfl.BinaryOp.MUL
        assert isinstance(elem.left, primitive.DecimalLiteral)
        assert elem.left.value == 5

    def test_expand_map_with_lambda_multiple_elements(self) -> None:
        """Test expanding map with lambda over multiple elements."""
        lambda_expr = _parse_and_convert("fn(x) x + 1")
        assert isinstance(lambda_expr, dfl.Lambda)

        collection_expr = _parse_and_convert("[1, 2, 3]")
        assert isinstance(collection_expr, dfl.ExprTuple)

        terminals = terminalsrc.TerminalSource("")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        expanded = dfl.expand_map_with_lambda(lambda_expr, collection_expr, ctx)

        # map(fn(x) x + 1, [1, 2, 3]) should expand to [1 + 1, 2 + 1, 3 + 1]
        assert isinstance(expanded, dfl.ExprTuple)
        assert len(expanded.elements) == 3
        for i, elem in enumerate(expanded.elements):
            assert isinstance(elem, dfl.Binary)
            assert elem.op == dfl.BinaryOp.ADD
            assert isinstance(elem.left, primitive.DecimalLiteral)
            assert elem.left.value == i + 1  # 1, 2, 3

    def test_lambda_type_inference_map(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type inference for lambda in map call."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("map(fn(x) x * 2, coll_i)", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_lambda_type_inference_map_type_change(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type inference for lambda that changes element type."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("map(fn(x) x > 0, coll_i)", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        # Lambda returns Bool, so result is Collection<Bool>
        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.BOOL

    def test_lambda_type_inference_filter(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test type inference for lambda in filter call."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("filter(fn(x) x > 0, coll_i)", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert isinstance(typed.type_info, dfl_types.CollectionType)
        assert typed.type_info.element_type is clkbuiltins.INT64

    def test_filter_lambda_must_return_bool(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test that filter rejects lambda that doesn't return Bool."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("filter(fn(x) x * 2, coll_i)", scope)

        with pytest.raises(TypeError, match="must return Bool"):
            dfl.type_check_expr(expr, std_traits_registry)

    def test_lambda_wrong_param_count_in_map(self) -> None:
        """Test that map rejects lambda with wrong parameter count."""
        lambda_expr = _parse_and_convert("fn(x, y) x + y")
        assert isinstance(lambda_expr, dfl.Lambda)

        collection_expr = _parse_and_convert("[1, 2, 3]")
        assert isinstance(collection_expr, dfl.ExprTuple)

        terminals = terminalsrc.TerminalSource("")
        module = _make_test_module(terminals)
        ctx = dfl.Context(scope=module.inner_scope, terminals=terminals, module_id=module.module_id)

        with pytest.raises(TypeError, match="exactly one parameter"):
            dfl.expand_map_with_lambda(lambda_expr, collection_expr, ctx)

    def test_lambda_with_complex_body(self) -> None:
        """Test lambda with complex expression body."""
        expr = _parse_and_convert("fn(x) if x > 0 then x else -x")
        assert isinstance(expr, dfl.Lambda)
        assert expr.params == ("x",)
        assert isinstance(expr.body, dfl.IfElse)

    def test_map_with_lambda_then_sum(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test sum(map(fn(x) ..., coll)) type inference."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("sum(map(fn(x) x * 2, coll_i))", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert typed.type_info is clkbuiltins.INT64

    def test_filter_with_lambda_then_count(self, std_traits_registry: dfl_types.TraitRegistry) -> None:
        """Test count(filter(fn(x) ..., coll)) type inference."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("count(filter(fn(x) x > 0, coll_i))", scope)
        typed = dfl.type_check_expr(expr, std_traits_registry)

        assert typed.type_info is clkbuiltins.UINT64

    def test_expand_all_calls_map_with_lambda(self) -> None:
        """Test expand_all_calls expands map with lambda."""
        expr = _parse_and_convert("map(fn(x) x * 2, [1, 2, 3])")

        expanded = dfl.expand_all_calls(expr)

        # Should expand to [1 * 2, 2 * 2, 3 * 2]
        assert isinstance(expanded, dfl.ExprTuple)
        assert len(expanded.elements) == 3
        for i, elem in enumerate(expanded.elements):
            assert isinstance(elem, dfl.Binary)
            assert elem.op == dfl.BinaryOp.MUL
            assert isinstance(elem.left, primitive.DecimalLiteral)
            assert elem.left.value == i + 1

    def test_expand_all_calls_nested(self) -> None:
        """Test expand_all_calls expands nested calls."""
        _fn_def, module = _compile_fn_def("fn double(x) { x * 2 }", "double")

        expr = _parse_and_convert("double(5)", module.inner_scope)

        expanded = dfl.expand_all_calls(expr)

        # Should expand to 5 * 2
        assert isinstance(expanded, dfl.Binary)
        assert expanded.op == dfl.BinaryOp.MUL
        assert isinstance(expanded.left, primitive.DecimalLiteral)
        assert expanded.left.value == 5

    def test_expand_all_calls_sum_of_map(self) -> None:
        """Test expand_all_calls expands map inside sum call, producing sum(tuple)."""
        expr = _parse_and_convert("sum(map(fn(x) x * 2, [1, 2]))")

        expanded = dfl.expand_all_calls(expr)

        # sum() is a builtin and stays as a Call, but map should be expanded
        assert isinstance(expanded, dfl.Call)
        assert len(expanded.args) == 1
        inner = expanded.args[0].expr
        assert isinstance(inner, dfl.ExprTuple)
        assert len(inner.elements) == 2

    def test_expand_all_calls_preserves_non_expandable(self) -> None:
        """Test expand_all_calls preserves calls that can't be expanded."""
        scope = _make_typed_scope()

        expr = _parse_and_convert("filter(fn(x) x > 0, coll_i)", scope)

        expanded = dfl.expand_all_calls(expr)

        # filter() with lambda can't be expanded at compile time
        assert isinstance(expanded, dfl.Call)
        assert isinstance(expanded.func, dfl.Ref)
        assert expanded.func.path == ("filter",)

    def test_validate_names_lambda_param_in_scope(self) -> None:
        """Test that validate_names recognizes lambda parameters as defined names."""
        expr = _parse_and_convert("fn(t) t + 1")
        dfl.validate_names(expr)  # Lambda param 't' should be in scope

    def test_validate_names_lambda_param_multi_params(self) -> None:
        """Test that validate_names resolves multiple lambda parameters."""
        expr = _parse_and_convert("fn(x, y) x + y")
        dfl.validate_names(expr)  # Lambda params 'x' and 'y' should be in scope

    def test_check_no_closures_fn_with_lambda_in_body(self) -> None:
        """Test that check_no_closures handles lambdas inside function bodies."""
        fn_def, _module = _compile_fn_def(
            "fn apply_twice(f, x) { map(fn(i) f(i), [x, x]) }",
            "apply_twice",
        )
        # Lambda param 'i' should be in scope; fn params 'f' and 'x' are also valid.
        dfl.check_no_closures(fn_def)

    def test_expand_all_calls_lambda_calls_function(self) -> None:
        """Test expand_all_calls expands function calls inside lambda body."""
        _fn_def, module = _compile_fn_def("fn double(x) { x * 2 }", "double")

        expr = _parse_and_convert("map(fn(x) double(x), [1, 2])", module.inner_scope)

        expanded = dfl.expand_all_calls(expr)

        # map should expand to [double(1), double(2)]
        # and then double(1) and double(2) should expand to [1 * 2, 2 * 2]
        assert isinstance(expanded, dfl.ExprTuple)
        assert len(expanded.elements) == 2

        # Each element should be Binary(MUL, literal, 2)
        for i, elem in enumerate(expanded.elements):
            assert isinstance(elem, dfl.Binary), f"Element {i} should be Binary but got {type(elem)}"
            assert elem.op == dfl.BinaryOp.MUL
            assert isinstance(elem.left, primitive.DecimalLiteral)
            assert elem.left.value == i + 1

    def test_expand_all_calls_fn_returning_map(self) -> None:
        """Expansion re-expands results so fn bodies containing map() are fully expanded."""
        _fn_def, module = _compile_fn_def("fn apply_all(f, items...) { map(f, items) }", "apply_all")

        expr = _parse_and_convert("apply_all(fn(x) x * 2, 10, 20)", module.inner_scope)

        expanded = dfl.expand_all_calls(expr)

        # apply_all expands to map(fn(x) x*2, [10, 20])
        # which then expands to [10*2, 20*2]
        assert isinstance(expanded, dfl.ExprTuple), f"Expected ExprTuple, got {type(expanded)}"
        assert len(expanded.elements) == 2

        for i, elem in enumerate(expanded.elements):
            assert isinstance(elem, dfl.Binary), f"Element {i} should be Binary(MUL) but got {type(elem)}"
            assert elem.op == dfl.BinaryOp.MUL
