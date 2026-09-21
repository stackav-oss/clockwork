# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Statement IR nodes."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import clkbuiltins, expr, fmt_string, node, typesys
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override


@dataclass
class NewStmt(node.CstNode[cst.NewStmt], node.DocableEntity):
    """IR node class holding a new expression."""

    name: str
    typespec: expr.Expr | typesys.InstantiatableEntity
    instantiation: typesys.Instantiation | None

    @classmethod
    def from_cst(cls: type[NewStmt], cst_node: cst.NewStmt, module: node.Module) -> NewStmt:
        """Create a new IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(doc, module, cst_node, name, typespec, None)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.typespec, expr.Expr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve NewStmt twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        orig_typespec = self.typespec.evaluate()
        typespec = orig_typespec
        instantiation = None
        if isinstance(typespec, ImmutableBinding):
            typespec = typespec.value
        if isinstance(typespec, InstantiateStmt):
            typespec = typespec.instantiated or typespec.typespec
        if isinstance(typespec, typesys.Instantiation) and isinstance(
            typespec.instantiates, InstantiatableEntityFactory
        ):
            instantiation = typespec
            typespec = typespec.instantiates.make_from_new_stmt(new_stmt=self)
        if not isinstance(typespec, typesys.InstantiatableEntity):
            msg = self.typespec.append_error_line(
                f"Expected an instantiatable type in new expression but got {orig_typespec}"
            )
            raise TypeError(msg)
        self.typespec = typespec
        self.instantiation = instantiation


@dataclass
class ImmutableBinding(
    node.CstNode[cst.AssignmentStmt],
    node.DocableEntity,
    typesys.NamedValue,
    typesys.MembershipEntity,
    node.NamedBinding[typesys.Value],
):
    """IR Node representing an immutable binding of a name to an entity within a scope.

    Note that the *binding* is immutable; this does not necessarily imply that the entity is immutable, just that the name can't be made to refer to a different entity later.
    """

    value: typesys.Value | expr.Expr
    typespec: expr.TypeExpression | None
    attributes: node.ClkAttributes | None

    @classmethod
    def from_cst(
        cls: type[ImmutableBinding],
        cst_node: cst.AssignmentStmt,
        module: node.Module,
        scope: node.Scope,
    ) -> ImmutableBinding:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        attributes = module.handle_outer_attrs(cst_node.maybe_clk_outer_attrs())
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
            attributes=attributes,
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

        if isinstance(self.value, ImmutableBinding):
            self.value = self.value.value
        if isinstance(self.value, fmt_string.UnevaluatedFmtString):
            self.value = self.value.evaluate_from_scope(self.scope)

    def get_resolved(self) -> typesys.Value:
        """Get the resolved value of the binding."""
        if isinstance(self.value, expr.Expr):
            msg = self.append_error_line(f"Binding {self.name} has not been resolved")
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is more correct here than TypeError)
        return self.value

    @override
    def bound_value(self) -> typesys.Value:
        """To comply with the interface of typesys.Binding."""
        return self.get_resolved()

    @override
    def concrete_type_info(self) -> typesys.TypeVal | typesys.InferenceVar:
        """Delegate to the bound value's concrete type info.

        When the binding has been resolved (e.g. a channel alias such as
        ``Foo = channels::Bar<...>``), the concrete type is determined by the
        bound value rather than by the inference variable stored on this node.
        This ensures that channel aliases are recognised as ``CHANNEL_TYPE``
        rather than the generic ``TYPE_TYPE`` that parameterised channel
        templates carry.
        """
        if isinstance(self.value, expr.Expr):
            return self.type_info
        return self.value.concrete_type_info()

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


@dataclass
class InstantiateStmt(
    typesys.TypeDef,
    node.DocableEntity,
    node.CstNode[cst.ClkInstantiateStmt | cst.CppInstantiateStmt | cst.CasingInstantiateStmt],
):
    """IR Node representing an instantiate statement in low boilerplate clockwork files.

    Attributes:
        typespec: Instantiated entity
        attributes: Outer attributes
        instantiated: Instantiated entity
    """

    typespec: expr.InstantiateExpr | typesys.Instantiation
    attributes: node.ClkAttributes | None = field(repr=False)
    instantiated: node.NamedEntity | None = field(repr=False)

    @classmethod
    def from_cst(
        cls: type[InstantiateStmt],
        cst_node: cst.ClkInstantiateStmt | cst.CppInstantiateStmt | cst.CasingInstantiateStmt,
        module: node.Module,
    ) -> InstantiateStmt:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        attributes = None
        doc = None
        if cst_node.kind == cst.ClkInstantiateStmt.kind:
            attributes = module.handle_outer_attrs(cst_node.maybe_clk_outer_attrs())
            assert attributes is not None
            doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = ""
        if cst_node.kind != cst.CasingInstantiateStmt.kind and (cst_name := cst_node.maybe_name()) is not None:
            name = get_span(cst_name.child_value(), module.terminals)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        if not isinstance(typespec, expr.InstantiateExpr):
            msg = node.append_error_line(
                cst_node.child_typespec(), module, "Instantiate statement expects an instantiation"
            )
            raise TypeError(msg)

        return cls(
            doc=doc,
            module=module,
            cst_node=cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            scope=module.inner_scope,
            name=name,
            typespec=typespec,
            attributes=attributes,
            instantiated=None,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.typespec, expr.InstantiateExpr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve instantiate statement twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)

        eval_result = self.typespec.evaluate()
        if not isinstance(eval_result, typesys.Instantiation):
            msg = node.append_error_line(
                self.cst_node, self.module, "Error in type exression for instantiate statement"
            )
            raise TypeError(msg)
        self.typespec = eval_result
        if isinstance(self.typespec.instantiates, InstantiationFactory):
            self.instantiated = self.typespec.instantiates.make_from_instantiate_stmt(instantiate_stmt=self)

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        assert isinstance(self.typespec, typesys.Instantiation)
        return self.typespec.value_key()


class InstantiatableEntityFactory(ABC):
    """Base class for things which can make an InstantiatableEntity fron a NewStmt.

    This class is needed to break circular imports like statement <-> cog.
    """

    @abstractmethod
    def make_from_new_stmt(
        self,
        *,
        new_stmt: NewStmt,
    ) -> typesys.InstantiatableEntity:
        """Make an InstantiatabeEntity from a NewStmt."""


class InstantiationFactory(ABC):
    """Base class for things which can make an instantiation of an entity from an InstantiateStmt.

    This class is needed to break circular imports like statement <-> cog.
    """

    @abstractmethod
    def make_from_instantiate_stmt(
        self,
        *,
        instantiate_stmt: InstantiateStmt,
    ) -> node.NamedEntity:
        """Instantiate an entity from an InstantiateStmt."""
