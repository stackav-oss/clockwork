# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner compiler — constraint and objective extraction from aligner bodies."""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass
from decimal import Decimal
from enum import Enum
from typing import TYPE_CHECKING, Final

from clockwork.dsl.ir import aligner_builtins, clkbuiltins, dfl, dfl_traits, primitive, typesys, units
from clockwork.dsl.ir.aligner import Aligner, AlignerInputDef, AlignerLetBinding, AlignerSpecStmt
from clockwork.dsl.ir.dfl_types import TraitDef, TraitRegistry, get_trait_registry

if TYPE_CHECKING:
    from clockwork.dsl.ir.aligner import ResolvedAlignerInput


BoundValue = primitive.UnitValue | primitive.DecimalValue


@dataclass(frozen=True, slots=True)
class FirstInBatch:
    """Refers to the first message to be selected in a batch input."""

    input_name: str


@dataclass(frozen=True, slots=True)
class LastInBatch:
    """Refers to the last message to be selected in a batch input."""

    input_name: str


InputSelector = str | FirstInBatch | LastInBatch


@dataclass(frozen=True, slots=True)
class IndexInView:
    """Sentinel for a message's position index within its view.

    Used by the compiler for batch boundary index variables (``lo_idx``,
    ``hi_idx``).  Cannot be constructed from the DSL.
    """


INDEX_IN_VIEW: Final = IndexInView()


@dataclass(frozen=True, slots=True)
class FieldAccessor:
    """A field accessor in a constraint expression.

    Identified by ``(input_name, field_name)``, e.g.,
    ``("lidar1", "observation_time")``.

    Not limited to timestamps — any field whose type supports subtraction
    and comparison is valid.

    ``input_name`` is a plain ``str`` for non-batch inputs, or
    ``FirstInBatch`` / ``LastInBatch`` for batch boundary references.

    ``field_name`` is either a schema field name (``str``) or
    :class:`IndexInView` for compiler-generated view-index variables.
    """

    input_name: InputSelector
    field_name: str | IndexInView

    @property
    def base_input_name(self) -> str:
        """The underlying input name, stripping any batch position wrapper."""
        return base_input_name(self.input_name)


def base_input_name(selector: InputSelector) -> str:
    """The underlying input name, stripping any batch position wrapper."""
    match selector:
        case str() as name:
            return name
        case FirstInBatch(input_name=name) | LastInBatch(input_name=name):
            return name


@dataclass(frozen=True, slots=True)
class DifferenceConstraint:
    """A normalized difference constraint: *minuend* - *subtrahend* <= *bound*.

    Attributes:
        minuend: Left-hand side of the difference.
        subtrahend: Right-hand side of the difference.
        bound: Upper bound value on the difference.
        source_expr: The original DFL expression, retained for error reporting.
    """

    minuend: FieldAccessor
    subtrahend: FieldAccessor
    bound: BoundValue
    source_expr: dfl.Expr | None


class FieldProperty(Enum):
    """Assumed property of a field across messages on one input."""

    STRICTLY_INCREASING = "strictly_increasing"
    NON_DECREASING = "non_decreasing"
    UNIQUE = "unique"


def _is_sorted(prop: FieldProperty | None) -> bool:
    """Return ``True`` iff *prop* guarantees sorted (non-decreasing) order.

    Both ``STRICTLY_INCREASING`` and ``NON_DECREASING`` imply that values
    on the input view are sorted; the difference lies only in whether
    consecutive duplicates are permitted.  Callers that care strictly
    about uniqueness must check ``FieldProperty.UNIQUE`` /
    ``FieldProperty.STRICTLY_INCREASING`` directly.
    """
    return prop is FieldProperty.STRICTLY_INCREASING or prop is FieldProperty.NON_DECREASING


@dataclass(frozen=True, slots=True)
class EqualityConstraint:
    """Equality constraint on a non-ordered field: left == right.

    Unlike :class:`DifferenceConstraint`, this does not enter the STN
    because the fields may lack ordering (e.g., UUIDs).

    Attributes:
        left: Left-hand side field accessor.
        right: Right-hand side field accessor.
        source_expr: The original DFL expression, retained for error reporting.
    """

    left: FieldAccessor
    right: FieldAccessor
    source_expr: dfl.Expr


class ObjectiveSense(Enum):
    """Whether an objective is a minimization or maximization."""

    MINIMIZE = "minimize"
    MAXIMIZE = "maximize"


@dataclass(frozen=True, slots=True)
class Objective:
    """An objective extracted from a ``minimize`` / ``maximize`` call.

    Attributes:
        sense: Minimization or maximization.
        expr: The DFL expression being optimized (inner argument).
        source_expr: The full ``minimize(…)`` / ``maximize(…)`` call.
        source_index: Position in the overall declaration sequence.
        else_guard_input: When set, this objective comes from an ``else``
            branch of ``has_candidates(input_name)``.
        conditional_guard_input: When set, this objective comes from a
            ``then`` branch of ``has_candidates(input_name)``.
    """

    sense: ObjectiveSense
    expr: dfl.Expr
    source_expr: dfl.Expr
    source_index: int = 0
    else_guard_input: str | None = None
    conditional_guard_input: str | None = None


