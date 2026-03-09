# Copyright 2025 Stack AV Co.
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

from clockwork.dsl.ir import dfl


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
