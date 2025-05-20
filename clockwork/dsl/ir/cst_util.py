# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utilities for dealing with the Clockwork CST (Concrete Syntax Tree)."""

from __future__ import annotations

from decimal import Decimal
from typing import TYPE_CHECKING, Any

from fltk.fegen.pyrt.terminalsrc import Span, TerminalSource

if TYPE_CHECKING:
    from clockwork.dsl import cst
    from clockwork.dsl.ir.module_id import ModuleID


def get_span(span: Span, terminals: TerminalSource) -> str:
    """Retrieve a source span as a string.

    Args:
        span: The span of source desired.
        terminals: The terminal source containing the span.

    Returns:
        The corresponding source text as a string.
    """
    return terminals.terminals[span.start : span.end]


def span_for_node(node: Any) -> Span | None:  # noqa: ANN401 (Any required to break circular dependency on callers)
    """Attempt to get a Span for the given Node.

    Args:
        node: A CST node.

    Returns:
        The source span corresponding to that node if possible, else None.
    """
    if isinstance(node, Span):
        return node
    try:
        if isinstance(node.span, Span):
            return node.span
    except AttributeError:
        pass
    return None


def format_line_with_error(span: Span, terminals: TerminalSource, module_id: ModuleID | None) -> str:
    """Format an error string highlighting the source line with the error.

    Args:
        span: The source span with the error.
        terminals: The terminal source containing the span.
        module_id: The ModuleID the error is located.
    """
    module_path = "(unknown location)"
    if module_id is not None and module_id.name:
        module_path = ""
        # add repo if it's not empty
        if module_id.repo:
            module_path += f"@{module_id.repo}: "
        # add the file path
        module_path += str(module_id.get_base_path())

    line_col = terminals.pos_to_line_col(span.start)
    return (
        f"\nIn {module_path}:{line_col.line + 1}:{line_col.col + 1}:\n"
        f"{get_span(line_col.line_span, terminals)}\n"
        f"{' ' * line_col.col}^\n"
    )


def strip_numeric_separators(literal: str) -> str:
    """Strips the optional digit separator from number literals.

    Clockwork syntax allows use of the single-quote character as a digit separator.  This function strips those out.
    """
    return literal.replace("'", "")


def decimal_from_cst(number: cst.Number, terminals: TerminalSource) -> Decimal:
    """Convert a CST Number to a Decimal.

    Args:
        number: The CST node corresponding to the number; this must have a span!
        terminals: The terminal source containing the span.

    Returns:
        A Python Decimal representing the source number with no loss of precision.
    """
    return Decimal(strip_numeric_separators(get_span(number.span, terminals)))


def int_from_cst(number: cst.NonnegativeInteger | cst.Integer, terminals: TerminalSource) -> int:
    """Convert a CST integer literal to a Python int.

    Args:
        number: The CST node corresponding to the number; this must have a span!
        terminals: The terminal source containing the span.

    Returns:
        A Python int representing the integer.
    """
    return int(strip_numeric_separators(get_span(number.span, terminals)))