@dataclass(frozen=True, slots=True)
class ExtractedSpecs:
    """All constraints, objectives, and field assumptions extracted from an aligner body.

    Attributes:
        field_accessors: Every ``(input, field)`` pair referenced in constraints.
        field_assumptions: Declared field properties (from ``assume()`` calls).
        unconditional_constraints: Difference constraints active in all presence
            configs.  Only fields declared sorted (``is_strictly_increasing``
            or ``is_non_decreasing``) produce these.
        unconditional_equalities: Equality constraints active in all presence
            configs.  Fields without ordering produce these.
        unconditional_objectives: Objectives active in all presence configs.
        conditional_constraints: ``{optional_input: constraints}`` from the
            then-branch of ``if has_candidates(optional_input)``; active only
            when that optional input is present.
        conditional_equalities: ``{optional_input: equalities}`` from the
            then-branch.
        conditional_objectives: ``{optional_input: objectives}`` from the
            then-branch; active only when that optional input is present.
        else_constraints: ``{optional_input: constraints}`` from the
            else-branch of ``if has_candidates(optional_input)``; active only
            when that optional input is *absent*.
        else_equalities: ``{optional_input: equalities}`` from the
            else-branch.
        else_objectives: ``{optional_input: objectives}`` from the
            else-branch; active only when that optional input is *absent*.
    """

    field_accessors: frozenset[FieldAccessor]
    field_assumptions: dict[FieldAccessor, FieldProperty]
    unconditional_constraints: tuple[DifferenceConstraint, ...]
    unconditional_equalities: tuple[EqualityConstraint, ...]
    unconditional_objectives: tuple[Objective, ...]
    conditional_constraints: dict[str, tuple[DifferenceConstraint, ...]]
    conditional_equalities: dict[str, tuple[EqualityConstraint, ...]]
    conditional_objectives: dict[str, tuple[Objective, ...]]
    else_constraints: dict[str, tuple[DifferenceConstraint, ...]]
    else_equalities: dict[str, tuple[EqualityConstraint, ...]]
    else_objectives: dict[str, tuple[Objective, ...]]


class SpecExtractionError(Exception):
    """Raised when a spec statement cannot be normalized."""


def _decompose_member_chain(expr: dfl.Expr) -> tuple[str, dfl.Ref, str] | None:
    """Decompose a (possibly nested) member chain into its root input and dotted field path.

    For ``input.field``, returns ``("input", <Ref>, "field")``.
    For ``input.sub.field``, returns ``("input", <Ref>, "sub.field")``.

    Returns ``None`` if the base is not a single-name ``Ref``.
    """
    fields: list[str] = []
    current: dfl.Expr = expr
    while isinstance(current, dfl.Member):
        fields.append(current.field_name)
        current = current.base
    if not fields:
        return None
    if isinstance(current, dfl.Ref) and len(current.path) == 1:
        return current.path[0], current, ".".join(reversed(fields))
    return None


def _resolve_nested_attribute(
    entity: typesys.MembershipEntity,
    field_name: str,
) -> typesys.Value | None:
    """Resolve a possibly dotted *field_name* through nested schemas.

    For ``"field"``, delegates to ``entity.attribute("field")``.
    For ``"sub.field"``, resolves ``"sub"`` first, then ``"field"`` on the
    resulting sub-schema type.

    Returns ``None`` if any step fails to resolve.
    """
    parts = field_name.split(".")
    current: typesys.MembershipEntity = entity
    result: typesys.Value | None = None
    for i, part in enumerate(parts):
        result = current.attribute(part)
        if result is None:
            return None
        if i < len(parts) - 1:
            type_info = getattr(result, "type_info", None)
            if not isinstance(type_info, typesys.MembershipEntity):
                return None
            current = type_info
    return result


def try_field_accessor(expr_node: dfl.Expr) -> FieldAccessor | None:
    """Try to interpret *expr_node* as ``input.field`` (or ``input.sub.field``, etc.) on an ``AlignerInputDef``.

    Recognizes three patterns:

    - ``input.field[.subfield...]`` on a non-batch input → ``FieldAccessor(input_name, dotted_field)``
    - ``min(batch_input.field[.subfield...])`` → ``FieldAccessor(FirstInBatch(name), dotted_field)``
    - ``max(batch_input.field[.subfield...])`` → ``FieldAccessor(LastInBatch(name), dotted_field)``

    Returns ``None`` if the expression is not a recognized field accessor
    pattern (e.g., bare ``batch_input.field``, ``min(non_batch.field)``,
    or anything else).
    """
    decomposed = _decompose_member_chain(expr_node)
    if decomposed is not None:
        input_name, base_ref, field_path = decomposed
        entity = base_ref.lookup()
        if isinstance(entity, AlignerInputDef) and entity.batch_size_lo_expr is None:
            return FieldAccessor(input_name=input_name, field_name=field_path)

    match expr_node:
        case dfl.Call(
            func=dfl.Ref() as func_ref,
            args=[dfl.CallArg(expr=inner_expr)],
        ):
            inner_decomposed = _decompose_member_chain(inner_expr)
            if inner_decomposed is not None:
                input_name, base_ref, field_path = inner_decomposed
                func_entity = func_ref.lookup()
                base_entity = base_ref.lookup()
                if (
                    isinstance(func_entity, dfl.MinMaxBuiltin)
                    and isinstance(base_entity, AlignerInputDef)
                    and base_entity.batch_size_lo_expr is not None
                ):
                    if func_entity.name == "min":
                        return FieldAccessor(input_name=FirstInBatch(input_name), field_name=field_path)
                    if func_entity.name == "max":
                        return FieldAccessor(input_name=LastInBatch(input_name), field_name=field_path)

        case _:
            pass
    return None


def _require_field_accessor(expr: dfl.Expr) -> FieldAccessor:
    """Like :func:`try_field_accessor` but raises on failure."""
    accessor = try_field_accessor(expr)
    if accessor is not None:
        return accessor

    decomposed = _decompose_member_chain(expr)
    if decomposed is not None:
        input_name, base_ref, field_path = decomposed
        entity = base_ref.lookup()
        if isinstance(entity, AlignerInputDef) and entity.batch_size_lo_expr is not None:
            full_path = f"{input_name}.{field_path}"
            msg = (
                f"Constraint operand '{full_path}' references a "
                "batch input. Use min() or max() to select a boundary: "
                f"min({full_path}) or max({full_path})."
            )
            raise SpecExtractionError(msg)

    msg = "Constraint operand is not a field accessor on an aligner input. Expected 'input.field' form."
    raise SpecExtractionError(msg)


