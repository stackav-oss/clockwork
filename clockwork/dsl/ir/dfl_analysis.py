# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Static analysis utilities for DFL expressions.

This module provides reusable analysis functions that build on the expression
traversal infrastructure (map_expr, fold_expr) to support static analysis and
transformation of DFL expressions.

Functions:
    free_vars: Extract variable names referenced in an expression.
    find_calls: Find all function calls in an expression.
    substitute: Substitute expressions for named references.
"""

from __future__ import annotations

import itertools
from typing import TYPE_CHECKING

from clockwork.dsl.ir import dfl

if TYPE_CHECKING:
    from collections.abc import Callable


def free_vars(expr: dfl.Expr) -> frozenset[str]:
    """Extract all variable names referenced in an expression.

    This collects all Ref names that appear in the expression tree.
    Useful for dependency analysis and determining what bindings an
    expression requires.

    Args:
        expr: The expression to analyze.

    Returns:
        Frozen set of variable names referenced in the expression.

    Example:
        >>> free_vars(parse("a + b * c"))
        frozenset({'a', 'b', 'c'})
    """

    def collect_refs(node: dfl.Expr, child_results: list[frozenset[str]]) -> frozenset[str]:
        result = frozenset[str]().union(*child_results)
        if isinstance(node, dfl.Ref) and node.path:
            result = result | frozenset({node.path[0]})
        return result

    return dfl.fold_expr(collect_refs, expr)


def find_calls(expr: dfl.Expr, func_names: set[str] | None = None) -> list[dfl.Call]:
    """Find all function calls in an expression, optionally filtered by name.

    Args:
        expr: The expression to search.
        func_names: Optional set of function names to filter by. If None,
            returns all calls.

    Returns:
        List of Call nodes found in the expression tree, in depth-first order.

    Example:
        >>> find_calls(parse("max(a, min(b, c))"))
        [Call(func=Ref('max'), ...), Call(func=Ref('min'), ...)]
        >>> find_calls(parse("max(a, min(b, c))"), {"min"})
        [Call(func=Ref('min'), ...)]
    """

    def collect_calls(node: dfl.Expr, child_results: list[list[dfl.Call]]) -> list[dfl.Call]:
        result: list[dfl.Call] = []
        for child_calls in child_results:
            result.extend(child_calls)

        if isinstance(node, dfl.Call):
            # Check if we should include this call
            if func_names is None:
                result.append(node)
            elif isinstance(node.func, dfl.Ref) and node.func.path:
                func_name = node.func.path[0]
                if func_name in func_names:
                    result.append(node)

        return result

    return dfl.fold_expr(collect_calls, expr)


def substitute(expr: dfl.Expr, bindings: dict[str, dfl.Expr]) -> dfl.Expr:
    """Substitute expressions for named references.

    This is a more general version of substitute_params that works on any Ref,
    not just function parameters. Useful for inlining, specialization, and
    expression transformation.

    Args:
        expr: The expression to transform.
        bindings: Map from variable name to replacement expression.

    Returns:
        New expression with references replaced according to bindings.

    Example:
        >>> substitute(parse("a + b"), {"a": parse("1"), "b": parse("2")})
        # Returns IR equivalent to "1 + 2"
    """

    def replace_ref(e: dfl.Expr) -> dfl.Expr:
        if isinstance(e, dfl.Ref) and len(e.path) == 1:
            name = e.path[0]
            if name in bindings:
                return bindings[name]
        return dfl.map_expr(replace_ref, e)

    return replace_ref(expr)


def refs_in_expr(expr: dfl.Expr) -> list[dfl.Ref]:
    """Find all Ref nodes in an expression tree.

    This is an alias for dfl.find_refs but provided here for convenience.

    Args:
        expr: The expression to search.

    Returns:
        List of all Ref nodes found, in depth-first order.
    """
    return dfl.find_refs(expr)


def _collect_statements(
    node: dfl.Expr,
    child_results: list[list[dfl.Statement]],
) -> list[dfl.Statement]:
    result: list[dfl.Statement] = []
    match node:
        case dfl.CstPassthrough() | dfl.CondExpr() | dfl.Match() | dfl.IfElse():
            result.append(node)
        case dfl.Definition(name=name, value=value, options=options, cst_node=cst_node, span=span, ctx=ctx):
            if options is not None:
                result.append(
                    dfl.Definition(
                        name=name,
                        value=value,
                        options=dfl.Block(
                            name=options.name,
                            statements=list(itertools.chain.from_iterable(child_results)),
                            span=options.span,
                            ctx=options.ctx,
                        ),
                        cst_node=cst_node,
                        span=span,
                        ctx=ctx,
                    )
                )
            else:
                result.append(node)
        case dfl.Block(name=name, span=span, ctx=ctx):
            if name is None:
                for children in child_results:
                    result.extend(children)
            else:
                result.append(
                    dfl.Block(
                        name=name, statements=list(itertools.chain.from_iterable(child_results)), span=span, ctx=ctx
                    )
                )

    return result


def flatten_blocks(statement: dfl.Statement) -> dfl.Statement:
    """Flatten the statements from nested anonymous blocks into their parents.

    Args:
        statement: The statement to start flattening from.

    Returns:
        The flattened block.
    """
    match statement:
        case dfl.Block(name=name, span=span, ctx=ctx):
            statements = dfl.fold_expr(_collect_statements, statement)
            return dfl.Block(
                name=name,
                statements=statements,
                span=span,
                ctx=ctx,
            )
        case dfl.Definition(name=name, value=value, options=options, cst_node=cst_node, span=span, ctx=ctx):
            new_options = options
            if options is not None:
                statements = dfl.fold_expr(_collect_statements, options)
                new_options = dfl.Block(name=options.name, statements=statements, span=span, ctx=ctx)
            return dfl.Definition(
                name=name,
                value=value,
                options=new_options,
                cst_node=cst_node,
                span=span,
                ctx=ctx,
            )
        case _:
            return statement


def find_with_pred(expr: dfl.Expr, predicate: Callable[[dfl.Expr], bool]) -> list[dfl.Expr]:
    """Extract all expressions in an expression tree that match a predicate.

    Args:
        expr: The expression to search.
        predicate: The predicate to check nodes with.

    Returns:
        List of matching nodes found in the expression tree, in depth-first order.

    """

    def collect_matching(node: dfl.Expr, child_results: list[list[dfl.Expr]]) -> list[dfl.Expr]:
        result: list[dfl.Expr] = []
        for children in child_results:
            result.extend(children)

        if predicate(node):
            result.append(node)

        return result

    return dfl.fold_expr(collect_matching, expr)
