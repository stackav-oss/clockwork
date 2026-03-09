# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""DFL (Declarative Functional Language) IR nodes.

DFL is a pure-functional sublanguage within Clockwork for specifying runtime computations
such as signal transformations, detector conditions, and aligner constraints.

This module defines the core IR (Intermediate Representation) nodes for DFL expressions.
"""

from __future__ import annotations

from abc import abstractmethod
from dataclasses import dataclass
from dataclasses import field as dataclass_field
from enum import Enum
from typing import TYPE_CHECKING, Final, Generic, TypeVar

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    dfl_types,
    expr,
    node,
    primitive,
    statement,
    typesys,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span
from clockwork.dsl.ir.module_id import ModuleID
from fltk.fegen.pyrt.terminalsrc import Span, TerminalSource
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable


# FQN path prefix for standard library traits
_STD_TRAITS_PATH = "@clockwork::std::traits"


@dataclass
class Context:
    """Context for DFL expression compilation.

    This is shared across all nodes in an expression tree and provides access
    to the lexical scope, terminal source (for error formatting), and module ID.

    Attributes:
        scope: The lexical scope for name resolution.
        terminals: Source text for error message formatting.
        module_id: Module identifier for error message formatting.
    """

    scope: node.Scope
    terminals: TerminalSource
    module_id: ModuleID

    def format_error(self, span: Span, msg: str) -> str:
        """Format an error message with source location information.

        Args:
            span: The source span where the error occurred.
            msg: The error message.

        Returns:
            The error message with source location appended.
        """
        return msg + format_line_with_error(span, self.terminals, self.module_id)

    @classmethod
    def for_testing(
        cls,
        bindings: dict[str, node.NamedEntity] | None = None,
    ) -> Context:
        """Create a context suitable for unit tests with optional pre-populated bindings.

        This creates a minimal context with an in-memory scope and empty terminal source.

        Args:
            bindings: Optional dictionary mapping names to NamedEntity values to pre-populate in scope.

        Returns:
            A new Context ready for testing.
        """
        test_scope = node.Scope(parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="test", module_id_for_errors=None)
        test_terminals = TerminalSource("")
        test_module_id = ModuleID("", "test")

        ctx = cls(scope=test_scope, terminals=test_terminals, module_id=test_module_id)

        if bindings:
            for name, entity in bindings.items():
                test_scope.define(name, entity, test_terminals)

        return ctx

    def bind(self, name: str, entity: node.NamedEntity) -> None:
        """Add a binding to the context's scope.

        Args:
            name: The name to bind.
            entity: The named entity to bind to the name.
        """
        self.scope.define(name, entity, self.terminals)


@dataclass
class BuiltinFn(node.NamedEntity):
    """Abstract base class for built-in DFL functions.

    Built-in functions are predefined functions that can be called in DFL expressions.
    Each subclass defines how to infer the return type from argument types.

    Attributes:
        variadic: True if the function accepts variable number of arguments.
        min_args: Minimum number of arguments required.
    """

    variadic: bool
    min_args: int

    @abstractmethod
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> typesys.TypeVal | typesys.InferenceVar:
        """Infer the return type from the argument types.

        Args:
            arg_types: The types of the arguments passed to the function.
            arg_exprs: The actual argument expressions (needed for map/filter).
            registry: The trait registry for type checking.
            span: Source span for error messages.
            ctx: Context for error message formatting.

        Returns:
            The return type of the function call.

        Raises:
            TypeError: If the return type cannot be inferred.
        """
        ...


def _format_error(msg: str, span: Span | None, ctx: Context | None) -> str:
    """Format an error message with source location if available.

    Args:
        msg: The base error message.
        span: Source span for error location (may be None).
        ctx: Context for error formatting (may be None).

    Returns:
        The error message, with source location appended if ctx and span are available.
    """
    if ctx is not None and span is not None:
        return ctx.format_error(span, msg)
    return msg


class UnaryOp(Enum):
    """Unary operators in DFL expressions."""

    NEG = "neg"  # Arithmetic negation: -x
    POS = "pos"  # Unary plus (identity): +x
    NOT = "not"  # Logical negation: not x
    ABS = "abs"  # Absolute value: |x|


class BinaryOp(Enum):
    """Binary operators in DFL expressions."""

    # Arithmetic
    ADD = "add"
    SUB = "sub"
    MUL = "mul"
    DIV = "div"
    MOD = "mod"

    # Comparison
    EQ = "eq"
    NE = "ne"
    LT = "lt"
    LE = "le"
    GT = "gt"
    GE = "ge"

    # Logical
    AND = "and"
    OR = "or"


class NameValidationError(Exception):
    """Raised when a name reference cannot be resolved."""


@dataclass(frozen=True, slots=True)
class Ref:
    """Reference to a named entity.

    Stores path + scope for deferred lookup. The actual entity is resolved
    when lookup() is called, allowing forward references to work naturally.

    For simple identifiers like `x`, path is `("x",)`.
    For namespaced identifiers like `Enum::Variant`, path is `("Enum", "Variant")`.
    Multi-level namespaces like `mod::Enum::Variant` become `("mod", "Enum", "Variant")`.

    Attributes:
        path: Tuple of identifier segments. First is looked up in scope,
              rest are looked up via NamespaceEntity.lookup().
        scope: The lexical scope for the initial lookup.
        span: Source location of this reference.
        ctx: Compilation context.
    """

    path: tuple[str, ...]
    scope: node.Scope = dataclass_field(repr=False)
    span: Span
    ctx: Context = dataclass_field(repr=False)

    @property
    def is_namespaced(self) -> bool:
        """True if this is a namespaced reference (path has multiple segments)."""
        return len(self.path) > 1

    def lookup(self) -> node.NamedEntity:
        """Resolve this reference in its scope.

        Returns:
            The named entity this reference refers to.

        Raises:
            NameValidationError: If any name in the path is not defined.
        """
        current: node.NamedEntity | None = None

        for i, segment in enumerate(self.path):
            if i == 0:
                current = self.scope.lookup(segment)
            else:
                if not isinstance(current, node.NamespaceEntity):
                    resolved_path = "::".join(self.path[:i])
                    msg = f"{resolved_path} is not a namespace"
                    raise NameValidationError(self.ctx.format_error(self.span, msg))
                current = current.lookup(segment)

            if current is None:
                resolved_path = "::".join(self.path[: i + 1])
                msg = f"Undefined name: {resolved_path}"
                raise NameValidationError(self.ctx.format_error(self.span, msg))

        assert current is not None
        return current

    @classmethod
    def from_identifier(cls, cst_node: cst.Identifier, ctx: Context) -> Ref:
        """Create a Ref from a simple identifier CST."""
        name = get_span(cst_node.child_value(), ctx.terminals)
        return cls(
            path=(name,),
            scope=ctx.scope,
            span=cst_node.span,
            ctx=ctx,
        )

    @classmethod
    def from_namespaced_identifier(cls, cst_node: cst.NamespacedIdentifier, ctx: Context) -> Ref:
        """Create a Ref from a namespaced identifier CST."""
        namespace = get_span(cst_node.child_namespace().child_value(), ctx.terminals)
        member = get_span(cst_node.child_extern_entity().child_value(), ctx.terminals)
        return cls(
            path=(namespace, member),
            scope=ctx.scope,
            span=cst_node.span,
            ctx=ctx,
        )


@dataclass(frozen=True, slots=True)
class Member:
    """Field access expression: base.field_name.

    Attributes:
        base: The expression to access a field from.
        field_name: The field name.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    base: Expr
    field_name: str
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class Unary:
    """Unary operator expression.

    Attributes:
        op: The unary operator.
        operand: The operand expression.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    op: UnaryOp
    operand: Expr
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class Binary:
    """Binary operator expression.

    Attributes:
        op: The binary operator.
        left: The left operand.
        right: The right operand.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    op: BinaryOp
    left: Expr
    right: Expr
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class IfElse:
    """Ternary conditional: if test then then_ else else_.

    Attributes:
        test: The condition expression (must be Bool).
        then_: Expression evaluated when test is true.
        else_: Expression evaluated when test is false.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    test: Expr
    then_: Expr
    else_: Expr
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class CondArm:
    """A single arm in a cond expression.

    Attributes:
        guard: The condition to test (None for else arm).
        body: The expression to evaluate if guard is true.
        span: Source location of this arm.
    """

    guard: Expr | None  # None = else arm
    body: Expr
    span: Span


@dataclass(frozen=True, slots=True)
class CondExpr:
    """Multi-way condition guards: cond { guard1 => body1, else => body2 }.

    The last arm must be an else arm (guard is None).

    Attributes:
        arms: The condition arms.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    arms: tuple[CondArm, ...]
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class LiteralPattern:
    """Match a literal value.

    Attributes:
        value: The literal to match against.
        span: Source location of this pattern.
        ctx: Compilation context.
    """

    value: primitive.DecimalLiteral | primitive.UnitLiteral | primitive.StringLiteral
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class EnumPattern:
    """Match an enum variant.

    Attributes:
        variant: The enum variant entity to match.
        span: Source location of this pattern.
        ctx: Compilation context.
    """

    variant: clkenum.ValueDef
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class RangePattern:
    """Match a range (inclusive): lo..hi.

    Attributes:
        lo: Lower bound literal.
        hi: Upper bound literal.
        span: Source location of this pattern.
        ctx: Compilation context.
    """

    lo: primitive.DecimalLiteral | primitive.UnitLiteral
    hi: primitive.DecimalLiteral | primitive.UnitLiteral
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class WildcardPattern:
    """Match anything (else/_). Opts out of exhaustiveness checking.

    Attributes:
        span: Source location of this pattern.
        ctx: Compilation context.
    """

    span: Span
    ctx: Context = dataclass_field(repr=False)


Pattern = LiteralPattern | EnumPattern | RangePattern | WildcardPattern


@dataclass(frozen=True, slots=True)
class MatchArm:
    """A single arm in a match expression.

    Each arm has one or more patterns (union: pattern1 | pattern2) and a body expression.
    If any pattern matches the scrutinee, the body is evaluated.

    Attributes:
        patterns: Tuple of patterns (union patterns allow multiple).
        body: Expression to evaluate if any pattern matches.
        span: Source location of this arm.
    """

    patterns: tuple[Pattern, ...]
    body: Expr
    span: Span