def _extract_bound(expr: dfl.Expr) -> BoundValue:
    """Extract a bound value from a literal expression."""
    match expr:
        case primitive.UnitLiteral() | primitive.DecimalLiteral():
            return expr
        case dfl.Unary(op=dfl.UnaryOp.NEG, operand=primitive.UnitLiteral() | primitive.DecimalLiteral() as lit):
            return dataclasses.replace(lit, value=-lit.value)
        case _:
            msg = f"Constraint bound must be a literal value (e.g., 100ms or 42), not {expr}."
            raise SpecExtractionError(msg)


def _negate_bound(
    bound: BoundValue,
) -> BoundValue:
    """Negate a bound value: returns a copy with ``-bound.value``."""
    return dataclasses.replace(bound, value=-bound.value)


def _try_abs_bounded(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
) -> list[DifferenceConstraint] | None:
    """Try to match ``|a - b| <= d`` (given *left* ``<=`` *right*) and produce two bounded constraints."""
    match left:
        case dfl.Unary(
            op=dfl.UnaryOp.ABS,
            operand=dfl.Binary(op=dfl.BinaryOp.SUB, left=a, right=b),
        ):
            lhs = try_field_accessor(a)
            rhs = try_field_accessor(b)
            if lhs is None or rhs is None:
                return None
            bound = _extract_bound(right)
            return [
                DifferenceConstraint(minuend=lhs, subtrahend=rhs, bound=bound, source_expr=source_expr),
                DifferenceConstraint(minuend=rhs, subtrahend=lhs, bound=bound, source_expr=source_expr),
            ]
        case _:
            return None


def _try_sub_both_accessors(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
) -> list[DifferenceConstraint] | None:
    """Match SUB where both operands are field accessors: ``(fa - fa) <= lit`` or ``lit <= (fa - fa)``."""
    match (left, right):
        case (dfl.Binary(op=dfl.BinaryOp.SUB, left=a, right=b), _):
            fa_a = try_field_accessor(a)
            fa_b = try_field_accessor(b)
            if fa_a is not None and fa_b is not None:
                bound = _extract_bound(right)
                return [DifferenceConstraint(minuend=fa_a, subtrahend=fa_b, bound=bound, source_expr=source_expr)]

        case (_, dfl.Binary(op=dfl.BinaryOp.SUB, left=a, right=b)):
            fa_a = try_field_accessor(a)
            fa_b = try_field_accessor(b)
            if fa_a is not None and fa_b is not None:
                bound = _negate_bound(_extract_bound(left))
                return [DifferenceConstraint(minuend=fa_b, subtrahend=fa_a, bound=bound, source_expr=source_expr)]

        case _:
            pass

    return None


def _try_add_with_accessor(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
) -> list[DifferenceConstraint] | None:
    """Match ADD on one side, field accessor on the other: ``fa <= (fa + lit)`` or ``(fa + lit) <= fa``.

    Also handles commuted addition ``fa <= (lit + fa)`` for types with commutative Add.
    """
    match (left, right):
        case (_, dfl.Binary(op=dfl.BinaryOp.ADD, left=add_left, right=add_right)):
            fa_lhs = try_field_accessor(left)
            if fa_lhs is not None:
                fa_add_left = try_field_accessor(add_left)
                if fa_add_left is not None:
                    bound = _extract_bound(add_right)
                    return [
                        DifferenceConstraint(
                            minuend=fa_lhs, subtrahend=fa_add_left, bound=bound, source_expr=source_expr
                        )
                    ]
                fa_add_right = try_field_accessor(add_right)
                if fa_add_right is not None:
                    bound = _extract_bound(add_left)
                    return [
                        DifferenceConstraint(
                            minuend=fa_lhs, subtrahend=fa_add_right, bound=bound, source_expr=source_expr
                        )
                    ]

        case (dfl.Binary(op=dfl.BinaryOp.ADD, left=add_left, right=add_right), _):
            fa_rhs = try_field_accessor(right)
            if fa_rhs is not None:
                fa_add_left = try_field_accessor(add_left)
                if fa_add_left is not None:
                    bound = _negate_bound(_extract_bound(add_right))
                    return [
                        DifferenceConstraint(
                            minuend=fa_add_left, subtrahend=fa_rhs, bound=bound, source_expr=source_expr
                        )
                    ]
                fa_add_right = try_field_accessor(add_right)
                if fa_add_right is not None:
                    bound = _negate_bound(_extract_bound(add_left))
                    return [
                        DifferenceConstraint(
                            minuend=fa_add_right, subtrahend=fa_rhs, bound=bound, source_expr=source_expr
                        )
                    ]

        case _:
            pass

    return None


def _try_sub_accessor_literal(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
) -> list[DifferenceConstraint] | None:
    """Match SUB where one operand is a field accessor and the other a literal.

    Handles ``fa <= (fa - lit)`` and ``(fa - lit) <= fa``.
    Reached only when ``_try_sub_both_accessors`` fails (one SUB operand is not a field accessor).
    """
    match (left, right):
        case (_, dfl.Binary(op=dfl.BinaryOp.SUB, left=sub_left, right=sub_right)):
            fa_lhs = try_field_accessor(left)
            fa_sub_left = try_field_accessor(sub_left)
            if fa_lhs is not None and fa_sub_left is not None:
                bound = _negate_bound(_extract_bound(sub_right))
                return [
                    DifferenceConstraint(minuend=fa_lhs, subtrahend=fa_sub_left, bound=bound, source_expr=source_expr)
                ]

        case (dfl.Binary(op=dfl.BinaryOp.SUB, left=sub_left, right=sub_right), _):
            fa_rhs = try_field_accessor(right)
            fa_sub_left = try_field_accessor(sub_left)
            if fa_rhs is not None and fa_sub_left is not None:
                bound = _extract_bound(sub_right)
                return [
                    DifferenceConstraint(minuend=fa_sub_left, subtrahend=fa_rhs, bound=bound, source_expr=source_expr)
                ]

        case _:
            pass

    return None


