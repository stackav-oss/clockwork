# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for expr module."""

import re
from copy import deepcopy
from typing import TypeVar
from unittest.mock import MagicMock

import pytest
from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl import clockwork_parser as parser
from clockwork.dsl import compiler_context
from clockwork.dsl.ir import clkbuiltins, expr, node, parse, typesys
from clockwork.dsl.ir.module_id import ModuleID
from fltk.fegen.pyrt import terminalsrc


@pytest.fixture()
def mock_module() -> node.Module:
    return node.Module(
        doc=None,
        module_id=ModuleID("", "testmod"),
        inner_scope=node.Scope(parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="testmod", module_id_for_errors=None),
        terminals=None,
        cst_node=None,
        unresolved_imports=[],
        context=compiler_context.CompilerContext(),
        generates=None,
        inner_attrs=None,
    )


CstType = TypeVar("CstType")


def _parse_as(
    source: str,
    cst_type: type[CstType],
) -> tuple[CstType, terminalsrc.TerminalSource]:
    """Parse a source string as a particular CST node type."""
    terminals = terminalsrc.TerminalSource(source + "\n")
    clk_parser = parser.Parser(terminalsrc=terminals)
    result = parse.parse_rule(clk_parser, "expr", cst_type)
    assert result is not None
    assert result.pos == len(source), parse.format_parse_error(clk_parser, terminals)

    return result.result, terminals


def test_simple_expr_evaluate_resolved_value(mock_module: MagicMock) -> None:
    expr_val = MagicMock(spec=typesys.Value)
    simple_expr = expr.SimpleExpr(
        value=expr_val,
        resolved_value=expr_val,
        cst_node=MagicMock(spec=cst.Expr),
        module=mock_module,
        type_info=MagicMock(),
    )

    assert simple_expr.evaluate() is expr_val


def test_simple_expr_evaluate_unresolved_value_raises(mock_module: node.Module) -> None:
    deferred_lookup = MagicMock(spec=node.DeferredLookup)

    simple_expr = expr.SimpleExpr(
        value=deferred_lookup,
        resolved_value=None,
        cst_node=MagicMock(spec=cst.Expr),
        module=mock_module,
        type_info=MagicMock(),
    )

    with pytest.raises(
        ValueError,
        match=re.escape("During expression evaluation, encountered unresolved DeferredLookup: "),
    ):
        simple_expr.evaluate()


def test_namespace_lookup_expr(mock_module: node.Module) -> None:
    expr_cst, terminals = _parse_as("foo::bar", cst.Expr)
    with pytest.raises(ValueError, match=re.escape("Cannot construct IR nodes from CST without a TerminalSource")):
        expr_ir = expr.Expr.from_cst(cst_node=expr_cst, module=mock_module)
    mock_module.terminals = terminals
    expr_ir = expr.Expr.from_cst(cst_node=expr_cst, module=mock_module)
    assert isinstance(expr_ir, expr.NamespaceLookupExpr)
    assert isinstance(expr_ir.namespace, node.DeferredLookup)
    assert expr_ir.namespace.identifier == "foo"
    assert expr_ir.namespace.expected_type is node.NamespaceEntity
    assert expr_ir.name == "bar"
    with pytest.raises(ValueError, match=re.escape("Undefined identifier foo")):
        node.resolve_names(expr_ir, scope=mock_module.inner_scope)
    ns_mod = node.NamespacedModule(
        name="foo",
        scope=node.Scope(parent=None, uniq_path="test", module_id_for_errors=None),
        module=mock_module,
        cst_node=None,
        extern_module=deepcopy(mock_module),
    )
    mock_module.inner_scope.define("foo", ns_mod, terminals=None)
    with pytest.raises(
        ValueError,
        match=re.escape(
            "During expression evaluation, encountered unresolved DeferredLookup: foo",
        ),
    ):
        expr_ir.evaluate()
    node.resolve_names(expr_ir, scope=mock_module.inner_scope)
    with pytest.raises(
        KeyError,
        match=re.escape(f"No such name bar in namespace {ns_mod.name}"),
    ):
        expr_ir.evaluate()
    bar = clkbuiltins.TYPE_TYPE
    ns_mod.extern_module.inner_scope.define("bar", bar, terminals=None)
    assert expr_ir.evaluate() is bar
    # Evaluate again, should be idempotent:
    assert expr_ir.evaluate() is bar
    # Make sure we are unifying types correctly, by forcing a failure:
    with pytest.raises(TypeError, match="Type inference failed"):
        typesys.unify(clkbuiltins.INT32, expr_ir.type_info)