@dataclass(frozen=True, slots=True)
class Match:
    """Multi-way value matching expression.

    Evaluates the scrutinee and compares it against patterns in each arm.
    Returns the body of the first matching arm.

    Attributes:
        scrutinee: Expression whose value is matched against patterns.
        arms: Tuple of match arms to try in order.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    scrutinee: Expr
    arms: tuple[MatchArm, ...]
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class CallArg:
    """A single argument in a function call.

    Supports positional args, named args (kwargs), and spread args.

    Attributes:
        expr: The argument expression.
        name: Optional name for named arguments (e.g., "alpha" in alpha=0.1).
        is_spread: True if this argument uses spread syntax (e.g., values...).
        span: Source location of this argument.
    """

    expr: Expr
    name: str | None = None
    is_spread: bool = False
    span: Span | None = None


@dataclass(frozen=True, slots=True)
class Call:
    """Function call expression: func(arg1, name=arg2, spread_arg...).

    Supports positional arguments, named arguments (kwargs), and spread arguments.

    Attributes:
        func: Expression that evaluates to the function to call (usually a Ref).
        args: Tuple of call arguments.
        span: Source location of this call expression.
        ctx: Compilation context.
    """

    func: Expr
    args: tuple[CallArg, ...]
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass(frozen=True, slots=True)
class ExprTuple:
    """A tuple of expressions.

    Used for:
    - Array literals in source: [expr, expr, ...]
    - Variadic parameter packs: remaining args collected into a tuple
    - Spreadable collections: only ExprTuples can be spread since their size is known

    All elements should have compatible types.

    Attributes:
        elements: Tuple of element expressions.
        span: Source location of this expression.
        ctx: Compilation context.
    """

    elements: tuple[Expr, ...]
    span: Span
    ctx: Context = dataclass_field(repr=False)


@dataclass
class FnParam(node.NamedEntity):
    """A function parameter - defined in the function's scope.

    This is the unresolved form. Parameter types are expr.Expr that may contain DeferredLookup.

    Attributes:
        name: The parameter name.
        scope: The function's body scope.
        type_expr: Optional type expression for the parameter (may contain DeferredLookup).
        is_variadic: True if this is a variadic parameter (values...).
    """

    type_expr: expr.Expr | None = None
    is_variadic: bool = False


@dataclass
class ResolvedFnParam:
    """A resolved function parameter with evaluated type.

    Attributes:
        name: The parameter name.
        type_val: Resolved type of the parameter, or None if no type annotation.
        is_variadic: True if this is a variadic parameter (values...).
    """

    name: str
    type_val: typesys.TypeVal | None = None
    is_variadic: bool = False


@dataclass
class FnDef(node.NamedEntity):
    """A user-defined function (template-style, expanded before type checking).

    This is the unresolved form created from CST. Call resolve() to get ResolvedFnDef.

    Attributes:
        name: The function name.
        scope: The scope where this function is defined.
        body: The function body expression.
        module: The module containing this function.
        params: Tuple of function parameters (unresolved).
        return_type_expr: Optional return type expression (may contain DeferredLookup).
        body_scope: The scope containing parameter definitions.
        span: Source location of this function definition.
        ctx: Compilation context.
        resolved: The resolved form, or None if not yet resolved.
    """

    body: Expr
    module: node.Module | None = None
    params: tuple[FnParam, ...] = dataclass_field(default_factory=tuple)
    return_type_expr: expr.Expr | None = None
    body_scope: node.Scope | None = dataclass_field(default=None, repr=False)
    span: Span | None = None
    ctx: Context | None = dataclass_field(default=None, repr=False)
    resolved: ResolvedFnDef | None = dataclass_field(default=None, repr=False)

    def name_resolution_fields(self) -> tuple[str, ...]:
        """Return fields that need name resolution.

        Excludes 'body' because DFL expressions use frozen Ref nodes that do
        deferred lookup via Ref.lookup() rather than in-place mutation.
        """
        return ("params", "return_type_expr")

    @classmethod
    def from_cst(
        cls: type[FnDef],
        cst_node: cst.DflFnDef,
        module: node.Module,
        scope: node.Scope,
    ) -> FnDef:
        """Create an unresolved FnDef from a CST node.

        Args:
            cst_node: The CST node for the function definition.
            module: The module containing this function.
            scope: The scope to define the function in.

        Returns:
            The unresolved FnDef.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name_cst = cst_node.child_name()
        name = get_span(name_cst.child_value(), module.terminals)

        body_scope = node.Scope(
            parent=scope, uniq_path=f"{scope.uniq_path}.{name}", module_id_for_errors=module.module_id
        )

        params: list[FnParam] = []
        if (params_cst := cst_node.maybe_dfl_fn_params()) is not None:
            for param_cst in params_cst.children_dfl_fn_param():
                param_name_cst = param_cst.child_name()
                param_name = get_span(param_name_cst.child_value(), module.terminals)

                type_expr: expr.Expr | None = None
                if (typespec_cst := param_cst.maybe_typespec()) is not None:
                    type_expr = expr.Expr.from_cst(typespec_cst, module)

                param = FnParam(name=param_name, scope=body_scope, type_expr=type_expr, is_variadic=False)
                params.append(param)
                body_scope.define(param_name, param, module.terminals)

            if (variadic_cst := params_cst.maybe_dfl_variadic_param()) is not None:
                param_name_cst = variadic_cst.child_name()
                param_name = get_span(param_name_cst.child_value(), module.terminals)

                type_expr = None
                if (typespec_cst := variadic_cst.maybe_typespec()) is not None:
                    type_expr = expr.Expr.from_cst(typespec_cst, module)

                param = FnParam(name=param_name, scope=body_scope, type_expr=type_expr, is_variadic=True)
                params.append(param)
                body_scope.define(param_name, param, module.terminals)

        return_type_expr: expr.Expr | None = None
        if (return_type_cst := cst_node.maybe_dfl_fn_return_type()) is not None:
            typespec_cst = return_type_cst.child_typespec()
            return_type_expr = expr.Expr.from_cst(typespec_cst, module)

        body_cst = cst_node.child_body()
        dfl_ctx = Context(scope=body_scope, terminals=module.terminals, module_id=module.module_id)
        body = expr_from_cst(body_cst, dfl_ctx, module)

        return cls(
            name=name,
            scope=scope,
            module=module,
            params=tuple(params),
            return_type_expr=return_type_expr,
            body=body,
            body_scope=body_scope,
            span=cst_node.span,
            ctx=dfl_ctx,
            resolved=None,
        )

    def resolve(self) -> ResolvedFnDef:
        """Resolve all type expressions and return the resolved FnDef.

        Returns:
            The resolved FnDef.
        """
        if self.resolved is not None:
            return self.resolved

        resolved_params: list[ResolvedFnParam] = []
        for param in self.params:
            type_val: typesys.TypeVal | None = None
            if param.type_expr is not None:
                val = param.type_expr.evaluate()
                if not isinstance(val, typesys.TypeVal):
                    msg = f"Expected type for parameter '{param.name}', got {val}"
                    if self.module is not None and self.module.terminals is not None and self.span is not None:
                        msg += format_line_with_error(self.span, self.module.terminals, self.module.module_id)
                    raise TypeError(msg)
                type_val = val
            resolved_params.append(ResolvedFnParam(name=param.name, type_val=type_val, is_variadic=param.is_variadic))

        return_type: typesys.TypeVal | None = None
        if self.return_type_expr is not None:
            val = self.return_type_expr.evaluate()
            if not isinstance(val, typesys.TypeVal):
                msg = f"Expected type for return type, got {val}"
                if self.module is not None and self.module.terminals is not None and self.span is not None:
                    msg += format_line_with_error(self.span, self.module.terminals, self.module.module_id)
                raise TypeError(msg)
            return_type = val

        self.resolved = ResolvedFnDef(
            name=self.name,
            params=tuple(resolved_params),
            return_type=return_type,
            body=self.body,
            body_scope=self.body_scope,
            span=self.span,
            ctx=self.ctx,
        )
        return self.resolved


@dataclass(frozen=True, slots=True)
class ResolvedFnDef:
    """A resolved function definition with evaluated types.

    Attributes:
        name: The function name.
        params: Tuple of resolved function parameters.
        return_type: Resolved return type, or None if no annotation.
        body: The function body expression.
        body_scope: The scope containing parameter definitions.
        span: Source location of this function definition.
        ctx: Compilation context.
    """

    name: str
    params: tuple[ResolvedFnParam, ...]
    body: Expr
    return_type: typesys.TypeVal | None = None
    body_scope: node.Scope | None = None
    span: Span | None = None
    ctx: Context | None = None


@dataclass(frozen=True, slots=True)
class Lambda:
    """An inline lambda expression.

    Lambdas are anonymous functions that can capture values from their enclosing scope.
    They are used with `map` and `filter` to apply transformations inline.
    The scope for captures is inside the `ctx` attribute.

    Attributes:
        params: Parameter names as a tuple of strings.
        body: The lambda body expression.
        span: Source location.
        ctx: Compilation context.
    """

    params: tuple[str, ...]
    body: Expr
    span: Span | None = None
    ctx: Context | None = None


Literal = primitive.DecimalLiteral | primitive.UnitLiteral | primitive.StringLiteral
Expr = Literal | Ref | Member | Unary | Binary | IfElse | CondExpr | Match | Call | ExprTuple | Lambda

E = TypeVar("E")


@dataclass(frozen=True, slots=True)
class Typed(Generic[E]):
    """A typed expression wrapper.

    Wraps an expression node with its inferred/checked type information.

    Attributes:
        expr: The underlying expression.
        type_info: The type of the expression.
    """

    expr: E
    type_info: typesys.TypeVal | typesys.InferenceVar


def typeof(expr: Expr | Typed[Expr]) -> typesys.TypeVal | typesys.InferenceVar:
    """Get the type of an expression.

    For Typed expressions, returns the stored type_info.
    For literal expressions, returns the literal's type_info.

    Args:
        expr: The expression to get the type of.

    Returns:
        The type of the expression (may be an InferenceVar for literals).

    Raises:
        TypeError: If the expression type cannot be determined without type checking.
    """
    if isinstance(expr, Typed):
        return expr.type_info
    if isinstance(expr, primitive.DecimalLiteral | primitive.UnitLiteral | primitive.StringLiteral):
        return expr.type_info
    msg = f"Cannot determine type of {type(expr).__name__} without type checking"
    raise TypeError(msg)


def _binary_op_from_cst_comparison(op_cst: cst.DflComparisonOp) -> BinaryOp:
    """Convert a CST comparison operator to a BinaryOp."""
    if op_cst.maybe_eq() is not None:
        return BinaryOp.EQ
    if op_cst.maybe_ne() is not None:
        return BinaryOp.NE
    if op_cst.maybe_lt() is not None:
        return BinaryOp.LT
    if op_cst.maybe_le() is not None:
        return BinaryOp.LE
    if op_cst.maybe_gt() is not None:
        return BinaryOp.GT
    if op_cst.maybe_ge() is not None:
        return BinaryOp.GE
    msg = f"Unknown comparison operator: {op_cst}"
    raise ValueError(msg)


def _binary_op_from_cst_additive(op_cst: cst.DflAdditiveOp) -> BinaryOp:
    """Convert a CST additive operator to a BinaryOp."""
    if op_cst.maybe_add() is not None:
        return BinaryOp.ADD
    if op_cst.maybe_sub() is not None:
        return BinaryOp.SUB
    msg = f"Unknown additive operator: {op_cst}"
    raise ValueError(msg)


def _binary_op_from_cst_multiplicative(op_cst: cst.DflMultiplicativeOp) -> BinaryOp:
    """Convert a CST multiplicative operator to a BinaryOp."""
    if op_cst.maybe_mul() is not None:
        return BinaryOp.MUL
    if op_cst.maybe_div() is not None:
        return BinaryOp.DIV
    if op_cst.maybe_mod() is not None:
        return BinaryOp.MOD
    msg = f"Unknown multiplicative operator: {op_cst}"
    raise ValueError(msg)


def _literal_from_cst(literal_cst: cst.Literal, module: node.Module) -> Literal:
    """Convert a CST literal to an IR Literal."""
    result = primitive.Literal.from_cst(literal_cst, module)
    # The result is guaranteed to be one of the literal subtypes
    assert isinstance(result, primitive.DecimalLiteral | primitive.UnitLiteral | primitive.StringLiteral)
    return result


def _lambda_from_cst(lambda_cst: cst.DflLambda, ctx: Context, module: node.Module) -> Lambda:
    """Convert a CST lambda expression to an IR Lambda.

    Args:
        lambda_cst: The CST lambda node.
        ctx: The compilation context.
        module: The module containing this lambda.

    Returns:
        The Lambda IR node.
    """
    if module.terminals is None:
        msg = "Cannot construct IR nodes from CST without a TerminalSource"
        raise ValueError(msg)

    # Create a child scope for the lambda body so that lambda parameters are
    # resolvable via Ref.lookup(). This mirrors the pattern in FnDef.from_cst().
    lambda_scope = node.Scope(
        parent=ctx.scope, uniq_path=f"{ctx.scope.uniq_path}.<lambda>", module_id_for_errors=module.module_id
    )

    params: list[str] = []
    if (params_cst := lambda_cst.maybe_dfl_lambda_params()) is not None:
        for identifier in params_cst.children_identifier():
            param_name = get_span(identifier.child_value(), module.terminals)
            params.append(param_name)
            # Register the parameter as a FnParam entity in the lambda scope.
            fn_param = FnParam(name=param_name, scope=lambda_scope, type_expr=None, is_variadic=False)
            lambda_scope.define(param_name, fn_param, module.terminals)

    body_ctx = Context(scope=lambda_scope, terminals=ctx.terminals, module_id=ctx.module_id)
    body = expr_from_cst(lambda_cst.child_dfl_expr(), body_ctx, module)

    return Lambda(
        params=tuple(params),
        body=body,
        span=lambda_cst.span,
        ctx=body_ctx,
    )