def _try_directed_le(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
) -> list[DifferenceConstraint] | None:
    """Try to match directed bounded forms for ``left <= right``.

    Recognized patterns (where ``fa`` = field accessor, ``lit`` = literal)::

        (fa_a - fa_b) <= lit   -> [a - b <= d]
        lit <= (fa_a - fa_b)   -> [b - a <= -d]
        fa_a <= (fa_b + lit)   -> [a - b <= d]
        fa_a <= (lit + fa_b)   -> [a - b <= d]     (commuted addition)
        fa_a <= (fa_b - lit)   -> [a - b <= -d]
        (fa_b + lit) <= fa_a   -> [b - a <= -d]
        (lit + fa_b) <= fa_a   -> [b - a <= -d]    (commuted addition)
        (fa_b - lit) <= fa_a   -> [b - a <= d]

    The helpers use overlapping structural patterns with runtime guards to
    disambiguate.  ``_try_sub_both_accessors`` handles SUB where both operands
    are field accessors; ``_try_sub_accessor_literal`` handles SUB where one
    operand is a literal.  If the first matches structurally but fails its
    guard, execution falls through to the second.  The DFL type system prevents
    pathological forms like ``(fa - fa) <= (fa - lit)`` from reaching
    ``_extract_bound`` on a non-literal.
    """
    return (
        _try_sub_both_accessors(left, right, source_expr)
        or _try_add_with_accessor(left, right, source_expr)
        or _try_sub_accessor_literal(left, right, source_expr)
    )


@dataclass(frozen=True, slots=True)
class _TypeContext:
    """Shared context for type-aware constraint extraction.

    Bundles the inputs, trait registry, and Sub trait definition
    needed by ``_resolve_difference_zero`` and its callers.
    """

    inputs: dict[str, AlignerInputDef]
    registry: TraitRegistry
    sub_trait: TraitDef


def _make_typed_zero(
    diff_type: typesys.TypeVal,
) -> BoundValue:
    """Create a typed zero from a Sub trait output type."""
    if diff_type is clkbuiltins.DURATION:
        return primitive.UnitValue.make(value=Decimal(0), unit=units.SECONDS)
    if isinstance(diff_type, clkbuiltins.PrimitiveType):
        return primitive.DecimalValue(type_info=diff_type, value=Decimal(0))
    msg = f"Unsupported Sub output type for aligner constraint: {diff_type}"
    raise SpecExtractionError(msg)


def _resolve_difference_zero(
    lhs: FieldAccessor,
    rhs: FieldAccessor,
    ctx: _TypeContext,
) -> BoundValue:
    """Create a typed zero for the difference ``lhs - rhs`` via the Sub trait.

    Args:
        lhs: Left-hand field accessor (minuend).
        rhs: Right-hand field accessor (subtrahend).
        ctx: Type context for field type lookup and trait resolution.

    Returns:
        A typed zero bound (``UnitValue`` or ``DecimalValue``).

    Raises:
        SpecExtractionError: If field types cannot be determined,
            the Sub trait has no implementation, or the difference
            type is unsupported.
    """
    lhs_input = ctx.inputs[lhs.base_input_name]
    rhs_input = ctx.inputs[rhs.base_input_name]

    lhs_attr = _resolve_nested_attribute(lhs_input, lhs.field_name) if isinstance(lhs.field_name, str) else None
    rhs_attr = _resolve_nested_attribute(rhs_input, rhs.field_name) if isinstance(rhs.field_name, str) else None
    if lhs_attr is None or rhs_attr is None:
        msg = (
            f"Cannot determine field type for bare ordering constraint: "
            f"{lhs.base_input_name}.{lhs.field_name} - {rhs.base_input_name}.{rhs.field_name}"
        )
        raise SpecExtractionError(msg)

    lhs_type = lhs_attr.type_info
    rhs_type = rhs_attr.type_info

    # Schema field types are always concrete after compilation (never InferenceVar).
    assert not isinstance(lhs_type, typesys.InferenceVar)
    assert not isinstance(rhs_type, typesys.InferenceVar)

    impl = ctx.registry.find_impl(ctx.sub_trait, lhs_type, rhs_type)
    if impl is None:
        msg = (
            f"No Sub trait implementation for "
            f"{lhs.base_input_name}.{lhs.field_name} ({lhs_type}) - "
            f"{rhs.base_input_name}.{rhs.field_name} ({rhs_type})"
        )
        raise SpecExtractionError(msg)

    diff_type = ctx.registry.output_type(impl)
    assert diff_type is not None  # Sub trait requires an Output type
    return _make_typed_zero(diff_type)


def _normalize_le_comparison(
    left: dfl.Expr,
    right: dfl.Expr,
    source_expr: dfl.Expr,
    ctx: _TypeContext,
) -> list[DifferenceConstraint]:
    """Normalize a ``left <= right`` comparison into difference constraints.

    Tries patterns in priority order: abs bounded, directed bounded, bare ordering.
    Both LE and GE comparisons are routed here (GE flips operands first).
    """
    abs_result = _try_abs_bounded(left, right, source_expr)
    if abs_result is not None:
        return abs_result

    dir_result = _try_directed_le(left, right, source_expr)
    if dir_result is not None:
        return dir_result

    # Bare ordering: both sides must be field accessors → left - right <= 0
    lhs = _require_field_accessor(left)
    rhs = _require_field_accessor(right)
    zero = _resolve_difference_zero(lhs, rhs, ctx)
    return [DifferenceConstraint(minuend=lhs, subtrahend=rhs, bound=zero, source_expr=source_expr)]


