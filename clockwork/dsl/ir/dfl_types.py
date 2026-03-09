# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""DFL Trait System.

This module implements a Rust-like trait system for DFL expressions. Traits define
sets of operations that types can implement (e.g., Add, Sub, Ord). The trait registry
tracks which types implement which traits and resolves operator result types.

Traits and impls are defined in .clk files (see std/traits.clk):

    trait Add<Rhs = Self> {
        type Output;
    }

    impl Add for Int64 { type Output = Int64; }
    impl Add<Float64> for Duration { type Output = Duration; }

Example usage:
    # Create registry from compiled trait definitions and implementations
    registry = create_registry(trait_defs, trait_impls)

    # Find what Int64 + Int64 returns
    add_trait = registry.get_trait("std.traits.Add")
    impl = registry.find_impl(add_trait, INT64, INT64)
    result_type = registry.output_type(impl)  # Returns INT64
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Final

from clockwork.dsl.compiler_context import CompilerContext, ContextKey
from clockwork.dsl.ir import clkbuiltins, expr, node, typesys
from clockwork.dsl.ir.cst_util import format_line_with_error
from clockwork.dsl.ir.cst_util import get_span as get_span_text
from typing_extensions import Self, override

if TYPE_CHECKING:
    from clockwork.dsl import clockwork_cst as cst
    from fltk.fegen.pyrt.terminalsrc import Span


