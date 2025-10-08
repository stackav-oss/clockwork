# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Statement IR nodes."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, typesys
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override


@dataclass
class NewStmt(node.CstNode[cst.NewStmt], node.DocableEntity):
    """IR node class holding a new expression."""

    name: str
    typespec: expr.Expr | typesys.InstantiatableEntity

    @classmethod
    def from_cst(cls: type[NewStmt], cst_node: cst.NewStmt, module: node.Module) -> NewStmt:
        """Create a new IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        typesys.unify(typespec.type_info, clkbuiltins.TYPE_TYPE)
        return cls(doc, module, cst_node, name, typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.typespec, expr.Expr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve NewStmt twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        typespec = self.typespec.evaluate()
        if not isinstance(typespec, typesys.InstantiatableEntity):
            msg = self.typespec.append_error_line(
                f"Expected an instantiatable type in new expression but got {typespec}"
            )
            raise TypeError(msg)
        self.typespec = typespec


@dataclass
class ImmutableBinding(
    node.CstNode[cst.AssignmentStmt],
    node.DocableEntity,
    typesys.NamedValue,
    typesys.MembershipEntity,
):
    """IR Node representing an immutable binding of a name to an entity within a scope.

    Note that the *binding* is immutable; this does not necessarily imply that the entity is immutable, just that the name can't be made to refer to a different entity later.
    """

    value: typesys.Value | expr.Expr
    typespec: expr.TypeExpression | None

    @classmethod
    def from_cst(
        cls: type[ImmutableBinding], cst_node: cst.AssignmentStmt, module: node.Module, scope: node.Scope
    ) -> ImmutableBinding:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)

        type_info = typesys.InferenceVar.make(module, cst_node)
        rhs = expr.Expr.from_cst(cst_node.child_rhs(), module)
        typesys.unify(type_info, rhs.type_info)
        typespec = None
        if (child_typespec := cst_node.maybe_typespec()) is not None:
            typespec = expr.TypeExpression.make(expr.Expr.from_cst(child_typespec, module))
            typesys.unify(typespec.inference_var, type_info)

        result = cls(
            type_info=type_info,
            name=name,
            scope=scope,
            doc=doc,
            module=module,
            cst_node=cst_node,
            value=rhs,
            typespec=typespec,
        )
        scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.value, expr.Expr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve binding twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)

        if self.typespec:
            eval_result = self.typespec.evaluate()
            if not isinstance(eval_result, typesys.TypeVal):
                msg = self.typespec.append_error_line(f"Error in type exression for {self.name}")
                raise TypeError(msg)

        self.value = self.value.evaluate()

    def get_resolved(self) -> typesys.Value:
        """Get the resolved value of the binding."""
        if isinstance(self.value, expr.Expr):
            msg = self.append_error_line(f"Binding {self.name} has not been resolved")
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is more correct here than TypeError)
        return self.value

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a definition in the namespace entity.

        Returns:
            The entity with that name, or None if not found.
        """
        if not isinstance(self.value, typesys.MembershipEntity):
            msg = self.append_error_line(f"Bound entity does not support attribute access: {self.value}")
            raise TypeError(msg)
        return self.value.attribute(name)