def _normalize_require(
    arg: dfl.Expr,
    source_expr: dfl.Expr,
    field_assumptions: dict[FieldAccessor, FieldProperty],
    ctx: _TypeContext,
) -> list[DifferenceConstraint | EqualityConstraint]:
    """Normalize a ``require(arg)`` into difference or equality constraints.

    Recognized patterns::

        require(a == b)       -> DifferenceConstraint pair if both are
                                 sorted (strictly_increasing or non_decreasing),
                                 else EqualityConstraint
        require(a <= b)       -> [a-b <= 0]
        require(a >= b)       -> [b-a <= 0]
        require(|a-b| <= d)   -> [a-b <= d, b-a <= d]
        require(a - b <= d)   -> [a-b <= d]
        require(a <= b + d)   -> [a-b <= d]
        require(a - b >= d)   -> [b-a <= -d]
        require(a >= b - d)   -> [b-a <= d]
        require(true)         -> []
        require(false)        -> SpecExtractionError

    Ordering constraints (LE/GE/abs-bounded/directed-bounded) validate that
    all referenced fields are declared sorted (``is_strictly_increasing``
    or ``is_non_decreasing``).
    """
    match arg:
        case dfl.Ref() as ref if ref.lookup() is clkbuiltins.TRUE_VALUE:
            return []

        case dfl.Ref() as ref if ref.lookup() is clkbuiltins.FALSE_VALUE:
            msg = "require(false) makes the aligner trivially unsatisfiable."
            raise SpecExtractionError(msg)

        case dfl.Binary(op=dfl.BinaryOp.EQ, left=left, right=right):
            lhs = _require_field_accessor(left)
            rhs = _require_field_accessor(right)
            both_sorted = _is_sorted(field_assumptions.get(lhs)) and _is_sorted(field_assumptions.get(rhs))
            if both_sorted:
                zero = _resolve_difference_zero(lhs, rhs, ctx)
                return [
                    DifferenceConstraint(minuend=lhs, subtrahend=rhs, bound=zero, source_expr=source_expr),
                    DifferenceConstraint(minuend=rhs, subtrahend=lhs, bound=zero, source_expr=source_expr),
                ]
            return [EqualityConstraint(left=lhs, right=rhs, source_expr=source_expr)]

        case dfl.Binary(op=dfl.BinaryOp.LE, left=left, right=right):
            diff_constraints = _normalize_le_comparison(left, right, source_expr, ctx)
            _validate_ordering_constraints(diff_constraints, field_assumptions)
            return list(diff_constraints)

        case dfl.Binary(op=dfl.BinaryOp.GE, left=left, right=right):
            # Flip GE to LE: a >= b  ⟺  b <= a
            diff_constraints = _normalize_le_comparison(right, left, source_expr, ctx)
            _validate_ordering_constraints(diff_constraints, field_assumptions)
            return list(diff_constraints)

        case _:
            msg = (
                "Cannot normalize constraint. Supported forms: "
                "require(a == b), require(a <= b), require(a >= b), "
                "require(|a - b| <= d), require(a - b <= d), require(a <= b + d)."
            )
            raise SpecExtractionError(msg)


def _validate_ordering_constraints(
    constraints: list[DifferenceConstraint],
    field_assumptions: dict[FieldAccessor, FieldProperty],
) -> None:
    """Validate that all fields in ordering constraints are sorted.

    A field is "sorted" if it is declared ``is_strictly_increasing`` or
    ``is_non_decreasing``.  Either assumption suffices: ordering
    constraints reason about relative order only and do not require
    uniqueness.

    Raises:
        SpecExtractionError: If any field is not declared sorted.
    """
    for c in constraints:
        for accessor in (c.minuend, c.subtrahend):
            if not _is_sorted(field_assumptions.get(accessor)):
                msg = (
                    f"Ordering constraint references field '{accessor.base_input_name}.{accessor.field_name}' "
                    f"which is not declared as strictly increasing or non-decreasing. "
                    f"Add assume(is_strictly_increasing({accessor.base_input_name}.{accessor.field_name})) "
                    f"or assume(is_non_decreasing({accessor.base_input_name}.{accessor.field_name})) "
                    f"if the field is known to be sorted."
                )
                raise SpecExtractionError(msg)


def _has_candidates_input(expr: dfl.Expr) -> str | None:
    """If *expr* is ``has_candidates(input)``, return the input name; else ``None``."""
    match expr:
        case dfl.Call(func=dfl.Ref() as func_ref, args=[arg_node]) if isinstance(
            func_ref.lookup(), aligner_builtins.HasCandidatesBuiltin
        ):
            match arg_node.expr:
                case dfl.Ref(path=(input_name,)):
                    return input_name
                case _:
                    pass
        case _:
            pass
    return None


def _generate_batch_constraints(input_name: str, resolved_input: ResolvedAlignerInput) -> list[DifferenceConstraint]:
    """Generate ordering and size constraints for a batch input.

    For ``batch_size: [lo, hi]``, generates:

    - Max size: ``hi_idx - lo_idx <= hi - 1``
    - Min size: ``lo_idx - hi_idx <= 1 - lo``

    Uses the ``batch_size`` expression nodes from the input definition as
    ``source_expr`` for error reporting traceability.
    """
    if resolved_input.batch_size is None:
        return []
    lo, hi = resolved_input.batch_size
    lo_idx = FieldAccessor(input_name=FirstInBatch(input_name), field_name=INDEX_IN_VIEW)
    hi_idx = FieldAccessor(input_name=LastInBatch(input_name), field_name=INDEX_IN_VIEW)

    constraints: list[DifferenceConstraint] = []

    max_bound = primitive.DecimalValue(type_info=clkbuiltins.INT64, value=Decimal(hi - 1))
    constraints.append(
        DifferenceConstraint(minuend=hi_idx, subtrahend=lo_idx, bound=max_bound, source_expr=None),
    )

    min_bound = primitive.DecimalValue(type_info=clkbuiltins.INT64, value=Decimal(1 - lo))
    constraints.append(
        DifferenceConstraint(minuend=lo_idx, subtrahend=hi_idx, bound=min_bound, source_expr=None),
    )

    return constraints