def _primary_from_cst(primary_cst: cst.DflPrimaryExpr, ctx: Context, module: node.Module) -> Expr:  # noqa: PLR0911 # One return per expression type
    """Convert a CST primary expression to an IR Expr."""
    if (paren := primary_cst.maybe_dfl_paren_expr()) is not None:
        return expr_from_cst(paren.child_dfl_expr(), ctx, module)

    if (abs_expr := primary_cst.maybe_dfl_abs_expr()) is not None:
        inner = expr_from_cst(abs_expr.child_dfl_expr(), ctx, module)
        return Unary(UnaryOp.ABS, inner, abs_expr.span, ctx)

    if (if_expr := primary_cst.maybe_dfl_if_expr()) is not None:
        return _if_from_cst(if_expr, ctx, module)

    if (cond_expr := primary_cst.maybe_dfl_cond_expr()) is not None:
        return _cond_from_cst(cond_expr, ctx, module)

    if (match_expr := primary_cst.maybe_dfl_match_expr()) is not None:
        return _match_from_cst(match_expr, ctx, module)

    if (array_lit := primary_cst.maybe_dfl_array_literal()) is not None:
        return _expr_tuple_from_cst(array_lit, ctx, module)

    if (lambda_expr := primary_cst.maybe_dfl_lambda()) is not None:
        return _lambda_from_cst(lambda_expr, ctx, module)

    if (literal := primary_cst.maybe_literal()) is not None:
        return _literal_from_cst(literal, module)

    if (identifier := primary_cst.maybe_identifier()) is not None:
        return Ref.from_identifier(identifier, ctx)

    msg = f"Unknown primary expression type: {primary_cst}"
    raise ValueError(msg)


def _if_from_cst(if_cst: cst.DflIfExpr, ctx: Context, module: node.Module) -> IfElse:
    """Convert a CST if-then-else expression to an IR IfElse."""
    test = expr_from_cst(if_cst.child_condition(), ctx, module)
    then_ = expr_from_cst(if_cst.child_if_true(), ctx, module)
    else_ = expr_from_cst(if_cst.child_if_false(), ctx, module)
    return IfElse(test, then_, else_, if_cst.span, ctx)


def _expr_tuple_from_cst(array_cst: cst.DflArrayLiteral, ctx: Context, module: node.Module) -> ExprTuple:
    """Convert a CST array literal to an IR ExprTuple."""
    elements = tuple(expr_from_cst(elem, ctx, module) for elem in array_cst.children_dfl_expr())
    return ExprTuple(elements, array_cst.span, ctx)


def _cond_from_cst(cond_cst: cst.DflCondExpr, ctx: Context, module: node.Module) -> CondExpr:
    """Convert a CST cond expression to an IR CondExpr."""
    arms: list[CondArm] = []
    cond_arms_cst = cond_cst.child_dfl_cond_arms()
    for arm_cst in cond_arms_cst.children_dfl_cond_arm():
        guard_cst = arm_cst.child_dfl_cond_guard()
        body_cst = arm_cst.child_dfl_expr()

        if guard_cst.maybe_else_guard() is not None:
            guard = None
        else:
            guard = expr_from_cst(guard_cst.child_dfl_expr(), ctx, module)

        body = expr_from_cst(body_cst, ctx, module)
        arms.append(CondArm(guard, body, arm_cst.span))

    return CondExpr(tuple(arms), cond_cst.span, ctx)


def _match_from_cst(match_cst: cst.DflMatchExpr, ctx: Context, module: node.Module) -> Match:
    """Convert a CST match expression to an IR Match."""
    scrutinee = expr_from_cst(match_cst.child_scrutinee(), ctx, module)

    arms: list[MatchArm] = []
    match_arms_cst = match_cst.child_dfl_match_arms()
    for arm_cst in match_arms_cst.children_dfl_match_arm():
        pattern_cst = arm_cst.child_dfl_match_pattern()
        body_cst = arm_cst.child_dfl_expr()

        patterns = match_pattern_from_cst(pattern_cst, ctx, module)
        body = expr_from_cst(body_cst, ctx, module)
        arms.append(MatchArm(patterns, body, arm_cst.span))

    return Match(scrutinee, tuple(arms), match_cst.span, ctx)