def test_parse_rule_accepts_kind() -> None:
    terminals = terminalsrc.TerminalSource("foo::bar\n")
    clk_parser = parser.Parser(terminalsrc=terminals)
    result = parse.parse_rule(clk_parser, "expr", cst.Expr.kind)

    assert result is not None
    assert result.result.kind == cst.Expr.kind
    assert result.pos == len("foo::bar")


def test_instantiate_expr_evaluate_with_generic_type_value(mock_module: node.Module) -> None:
    expr_ir = expr.Expr.from_str("FixedArray<UInt64, 1>", mock_module)
    assert isinstance(expr_ir, expr.InstantiateExpr)
    # Resolve built-in types
    node.resolve_names(expr_ir, scope=clkbuiltins.BUILTINS_SCOPE)
    result = expr_ir.evaluate()
    assert isinstance(result, typesys.Instantiation)
    # Evaluating again is idempotent:
    result2 = expr_ir.evaluate()
    assert result2 is result


def test_instantiate_expr_evaluate_with_non_generic_type_value(mock_module: node.Module) -> None:
    expr_ir = expr.Expr.from_str("Int32<1>", mock_module)
    assert isinstance(expr_ir, expr.InstantiateExpr)
    node.resolve_names(expr_ir, scope=clkbuiltins.BUILTINS_SCOPE)
    with pytest.raises(
        TypeError,
        match=re.escape(
            f"Operand of instantiation expression has no generic parameters: {clkbuiltins.INT32}",
        ),
    ):
        expr_ir.evaluate()


def test_list_literal(mock_module: node.Module) -> None:
    expr_ir = expr.Expr.from_str('Foo<[1, 2, "blah"]>', mock_module)
    assert isinstance(expr_ir, expr.InstantiateExpr)
    assert len(expr_ir.arguments) == 1
    _, list_ir = expr_ir.arguments[0]
    assert isinstance(list_ir, expr.ListLiteral)
    assert len(list_ir.elements) == 3
    assert list_ir.type_info == clkbuiltins.LIST
    assert all(isinstance(element, expr.SimpleExpr) for element in list_ir.elements)
    val_list = list_ir.evaluate()
    assert isinstance(val_list, typesys.Values)
    assert len(val_list.elements) == 3
    typesys.unify(clkbuiltins.UINT64, val_list.elements[0].type_info)
    typesys.unify(clkbuiltins.UINT64, val_list.elements[1].type_info)
    typesys.unify(clkbuiltins.STRING, val_list.elements[2].type_info)
    assert val_list.value_key() == "[1,2,blah]"


def test_type_union(mock_module: node.Module) -> None:
    union_ir = expr.Expr.from_str("Int32 |Float32|Bool", mock_module)
    assert isinstance(union_ir, expr.TypeUnionExpr)
    assert len(union_ir.member_types) == 3
    assert union_ir.type_info == clkbuiltins.TYPE_TYPE
    assert all(isinstance(member, expr.SimpleExpr) for member in union_ir.member_types)
    node.resolve_names(union_ir, scope=clkbuiltins.BUILTINS_SCOPE)
    union_type = union_ir.evaluate()
    assert isinstance(union_type, typesys.TypeUnion)
    assert len(union_type.types) == 3
    assert union_type.types[0] is clkbuiltins.INT32
    assert union_type.types[1] is clkbuiltins.FLOAT32
    assert union_type.types[2] is clkbuiltins.BOOL