def _add_batch_constraints(aligner_node: Aligner, collector: _SpecCollector) -> None:
    """Add batch size constraints for all batch inputs in the aligner."""
    resolved = aligner_node.resolved
    if resolved is not None:
        for name, resolved_input in resolved.inputs.items():
            for c in _generate_batch_constraints(name, resolved_input):
                collector.add_constraint(c)


class _SpecCollector:
    """Accumulates constraints, objectives, and field assumptions during spec extraction."""

    def __init__(self) -> None:
        self.constraints: list[DifferenceConstraint] = []
        self.equalities: list[EqualityConstraint] = []
        self.objectives: list[Objective] = []
        self.conditional_constraints: dict[str, list[DifferenceConstraint]] = {}
        self.conditional_equalities: dict[str, list[EqualityConstraint]] = {}
        self.conditional_objectives: dict[str, list[Objective]] = {}
        self.else_constraints: dict[str, list[DifferenceConstraint]] = {}
        self.else_equalities: dict[str, list[EqualityConstraint]] = {}
        self.else_objectives: dict[str, list[Objective]] = {}
        self.field_accessors: set[FieldAccessor] = set()
        self.field_assumptions: dict[FieldAccessor, FieldProperty] = {}
        self._next_objective_index: int = 0

    def add_assumption(self, accessor: FieldAccessor, prop: FieldProperty) -> None:
        """Record a field assumption, rejecting duplicates and conflicts.

        Canonicalization: declaring ``is_non_decreasing`` together with
        ``is_unique`` (in either order) is semantically equivalent to
        ``is_strictly_increasing`` and is stored as such.

        Args:
            accessor: The field accessor.
            prop: The assumed property.

        Raises:
            SpecExtractionError: If the combination is a duplicate or a
                redundant declaration.
        """
        existing = self.field_assumptions.get(accessor)
        field = f"{accessor.base_input_name}.{accessor.field_name}"
        if existing is None:
            self.field_assumptions[accessor] = prop
            return

        # Canonicalize {non_decreasing, unique} (either order) to strictly_increasing.
        if {existing, prop} == {FieldProperty.NON_DECREASING, FieldProperty.UNIQUE}:
            self.field_assumptions[accessor] = FieldProperty.STRICTLY_INCREASING
            return

        if existing is prop:
            msg = (
                f"Duplicate assumption on '{field}': "
                f"{prop.value} is already declared. Remove the redundant assume() call."
            )
            raise SpecExtractionError(msg)

        # Redundancy: strict already implies both non-decreasing and unique.
        if existing is FieldProperty.STRICTLY_INCREASING and prop is FieldProperty.NON_DECREASING:
            msg = (
                f"Field '{field}' is already declared is_strictly_increasing "
                f"(which implies is_non_decreasing); remove the redundant "
                f"is_non_decreasing declaration."
            )
            raise SpecExtractionError(msg)
        if existing is FieldProperty.NON_DECREASING and prop is FieldProperty.STRICTLY_INCREASING:
            msg = (
                f"Field '{field}' is already declared is_non_decreasing; "
                f"is_strictly_increasing is redundant (strict implies non-decreasing). "
                f"Use is_strictly_increasing alone, or combine is_non_decreasing with is_unique."
            )
            raise SpecExtractionError(msg)

        msg = (
            f"Conflicting assumptions on '{field}': "
            f"already declared as {existing.value}, cannot also declare as {prop.value}. "
            f"Use only one of is_strictly_increasing or is_unique per field."
        )
        raise SpecExtractionError(msg)

    def add_constraint(
        self,
        constraint: DifferenceConstraint | EqualityConstraint,
        *,
        condition: str | None = None,
        else_condition: str | None = None,
    ) -> None:
        """Add a constraint, optionally conditional on an input's presence.

        Args:
            constraint: The constraint to add.
            condition: If set, the constraint is active when this input IS present
                (has_candidates then-branch).
            else_condition: If set, the constraint is active when this input is
                ABSENT (has_candidates else-branch).
        """
        match constraint:
            case DifferenceConstraint():
                if condition is not None:
                    self.conditional_constraints.setdefault(condition, []).append(constraint)
                elif else_condition is not None:
                    self.else_constraints.setdefault(else_condition, []).append(constraint)
                else:
                    self.constraints.append(constraint)
                self.field_accessors.add(constraint.minuend)
                self.field_accessors.add(constraint.subtrahend)
            case EqualityConstraint():
                if condition is not None:
                    self.conditional_equalities.setdefault(condition, []).append(constraint)
                elif else_condition is not None:
                    self.else_equalities.setdefault(else_condition, []).append(constraint)
                else:
                    self.equalities.append(constraint)
                self.field_accessors.add(constraint.left)
                self.field_accessors.add(constraint.right)

    def add_objective(
        self,
        objective: Objective,
        *,
        condition: str | None = None,
        else_condition: str | None = None,
    ) -> None:
        """Add an objective, optionally conditional on an input's presence.

        Args:
            objective: The objective to add.
            condition: If set, the objective is active when this input IS present.
            else_condition: If set, the objective is active when this input is ABSENT.
        """
        indexed = Objective(
            sense=objective.sense,
            expr=objective.expr,
            source_expr=objective.source_expr,
            source_index=self._next_objective_index,
            else_guard_input=else_condition,
            conditional_guard_input=condition,
        )
        self._next_objective_index += 1
        if condition is not None:
            self.conditional_objectives.setdefault(condition, []).append(indexed)
        elif else_condition is not None:
            self.else_objectives.setdefault(else_condition, []).append(indexed)
        else:
            self.objectives.append(indexed)

    def to_extracted_specs(self) -> ExtractedSpecs:
        """Build the final ``ExtractedSpecs``."""
        return ExtractedSpecs(
            field_accessors=frozenset(self.field_accessors),
            field_assumptions=dict(self.field_assumptions),
            unconditional_constraints=tuple(self.constraints),
            unconditional_equalities=tuple(self.equalities),
            unconditional_objectives=tuple(self.objectives),
            conditional_constraints={k: tuple(v) for k, v in self.conditional_constraints.items()},
            conditional_equalities={k: tuple(v) for k, v in self.conditional_equalities.items()},
            conditional_objectives={k: tuple(v) for k, v in self.conditional_objectives.items()},
            else_constraints={k: tuple(v) for k, v in self.else_constraints.items()},
            else_equalities={k: tuple(v) for k, v in self.else_equalities.items()},
            else_objectives={k: tuple(v) for k, v in self.else_objectives.items()},
        )