def _postfix_from_cst(postfix_cst: cst.DflPostfixExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST postfix expression to an IR Expr.

    Handles member access (obj.field) and function calls (func(args)).
    These can be chained: obj.method(arg1, arg2).field

    Note: Process children in source order to maintain correct evaluation.
    """
    result = _primary_from_cst(postfix_cst.child_dfl_primary_expr(), ctx, module)

    postfix_label = cst.DflPostfixExpr.Label
    for label, child in postfix_cst.children:
        match label:
            case postfix_label.DFL_PRIMARY_EXPR:
                # Already processed above via child_dfl_primary_expr()
                pass
            case postfix_label.DFL_MEMBER_SUFFIX:
                assert isinstance(child, cst.DflMemberSuffix)
                field_name = get_span(child.child_identifier().child_value(), ctx.terminals)
                result = Member(result, field_name, child.span, ctx)
            case postfix_label.DFL_CALL_SUFFIX:
                assert isinstance(child, cst.DflCallSuffix)
                args: list[CallArg] = []
                if (arg_list := child.maybe_dfl_arg_list()) is not None:
                    for arg_cst in arg_list.children_dfl_arg():
                        value_expr = expr_from_cst(arg_cst.child_value(), ctx, module)
                        arg_name: str | None = None
                        if (name_cst := arg_cst.maybe_name()) is not None:
                            arg_name = get_span(name_cst.child_value(), ctx.terminals)
                        is_spread = arg_cst.maybe_spread() is not None
                        args.append(CallArg(expr=value_expr, name=arg_name, is_spread=is_spread, span=arg_cst.span))
                result = Call(result, tuple(args), child.span, ctx)
            case None:
                # Trivia nodes have no label - skip them
                pass

    return result


def _unary_from_cst(unary_cst: cst.DflUnaryExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST unary expression to an IR Expr."""
    if (inner_unary := unary_cst.maybe_dfl_unary_expr()) is not None:
        inner = _unary_from_cst(inner_unary, ctx, module)
        if unary_cst.maybe_neg() is not None:
            return Unary(UnaryOp.NEG, inner, unary_cst.span, ctx)
        if unary_cst.maybe_pos() is not None:
            return Unary(UnaryOp.POS, inner, unary_cst.span, ctx)

    if (postfix := unary_cst.maybe_dfl_postfix_expr()) is not None:
        return _postfix_from_cst(postfix, ctx, module)

    msg = f"Unknown unary expression type: {unary_cst}"
    raise ValueError(msg)


def _multiplicative_from_cst(mult_cst: cst.DflMultiplicativeExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST multiplicative expression to an IR Expr."""
    unaries = list(mult_cst.children_dfl_unary_expr())
    result = _unary_from_cst(unaries[0], ctx, module)

    ops = list(mult_cst.children_dfl_multiplicative_op())

    for op_cst, right_cst in zip(ops, unaries[1:], strict=True):
        right = _unary_from_cst(right_cst, ctx, module)
        op = _binary_op_from_cst_multiplicative(op_cst)
        span = Span(get_expr_span(result).start, right_cst.span.end)
        result = Binary(op, result, right, span, ctx)

    return result


def _additive_from_cst(add_cst: cst.DflAdditiveExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST additive expression to an IR Expr."""
    mults = list(add_cst.children_dfl_multiplicative_expr())
    result = _multiplicative_from_cst(mults[0], ctx, module)

    ops = list(add_cst.children_dfl_additive_op())

    for op_cst, right_cst in zip(ops, mults[1:], strict=True):
        right = _multiplicative_from_cst(right_cst, ctx, module)
        op = _binary_op_from_cst_additive(op_cst)
        span = Span(get_expr_span(result).start, right_cst.span.end)
        result = Binary(op, result, right, span, ctx)

    return result


def _comparison_from_cst(comp_cst: cst.DflComparisonExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST comparison expression to an IR Expr."""
    additives = list(comp_cst.children_dfl_additive_expr())
    result = _additive_from_cst(additives[0], ctx, module)

    if (op_cst := comp_cst.maybe_dfl_comparison_op()) is not None and len(additives) > 1:
        right = _additive_from_cst(additives[1], ctx, module)
        op = _binary_op_from_cst_comparison(op_cst)
        span = Span(get_expr_span(result).start, additives[1].span.end)
        result = Binary(op, result, right, span, ctx)

    return result


def _not_from_cst(not_cst: cst.DflNotExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST not expression to an IR Expr."""
    if (inner_not := not_cst.maybe_dfl_not_expr()) is not None:
        inner = _not_from_cst(inner_not, ctx, module)
        return Unary(UnaryOp.NOT, inner, not_cst.span, ctx)

    if (comp := not_cst.maybe_dfl_comparison_expr()) is not None:
        return _comparison_from_cst(comp, ctx, module)

    msg = f"Unknown not expression type: {not_cst}"
    raise ValueError(msg)


def _and_from_cst(and_cst: cst.DflAndExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST and expression to an IR Expr."""
    nots = list(and_cst.children_dfl_not_expr())
    result = _not_from_cst(nots[0], ctx, module)

    for right_cst in nots[1:]:
        right = _not_from_cst(right_cst, ctx, module)
        span = Span(get_expr_span(result).start, right_cst.span.end)
        result = Binary(BinaryOp.AND, result, right, span, ctx)

    return result


def _or_from_cst(or_cst: cst.DflOrExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a CST or expression to an IR Expr."""
    ands = list(or_cst.children_dfl_and_expr())
    result = _and_from_cst(ands[0], ctx, module)

    for right_cst in ands[1:]:
        right = _and_from_cst(right_cst, ctx, module)
        span = Span(get_expr_span(result).start, right_cst.span.end)
        result = Binary(BinaryOp.OR, result, right, span, ctx)

    return result


def expr_from_cst(cst_node: cst.DflExpr, ctx: Context, module: node.Module) -> Expr:
    """Convert a DFL CST expression node to an IR expression.

    This is the main entry point for CST-to-IR conversion.

    Args:
        cst_node: The CST expression node to convert.
        ctx: The DFL compilation context.
        module: The module containing the expression (for literal conversion).

    Returns:
        The corresponding IR expression.
    """
    return _or_from_cst(cst_node.child_dfl_or_expr(), ctx, module)


def pattern_from_cst(cst_node: cst.DflPattern, ctx: Context, module: node.Module) -> Pattern:
    """Convert a DFL pattern CST node to an IR pattern.

    Args:
        cst_node: The CST pattern node to convert.
        ctx: The DFL compilation context.
        module: The module containing the pattern.

    Returns:
        The corresponding IR pattern.

    Raises:
        NameValidationError: If a namespaced identifier cannot be resolved.
    """
    if (range_pat := cst_node.maybe_dfl_range_pattern()) is not None:
        return _range_pattern_from_cst(range_pat, ctx, module)

    if (simple := cst_node.maybe_dfl_simple_pattern()) is not None:
        if (lit_cst := simple.maybe_literal()) is not None:
            lit_ir = primitive.Literal.from_cst(lit_cst, module)
            assert isinstance(lit_ir, (primitive.DecimalLiteral, primitive.UnitLiteral, primitive.StringLiteral))
            return LiteralPattern(lit_ir, lit_cst.span, ctx)

        if (ns_id := simple.maybe_namespaced_identifier()) is not None:
            return _enum_pattern_from_cst(ns_id, ctx)

    msg = f"Unknown pattern type: {cst_node}"
    raise ValueError(msg)


def _range_pattern_from_cst(range_pat: cst.DflRangePattern, ctx: Context, module: node.Module) -> RangePattern:
    """Convert a range pattern CST to IR.

    Range patterns use integers only (not floats) to avoid grammar ambiguity with '..' token.
    """
    lo_cst = range_pat.child_lo()
    hi_cst = range_pat.child_hi()

    lo_ir = primitive.DecimalLiteral.from_child_cst(lo_cst, parent_cst=None, module=module)
    hi_ir = primitive.DecimalLiteral.from_child_cst(hi_cst, parent_cst=None, module=module)
    return RangePattern(lo_ir, hi_ir, range_pat.span, ctx)


def _enum_pattern_from_cst(ns_id: cst.NamespacedIdentifier, ctx: Context) -> EnumPattern:
    """Convert a namespaced identifier to an enum pattern."""
    ref = Ref.from_namespaced_identifier(ns_id, ctx)
    entity = ref.lookup()

    if not isinstance(entity, clkenum.ValueRef):
        msg = f"Expected enum variant but got {type(entity).__name__}"
        raise TypeError(ctx.format_error(ns_id.span, msg))

    return EnumPattern(entity.value_def, ns_id.span, ctx)


def match_pattern_from_cst(cst_node: cst.DflMatchPattern, ctx: Context, module: node.Module) -> tuple[Pattern, ...]:
    """Convert a match pattern CST to a tuple of IR patterns.

    Match patterns can be either:
    - A wildcard pattern ("else")
    - A pattern list (one or more patterns separated by |)

    Args:
        cst_node: The CST match pattern node.
        ctx: The DFL compilation context.
        module: The module containing the pattern.

    Returns:
        Tuple of patterns (usually 1, but can be multiple for union patterns).
    """
    if (else_kw := cst_node.maybe_else()) is not None:
        return (WildcardPattern(else_kw, ctx),)

    pat_list = cst_node.child_dfl_pattern_list()
    return tuple(pattern_from_cst(p, ctx, module) for p in pat_list.children_dfl_pattern())


def check_pattern_type(
    pattern: Pattern,
    scrutinee_type: typesys.TypeVal | typesys.InferenceVar,
    ctx: Context,
) -> None:
    """Verify pattern is compatible with scrutinee type.

    Args:
        pattern: The pattern to check.
        scrutinee_type: The type being matched against.
        ctx: Context for error messages.

    Raises:
        TypeCheckError: If pattern is incompatible with scrutinee type.
    """
    match pattern:
        case WildcardPattern():
            # Wildcards match anything
            pass
        case LiteralPattern(value=value, span=span):
            _check_literal_pattern_type(value, scrutinee_type, span, ctx)
        case EnumPattern(variant=variant, span=span):
            _check_enum_pattern_type(variant, scrutinee_type, span, ctx)
        case RangePattern(lo=lo, hi=hi, span=span):
            _check_range_pattern_type(lo, hi, scrutinee_type, span, ctx)


def _check_literal_pattern_type(
    value: primitive.DecimalLiteral | primitive.UnitLiteral | primitive.StringLiteral,
    scrutinee_type: typesys.TypeVal | typesys.InferenceVar,
    span: Span,
    ctx: Context,
) -> None:
    """Check that a literal pattern is compatible with scrutinee type."""
    lit_type = value.type_info
    _unify_types(lit_type, scrutinee_type, span, ctx)


def _check_enum_pattern_type(
    variant: clkenum.ValueDef,
    scrutinee_type: typesys.TypeVal | typesys.InferenceVar,
    span: Span,
    ctx: Context,
) -> None:
    """Check that an enum pattern variant belongs to the scrutinee's enum type."""
    variant_type = variant.enum
    _unify_types(variant_type, scrutinee_type, span, ctx)


def _check_range_pattern_type(
    lo: primitive.DecimalLiteral | primitive.UnitLiteral,
    hi: primitive.DecimalLiteral | primitive.UnitLiteral,
    scrutinee_type: typesys.TypeVal | typesys.InferenceVar,
    span: Span,
    ctx: Context,
) -> None:
    """Check that a range pattern is compatible with scrutinee type."""
    _check_literal_pattern_type(lo, scrutinee_type, span, ctx)
    _check_literal_pattern_type(hi, scrutinee_type, span, ctx)


def check_match_type(
    match_expr: Match,
    scrutinee_type: typesys.TypeVal | typesys.InferenceVar,
    check: Callable[[Expr], typesys.TypeVal | typesys.InferenceVar],
) -> typesys.TypeVal | typesys.InferenceVar:
    """Type check a match expression.

    Validates that:
    1. All patterns are compatible with the scrutinee type (if known)
    2. All arm bodies have compatible types (unifies to common result type)

    Args:
        match_expr: The match expression to check.
        scrutinee_type: The type of the scrutinee expression.
        check: The type checking function for subexpressions.

    Returns:
        The unified result type of all arm bodies.

    Raises:
        TypeCheckError: If pattern/body type checking fails.
    """
    ctx = match_expr.ctx

    if not match_expr.arms:
        line_info = format_line_with_error(match_expr.span, ctx.terminals, ctx.module_id)
        msg = f"Match expression has no arms\n{line_info}"
        raise TypeCheckError(msg)

    result_type: typesys.TypeVal | typesys.InferenceVar | None = None
    for arm in match_expr.arms:
        for pattern in arm.patterns:
            check_pattern_type(pattern, scrutinee_type, ctx)
        body_type = check(arm.body)
        if result_type is None:
            result_type = body_type
        else:
            _unify_types(result_type, body_type, arm.span, ctx)

    assert result_type is not None  # Guaranteed by arms non-empty check above
    return result_type


def has_wildcard(arms: tuple[MatchArm, ...]) -> bool:
    """Check if any arm has a WildcardPattern.

    Args:
        arms: The match arms to check.

    Returns:
        True if any arm contains a WildcardPattern.
    """
    for arm in arms:
        for pattern in arm.patterns:
            if isinstance(pattern, WildcardPattern):
                return True
    return False


def covered_variants(arms: tuple[MatchArm, ...]) -> set[str]:
    """Extract enum variant names covered by match arms.

    Args:
        arms: The match arms to analyze.

    Returns:
        Set of enum variant names covered by EnumPattern patterns.
    """
    variants: set[str] = set()
    for arm in arms:
        for pattern in arm.patterns:
            if isinstance(pattern, EnumPattern):
                variants.add(pattern.variant.name)
    return variants


def check_match_exhaustiveness(
    match_expr: Match,
    scrutinee_type: typesys.TypeVal,
) -> None:
    """Verify match covers all cases for enum types.

    For enum types without a wildcard/else arm, all enum variants must be
    covered by patterns. Non-enum types and matches with wildcards are exempt.

    Args:
        match_expr: The match expression to check.
        scrutinee_type: The type of the scrutinee expression.

    Raises:
        TypeCheckError: If the match is non-exhaustive (missing enum variants).
    """
    # If there's a wildcard, exhaustiveness is satisfied
    if has_wildcard(match_expr.arms):
        return

    # Only check exhaustiveness for enum types
    # ResolvedEnum is the resolved form of ClkEnum
    if not isinstance(scrutinee_type, clkenum.ClkEnum | clkenum.ResolvedEnum):
        return

    # Get all enum variant names from the values dict
    all_variants = {val_def.name for val_def in scrutinee_type.values.values()}

    # Get covered variant names
    covered = covered_variants(match_expr.arms)

    # Check for missing variants
    missing = all_variants - covered
    if missing:
        ctx = match_expr.ctx
        line_info = format_line_with_error(match_expr.span, ctx.terminals, ctx.module_id)
        missing_list = ", ".join(sorted(missing))
        msg = f"Non-exhaustive match: missing variants {missing_list}\n{line_info}"
        raise TypeCheckError(msg)


def get_expr_span(expr: Expr) -> Span:
    """Get the source span for an expression.

    Args:
        expr: The expression to get the span for.

    Returns:
        The source span.

    Raises:
        ValueError: If the expression has no source span.
    """
    match expr:
        case primitive.DecimalLiteral() | primitive.UnitLiteral() | primitive.StringLiteral():
            if expr.cst_node is not None:
                return expr.cst_node.span
            msg = "Literal has no CST node"
            raise ValueError(msg)
        case (
            Ref(span=span)
            | Member(span=span)
            | Unary(span=span)
            | Binary(span=span)
            | IfElse(span=span)
            | CondExpr(span=span)
            | Match(span=span)
            | Call(span=span)
            | ExprTuple(span=span)
            | Lambda(span=span)
        ):
            if span is None:
                msg = f"Expression has no source span: {expr}"
                raise ValueError(msg)
            return span


def map_expr(f: Callable[[Expr], Expr], expr: Expr) -> Expr:  # noqa: PLR0911, C901 # Many branches/returns by nature of one per expression type
    """Apply f to all immediate children, return new expr with results.

    This is useful for tree transformations like substitution. The function
    f is applied to each immediate child expression, and a new expression
    node is constructed with the transformed children.

    Note: This only maps over immediate children, not recursively. For a
    recursive transformation, have f call map_expr recursively.

    Args:
        f: Function to apply to each child expression.
        expr: The expression to transform.

    Returns:
        A new expression with f applied to all children.
    """
    match expr:
        case primitive.DecimalLiteral() | primitive.UnitLiteral() | primitive.StringLiteral():
            return expr
        case Ref():
            return expr
        case Member(base=base, field_name=field_name, span=span, ctx=ctx):
            return Member(f(base), field_name, span, ctx)
        case Unary(op=op, operand=operand, span=span, ctx=ctx):
            return Unary(op, f(operand), span, ctx)
        case Binary(op=op, left=left, right=right, span=span, ctx=ctx):
            return Binary(op, f(left), f(right), span, ctx)
        case IfElse(test=test, then_=then_, else_=else_, span=span, ctx=ctx):
            return IfElse(f(test), f(then_), f(else_), span, ctx)
        case CondExpr(arms=arms, span=span, ctx=ctx):
            new_arms = tuple(
                CondArm(f(arm.guard) if arm.guard is not None else None, f(arm.body), arm.span) for arm in arms
            )
            return CondExpr(new_arms, span, ctx)
        case Match(scrutinee=scrutinee, arms=arms, span=span, ctx=ctx):
            new_arms = tuple(MatchArm(arm.patterns, f(arm.body), arm.span) for arm in arms)
            return Match(f(scrutinee), new_arms, span, ctx)
        case Call(func=func, args=args, span=span, ctx=ctx):
            new_args = tuple(
                CallArg(expr=f(arg.expr), name=arg.name, is_spread=arg.is_spread, span=arg.span) for arg in args
            )
            return Call(f(func), new_args, span, ctx)
        case ExprTuple(elements=elements, span=span, ctx=ctx):
            return ExprTuple(tuple(f(elem) for elem in elements), span, ctx)
        case Lambda(params=params, body=body, span=span, ctx=ctx):
            return Lambda(params, f(body), span, ctx)


T = TypeVar("T")


def fold_expr(f: Callable[[Expr, list[T]], T], expr: Expr) -> T:  # noqa: PLR0911, PLR0912, C901 # Many branches/returns by nature of one per expression type
    """Bottom-up fold over expression tree.

    Process children first, then call f(node, child_results). This is useful
    for computing aggregate properties (size, depth, etc.) or collecting
    information from the tree.

    Args:
        f: Function that takes a node and list of child results, returns result.
        expr: The expression to fold over.

    Returns:
        The result of folding f over the tree.
    """
    match expr:
        case primitive.DecimalLiteral() | primitive.UnitLiteral() | primitive.StringLiteral():
            return f(expr, [])
        case Ref():
            return f(expr, [])
        case Member(base=base):
            child_results = [fold_expr(f, base)]
            return f(expr, child_results)
        case Unary(operand=operand):
            child_results = [fold_expr(f, operand)]
            return f(expr, child_results)
        case Binary(left=left, right=right):
            child_results = [fold_expr(f, left), fold_expr(f, right)]
            return f(expr, child_results)
        case IfElse(test=test, then_=then_, else_=else_):
            child_results = [fold_expr(f, test), fold_expr(f, then_), fold_expr(f, else_)]
            return f(expr, child_results)
        case CondExpr(arms=arms):
            child_results: list[T] = []
            for arm in arms:
                if arm.guard is not None:
                    child_results.append(fold_expr(f, arm.guard))
                child_results.append(fold_expr(f, arm.body))
            return f(expr, child_results)
        case Match(scrutinee=scrutinee, arms=arms):
            child_results = [fold_expr(f, scrutinee)]
            for arm in arms:
                child_results.append(fold_expr(f, arm.body))
            return f(expr, child_results)
        case Call(func=func, args=args):
            child_results = [fold_expr(f, func)]
            for arg in args:
                child_results.append(fold_expr(f, arg.expr))
            return f(expr, child_results)
        case ExprTuple(elements=elements):
            child_results = [fold_expr(f, elem) for elem in elements]
            return f(expr, child_results)
        case Lambda(body=body):
            child_results = [fold_expr(f, body)]
            return f(expr, child_results)


def expr_size(expr: Expr) -> int:
    """Count the number of nodes in an expression tree.

    Args:
        expr: The expression to count nodes in.

    Returns:
        The total number of nodes in the tree.
    """

    def count_nodes(_node: Expr, child_counts: list[int]) -> int:
        return 1 + sum(child_counts)

    return fold_expr(count_nodes, expr)


def expr_depth(expr: Expr) -> int:
    """Calculate the maximum depth of an expression tree.

    The depth of a leaf node is 1.

    Args:
        expr: The expression to calculate depth for.

    Returns:
        The maximum depth of the tree.
    """

    def max_depth(_node: Expr, child_depths: list[int]) -> int:
        if not child_depths:
            return 1
        return 1 + max(child_depths)

    return fold_expr(max_depth, expr)


def find_refs(expr: Expr) -> list[Ref]:
    """Find all Ref nodes in an expression tree.

    Args:
        expr: The expression to search.

    Returns:
        List of all Ref nodes found, in depth-first order.
    """

    def collect_refs(node: Expr, child_results: list[list[Ref]]) -> list[Ref]:
        result: list[Ref] = []
        for child_refs in child_results:
            result.extend(child_refs)
        if isinstance(node, Ref):
            result.append(node)
        return result

    return fold_expr(collect_refs, expr)


def validate_names(expr: Expr) -> None:
    """Validate that all names in an expression exist in their scopes.

    This checks that every Ref node can be successfully looked up. If any
    lookup fails, raises NameValidationError with source location.

    Args:
        expr: The expression to validate.

    Raises:
        NameValidationError: If any name cannot be resolved.
    """
    for ref in find_refs(expr):
        ref.lookup()


def substitute_params(expr: Expr, substitutions: dict[str, tuple[FnParam, Expr]]) -> Expr:
    """Replace Ref nodes that resolve to specific FnParam instances with argument expressions.

    Uses identity (``is``) checks to ensure only the intended FnParam instances are
    substituted. If a Ref resolves to a FnParam with the same name but a different
    identity (e.g. from a nested lambda), that is a variable shadowing error.

    Args:
        expr: The expression to substitute within.
        substitutions: Map from parameter name to (expected FnParam, replacement expression).

    Returns:
        New expression with substitutions applied.

    Raises:
        ValueError: If a Ref resolves to a different FnParam than expected (shadowing).
    """

    def substitute_one(e: Expr) -> Expr:
        match e:
            case Ref():
                entity = e.lookup()
                if isinstance(entity, FnParam) and len(e.path) == 1:
                    name = e.path[0]
                    if name not in substitutions:
                        # FnParam from a different scope (e.g. lambda parameter)
                        return e
                    expected_param, replacement = substitutions[name]
                    if entity is not expected_param:
                        msg = e.ctx.format_error(
                            e.span, f"Variable shadowing: '{name}' resolves to a different parameter than expected"
                        )
                        raise ValueError(msg)
                    return replacement
                return e  # Leave non-parameter refs (builtins, constants, types, etc.) unchanged
            case _:
                return map_expr(substitute_one, e)

    return substitute_one(expr)


def substitute_lambda_param(body: Expr, param_name: str, replacement: Expr) -> Expr:
    """Replace references to a lambda parameter with a replacement expression.

    Unlike substitute_params, this substitutes based on the parameter name string
    rather than requiring the reference to resolve to a FnParam entity. This is
    necessary for lambdas where the parameter is not registered in a scope.

    Args:
        body: The lambda body expression to substitute within.
        param_name: The name of the parameter to replace.
        replacement: The expression to replace the parameter with.

    Returns:
        New expression with substitutions applied.
    """

    def substitute_one(e: Expr) -> Expr:
        match e:
            case Ref(path=(name,)) if name == param_name:
                return replacement
            case _:
                return map_expr(substitute_one, e)

    return substitute_one(body)


def expand_map_with_lambda(
    lambda_expr: Lambda,
    collection_expr: ExprTuple,
    ctx: Context,
) -> ExprTuple:
    """Expand map(fn(x) body, [a, b, c]) to [body[x:=a], body[x:=b], body[x:=c]].

    This performs compile-time expansion of a map call with a lambda over a known
    collection (ExprTuple).

    Args:
        lambda_expr: The lambda to apply.
        collection_expr: The collection of elements (must be ExprTuple).
        ctx: Compilation context for error messages.

    Returns:
        An ExprTuple with the lambda applied to each element.

    Raises:
        TypeError: If lambda has wrong number of parameters.
    """
    if len(lambda_expr.params) != 1:
        msg = f"map() lambda must have exactly one parameter, got {len(lambda_expr.params)}"
        raise TypeError(_format_error(msg, lambda_expr.span, ctx))

    param_name = lambda_expr.params[0]
    expanded_elements: list[Expr] = []

    for elem in collection_expr.elements:
        substituted = substitute_lambda_param(lambda_expr.body, param_name, elem)
        expanded_elements.append(substituted)

    return ExprTuple(
        elements=tuple(expanded_elements),
        span=collection_expr.span,
        ctx=ctx,
    )


def _expand_spread_arg(arg_expr: Expr, arg_span: Span | None, call_span: Span, ctx: Context) -> list[Expr]:
    """Expand a spread argument into its constituent elements.

    Spread arguments (expr...) can only be applied to ExprTuples, which represent
    compile-time known collections (e.g., from variadic parameter packs). Runtime
    collections cannot be spread because their size is not known at expansion time.

    Args:
        arg_expr: The expression being spread.
        arg_span: Source span of the spread argument.
        call_span: Source span of the call (fallback for errors).
        ctx: Compilation context.

    Returns:
        List of expressions from the ExprTuple.

    Raises:
        TypeError: If the spread target is not an ExprTuple.
    """
    if isinstance(arg_expr, ExprTuple):
        return list(arg_expr.elements)

    msg = ctx.format_error(
        arg_span or call_span,
        (
            f"Cannot spread expression of type {type(arg_expr).__name__}.\n"
            "Only ExprTuples (from variadic parameter packs) can be spread.\n"
            "Runtime collections have unknown size at expansion time."
        ),
    )
    raise TypeError(msg)


def _flatten_call_args(args: tuple[CallArg, ...], call_span: Span, ctx: Context) -> tuple[dict[str, Expr], list[Expr]]:
    """Process call arguments, expanding spreads in-place.

    Spread arguments (expr...) are expanded inline into the positional argument list.
    For example, if `a = [1, 2, 3]`, then `f(4, a..., 5)` produces positional args
    `[4, 1, 2, 3, 5]`.

    Args:
        args: The call arguments to process.
        call_span: Source location for error messages.
        ctx: Compilation context.

    Returns:
        Tuple of (named_args dict, flattened positional_args list).

    Raises:
        ValueError: If duplicate named args or positional after named.
        TypeError: If a spread argument cannot be expanded.
    """
    named_args: dict[str, Expr] = {}
    positional_args: list[Expr] = []
    seen_named = False

    for arg in args:
        if arg.is_spread:
            if seen_named:
                msg = "Spread arguments cannot appear after named arguments"
                raise ValueError(ctx.format_error(arg.span or call_span, msg))
            expanded = _expand_spread_arg(arg.expr, arg.span, call_span, ctx)
            positional_args.extend(expanded)
        elif arg.name is not None:
            seen_named = True
            if arg.name in named_args:
                msg = f"Duplicate named argument '{arg.name}'"
                raise ValueError(ctx.format_error(arg.span or call_span, msg))
            named_args[arg.name] = arg.expr
        else:
            if seen_named:
                msg = "Positional arguments must come before named arguments"
                raise ValueError(ctx.format_error(arg.span or call_span, msg))
            positional_args.append(arg.expr)

    return named_args, positional_args


def _match_args_to_params(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    *,
    fn: ResolvedFnDef,
    original_params: tuple[FnParam, ...],
    named_args: dict[str, Expr],
    positional_args: list[Expr],
    call_span: Span,
    ctx: Context,
) -> dict[str, tuple[FnParam, Expr]]:
    """Match arguments to function parameters.

    Args:
        fn: The resolved function being called.
        original_params: The original FnParam instances from the unresolved FnDef.
        named_args: Named arguments (will be mutated - popped from).
        positional_args: Positional arguments (spreads already expanded).
        call_span: Source location for error messages.
        ctx: Compilation context.

    Returns:
        Substitution map from parameter name to (original FnParam, argument expression).

    Raises:
        TypeCheckError: If argument types don't match parameter types.
        ValueError: If argument count doesn't match or unknown named args.
    """
    substitutions: dict[str, tuple[FnParam, Expr]] = {}
    positional_idx = 0

    for resolved_param, orig_param in zip(fn.params, original_params, strict=True):
        if resolved_param.is_variadic:
            remaining_positional = positional_args[positional_idx:]
            positional_idx = len(positional_args)

            variadic_array = ExprTuple(
                elements=tuple(remaining_positional),
                span=call_span,
                ctx=ctx,
            )
            substitutions[resolved_param.name] = (orig_param, variadic_array)
        elif resolved_param.name in named_args:
            arg_expr = named_args.pop(resolved_param.name)
            if resolved_param.type_val is not None:
                arg_type = typeof(arg_expr)
                _unify_types(arg_type, resolved_param.type_val, call_span, ctx)
            substitutions[resolved_param.name] = (orig_param, arg_expr)
        elif positional_idx < len(positional_args):
            arg_expr = positional_args[positional_idx]
            positional_idx += 1
            if resolved_param.type_val is not None:
                arg_type = typeof(arg_expr)
                _unify_types(arg_type, resolved_param.type_val, call_span, ctx)
            substitutions[resolved_param.name] = (orig_param, arg_expr)
        else:
            msg = f"Missing argument for parameter '{resolved_param.name}'"
            raise ValueError(ctx.format_error(call_span, msg))

    if named_args:
        unused = ", ".join(f"'{k}'" for k in named_args)
        msg = f"Unknown named argument(s): {unused}"
        raise ValueError(ctx.format_error(call_span, msg))

    if positional_idx < len(positional_args):
        required_count = len(fn.params)
        msg = f"Function '{fn.name}' expects {required_count} arguments, got {len(positional_args)}"
        raise ValueError(ctx.format_error(call_span, msg))

    return substitutions


def expand_call(call: Call) -> Expr:
    """Expand a function call by substituting arguments for parameters.

    Performs template-style expansion: replaces parameter references in the
    function body with the corresponding argument expressions.

    Spread arguments are expanded in-place before parameter matching. For example,
    if `a = [1, 2, 3]`, then `f(4, a..., 5)` first expands to `f(4, 1, 2, 3, 5)`.
    This allows spreading into both variadic and non-variadic function calls.

    Only ExprTuples can be spread - these represent compile-time known
    collections (e.g., from variadic parameter packs). Runtime collections
    cannot be spread because their size is unknown at expansion time.

    Args:
        call: The Call expression to expand

    Returns:
        The function body with parameters substituted by arguments.

    Raises:
        TypeCheckError: If the call is not to a function reference or parameter type checks fail.
        ValueError: If argument matching fails.
        TypeError: If a spread argument cannot be expanded.
    """
    fn_ref = call.func.lookup() if isinstance(call.func, Ref) else call.func

    if not isinstance(fn_ref, FnDef):
        msg = "Can only expand calls to function references, not {fn_ref}"
        raise TypeCheckError(call.ctx.format_error(call.span, msg))

    fn = fn_ref.resolve()

    named_args, positional_args = _flatten_call_args(call.args, call.span, call.ctx)

    substitutions = _match_args_to_params(
        fn=fn,
        original_params=fn_ref.params,
        named_args=named_args,
        positional_args=positional_args,
        call_span=call.span,
        ctx=call.ctx,
    )
    return substitute_params(fn.body, substitutions)


def _can_expand_map_with_lambda(call: Call) -> tuple[Lambda, ExprTuple, Context] | None:
    """Check if a Call is a map() with lambda and ExprTuple that can be expanded.

    Args:
        call: The Call expression to check.

    Returns:
        Tuple of (lambda, collection, context) if expandable, None otherwise.
    """
    if not isinstance(call.func, Ref):
        return None

    func = call.func.lookup()
    if not isinstance(func, MapBuiltin):
        return None

    expected_args = 2
    if len(call.args) != expected_args:
        return None

    lambda_arg = call.args[0].expr
    collection_arg = call.args[1].expr

    if not isinstance(lambda_arg, Lambda) or not isinstance(collection_arg, ExprTuple):
        return None

    return lambda_arg, collection_arg, call.ctx


def expand_all_calls(expr: Expr) -> Expr:
    """Recursively expand all function calls in an expression.

    This performs a bottom-up traversal of the expression tree, expanding:
    - User-defined function calls (via expand_call)
    - map() calls with lambdas over known collections (via expand_map_with_lambda)

    The traversal is bottom-up so that nested calls are expanded first,
    allowing outer calls to see the expanded inner expressions.

    Args:
        expr: The expression to expand.

    Returns:
        A new expression with all expandable calls replaced by their expansions.

    Example::

        sum(map(fn(x) x * 2, [1, 2, 3]))  # noqa: ERA001
        # expands to: sum([1 * 2, 2 * 2, 3 * 2])
    """

    def expand_one(e: Expr) -> Expr:
        expanded_children = map_expr(expand_one, e)

        if isinstance(expanded_children, Call):
            map_parts = _can_expand_map_with_lambda(expanded_children)
            if map_parts is not None:
                lambda_arg, collection_arg, ctx = map_parts
                # Re-expand the result: the expanded map may contain calls
                # introduced by lambda body substitution.
                return expand_one(expand_map_with_lambda(lambda_arg, collection_arg, ctx))

            if isinstance(expanded_children.func, Ref):
                func = expanded_children.func.lookup()
                if isinstance(func, FnDef):
                    # Re-expand the result: the substituted fn body may contain
                    # calls.
                    return expand_one(expand_call(expanded_children))

        return expanded_children

    return expand_one(expr)


def check_no_closures(fn: FnDef) -> None:
    """Verify function body only references params, constants, and other functions.

    Uses fold_expr to find all Refs and validate their targets. Free variables
    in function bodies must be constants or other functions - no closures over
    signals, windows, or other runtime context.

    Args:
        fn: The function definition to check.

    Raises:
        ValueError: If the function has no body or references non-constant free variables.
    """
    for ref in find_refs(fn.body):
        entity = ref.lookup()

        match entity:
            case (
                FnParam()
                | FnDef()
                | BuiltinFn()
                | typesys.TypeDef()
                | statement.ImmutableBinding()
                | clkenum.ValueDef()
            ):
                continue
            case _:
                entity_type = type(entity).__name__
                msg = f"Function body cannot reference '{ref.path}' ({entity_type}): only parameters, constants, types, and other functions are allowed"
                raise ValueError(ref.ctx.format_error(ref.span, msg))


class TypeCheckError(Exception):
    """Raised when type checking fails."""


@dataclass
class FixedTypeBuiltin(BuiltinFn):
    """Builtin that always returns a fixed type regardless of arguments."""

    return_type: typesys.TypeVal

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> typesys.TypeVal:
        return self.return_type


@dataclass
class PreservesInputTypeBuiltin(BuiltinFn):
    """Builtin that returns the same type as its first argument."""

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> typesys.TypeVal | typesys.InferenceVar:
        if not arg_types:
            msg = f"Built-in '{self.name}' requires at least one argument"
            raise TypeError(_format_error(msg, span, ctx))
        return arg_types[0]


def verify_ord(
    t: typesys.TypeVal | typesys.InferenceVar,
    registry: dfl_types.TraitRegistry | None,
    span: Span | None,
    ctx: Context | None,
) -> None:
    """Verify a type implements the Ord trait.

    Gracefully skips validation when the registry is unavailable or the type
    is an unresolved InferenceVar.

    Raises:
        TypeCheckError: If the type does not implement Ord.
    """
    if registry is None or isinstance(t, typesys.InferenceVar):
        return

    ord_trait = registry.get_trait(f"{_STD_TRAITS_PATH}.Ord")
    if ord_trait is None:
        return

    impl = registry.find_impl(ord_trait, t, t)
    if impl is None:
        msg = f"Type {t} does not implement Ord"
        raise TypeCheckError(_format_error(msg, span, ctx))


@dataclass
class MinMaxBuiltin(BuiltinFn):
    """Builtin that supports both scalar min/max and collection min/max.

    Two calling conventions:

    - **Single arg** of ``CollectionType``: returns the element type (aggregate mode).
    - **Two or more** scalar args: unifies all arg types and returns the common type.

    Both modes validate that the result type implements the ``Ord`` trait.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> typesys.TypeVal | typesys.InferenceVar:
        if not arg_types:
            msg = f"Built-in '{self.name}' requires at least one argument"
            raise TypeCheckError(_format_error(msg, span, ctx))

        if len(arg_types) == 1:
            first_arg = arg_types[0]
            if isinstance(first_arg, dfl_types.CollectionType):
                result_type = first_arg.element_type
                verify_ord(result_type, registry, span, ctx)
                return result_type
            msg = f"Built-in '{self.name}' with a single argument requires a collection, got {first_arg}"
            raise TypeCheckError(_format_error(msg, span, ctx))

        result_type: typesys.TypeVal | typesys.InferenceVar = arg_types[0]
        for arg_t in arg_types[1:]:
            if span is not None and ctx is not None:
                result_type = _unify_types(result_type, arg_t, span, ctx)
            else:
                result_type = typesys.unify(result_type, arg_t)
        verify_ord(result_type, registry, span, ctx)
        return result_type


@dataclass
class CollectionElementTypeBuiltin(BuiltinFn):
    """Builtin that returns the element type of its collection argument."""

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> typesys.TypeVal | typesys.InferenceVar:
        if not arg_types:
            msg = f"Built-in '{self.name}' requires a collection argument"
            raise TypeError(_format_error(msg, span, ctx))
        first_arg = arg_types[0]
        if isinstance(first_arg, dfl_types.CollectionType):
            return first_arg.element_type
        msg = f"Built-in '{self.name}' expects a collection, got {first_arg}"
        raise TypeError(_format_error(msg, span, ctx))


@dataclass
class FlattenBuiltin(BuiltinFn):
    """Builtin that flattens a nested collection.

    flatten(Collection<Collection<T>>) -> Collection<T>
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> dfl_types.CollectionType:
        if not arg_types:
            msg = f"Built-in '{self.name}' requires a collection argument"
            raise TypeError(_format_error(msg, span, ctx))
        first_arg = arg_types[0]
        if not isinstance(first_arg, dfl_types.CollectionType):
            msg = f"Built-in '{self.name}' expects a collection, got {first_arg}"
            raise TypeError(_format_error(msg, span, ctx))
        inner = first_arg.element_type
        if not isinstance(inner, dfl_types.CollectionType):
            msg = f"Built-in '{self.name}' expects a nested collection, got Collection<{inner}>"
            raise TypeError(_format_error(msg, span, ctx))
        return dfl_types.CollectionType(inner.element_type)


@dataclass
class MapBuiltin(BuiltinFn):
    """Builtin that applies a function to each element of a collection.

    map(fn: T -> U, Collection<T>) -> Collection<U>

    The first argument can be either:
    - A function reference (builtin or user-defined)
    - An inline lambda: fn(x) x * 2
    """

    def _validate_args(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None,
        span: Span | None,
        ctx: Context | None,
    ) -> tuple[dfl_types.CollectionType, typesys.TypeVal, Ref | Lambda]:
        """Validate map() arguments and extract collection/element types and function/lambda.

        Args:
            arg_types: The types of the arguments.
            arg_exprs: The argument expressions.
            span: Source span for error messages.
            ctx: Context for error formatting.

        Returns:
            Tuple of (collection_type, element_type, func_or_lambda).

        Raises:
            TypeError: If arguments are invalid.
        """
        expected_args: Final = 2
        if len(arg_types) != expected_args:
            msg = f"map() requires {expected_args} arguments, got {len(arg_types)}"
            raise TypeError(_format_error(msg, span, ctx))

        if arg_exprs is None or len(arg_exprs) != expected_args:
            msg = "map() requires function argument expression for type inference"
            raise TypeError(_format_error(msg, span, ctx))

        func_expr = arg_exprs[0]
        if not isinstance(func_expr, Ref | Lambda):
            msg = "map() first argument must be a function reference or lambda"
            raise TypeError(_format_error(msg, span, ctx))

        collection_type = arg_types[1]
        if not isinstance(collection_type, dfl_types.CollectionType):
            msg = f"map() second argument must be a collection, got {collection_type}"
            raise TypeError(_format_error(msg, span, ctx))

        element_type = collection_type.element_type
        if isinstance(element_type, typesys.InferenceVar):
            msg = "map() cannot infer element type - collection type is unresolved"
            raise TypeError(_format_error(msg, span, ctx))

        return collection_type, element_type, func_expr

    def _infer_mapped_result_type(
        self,
        func: node.NamedEntity,
        element_type: typesys.TypeVal,
        registry: dfl_types.TraitRegistry | None,
        span: Span | None,
        ctx: Context | None,
    ) -> typesys.TypeVal:
        """Infer the return type of the mapping function.

        Args:
            func: The function being applied (builtin or user-defined).
            element_type: The element type of the input collection.
            registry: The trait registry.
            span: Source span for error messages.
            ctx: Context for error formatting.

        Returns:
            The resolved return type of the mapping function.

        Raises:
            TypeError: If the function type cannot be determined.
        """
        if isinstance(func, BuiltinFn):
            result_type = func.infer_return_type(
                arg_types=[element_type],
                arg_exprs=None,
                registry=registry,
                span=span,
                ctx=ctx,
            )
        elif isinstance(func, FnDef):
            if registry is None or span is None or ctx is None:
                msg = "map() with user function requires registry, span, and ctx"
                raise TypeError(_format_error(msg, span, ctx))
            result_type = infer_fn_return_type(func, element_type, registry, span, ctx)
        else:
            msg = f"map() second argument must be a function, got {type(func).__name__}"
            raise TypeError(_format_error(msg, span, ctx))

        if isinstance(result_type, typesys.InferenceVar):
            msg = "map() cannot determine return type of mapped function"
            raise TypeError(_format_error(msg, span, ctx))

        return result_type

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> dfl_types.CollectionType:
        _collection_type, element_type, func_or_lambda = self._validate_args(arg_types, arg_exprs, span, ctx)

        if isinstance(func_or_lambda, Lambda):
            if registry is None or span is None or ctx is None:
                msg = "map() with lambda requires registry, span, and ctx"
                raise TypeError(_format_error(msg, span, ctx))
            result_type = infer_lambda_return_type(func_or_lambda, element_type, registry, span, ctx)
        else:
            func = func_or_lambda.lookup()
            result_type = self._infer_mapped_result_type(func, element_type, registry, span, ctx)

        return dfl_types.CollectionType(result_type)


@dataclass
class FilterBuiltin(BuiltinFn):
    """Builtin that filters elements of a collection using a predicate.

    filter(pred: T -> Bool, Collection<T>) -> Collection<T>

    The first argument can be either:
    - A function reference (builtin or user-defined)
    - An inline lambda: fn(x) x > 0
    """

    def _validate_args(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None,
        span: Span | None,
        ctx: Context | None,
    ) -> tuple[dfl_types.CollectionType, typesys.TypeVal, Ref | Lambda]:
        """Validate filter() arguments and extract collection/element types and function/lambda.

        Args:
            arg_types: The types of the arguments.
            arg_exprs: The argument expressions.
            span: Source span for error messages.
            ctx: Context for error formatting.

        Returns:
            Tuple of (collection_type, element_type, func_or_lambda).

        Raises:
            TypeError: If arguments are invalid.
        """
        expected_args: Final = 2
        if len(arg_types) != expected_args:
            msg = f"filter() requires {expected_args} arguments, got {len(arg_types)}"
            raise TypeError(_format_error(msg, span, ctx))

        if arg_exprs is None or len(arg_exprs) < expected_args:
            msg = "filter() requires predicate argument expression for type inference"
            raise TypeError(_format_error(msg, span, ctx))

        func_expr = arg_exprs[0]
        if not isinstance(func_expr, Ref | Lambda):
            msg = "filter() first argument must be a function reference or lambda"
            raise TypeError(_format_error(msg, span, ctx))

        collection_type = arg_types[1]
        if not isinstance(collection_type, dfl_types.CollectionType):
            msg = f"filter() second argument must be a collection, got {collection_type}"
            raise TypeError(_format_error(msg, span, ctx))

        element_type = collection_type.element_type
        if isinstance(element_type, typesys.InferenceVar):
            msg = "filter() cannot infer element type - collection type is unresolved"
            raise TypeError(_format_error(msg, span, ctx))

        return collection_type, element_type, func_expr

    def _verify_predicate_returns_bool(
        self,
        func: node.NamedEntity,
        element_type: typesys.TypeVal,
        registry: dfl_types.TraitRegistry | None,
        span: Span | None,
        ctx: Context | None,
    ) -> None:
        """Verify that the predicate function returns Bool.

        Args:
            func: The predicate function (builtin or user-defined).
            element_type: The element type of the input collection.
            registry: The trait registry.
            span: Source span for error messages.
            ctx: Context for error formatting.

        Raises:
            TypeError: If the predicate does not return Bool.
        """
        if isinstance(func, BuiltinFn):
            result_type = func.infer_return_type(
                arg_types=[element_type],
                arg_exprs=None,
                registry=registry,
                span=span,
                ctx=ctx,
            )
        elif isinstance(func, FnDef):
            if registry is None or span is None or ctx is None:
                msg = "filter() with user predicate requires registry, span, and ctx"
                raise TypeError(_format_error(msg, span, ctx))
            result_type = infer_fn_return_type(func, element_type, registry, span, ctx)
        else:
            msg = f"filter() second argument must be a function, got {type(func).__name__}"
            raise TypeError(_format_error(msg, span, ctx))

        if result_type != clkbuiltins.BOOL:
            msg = f"filter() predicate must return Bool, got {result_type}"
            raise TypeError(_format_error(msg, span, ctx))

    def _verify_lambda_predicate_returns_bool(
        self,
        lambda_expr: Lambda,
        element_type: typesys.TypeVal,
        registry: dfl_types.TraitRegistry,
        span: Span,
        ctx: Context,
    ) -> None:
        """Verify that the lambda predicate returns Bool.

        Args:
            lambda_expr: The lambda expression.
            element_type: The element type of the input collection.
            registry: The trait registry.
            span: Source span for error messages.
            ctx: Context for error formatting.

        Raises:
            TypeError: If the predicate does not return Bool.
        """
        result_type = infer_lambda_return_type(lambda_expr, element_type, registry, span, ctx)
        if result_type != clkbuiltins.BOOL:
            msg = f"filter() lambda must return Bool, got {result_type}"
            raise TypeError(_format_error(msg, span, ctx))

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: Span | None = None,
        ctx: Context | None = None,
    ) -> dfl_types.CollectionType:
        collection_type, element_type, func_or_lambda = self._validate_args(arg_types, arg_exprs, span, ctx)

        if isinstance(func_or_lambda, Lambda):
            if registry is None or span is None or ctx is None:
                msg = "filter() with lambda requires registry, span, and ctx"
                raise TypeError(_format_error(msg, span, ctx))
            self._verify_lambda_predicate_returns_bool(func_or_lambda, element_type, registry, span, ctx)
        else:
            func = func_or_lambda.lookup()
            self._verify_predicate_returns_bool(func, element_type, registry, span, ctx)

        return collection_type


def make_typed_placeholder(
    return_type: typesys.TypeVal,
    name: str = "__placeholder__",
) -> tuple[FixedTypeBuiltin, node.Scope]:
    """Create a zero-arg builtin placeholder that returns a specific type.

    Args:
        return_type: The type this placeholder should return when called.
        name: The name for the placeholder builtin.

    Returns:
        Tuple of (FixedTypeBuiltin, Scope containing it).
    """
    placeholder_scope = node.Scope(parent=None, uniq_path="__placeholder__", module_id_for_errors=None)
    placeholder = FixedTypeBuiltin(
        name=name,
        scope=placeholder_scope,
        variadic=False,
        min_args=0,
        return_type=return_type,
    )
    placeholder_scope.names[name] = placeholder
    return placeholder, placeholder_scope


def infer_fn_return_type(
    fn: FnDef,
    element_type: typesys.TypeVal,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal | typesys.InferenceVar:
    """Infer the return type of a function when applied to an element type.

    Type-checks the function body with parameter references resolved to the
    element type, then returns the inferred result type.

    Args:
        fn: The function definition.
        element_type: The type of elements the function will be applied to.
        registry: The trait registry for type checking.
        span: Source span for error messages.
        ctx: Context for error message formatting.

    Returns:
        The inferred return type.

    Raises:
        TypeError: If type inference fails.
    """
    resolved = fn.resolve()

    if resolved.return_type is not None:
        return resolved.return_type

    if len(resolved.params) != 1:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"Function '{fn.name}' for map/filter must have exactly one parameter, got {len(resolved.params)}\n{error_loc}"
        raise TypeCheckError(msg)

    param = resolved.params[0]

    # To substitute the parameter, we create a fake builtin with a fixed return type of the type we want the parameter
    # to take on, then we substitute a Call to that builtin in place of the parameter reference.
    _placeholder_fn, placeholder_scope = make_typed_placeholder(element_type)

    placeholder_ref = Ref(path=("__placeholder__",), scope=placeholder_scope, span=span, ctx=ctx)
    placeholder_call = Call(func=placeholder_ref, args=(), span=span, ctx=ctx)

    substitutions: dict[str, tuple[FnParam, Expr]] = {param.name: (fn.params[0], placeholder_call)}
    substituted_body = substitute_params(resolved.body, substitutions)

    typed = type_check_expr(substituted_body, registry)
    return typed.type_info


def infer_lambda_return_type(
    lambda_expr: Lambda,
    element_type: typesys.TypeVal,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal | typesys.InferenceVar:
    """Infer the return type of a lambda when applied to an element type.

    Type-checks the lambda body with parameter references resolved to the
    element type, then returns the inferred result type.

    Args:
        lambda_expr: The lambda expression.
        element_type: The type of elements the lambda will be applied to.
        registry: The trait registry for type checking.
        span: Source span for error messages.
        ctx: Context for error message formatting.

    Returns:
        The inferred return type.

    Raises:
        TypeError: If type inference fails.
    """
    if len(lambda_expr.params) != 1:
        msg = f"Lambda for map/filter must have exactly one parameter, got {len(lambda_expr.params)}"
        raise TypeCheckError(_format_error(msg, span, ctx))

    param_name = lambda_expr.params[0]

    _placeholder_fn, placeholder_scope = make_typed_placeholder(element_type)
    placeholder_ref = Ref(path=("__placeholder__",), scope=placeholder_scope, span=span, ctx=ctx)
    placeholder_call = Call(func=placeholder_ref, args=(), span=span, ctx=ctx)

    substituted_body = substitute_lambda_param(lambda_expr.body, param_name, placeholder_call)

    typed = type_check_expr(substituted_body, registry)
    return typed.type_info


def _create_builtins_scope() -> node.Scope:
    """Create a scope containing all built-in DFL functions.

    This scope has clkbuiltins.BUILTINS_SCOPE as its parent, so both DFL functions
    and type builtins are accessible. This scope should be used as a parent scope
    for DFL compilation contexts so that built-in function names resolve correctly.

    Returns:
        A Scope containing all BuiltinFn instances.
    """
    builtins_scope = node.Scope(
        parent=clkbuiltins.BUILTINS_SCOPE, uniq_path="__dfl_builtins__", module_id_for_errors=None
    )

    builtins: list[BuiltinFn] = [
        MinMaxBuiltin(name="min", scope=builtins_scope, variadic=True, min_args=1),
        MinMaxBuiltin(name="max", scope=builtins_scope, variadic=True, min_args=1),
        PreservesInputTypeBuiltin(name="abs", scope=builtins_scope, variadic=False, min_args=1),
        PreservesInputTypeBuiltin(name="clamp", scope=builtins_scope, variadic=False, min_args=3),
        # Collection aggregates
        CollectionElementTypeBuiltin(name="sum", scope=builtins_scope, variadic=False, min_args=1),
        FixedTypeBuiltin(
            name="count", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.UINT64
        ),
        FixedTypeBuiltin(
            name="mean", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.FLOAT64
        ),
        FixedTypeBuiltin(name="any", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.BOOL),
        FixedTypeBuiltin(name="all", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.BOOL),
        # Collection transformers
        MapBuiltin(name="map", scope=builtins_scope, variadic=False, min_args=2),
        FilterBuiltin(name="filter", scope=builtins_scope, variadic=False, min_args=2),
        FlattenBuiltin(name="flatten", scope=builtins_scope, variadic=False, min_args=1),
        # Predicate functions
        FixedTypeBuiltin(
            name="is_positive", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.BOOL
        ),
        FixedTypeBuiltin(
            name="is_negative", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.BOOL
        ),
        FixedTypeBuiltin(
            name="is_zero", scope=builtins_scope, variadic=False, min_args=1, return_type=clkbuiltins.BOOL
        ),
    ]

    for builtin in builtins:
        builtins_scope.define(builtin.name, builtin, None)

    return builtins_scope


BUILTINS_SCOPE: Final = _create_builtins_scope()

BUILTINS: dict[str, BuiltinFn] = {
    name: entity for name, entity in BUILTINS_SCOPE.names.items() if isinstance(entity, BuiltinFn)
}


def get_builtin(name: str) -> BuiltinFn | None:
    """Get a built-in function by name.

    Args:
        name: The function name.

    Returns:
        The BuiltinFn if found, None otherwise.
    """
    return BUILTINS.get(name)


def _get_ref_type(ref: Ref) -> typesys.TypeVal | typesys.InferenceVar:
    """Get the type of a reference.

    Args:
        ref: The reference to get the type of.

    Returns:
        The type of the referenced entity. May be an InferenceVar.

    Raises:
        TypeCheckError: If the reference type cannot be determined.
    """
    entity = ref.lookup()
    type_info = getattr(entity, "type_info", None)
    if type_info is not None and isinstance(type_info, (typesys.TypeVal, typesys.InferenceVar)):
        return type_info
    raise TypeCheckError(ref.ctx.format_error(ref.span, f"Cannot determine type of '{ref}'"))


def _op_to_trait(op: BinaryOp | UnaryOp, registry: dfl_types.TraitRegistry) -> dfl_types.TraitDef:
    """Map an operator to its corresponding trait.

    Args:
        op: The binary or unary operator.
        registry: The trait registry to look up trait definitions.

    Returns:
        The trait definition.

    Raises:
        TypeCheckError: If the trait is not found in the registry.
    """
    trait_name_mapping: dict[BinaryOp | UnaryOp, str] = {
        # Binary arithmetic
        BinaryOp.ADD: "Add",
        BinaryOp.SUB: "Sub",
        BinaryOp.MUL: "Mul",
        BinaryOp.DIV: "Div",
        BinaryOp.MOD: "Rem",
        # Binary comparison
        BinaryOp.EQ: "Eq",
        BinaryOp.NE: "Eq",  # != uses Eq trait
        BinaryOp.LT: "Ord",
        BinaryOp.LE: "Ord",
        BinaryOp.GT: "Ord",
        BinaryOp.GE: "Ord",
        # Binary logical
        BinaryOp.AND: "And",
        BinaryOp.OR: "Or",
        # Unary
        UnaryOp.NEG: "Neg",
        UnaryOp.POS: "Pos",
        UnaryOp.NOT: "Not",
        UnaryOp.ABS: "Abs",
    }
    trait_name = trait_name_mapping[op]
    trait_fqn = f"{_STD_TRAITS_PATH}.{trait_name}"
    trait = registry.get_trait(trait_fqn)
    if trait is None:
        msg = f"Trait '{trait_fqn}' not found in registry"
        raise TypeCheckError(msg)
    return trait


def _describe_type(operand: typesys.TypeVal | typesys.InferenceVar) -> str:
    """Get a human-readable description of an operand type for error messages."""
    if isinstance(operand, typesys.InferenceVar):
        return "untyped literal"
    return operand.value_key()


def _find_trait_impl(  # noqa: PLR0913 # Too many parameters mitigated by keyword-only args
    *,
    trait: dfl_types.TraitDef,
    for_type: typesys.TypeVal | typesys.InferenceVar,
    rhs: typesys.TypeVal | typesys.InferenceVar | None,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> dfl_types.ResolvedTraitImpl:
    """Find a trait implementation for the given operand types.

    Handles both concrete types and InferenceVars.
    InferenceVars will be accepted if there is an unambiguous matching impl and otherwise rejected.
    Raises TypeCheckError with source location if no match or ambiguous.

    Args:
        trait: The trait to find an implementation for.
        for_type: The left/only operand type (may be InferenceVar).
        rhs: The right operand type for binary ops, None for unary.
        registry: The trait registry for looking up implementations.
        span: Source span for error messages.
        ctx: Context for error formatting.

    Returns:
        The matching trait implementation.

    Raises:
        TypeCheckError: If no valid implementation exists or if ambiguous.
    """
    matching_impls = registry.find_matching_impls(trait, for_type, rhs)

    if len(matching_impls) == 0:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        if rhs is not None:
            msg = f"No implementation of {trait.name} for {_describe_type(for_type)} and {_describe_type(rhs)}\n{error_loc}"
        else:
            msg = f"No implementation of {trait.name} for {_describe_type(for_type)}\n{error_loc}"
        raise TypeCheckError(msg)

    if len(matching_impls) > 1:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"Ambiguous operation: multiple implementations of {trait.name} could apply. "
        msg += "Add explicit type annotation to resolve.\n"
        msg += error_loc
        raise TypeCheckError(msg)

    return matching_impls[0]


def _get_trait_output(
    trait: dfl_types.TraitDef,
    impl: dfl_types.ResolvedTraitImpl,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal:
    """Get the Output type from a trait implementation.

    Args:
        trait: The trait (for error messages).
        impl: The trait implementation.
        registry: The trait registry.
        span: Source span for error messages.
        ctx: Context for error formatting.

    Returns:
        The Output type.

    Raises:
        TypeCheckError: If the trait has no Output type.
    """
    output = registry.output_type(impl)
    if output is None:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"Trait {trait.name} has no Output type\n{error_loc}"
        raise TypeCheckError(msg)
    return output


def binary_result_type(  # noqa: PLR0913 # Too many parameters mitigated by keyword-only args
    *,
    op: BinaryOp,
    left: typesys.TypeVal | typesys.InferenceVar,
    right: typesys.TypeVal | typesys.InferenceVar,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal:
    """Determine the result type of a binary operation.

    When one operand is an InferenceVar, we try to find matching implementations.
    If exactly one impl matches, we use it. If multiple match, we error.

    Args:
        op: The binary operator.
        left: The left operand type (may be InferenceVar).
        right: The right operand type (may be InferenceVar).
        registry: The trait registry for looking up implementations.
        span: Source span for error messages.
        ctx: Context for error formatting.

    Returns:
        The result type of the operation.

    Raises:
        TypeCheckError: If no valid implementation exists or if ambiguous.
    """
    trait = _op_to_trait(op, registry)
    impl = _find_trait_impl(trait=trait, for_type=left, rhs=right, registry=registry, span=span, ctx=ctx)
    return _get_trait_output(trait, impl, registry, span, ctx)


def unary_result_type(
    *,
    op: UnaryOp,
    operand: typesys.TypeVal | typesys.InferenceVar,
    registry: dfl_types.TraitRegistry,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal:
    """Determine the result type of a unary operation.

    When the operand is an InferenceVar, we try to find matching implementations.
    If exactly one impl matches, we use it. If multiple match, we error.

    Args:
        op: The unary operator.
        operand: The operand type (may be InferenceVar).
        registry: The trait registry for looking up implementations.
        span: Source span for error messages.
        ctx: Context for error formatting.

    Returns:
        The result type of the operation.

    Raises:
        TypeCheckError: If no valid implementation exists or if ambiguous.
    """
    trait = _op_to_trait(op, registry)
    impl = _find_trait_impl(trait=trait, for_type=operand, rhs=None, registry=registry, span=span, ctx=ctx)
    return _get_trait_output(trait, impl, registry, span, ctx)


def _check_member_access(
    base_type: typesys.TypeVal | typesys.InferenceVar,
    field_name: str,
    span: Span,
    ctx: Context,
    *,
    base_entity: node.NamedEntity | None = None,
) -> typesys.TypeVal:
    """Type-check member access on a base type.

    Args:
        base_type: The type of the base expression.
        field_name: The field name being accessed.
        span: Source span for error messages.
        ctx: Context for error message formatting.
        base_entity: The entity the base expression resolves to, if any.

    Returns:
        The type of the field.

    Raises:
        TypeCheckError: If member access is invalid.
    """
    if isinstance(base_type, typesys.InferenceVar):
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"Cannot access field '{field_name}' on untyped expression\n{error_loc}"
        raise TypeCheckError(msg)

    if isinstance(base_type, dfl_types.CollectionType):
        element_type = base_type.element_type
        field_type = _check_member_access(element_type, field_name, span, ctx, base_entity=base_entity)
        return dfl_types.CollectionType(field_type)

    if isinstance(base_entity, typesys.MembershipEntity):
        field_entity = base_entity.attribute(field_name)
        if field_entity is not None:
            field_type_info = getattr(field_entity, "type_info", None)
            if isinstance(field_type_info, typesys.TypeVal):
                return field_type_info

    error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
    msg = f"Type {base_type.value_key()} has no field '{field_name}'\n{error_loc}"
    raise TypeCheckError(msg)


def _check_if_else(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    *,
    test: Expr,
    then_: Expr,
    else_: Expr,
    span: Span,
    ctx: Context,
    check: Callable[[Expr], typesys.TypeVal | typesys.InferenceVar],
) -> typesys.TypeVal | typesys.InferenceVar:
    """Type-check an if-then-else expression.

    Args:
        test: The condition expression.
        then_: The then branch.
        else_: The else branch.
        span: Source span for error messages.
        ctx: Context for error message formatting.
        check: The type checking function for subexpressions.

    Returns:
        The unified type of then_ and else_.

    Raises:
        TypeCheckError: If test is not Bool or branches don't unify.
    """
    test_type = check(test)

    if test_type is not clkbuiltins.BOOL:
        error_loc = format_line_with_error(get_expr_span(test), ctx.terminals, ctx.module_id)
        msg = f"Condition in 'if' must be Bool, got {_describe_type(test_type)}\n{error_loc}"
        raise TypeCheckError(msg)

    then_type = check(then_)
    else_type = check(else_)

    return _unify_types(then_type, else_type, span, ctx)


def _check_cond_expr(
    arms: tuple[CondArm, ...],
    span: Span,
    ctx: Context,
    check: Callable[[Expr], typesys.TypeVal | typesys.InferenceVar],
) -> typesys.TypeVal | typesys.InferenceVar:
    """Type-check a cond expression.

    Args:
        arms: The cond arms.
        span: Source span for error messages.
        ctx: Context for error message formatting.
        check: The type checking function for subexpressions.

    Returns:
        The unified type of all arm bodies.

    Raises:
        TypeCheckError: If guards are not Bool, no else arm, or bodies don't unify.
    """
    if not arms:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"'cond' expression must have at least one arm\n{error_loc}"
        raise TypeCheckError(msg)

    if arms[-1].guard is not None:
        error_loc = format_line_with_error(arms[-1].span, ctx.terminals, ctx.module_id)
        msg = f"'cond' expression must end with 'else' arm\n{error_loc}"
        raise TypeCheckError(msg)

    result_type: typesys.TypeVal | typesys.InferenceVar | None = None

    for arm in arms:
        if arm.guard is not None:
            guard_type = check(arm.guard)
            if guard_type is not clkbuiltins.BOOL:
                error_loc = format_line_with_error(get_expr_span(arm.guard), ctx.terminals, ctx.module_id)
                msg = f"Condition in 'cond' must be Bool, got {_describe_type(guard_type)}\n{error_loc}"
                raise TypeCheckError(msg)

        body_type = check(arm.body)

        result_type = body_type if result_type is None else _unify_types(result_type, body_type, arm.span, ctx)

    assert result_type is not None
    return result_type


def _unify_types(
    a: typesys.TypeVal | typesys.InferenceVar,
    b: typesys.TypeVal | typesys.InferenceVar,
    span: Span,
    ctx: Context,
) -> typesys.TypeVal | typesys.InferenceVar:
    """Unify two types, returning the common type.

    Args:
        a: First type.
        b: Second type.
        span: Source span for error messages.
        ctx: Context for error message formatting.

    Returns:
        The unified type.

    Raises:
        TypeCheckError: If types cannot be unified.
    """
    if a is b:
        return a

    try:
        return typesys.unify(a, b)
    except TypeError:
        pass

    error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
    msg = f"Type mismatch: {_describe_type(a)} vs {_describe_type(b)}\n{error_loc}"
    raise TypeCheckError(msg)


def _check_expr_tuple(
    elements: tuple[Expr, ...],
    span: Span,
    ctx: Context,
    check: Callable[[Expr], typesys.TypeVal | typesys.InferenceVar],
) -> dfl_types.CollectionType:
    """Type-check an ExprTuple expression.

    All elements must have compatible types (unifiable).

    Args:
        elements: The element expressions.
        span: Source span for error messages.
        ctx: Context for error message formatting.
        check: The type checking function for subexpressions.

    Returns:
        A CollectionType wrapping the unified element type.

    Raises:
        TypeCheckError: If elements have incompatible types.
    """
    if not elements:
        error_loc = format_line_with_error(span, ctx.terminals, ctx.module_id)
        msg = f"Empty array literals are not supported - element type cannot be inferred\n{error_loc}"
        raise TypeCheckError(msg)
        raise TypeCheckError(msg)

    element_type = check(elements[0])
    for elem in elements[1:]:
        elem_type = check(elem)
        element_type = _unify_types(element_type, elem_type, get_expr_span(elem), ctx)

    return dfl_types.CollectionType(element_type)


def type_check_expr(  # noqa: C901 # Many branches for exhaustive Expr type handling
    expr: Expr, registry: dfl_types.TraitRegistry
) -> Typed[Expr]:
    """Type-check a DFL expression and annotate with result types.

    This performs a bottom-up type check, computing the type of each
    subexpression and verifying that operators have valid implementations.

    Args:
        expr: The expression to type-check.
        registry: The trait registry for looking up trait implementations.

    Returns:
        The expression wrapped with type information.

    Raises:
        TypeCheckError: If type checking fails.
    """

    def check(e: Expr) -> typesys.TypeVal | typesys.InferenceVar:  # noqa: PLR0911, PLR0912, C901 # Many branches/returns by nature of one per expression type
        match e:
            case primitive.DecimalLiteral() | primitive.UnitLiteral() | primitive.StringLiteral():
                return e.type_info
            case Ref():
                return _get_ref_type(e)
            case Member(base=base, field_name=field_name, span=span, ctx=ctx):
                base_type = check(base)
                base_entity = base.lookup() if isinstance(base, Ref) else None
                return _check_member_access(base_type, field_name, span, ctx, base_entity=base_entity)
            case Unary(op=op, operand=operand, span=span, ctx=ctx):
                operand_type = check(operand)
                return unary_result_type(
                    op=op,
                    operand=operand_type,
                    registry=registry,
                    span=span,
                    ctx=ctx,
                )
            case Binary(op=op, left=left, right=right, span=span, ctx=ctx):
                left_type = check(left)
                right_type = check(right)
                return binary_result_type(
                    op=op,
                    left=left_type,
                    right=right_type,
                    registry=registry,
                    span=span,
                    ctx=ctx,
                )
            case IfElse(test=test, then_=then_, else_=else_, span=span, ctx=ctx):
                return _check_if_else(test=test, then_=then_, else_=else_, span=span, ctx=ctx, check=check)
            case CondExpr(arms=arms, span=span, ctx=ctx):
                return _check_cond_expr(arms, span, ctx, check)
            case Match() as match_expr:
                scrutinee_type = check(match_expr.scrutinee)
                return check_match_type(match_expr, scrutinee_type, check)
            case Call(func=func_expr, args=call_args, span=span, ctx=ctx):
                func = func_expr.lookup() if isinstance(func_expr, Ref) else func_expr
                if isinstance(func, BuiltinFn):
                    # For map/filter, the first argument is a function reference that we don't
                    # type-check here - its validation is deferred to infer_return_type() which
                    # looks up the function and validates it against the collection's element type.
                    # We DO type-check the collection (second argument) to get its element type.
                    if isinstance(func, (MapBuiltin, FilterBuiltin)):
                        arg_types: list[typesys.TypeVal | typesys.InferenceVar] = []
                        if call_args:
                            # Placeholder for the function argument - actual validation happens
                            # in infer_return_type() via _validate_args() which looks up the
                            # function from arg_exprs[0] and validates it.
                            arg_types.append(clkbuiltins.BOOL)  # Placeholder, not used
                        if len(call_args) > 1:
                            # Type-check the collection to get its type (Collection<T>)
                            arg_types.append(check(call_args[1].expr))
                    else:
                        arg_types = [check(arg.expr) for arg in call_args]
                    # Pass all expressions - for map/filter, infer_return_type uses arg_exprs[0]
                    # to look up the function reference and validate it.
                    arg_exprs: list[object] = [arg.expr for arg in call_args]
                    return func.infer_return_type(
                        arg_types=arg_types,
                        arg_exprs=arg_exprs,
                        registry=registry,
                        span=span,
                        ctx=ctx,
                    )
                # Call nodes to user functions should be expanded via expand_call() before type checking.
                # Type checking operates on the expanded body, not the Call itself.
                msg = "Cannot type-check Call nodes directly - expand function calls first"
                raise TypeCheckError(ctx.format_error(span, msg))
            case ExprTuple(elements=elements, span=span, ctx=ctx):
                return _check_expr_tuple(elements, span, ctx, check)
            case Lambda(span=span, ctx=ctx):
                # Lambdas are not first-class values - they can only appear as arguments
                # to map/filter where they are specialcased and not type-checked directly.
                msg = "Lambda expressions cannot be type-checked directly - they can only be used as arguments to map/filter"
                raise TypeCheckError(_format_error(msg, span, ctx))

    result_type = check(expr)
    return Typed(expr, result_type)
