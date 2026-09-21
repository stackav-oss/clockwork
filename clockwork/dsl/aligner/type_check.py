# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — type-checking and validation of aligner body statements."""

from __future__ import annotations

from typing import TYPE_CHECKING

from clockwork.dsl.ir import aligner_builtins, clkbuiltins, dfl, dfl_types
from clockwork.dsl.ir.aligner import Aligner, AlignerLetBinding, AlignerSpecStmt

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


def type_check_aligner(aligner_node: Aligner, compiler_context: CompilerContext) -> None:
    """Load aligner builtins and type-check an aligner's body statements.

    Args:
        aligner_node: The parsed Aligner IR node.
        compiler_context: The active compiler context.

    Raises:
        dfl.TypeCheckError: If a body statement fails type checking.
    """
    aligner_node.body_scope.parent = aligner_builtins.make_body_parent_scope(compiler_context, aligner_node.inner_scope)

    registry = dfl_types.get_trait_registry(aligner_node.module)

    for stmt in aligner_node.body_stmts:
        match stmt:
            case AlignerLetBinding(value=value) as let_binding:
                expanded = dfl.expand_all_calls(value)
                typed = dfl.type_check_expr(expanded, registry)
                let_binding.type_info = typed.type_info
            case AlignerSpecStmt(expr=spec_expr) as spec_stmt:
                expanded = dfl.expand_all_calls(spec_expr)
                typed = dfl.type_check_expr(expanded, registry)
                if typed.type_info is not clkbuiltins.SPEC_TYPE:
                    msg = f"Spec statement must have type Spec, got {typed.type_info}"
                    raise dfl.TypeCheckError(msg)
                spec_stmt.expanded_expr = expanded
            case dfl.FnDef():
                dfl.check_no_closures(stmt)  # Verify no runtime closures