def _extract_assumption_accessor(
    inner: dfl.Expr,
    builtin_name: str,
) -> list[FieldAccessor]:
    """Extract field accessor(s) from the argument of ``is_strictly_increasing``, ``is_non_decreasing``, or ``is_unique``.

    - Non-batch input: returns ``[FieldAccessor(input_name, dotted_field)]``.
    - Batch input: returns ``[FieldAccessor(FirstInBatch(name), dotted_field),
      FieldAccessor(LastInBatch(name), dotted_field)]``.

    Rejects ``min(batch.field)`` / ``max(batch.field)`` — assumptions
    describe the entire input stream, not individual batch endpoints.

    Raises:
        SpecExtractionError: If the argument is not a bare field accessor.
    """
    decomposed = _decompose_member_chain(inner)
    if decomposed is not None:
        input_name, base_ref, field_path = decomposed
        entity = base_ref.lookup()
        if isinstance(entity, AlignerInputDef):
            if entity.batch_size_lo_expr is not None:
                return [
                    FieldAccessor(input_name=FirstInBatch(input_name), field_name=field_path),
                    FieldAccessor(input_name=LastInBatch(input_name), field_name=field_path),
                ]
            return [FieldAccessor(input_name=input_name, field_name=field_path)]
    match inner:
        case dfl.Call(
            func=dfl.Ref() as func_ref,
            args=[dfl.CallArg(expr=_)],
        ):
            func_entity = func_ref.lookup()
            if isinstance(func_entity, dfl.MinMaxBuiltin):
                msg = (
                    f"{builtin_name}() argument must be 'input.field[.sub_field...]', not "
                    f"{func_entity.name}(). Assumptions apply to the entire input stream."
                )
                raise SpecExtractionError(msg)
        case _:
            pass
    msg = f"{builtin_name}() argument must be a field accessor in 'input.field[.sub_field...]' form."
    raise SpecExtractionError(msg)


def _extract_assumption(expr: dfl.Expr) -> list[tuple[FieldAccessor, FieldProperty]]:
    """Extract field assumption(s) from an ``assume()`` inner expression.

    Recognized forms::

        is_strictly_increasing(input.field)
        is_non_decreasing(input.field)
        is_unique(input.field)

    For batch inputs, a single ``assume`` expands to assumptions on both
    ``FirstInBatch`` and ``LastInBatch`` endpoints.

    Returns:
        List of ``(FieldAccessor, FieldProperty)`` pairs (1 for non-batch, 2 for batch).

    Raises:
        SpecExtractionError: If the expression is not a recognized assumption form.
    """
    match expr:
        case dfl.Call(func=dfl.Ref() as func_ref, args=[arg_node]):
            entity = func_ref.lookup()
            inner = arg_node.expr
            match entity:
                case aligner_builtins.IsStrictlyIncreasingBuiltin():
                    accessors = _extract_assumption_accessor(inner, "is_strictly_increasing")
                    return [(acc, FieldProperty.STRICTLY_INCREASING) for acc in accessors]
                case aligner_builtins.IsNonDecreasingBuiltin():
                    accessors = _extract_assumption_accessor(inner, "is_non_decreasing")
                    return [(acc, FieldProperty.NON_DECREASING) for acc in accessors]
                case aligner_builtins.IsUniqueBuiltin():
                    accessors = _extract_assumption_accessor(inner, "is_unique")
                    return [(acc, FieldProperty.UNIQUE) for acc in accessors]
                case _:
                    pass
        case _:
            pass
    msg = "assume() argument must be is_unique(input.field), is_strictly_increasing(input.field), or is_non_decreasing(input.field)."
    raise SpecExtractionError(msg)


def _reject_assume_in_branch(expr: dfl.Expr) -> None:
    """Reject ``assume()`` calls inside conditional branches.

    Assumptions are global properties that must be declared at the top level
    of the aligner body.  Placing them inside ``if has_candidates(...)``
    branches is semantically ambiguous and not supported.

    Raises:
        SpecExtractionError: If an ``assume()`` is found inside a branch.
    """
    match expr:
        case dfl.Binary(op=dfl.BinaryOp.AND, left=left, right=right):
            _reject_assume_in_branch(left)
            _reject_assume_in_branch(right)

        case dfl.Call(func=dfl.Ref() as func_ref):
            entity = func_ref.lookup()
            if isinstance(entity, aligner_builtins.AssumeBuiltin):
                msg = (
                    "assume() cannot appear inside a conditional branch. "
                    "Field assumptions must be declared at the top level of the aligner body."
                )
                raise SpecExtractionError(msg)

        case dfl.IfElse(then_=then_branch, else_=else_branch):
            _reject_assume_in_branch(then_branch)
            _reject_assume_in_branch(else_branch)

        case _:
            pass


