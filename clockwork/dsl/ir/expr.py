# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Clockwork expression IR and evaluation logic."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, node, parse, primitive, typesys
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.module_id import ModuleID
from typing_extensions import override

if TYPE_CHECKING:  # pragma: nocover
    from collections.abc import Collection, Sequence


@dataclass
class Expr(typesys.Value, node.CstNode[cst.Expr], ABC):
    """Base IR node class representing an expression."""

    resolved_value: typesys.Value | None

    @classmethod
    def from_cst(cls: type[Expr], cst_node: cst.Expr, module: node.Module) -> Expr:
        """Construct the appropriate Expr subclass from a CST Expr."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        if (identifier := cst_node.maybe_identifier()) is not None:
            return SimpleExpr(
                value=node.DeferredLookup.make(
                    cst_identifier=identifier,
                    expected_type=typesys.Value | node.NameProxy,  # pyright: ignore[reportArgumentType] Type not known ahead of time
                    terminals=module.terminals,
                ),
                resolved_value=None,
                cst_node=cst_node,
                module=module,
                type_info=typesys.InferenceVar.make(cst_node=identifier, context=module),
            )
        if (ns_identifier := cst_node.maybe_namespaced_identifier()) is not None:
            return NamespaceLookupExpr.from_child_cst(ns_identifier, cst_node, module)
        if (instantiate := cst_node.maybe_instantiate()) is not None:
            arg_list = instantiate.maybe_arg_list()
            args = arg_list.children_arg() if arg_list else []
            result = InstantiateExpr(
                operand=Expr.from_cst(instantiate.child_operand(), module=module),
                arguments=[
                    (
                        ((cst_name := arg.maybe_name()) and get_span(cst_name.child_value(), module.terminals)) or None,
                        Expr.from_cst(arg.child_expr(), module=module),
                    )
                    for arg in args
                ],
                resolved_value=None,
                cst_node=cst_node,
                module=module,
                type_info=clkbuiltins.TYPE_TYPE,
            )
            typesys.unify(result.operand.type_info, clkbuiltins.TYPE_TYPE)
            return result
        if (literal := cst_node.maybe_literal()) is not None:
            literal_value = primitive.Literal.from_cst(cst_node=literal, module=module)
            return SimpleExpr(
                value=literal_value,
                resolved_value=None,
                cst_node=cst_node,
                module=module,
                type_info=literal_value.type_info,
            )
        if dotted_identifier_cst := cst_node.maybe_dotted_identifier():
            return DottedIdentifierExpr.from_child_cst(dotted_identifier_cst, cst_node, module)
        if call_cst := cst_node.maybe_call():
            arg_list = call_cst.maybe_arg_list()
            args = arg_list.children_arg() if arg_list else []
            return CallExpr(
                module=module,
                cst_node=cst_node,
                type_info=typesys.InferenceVar.make(cst_node=call_cst, context=module),
                resolved_value=None,
                operand=Expr.from_cst(call_cst.child_operand(), module=module),
                arguments=[
                    (
                        ((cst_name := arg.maybe_name()) and get_span(cst_name.child_value(), module.terminals)) or None,
                        Expr.from_cst(arg.child_expr(), module=module),
                    )
                    for arg in args
                ],
            )
        msg = f"Expression type {cst_node} not yet implemented."
        raise NotImplementedError(msg)

    @classmethod
    def from_str(cls: type[Expr], code: str, module: node.Module) -> Expr:
        """Parse a string containing Clockwork source to CST."""
        result = parse.clk_string_to_any_cst(
            code + "\n", len(code), "expr", cst.Expr, f"{module.module_id.get_base_path()}(internal_expr)"
        )
        inner_module = node.Module(
            module_id=ModuleID(module.module_id.repo, "<inline>"),
            cst_node=None,
            terminals=result.terminals,
            inner_scope=module.inner_scope.make_anon_child_scope("internal_expr"),
            doc=None,
            unresolved_imports=[],
            context=module.context,
        )
        return cls.from_cst(cst_node=result.cst, module=inner_module)

    @abstractmethod
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        msg = f"Attempt to generate a value key for an unevaluated expression: {self}"
        raise RuntimeError(msg)


@dataclass
class SimpleExpr(Expr):
    """IR node class holding a single value requiring no further evaluation."""

    value: typesys.Value | node.DeferredLookup[typesys.Value | node.NameProxy[Any]]

    def name_resolution_fields(self) -> Collection[str]:
        """Override recursion for node.resolve_names.

        This prevents us from recursively resolving into the referenced module,
        which will already be resolved.
        """
        if isinstance(self.value, node.DeferredLookup):
            return ["value"]
        return []

    @override
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            return self.resolved_value
        if isinstance(self.value, node.DeferredLookup):
            msg = f"During expression evaluation, encountered unresolved DeferredLookup: {self.value}"
            raise ValueError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        if isinstance(self.value, node.NameProxy):
            final_value = self.value.final_value()
            if not isinstance(final_value, typesys.Value):
                msg = self.append_error_line(f"Expected NameProxy to resolve to Value, but got {final_value}")
                raise TypeError(msg)
            self.value = final_value
        if isinstance(self.value, clkbuiltins.MagicValue):
            self.value = self.value.factory(self.value)
        try:
            if self.value.type_info is clkbuiltins.INFERRED_TYPE:
                self.value.type_info = typesys.InferenceVar.make(context=self.module, cst_node=self.cst_node)
            typesys.unify(self.type_info, self.value.type_info)
        except TypeError as err:
            msg = self.append_error_line(str(err))
            raise TypeError(msg) from None
        self.resolved_value = self.value
        return self.resolved_value


@dataclass
class NamespaceLookupExpr(Expr):
    """IR Node representing a namespaced identifier."""

    namespace: node.Deferrable[node.NamespaceEntity]
    name: str

    @override
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            return self.resolved_value
        if isinstance(self.namespace, node.DeferredLookup):
            msg = f"During expression evaluation, encountered unresolved DeferredLookup: {self.namespace.identifier}"
            raise ValueError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        value = self.namespace.lookup(self.name)
        if value is None:
            namespace_name = self.namespace.name if isinstance(self.namespace, node.NamedEntity) else "UnknownNamespace"
            msg = self.append_error_line(f"No such name {self.name} in namespace {namespace_name}")
            raise KeyError(msg)
        if not isinstance(value, typesys.Value):
            msg = f"Lookup of name {self.namespace}::{self.name} resolves to non-Value: {value}"
            raise TypeError(msg)
        typesys.unify(self.type_info, value.type_info)
        self.resolved_value = value
        return self.resolved_value

    @classmethod
    def from_child_cst(
        cls: type[NamespaceLookupExpr],
        cst_node: cst.NamespacedIdentifier,
        parent_cst: cst.Expr,
        module: node.Module,
    ) -> NamespaceLookupExpr:
        """Construct a namespace lookup from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        namespace = get_span((namespace_cst := cst_node.child_namespace()).child_value(), module.terminals)
        name = get_span(cst_node.child_extern_entity().child_value(), module.terminals)
        return cls(
            module=module,
            cst_node=parent_cst,
            type_info=typesys.InferenceVar.make(context=module, cst_node=cst_node),
            resolved_value=None,
            namespace=node.DeferredLookup(
                identifier=namespace,
                expected_type=node.NamespaceEntity,
                cst_identifier=namespace_cst,
                terminals=module.terminals,
            ),
            name=name,
        )