class CollectionType(typesys.TypeVal):
    """A collection type (e.g., Collection<Int64>).

    Represents a homogeneous collection of elements.

    Attributes:
        element_type: The type of elements in the collection.
    """

    element_type: typesys.TypeVal | typesys.InferenceVar

    def __init__(self, element_type: typesys.TypeVal | typesys.InferenceVar) -> None:
        """Initialize CollectionType with element_type and TYPE_TYPE."""
        super().__init__(type_info=clkbuiltins.TYPE_TYPE)
        self.element_type = element_type

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation."""
        if isinstance(self.element_type, typesys.InferenceVar):
            msg = "Attempt to get value_key of CollectionType with unresolved InferenceVar"
            raise TypeError(msg)
        return f"Collection<{self.element_type.value_key()}>"


@dataclass
class ResolvedTraitTypeParam:
    """A resolved type parameter in a trait definition.

    For example, in `trait Add<Rhs = Self>`, this represents the `Rhs` parameter
    with a default of `Self` (meaning it defaults to the implementing type).

    Attributes:
        name: The parameter name (e.g., "Rhs").
        default_is_self: If True, default value is Self (the implementing type).
        default_type: If not None and not default_is_self, this is the resolved default type.
    """

    name: str
    default_is_self: bool = False
    default_type: typesys.TypeVal | None = None


@dataclass
class ResolvedTraitDef:
    """A fully resolved trait definition.

    Attributes:
        name: The trait name (e.g., "Add").
        fqn: Fully qualified name (e.g., "std.traits.Add").
        type_params: Resolved type parameters.
        associated_types: Names of associated types (e.g., ["Output"]).
        span: Source location if defined in DSL, None for built-ins.
    """

    name: str
    fqn: str
    type_params: tuple[ResolvedTraitTypeParam, ...] = ()
    associated_types: tuple[str, ...] = ()
    span: Span | None = None


@dataclass
class ResolvedTraitImpl:
    """A fully resolved implementation of a trait for a type.

    Attributes:
        trait: The resolved trait being implemented.
        for_type: The type implementing the trait (resolved).
        type_args: Values for the trait's type parameters (e.g., {"Rhs": Int64}).
        associated_types: Values for associated types (e.g., {"Output": Int64}).
        span: Source location if defined in DSL, None for built-ins.
    """

    trait: ResolvedTraitDef
    for_type: typesys.TypeVal
    type_args: dict[str, typesys.TypeVal] = field(default_factory=dict)
    associated_types: dict[str, typesys.TypeVal] = field(default_factory=dict)
    span: Span | None = None


@dataclass
class TraitTypeParam:
    """An unresolved type parameter in a trait definition.

    Attributes:
        name: The parameter name (e.g., "Rhs").
        default_is_self: If True, default value is Self.
        default_type_expr: Expression for default type, or None.
    """

    name: str
    default_is_self: bool = False
    default_type_expr: expr.Expr | None = None


@dataclass
class TraitDef(typesys.NamedValue):
    """An unresolved trait definition.

    This is the IR node created from CST. Call resolve() to get ResolvedTraitDef.

    Attributes:
        name: The trait name (e.g., "Add"). Inherited from NamedEntity.
        scope: The scope containing this trait. Inherited from NamedEntity.
        module: The module containing this trait.
        type_params: Unresolved type parameters.
        associated_types: Names of associated types (e.g., ["Output"]).
        span: Source location if defined in DSL, None for built-ins.
        resolved: The resolved form, or None if not yet resolved.
    """

    module: node.Module | None = None
    type_params: tuple[TraitTypeParam, ...] = ()
    associated_types: tuple[str, ...] = ()
    span: Span | None = None
    resolved: ResolvedTraitDef | None = field(default=None, repr=False)

    @classmethod
    def from_cst(
        cls: type[TraitDef],
        cst_node: cst.DflTraitDef,
        module: node.Module,
        scope: node.Scope,
    ) -> TraitDef:
        """Create an unresolved TraitDef from a CST node.

        Args:
            cst_node: The CST node for the trait definition.
            module: The module containing this trait.
            scope: The scope to define the trait in.

        Returns:
            The unresolved TraitDef.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name_cst = cst_node.child_identifier()
        name = get_span_text(name_cst.child_value(), module.terminals)

        type_params: list[TraitTypeParam] = []
        if (params_cst := cst_node.maybe_dfl_trait_type_params()) is not None:
            for param_cst in params_cst.children_dfl_trait_type_param():
                param_name_cst = param_cst.child_name()
                param_name = get_span_text(param_name_cst.child_value(), module.terminals)

                default_is_self = False
                default_type_expr: expr.Expr | None = None
                if (default_cst := param_cst.maybe_default()) is not None:
                    if default_cst.maybe_self_type() is not None:
                        default_is_self = True
                    else:
                        typespec_cst = default_cst.child_typespec()
                        default_type_expr = expr.Expr.from_cst(typespec_cst, module)

                type_params.append(TraitTypeParam(param_name, default_is_self, default_type_expr))

        associated_types: list[str] = []
        if (body_cst := cst_node.maybe_dfl_trait_body()) is not None:
            for assoc_decl_cst in body_cst.children_dfl_trait_assoc_type_decl():
                assoc_cst = assoc_decl_cst.child_dfl_trait_assoc_type()
                assoc_name_cst = assoc_cst.child_name()
                assoc_name = get_span_text(assoc_name_cst.child_value(), module.terminals)
                associated_types.append(assoc_name)

        return cls(
            name=name,
            scope=scope,
            module=module,
            type_params=tuple(type_params),
            associated_types=tuple(associated_types),
            span=cst_node.span,
            resolved=None,
            type_info=clkbuiltins.TYPE_TYPE,
        )

    def resolve(self) -> ResolvedTraitDef:
        """Resolve all expressions and return the resolved TraitDef.

        Returns:
            The resolved TraitDef.
        """
        if self.resolved is not None:
            return self.resolved

        resolved_params: list[ResolvedTraitTypeParam] = []
        for param in self.type_params:
            default_type: typesys.TypeVal | None = None
            if param.default_type_expr is not None:
                val = param.default_type_expr.evaluate()
                if not isinstance(val, typesys.TypeVal):
                    msg = f"Expected a type for default of '{param.name}', got {val}"
                    if self.module is not None and self.module.terminals is not None and self.span is not None:
                        msg += format_line_with_error(self.span, self.module.terminals, self.module.module_id)
                    raise TypeError(msg)
                default_type = val
            resolved_params.append(
                ResolvedTraitTypeParam(
                    name=param.name,
                    default_is_self=param.default_is_self,
                    default_type=default_type,
                )
            )

        self.resolved = ResolvedTraitDef(
            name=self.name,
            fqn=self.fqn,
            type_params=tuple(resolved_params),
            associated_types=self.associated_types,
            span=self.span,
        )
        return self.resolved