def _collect_assumptions_from_spec_expr(
    expr: dfl.Expr,
    collector: _SpecCollector,
) -> None:
    """Walk a spec expression collecting only ``assume()`` calls (pass 1).

    Non-assume calls are silently skipped.

    Raises:
        SpecExtractionError: If an assume() argument is malformed or appears
            inside a conditional branch.
    """
    match expr:
        case dfl.Binary(op=dfl.BinaryOp.AND, left=left, right=right):
            _collect_assumptions_from_spec_expr(left, collector)
            _collect_assumptions_from_spec_expr(right, collector)

        case dfl.Call(func=dfl.Ref() as func_ref, args=[arg_node]):
            entity = func_ref.lookup()
            if isinstance(entity, aligner_builtins.AssumeBuiltin):
                for accessor, prop in _extract_assumption(arg_node.expr):
                    collector.add_assumption(accessor, prop)

        case dfl.IfElse(then_=then_branch, else_=else_branch):
            _reject_assume_in_branch(then_branch)
            _reject_assume_in_branch(else_branch)

        case _:
            pass  # Non-assume expressions handled in pass 2


def _extract_from_spec_expr(
    expr: dfl.Expr,
    collector: _SpecCollector,
    ctx: _TypeContext,
    *,
    condition_input: str | None = None,
    else_condition_input: str | None = None,
) -> None:
    """Walk a spec expression, collecting constraints and objectives (pass 2).

    ``assume()`` calls are silently skipped (already collected in pass 1).

    Raises:
        SpecExtractionError: If an expression cannot be normalized.
    """
    match expr:
        case dfl.Binary(op=dfl.BinaryOp.AND, left=left, right=right):
            _extract_from_spec_expr(
                left,
                collector,
                ctx,
                condition_input=condition_input,
                else_condition_input=else_condition_input,
            )
            _extract_from_spec_expr(
                right,
                collector,
                ctx,
                condition_input=condition_input,
                else_condition_input=else_condition_input,
            )

        case dfl.Call(func=dfl.Ref() as func_ref, args=[arg_node]):
            entity = func_ref.lookup()
            inner = arg_node.expr
            match entity:
                case aligner_builtins.AssumeBuiltin():
                    pass  # Already collected in pass 1
                case aligner_builtins.RequireBuiltin():
                    for c in _normalize_require(inner, expr, collector.field_assumptions, ctx):
                        collector.add_constraint(
                            c,
                            condition=condition_input,
                            else_condition=else_condition_input,
                        )
                case aligner_builtins.MinimizeBuiltin():
                    collector.add_objective(
                        Objective(sense=ObjectiveSense.MINIMIZE, expr=inner, source_expr=expr),
                        condition=condition_input,
                        else_condition=else_condition_input,
                    )
                case aligner_builtins.MaximizeBuiltin():
                    collector.add_objective(
                        Objective(sense=ObjectiveSense.MAXIMIZE, expr=inner, source_expr=expr),
                        condition=condition_input,
                        else_condition=else_condition_input,
                    )
                case _:
                    msg = "Unrecognized builtin call in spec expression."
                    raise SpecExtractionError(msg)

        case dfl.IfElse(test=test, then_=then_branch, else_=else_branch):
            cond_input = _has_candidates_input(test)
            if cond_input is None:
                msg = "if-else in spec position must have has_candidates(input) as condition."
                raise SpecExtractionError(msg)
            _extract_from_spec_expr(then_branch, collector, ctx, condition_input=cond_input)
            _extract_from_spec_expr(else_branch, collector, ctx, else_condition_input=cond_input)

        case _:
            msg = f"Cannot extract spec from expression type {type(expr).__name__}."
            raise SpecExtractionError(msg)


def extract_specs(aligner_node: Aligner) -> ExtractedSpecs:
    """Extract constraints and objectives from a type-checked aligner body.

    Uses two-pass extraction:

    - **Pass 1**: Collects ``assume()`` calls into a field-property mapping.
    - **Pass 2**: Processes ``require``/``minimize``/``maximize`` with field
      assumptions available for constraint classification and validation.

    This ordering is necessary because ``require(a.f == b.f)`` classification
    depends on whether both fields have ``is_strictly_increasing`` assumptions.

    Prerequisites:
        ``type_check_aligner()`` must have been called first.
        Body expressions must have been expanded via ``expand_all_calls()``.

    Args:
        aligner_node: The parsed and type-checked ``Aligner`` IR node.

    Returns:
        An ``ExtractedSpecs`` containing all normalized constraints, equalities,
        objectives, and field assumptions.

    Raises:
        SpecExtractionError: If a body statement cannot be normalized.
    """
    collector = _SpecCollector()
    inputs = aligner_node.inputs
    registry = get_trait_registry(aligner_node.module)
    sub_trait = dfl_traits.get_entities(aligner_node.module.context).sub_trait
    ctx = _TypeContext(inputs=inputs, registry=registry, sub_trait=sub_trait)

    for stmt in aligner_node.body_stmts:
        match stmt:
            case AlignerSpecStmt(expanded_expr=expanded) if expanded is not None:
                _collect_assumptions_from_spec_expr(expanded, collector)
            case AlignerSpecStmt():
                msg = "AlignerSpecStmt has no expanded_expr — was type_check_aligner() called?"
                raise SpecExtractionError(msg)
            case AlignerLetBinding() | dfl.FnDef():
                pass

    for stmt in aligner_node.body_stmts:
        match stmt:
            case AlignerSpecStmt(expanded_expr=expanded) if expanded is not None:
                _extract_from_spec_expr(expanded, collector, ctx)
            case AlignerLetBinding() | dfl.FnDef() | AlignerSpecStmt():
                pass

    _add_batch_constraints(aligner_node, collector)

    return collector.to_extracted_specs()