@dataclass
class DottedIdentifierExpr(Expr):
    """IR Node representing a dotted identifier expression."""

    parent: DottedIdentifierExpr | SimpleExpr
    attribute: str

    @classmethod
    def from_child_cst(
        cls: type[DottedIdentifierExpr],
        cst_node: cst.DottedIdentifier,
        expr_cst: cst.Expr,
        module: node.Module,
    ) -> DottedIdentifierExpr:
        """Construct a dotted identifier from CST."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        parent: SimpleExpr | DottedIdentifierExpr
        if parent_cst := cst_node.maybe_parent():
            parent = DottedIdentifierExpr.from_child_cst(parent_cst, expr_cst, module)
            assert isinstance(parent, DottedIdentifierExpr)
        else:
            root = get_span(cst_node.child_root().child_value(), module.terminals)
            parent = SimpleExpr(
                module,
                expr_cst,
                typesys.InferenceVar.make(module, cst_node.child_root()),
                resolved_value=None,
                value=node.DeferredLookup(
                    root,
                    typesys.Value | node.NameProxy,  # pyright: ignore[reportArgumentType] Type not known ahead of time
                    cst_node.child_root(),
                    module.terminals,
                ),
            )
            assert isinstance(parent, SimpleExpr)
        attribute = get_span(cst_node.child_child().child_value(), module.terminals)
        return cls(
            module=module,
            cst_node=expr_cst,
            type_info=typesys.InferenceVar.make(module, expr_cst),
            resolved_value=None,
            parent=parent,
            attribute=attribute,
        )

    @override
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            return self.resolved_value
        parent = self.parent.evaluate()
        if not isinstance(parent, typesys.MembershipEntity):
            msg = self.parent.append_error_line(f"Dotted access parent does not support attribute access: {parent}")
            raise TypeError(msg)
        result = parent.attribute(self.attribute)
        if result is None:
            parent_name = parent.name if isinstance(parent, node.NamedEntity) else str(type(parent))
            msg = self.parent.append_error_line(f"No such attribute {self.attribute} in {parent_name}")
            raise ValueError(msg)
        typesys.unify(self.type_info, result.type_info)
        self.resolved_value = result
        return result


@dataclass
class InstantiateExpr(Expr):
    """IR node class holding an instantiation expression."""

    operand: Expr
    arguments: Sequence[tuple[str | None, Expr]]

    @override
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            return self.resolved_value
        operand = self.operand.evaluate()
        typesys.unify(self.type_info, operand.type_info)
        if not isinstance(operand, typesys.TypeVal):
            msg = self.operand.append_error_line(
                f"Operand of instantiation expression is not a generic type value: {operand}",
            )
            raise TypeError(msg)
        parameters = operand.generic_parameters()
        if parameters is None:
            msg = self.operand.append_error_line(
                f"Operand of instantiation expression has no generic parameters: {operand}",
            )
            raise TypeError(msg)
        args = [(name, value.evaluate()) for name, value in self.arguments]
        self.resolved_value = typesys.Instantiation(
            type_info=self.type_info,
            instantiates=operand,
            arguments=typesys.bind_args(parameters, args),
        )
        return self.resolved_value


@dataclass
class CallExpr(Expr):
    """IR node class holding a call expression."""

    operand: Expr
    arguments: Sequence[tuple[str | None, Expr]]

    @override
    def evaluate(self) -> typesys.Value:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            return self.resolved_value
        operand = self.operand.evaluate()
        if not isinstance(operand, typesys.CallableEntity):
            msg = self.operand.append_error_line(
                f"Operand of instantiation expression is not callable: {operand}",
            )
            raise TypeError(msg)
        args = [(name, value.evaluate()) for name, value in self.arguments]
        self.resolved_value = operand.evaluate_call(ir_node=self, module=self.module, args=args)
        typesys.unify(self.type_info, self.resolved_value.type_info)
        return self.resolved_value


@dataclass
class TypeExpression(Expr):
    """Wrapper for an expression expected to evaluate to a type.

    This supports type inference where other expressions' types need to be
    unified with the resolution of this expression, rather than with this
    expression's type.  (This expression's expected type is always TYPE_TYPE.)
    """

    expr: Expr
    inference_var: typesys.InferenceVar

    @classmethod
    def make(cls: type[TypeExpression], expr: Expr) -> TypeExpression:
        """Wrap an Expr expected to resolve to a type."""
        typesys.unify(expr.type_info, clkbuiltins.TYPE_TYPE)
        return cls(
            expr=expr,
            inference_var=typesys.InferenceVar.make(
                context=expr.module,
                cst_node=expr.cst_node,
            ),
            module=expr.module,
            cst_node=expr.cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            resolved_value=None,
        )

    @override
    def evaluate(self) -> typesys.TypeVal | typesys.DeferrableType:
        """Evaluate the expression.

        Returns:
            The resolved value.

        Raises:
            ValueError: if there is any error in expression evaluation.
        """
        if self.resolved_value is not None:
            assert isinstance(self.resolved_value, typesys.TypeVal)
            return self.resolved_value
        resolved_value = self.expr.evaluate()
        if not isinstance(resolved_value, typesys.TypeVal | typesys.DeferrableType):
            # This should be impossible because above we unified with TYPE_TYPE already.
            # But, if there's a bug in type inference, this could catch it.
            msg = self.expr.append_error_line(
                f"Expression expected to result to a type instead resolves to {resolved_value}",
            )
            raise TypeError(msg)
        typesys.unify(resolved_value, self.inference_var)
        self.resolved_value = resolved_value
        return resolved_value