@dataclass
class TraitImpl:
    """An unresolved implementation of a trait for a type.

    This is the IR node created from CST. Call resolve() to get ResolvedTraitImpl.

    Attributes:
        module: The module containing this impl.
        trait_ref: Reference expression for the trait being implemented.
        for_type_expr: Expression for the type implementing the trait.
        type_arg_exprs: Expressions for the trait's type parameters.
        associated_type_exprs: Expressions for associated types.
        span: Source location if defined in DSL, None for built-ins.
        resolved: The resolved form, or None if not yet resolved.
    """

    module: node.Module
    trait_ref: expr.Expr
    for_type_expr: expr.Expr
    type_arg_exprs: list[expr.Expr] = field(default_factory=list)
    associated_type_exprs: dict[str, expr.Expr] = field(default_factory=dict)
    span: Span | None = None
    resolved: ResolvedTraitImpl | None = field(default=None, repr=False)

    @classmethod
    def from_cst(
        cls: type[TraitImpl],
        cst_node: cst.DflImplDecl,
        module: node.Module,
    ) -> TraitImpl:
        """Create an unresolved TraitImpl from a CST node.

        Args:
            cst_node: The CST node for the impl declaration.
            module: The module containing this impl.

        Returns:
            The unresolved TraitImpl.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        trait_expr_cst = cst_node.child_trait()
        trait_expr_ir = expr.Expr.from_cst(trait_expr_cst, module)

        type_arg_exprs: list[expr.Expr] = []
        if isinstance(trait_expr_ir, expr.InstantiateExpr):
            type_arg_exprs = [arg_expr for _, arg_expr in trait_expr_ir.arguments]
            trait_ref = trait_expr_ir.operand
        else:
            trait_ref = trait_expr_ir

        for_type_cst = cst_node.child_for_type()
        for_type_expr = expr.Expr.from_cst(for_type_cst, module)

        associated_type_exprs: dict[str, expr.Expr] = {}
        if (body_cst := cst_node.maybe_dfl_impl_body()) is not None:
            for assoc_decl_cst in body_cst.children_dfl_impl_assoc_type_decl():
                assoc_cst = assoc_decl_cst.child_dfl_impl_assoc_type()
                assoc_name_cst = assoc_cst.child_name()
                assoc_name = get_span_text(assoc_name_cst.child_value(), module.terminals)
                assoc_type_cst = assoc_cst.child_typespec()
                assoc_type_expr = expr.Expr.from_cst(assoc_type_cst, module)
                associated_type_exprs[assoc_name] = assoc_type_expr

        return cls(
            module=module,
            trait_ref=trait_ref,
            for_type_expr=for_type_expr,
            type_arg_exprs=type_arg_exprs,
            associated_type_exprs=associated_type_exprs,
            span=cst_node.span,
            resolved=None,
        )

    def _format_error(self, msg: str) -> str:
        """Append source location to an error message if available."""
        if self.module.terminals is not None and self.span is not None:
            msg += format_line_with_error(self.span, self.module.terminals, self.module.module_id)
        return msg

    def _resolve_type_args(
        self,
        resolved_trait: ResolvedTraitDef,
        for_type_val: typesys.TypeVal,
    ) -> dict[str, typesys.TypeVal]:
        """Resolve type arguments, filling in defaults for missing ones."""
        type_args: dict[str, typesys.TypeVal] = {}
        if len(self.type_arg_exprs) > len(resolved_trait.type_params):
            raise TypeError(
                self._format_error(
                    f"Too many type arguments for trait '{resolved_trait.name}': "
                    + f"expected at most {len(resolved_trait.type_params)}, "
                    + f"got {len(self.type_arg_exprs)}"
                )
            )
        for i, arg_expr in enumerate(self.type_arg_exprs):
            param = resolved_trait.type_params[i]
            arg_val = arg_expr.evaluate()
            if not isinstance(arg_val, typesys.TypeVal):
                raise TypeError(self._format_error(f"Expected a type for '{param.name}', got {arg_val}"))
            type_args[param.name] = arg_val

        for param in resolved_trait.type_params:
            if param.name not in type_args:
                if param.default_is_self:
                    type_args[param.name] = for_type_val
                elif param.default_type is not None:
                    type_args[param.name] = param.default_type
        return type_args

    def _resolve_associated_types(self) -> dict[str, typesys.TypeVal]:
        """Resolve associated type expressions."""
        associated_types: dict[str, typesys.TypeVal] = {}
        for assoc_name, assoc_expr in self.associated_type_exprs.items():
            assoc_val = assoc_expr.evaluate()
            if not isinstance(assoc_val, typesys.TypeVal):
                raise TypeError(self._format_error(f"Expected a type for '{assoc_name}', got {assoc_val}"))
            associated_types[assoc_name] = assoc_val
        return associated_types

    def resolve(self) -> ResolvedTraitImpl:
        """Resolve all expressions and return the resolved TraitImpl.

        Returns:
            The resolved TraitImpl.
        """
        if self.resolved is not None:
            return self.resolved

        trait_val = self.trait_ref.evaluate()
        if not isinstance(trait_val, TraitDef):
            raise TypeError(self._format_error(f"Expected a trait, got {trait_val}"))
        resolved_trait = trait_val.resolve()

        for_type_val = self.for_type_expr.evaluate()
        if not isinstance(for_type_val, typesys.TypeVal):
            raise TypeError(self._format_error(f"Expected a type for 'for', got {for_type_val}"))

        self.resolved = ResolvedTraitImpl(
            trait=resolved_trait,
            for_type=for_type_val,
            type_args=self._resolve_type_args(resolved_trait, for_type_val),
            associated_types=self._resolve_associated_types(),
            span=self.span,
        )
        return self.resolved


@dataclass(frozen=True, slots=True)
class _ImplKey:
    """Key for looking up trait implementations in the registry.

    Attributes:
        trait_fqn: Fully-qualified name of the trait.
        for_type_id: Object id of the implementing type.
        rhs_type_id: Object id of the Rhs type parameter, or None for unary traits.
    """

    trait_fqn: str
    for_type_id: int
    rhs_type_id: int | None


class TraitRegistry:
    """Registry of trait definitions and implementations.

    The registry stores trait definitions and implementations, providing lookups
    to find the appropriate implementation for a given type and operation.
    Also serves as a compiler context for accumulating traits across modules.
    """

    def __init__(self) -> None:
        """Initialize an empty registry."""
        self._traits: dict[str, TraitDef] = {}
        self._impls: dict[_ImplKey, ResolvedTraitImpl] = {}

    def import_from(self, other: Self) -> None:
        """Combine this registry with items from another (Context protocol).

        Used when a module imports another module - merges the imported
        module's traits and impls into this registry.
        """
        for fqn, trait in other._traits.items():  # noqa: SLF001 (accessing same class type)
            if fqn in self._traits:
                if self._traits[fqn] is not trait:
                    msg = f"Conflicting trait definitions for {fqn}"
                    raise ValueError(msg)
            else:
                self._traits[fqn] = trait

        for key, impl in other._impls.items():  # noqa: SLF001 (accessing same class type)
            if key in self._impls:
                if self._impls[key] is not impl:
                    msg = f"Conflicting trait impl for {key}"
                    raise ValueError(msg)
            else:
                self._impls[key] = impl

    def define_trait(self, trait: TraitDef) -> None:
        """Register a trait definition using its FQN.

        Args:
            trait: The trait definition to register.
        """
        if trait.fqn in self._traits:
            existing = self._traits[trait.fqn]
            if existing is not trait:
                msg = f"Trait already defined: {trait.fqn}"
                raise ValueError(msg)
            return
        self._traits[trait.fqn] = trait

    def register(self, impl: ResolvedTraitImpl) -> None:
        """Register a trait implementation, detecting conflicts.

        Args:
            impl: The resolved trait implementation to register.

        Raises:
            ValueError: If a conflicting implementation already exists.
        """
        trait_fqn = impl.trait.fqn
        for_type_id = id(impl.for_type)
        rhs = impl.type_args.get("Rhs")
        rhs_type_id = id(rhs) if rhs is not None else None

        key = _ImplKey(trait_fqn=trait_fqn, for_type_id=for_type_id, rhs_type_id=rhs_type_id)
        if key in self._impls:
            existing = self._impls[key]
            if existing is not impl:
                msg = f"Conflicting impl: {trait_fqn} for {impl.for_type} already defined"
                if existing.span is not None:
                    msg += f" at {existing.span}"
                raise ValueError(msg)
            return
        self._impls[key] = impl

    def find_impl(
        self, trait: TraitDef, for_type: typesys.TypeVal, rhs: typesys.TypeVal | None = None
    ) -> ResolvedTraitImpl | None:
        """Find an implementation of a trait for a type (O(1) lookup).

        For binary traits (e.g., Add), `rhs` specifies the right-hand operand type.
        For unary traits (e.g., Neg), `rhs` should be None.

        Args:
            trait: The trait definition to look up.
            for_type: The type implementing the trait (left operand for binary ops).
            rhs: The right-hand operand type for binary traits, or None for unary.

        Returns:
            The matching ResolvedTraitImpl, or None if no implementation found.
        """
        trait_fqn = trait.fqn
        for_type_id = id(for_type)
        rhs_type_id = id(rhs) if rhs is not None else None
        key = _ImplKey(trait_fqn=trait_fqn, for_type_id=for_type_id, rhs_type_id=rhs_type_id)
        return self._impls.get(key)

    def _impl_matches_for_type(
        self,
        impl: ResolvedTraitImpl,
        for_type: typesys.TypeVal | typesys.InferenceVar,
    ) -> bool:
        """Check if an impl matches the given for_type."""
        if isinstance(for_type, typesys.InferenceVar):
            return impl.for_type.satisfies(for_type.numeric_type)
        return impl.for_type is for_type

    def _impl_matches_rhs(
        self,
        impl: ResolvedTraitImpl,
        rhs: typesys.TypeVal | typesys.InferenceVar | None,
    ) -> bool:
        """Check if an impl matches the given rhs type."""
        if rhs is None:
            return True
        impl_rhs = impl.type_args.get("Rhs")
        if impl_rhs is None:
            return False
        if isinstance(rhs, typesys.InferenceVar):
            return impl_rhs.satisfies(rhs.numeric_type)
        return impl_rhs is rhs

    def find_matching_impls(
        self,
        trait: TraitDef,
        for_type: typesys.TypeVal | typesys.InferenceVar,
        rhs: typesys.TypeVal | typesys.InferenceVar | None = None,
    ) -> list[ResolvedTraitImpl]:
        """Find all implementations that could match, considering InferenceVars.

        When either operand is an InferenceVar, this returns all impls that are
        compatible with its numeric_type constraint.

        Args:
            trait: The trait definition to look up.
            for_type: The type implementing the trait (may be InferenceVar).
            rhs: The right-hand operand type (may be InferenceVar), or None for unary.

        Returns:
            List of all matching ResolvedTraitImpls (may be empty, one, or multiple).
        """
        if isinstance(for_type, typesys.TypeVal) and (rhs is None or isinstance(rhs, typesys.TypeVal)):
            impl = self.find_impl(trait, for_type, rhs)
            return [impl] if impl is not None else []

        trait_fqn = trait.fqn
        matches: list[ResolvedTraitImpl] = []

        for key, impl in self._impls.items():
            if key.trait_fqn != trait_fqn:
                continue
            if not self._impl_matches_for_type(impl, for_type):
                continue
            if not self._impl_matches_rhs(impl, rhs):
                continue
            matches.append(impl)

        return matches

    def output_type(self, impl: ResolvedTraitImpl) -> typesys.TypeVal | None:
        """Get the Output associated type for an implementation.

        Args:
            impl: The resolved trait implementation.

        Returns:
            The Output type, or None if the trait has no Output.
        """
        return impl.associated_types.get("Output")

    def get_trait(self, fqn: str) -> TraitDef | None:
        """Get a trait definition by FQN.

        Args:
            fqn: The fully-qualified trait name (e.g., "std.traits.Add").

        Returns:
            The trait definition, or None if not found.
        """
        return self._traits.get(fqn)


class TraitRegistryKey(ContextKey[TraitRegistry]):
    """Compiler context key for the trait registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> TraitRegistry:
        """Create a default (empty) instance of the registry."""
        return TraitRegistry()


_TRAIT_REGISTRY_KEY: Final = TraitRegistryKey("TraitRegistry")


def register_trait(module: node.Module, trait: TraitDef) -> None:
    """Register a TraitDef in the compiler context."""
    registry = module.context[_TRAIT_REGISTRY_KEY]
    registry.define_trait(trait)


def register_trait_impl(module: node.Module, impl: TraitImpl) -> None:
    """Register a TraitImpl in the compiler context."""
    registry = module.context[_TRAIT_REGISTRY_KEY]
    registry.register(impl.resolve())


def get_trait_registry(module: node.Module) -> TraitRegistry:
    """Get the TraitRegistry for a module."""
    return module.context[_TRAIT_REGISTRY_KEY]
