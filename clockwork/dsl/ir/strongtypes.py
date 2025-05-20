# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Strong type support."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, typesys
from clockwork.dsl.ir.cst_util import get_span


@dataclass
class Tag(node.CstNode[cst.Tag], node.DocRequiredEntity, typesys.TypeDef):
    """A type tag declared in Clockwork."""

    @classmethod
    def from_cst(cls: type[Tag], cst_node: cst.Tag, module: node.Module, scope: node.Scope) -> Tag:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        result = cls(doc=doc, module=module, cst_node=cst_node, name=name, scope=scope, type_info=clkbuiltins.TYPE_TYPE)
        scope.define(name, result, module.terminals)
        return result


@dataclass
class StrongType(node.CstNode[cst.StrongType], node.DocRequiredEntity, typesys.TypeDef):
    """A class that is a strong type for a primitive type."""

    typespec: clkbuiltins.PrimitiveType | expr.Expr

    @classmethod
    def from_cst(cls: type[StrongType], cst_node: cst.StrongType, module: node.Module) -> StrongType:
        """Construct an IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        underlying_type = cst_node.child_underlying_type()
        typespec = expr.Expr.from_cst(underlying_type.child_typespec(), module)

        return StrongType(
            doc=doc,
            name=name,
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            typespec=typespec,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        if not isinstance(self.typespec, expr.Expr):
            msg = "Attempted to resolve twice."
            raise TypeError(msg)

        typespec = self.typespec.evaluate()

        if not isinstance(typespec, clkbuiltins.PrimitiveType):
            msg = self.typespec.append_error_line("Strong types only support underlying types that are primitive.")
            raise TypeError(msg)

        self.typespec = typespec

    def get_underlying_type(self) -> clkbuiltins.PrimitiveType:
        """Return the underlying type of this strong type."""
        if not isinstance(self.typespec, clkbuiltins.PrimitiveType):
            msg = self.typespec.append_error_line("Not resolved yet.")
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is used to indicate a programming error)

        return self.typespec

    def satisfies(self, constraint: typesys.NumericType) -> bool:
        """Check if this type satisfies a constraint.

        At present, we have a very simple constraint system that is special-cased for integer and floating point
        numbers.  By default, no types satisfy these constraints, but the child classes for these types override this
        appropriately.
        """
        if not isinstance(self.typespec, clkbuiltins.PrimitiveType):
            msg = self.typespec.append_error_line("Not resolved yet.")
            raise TypeError(msg)

        return self.typespec.satisfies(constraint)
