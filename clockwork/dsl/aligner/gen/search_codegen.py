# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate C++ search code for aligner cogs."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from typing import TYPE_CHECKING, Final, Literal

from clockwork.dsl.aligner.direction_analysis import Direction
from clockwork.dsl.aligner.extract_specs import (
    DifferenceConstraint,
    EqualityConstraint,
    FieldAccessor,
    FirstInBatch,
    IndexInView,
    InputSelector,
    LastInBatch,
    Objective,
    ObjectiveSense,
    base_input_name,
    try_field_accessor,
)
from clockwork.dsl.aligner.objective_analysis import (
    SearchType,
    collect_field_accessors,
    decompose_additive,
    referenced_base_inputs,
)
from clockwork.dsl.aligner.stn import bound_lt, negate_bound
from clockwork.dsl.cpp import typereg
from clockwork.dsl.cpp.context import CppModuleChunks, Header, SystemHeader
from clockwork.dsl.cpp.literal import int_to_cpp
from clockwork.dsl.cpp.types import NANOSECONDS, CppValue, render_value_expr
from clockwork.dsl.ir import clkbuiltins, dfl, primitive, typesys
from clockwork.dsl.ir.module_id import JEWELS_REPO
from clockwork.dsl.ir.units import SECONDS, TimeUnit

_linear_field_narrowing_max_span: Final = 8

if TYPE_CHECKING:
    from collections.abc import Callable

    from clockwork.dsl.aligner.direction_analysis import CutoffExpr
    from clockwork.dsl.aligner.extract_specs import BoundValue
    from clockwork.dsl.aligner.gen.codegen_plan import CodegenLevel, CodegenPlan, Constraint
    from clockwork.dsl.aligner.join_plan import FeasibleWindow
    from clockwork.dsl.aligner.partition_types import PartitionPoint
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.aligner import ResolvedAligner
    from clockwork.dsl.ir.cpp_executable import CppCog
    from clockwork.dsl.ir.cpp_target import CppTarget


@dataclass(frozen=True)
class _OptionalContext:
    """Parameters controlling optional-input codegen at a given level."""

    input_name: str
    is_optional: bool
    total_optionals: int
    use_bsf: bool
    req_tighten: bool
    opt_tighten: bool
    has_downstream_obj_refs: bool
    absent_members: tuple[str, ...]
    required_present_members: tuple[str, ...]


@dataclass(frozen=True)
class _ConstraintSides:
    """Result of classifying which side of a constraint belongs to the current level."""

    bound_input: InputSelector
    bound_field: str | IndexInView
    current_input: InputSelector
    current_field: str | IndexInView
    is_equality: bool


@dataclass(frozen=True)
class _IndexRangeWindow:
    """Iterator-index range relative to an already-bound candidate member."""

    reference_input: InputSelector
    lo: BoundValue | None
    hi: BoundValue | None
    reference_optional: bool


@dataclass(frozen=True)
class _RemainingConstraintChecks:
    """Constraint checks that are not already guaranteed by generated windowing."""

    unconditional: tuple[Constraint, ...]
    conditional_present: dict[str, tuple[Constraint, ...]]
    conditional_absent: dict[str, tuple[Constraint, ...]]


@dataclass(frozen=True)
class _DominatingObjectivePrune:
    """A monotone objective that allows tie-aware search pruning."""

    objective_index: int


@dataclass(frozen=True)
class _SearchFunctionContext:
    """Codegen context for one generated search function."""

    function_name: str
    level_index: int
    next_function_name: str | None
    component_level_indices: tuple[int, ...]
    suffix_level_indices: tuple[int, ...]
    bound_level_indices: tuple[int, ...]
    partition_id: int | None = None
    component_id: int | None = None

    @property
    def is_component(self) -> bool:
        """Return whether this function belongs to a partition component."""
        return self.partition_id is not None


@dataclass(frozen=True)
class _ScopeSearchContextBuilder:
    """Mutable context for collecting generated search functions by scope."""

    plan: CodegenPlan
    contexts: list[_SearchFunctionContext]
    visited_scopes: set[tuple[int, ...]]


@dataclass(frozen=True)
class _GeneratedSearchArtifacts:
    """Derived search-codegen artifacts shared by top-level emitters."""

    optional_input_names: list[str]
    search_contexts: tuple[_SearchFunctionContext, ...]
    partitions: tuple[PartitionPoint, ...]
    optional_contexts: dict[str, _OptionalContext]
    flat_best_contexts: tuple[_SearchFunctionContext, ...]
    best_level_indices: frozenset[int]
    component_best_contexts: tuple[_SearchFunctionContext, ...]
    suffix_optional_presence_level_indices: frozenset[int]
    component_optional_presence_contexts: tuple[_SearchFunctionContext, ...]
    used_objective_indices: frozenset[int]


def _to_pascal(snake: str) -> str:
    """Convert ``snake_case`` to ``PascalCase``."""
    return "".join(part.title() for part in snake.split("_"))


def _input_getter(input_name: str) -> str:
    return f"get_{input_name}()"


def _input_view_member_name(input_name: str) -> str:
    return f"{input_name}_view"


def _field_getter(field_name: str) -> str:
    parts = field_name.split(".")
    return ".".join(f"get_{p}()" for p in parts)


def _iter_type_name(input_name: str) -> str:
    return f"{_to_pascal(input_name)}It"


def _iter_member_name(selector: InputSelector) -> str:
    """Map an ``InputSelector`` to its ``CandidateAlignment`` member name."""
    match selector:
        case str() as name:
            return f"{name}_it"
        case FirstInBatch(input_name=name):
            return f"{name}_lo_it"
        case LastInBatch(input_name=name):
            return f"{name}_hi_it"


def _candidate_suffix_type_name(level_index: int) -> str:
    """Return the generated suffix struct type name for a search level."""
    return f"CandidateSuffix{level_index}"


def _best_suffix_type_name(level_index: int) -> str:
    """Return the generated best-so-far suffix struct type name for a search level."""
    return f"BestSuffix{level_index}"


def _component_context_suffix(context: _SearchFunctionContext) -> str:
    """Return a stable suffix for component-local helper names."""
    assert context.partition_id is not None
    assert context.component_id is not None
    return f"P{context.partition_id}C{context.component_id}L{context.level_index}"


def _component_best_type_name(context: _SearchFunctionContext) -> str:
    """Return a generated component-local best snapshot type name."""
    return f"BestComponent{_component_context_suffix(context)}"


def _best_type_name(context: _SearchFunctionContext) -> str:
    """Return the best snapshot type name for a search context."""
    if context.is_component:
        return _component_best_type_name(context)
    return _best_suffix_type_name(context.level_index)


def _store_best_function_name(context: _SearchFunctionContext) -> str:
    """Return the best-snapshot store helper name for a search context."""
    if context.is_component:
        return f"store_best_component_{context.partition_id}_{context.component_id}_{context.level_index}"
    return _store_best_suffix_function_name(context.level_index)


def _restore_best_function_name(context: _SearchFunctionContext) -> str:
    """Return the best-snapshot restore helper name for a search context."""
    if context.is_component:
        return f"restore_best_component_{context.partition_id}_{context.component_id}_{context.level_index}"
    return _restore_best_suffix_function_name(context.level_index)


def _best_comparator_function_name(context: _SearchFunctionContext) -> str:
    """Return the best-snapshot comparator helper name for a search context."""
    if context.is_component:
        return f"is_better_than_best_component_{context.partition_id}_{context.component_id}_{context.level_index}"
    return _best_suffix_comparator_function_name(context.level_index)


def _candidate_member_function_name(member_name: str) -> str:
    """Return the generated candidate member accessor function name."""
    return f"candidate_{member_name}"


def _candidate_member_call(member_name: str, candidate_expr: str) -> str:
    """Return a generated C++ accessor call for a candidate member."""
    return f"{_candidate_member_function_name(member_name)}({candidate_expr})"


def _member_level_index_map(plan: CodegenPlan) -> dict[str, int]:
    """Map generated candidate member names to their owning search level index."""
    return {_iter_member_name(level.join_level.input): level.level_index for level in plan.levels}


def _level_suffix_path(level_index: int) -> str:
    """Return ``suffix0.suffix1...suffixN`` for a level index."""
    return ".".join(f"suffix{index}" for index in range(level_index + 1))


def _candidate_suffix_expr(level_index: int, candidate_expr: str) -> str:
    """Return the C++ expression for a candidate's suffix at ``level_index``."""
    return f"{candidate_expr}.{_level_suffix_path(level_index)}"


def _candidate_member_expr(member_name: str, candidate_expr: str, member_levels: dict[str, int]) -> str:
    """Return the C++ expression for a candidate member."""
    level_index = member_levels[member_name]
    return f"{_candidate_suffix_expr(level_index, candidate_expr)}.{member_name}"


def _best_suffix_member_expr(member_name: str, best_level_index: int, member_levels: dict[str, int]) -> str:
    """Return the C++ expression for a member stored in a best suffix."""
    member_level_index = member_levels[member_name]
    assert member_level_index >= best_level_index
    suffix_path = ".".join(f"suffix{index}" for index in range(best_level_index + 1, member_level_index + 1))
    base = "best.suffix"
    suffix_expr = f"{base}.{suffix_path}" if suffix_path else base
    return f"{suffix_expr}.{member_name}"


def _candidate_optional_presence_expr(
    input_name: str,
    batch_input_names: frozenset[str],
    candidate_expr: str,
) -> str:
    """Return the C++ expression for an optional input being present on a candidate."""
    if input_name in batch_input_names:
        lo_expr = _candidate_member_call(f"{input_name}_lo_it", candidate_expr)
        hi_expr = _candidate_member_call(f"{input_name}_hi_it", candidate_expr)
        return f"{lo_expr}.has_value() && {hi_expr}.has_value()"
    return f"{_candidate_member_call(f'{input_name}_it', candidate_expr)}.has_value()"


def _candidate_optional_absence_expr(
    input_name: str,
    batch_input_names: frozenset[str],
    candidate_expr: str,
) -> str:
    """Return the C++ expression for an optional input being absent on a candidate."""
    if input_name not in batch_input_names:
        return f"!{_candidate_member_call(f'{input_name}_it', candidate_expr)}.has_value()"
    return f"!({_candidate_optional_presence_expr(input_name, batch_input_names, candidate_expr)})"


def _candidate_optional_member_presence_expr(
    accessor_input: InputSelector,
    optional_input_names: frozenset[str],
    candidate_expr: str,
) -> str | None:
    """Return the presence guard for an optional candidate iterator member."""
    if base_input_name(accessor_input) not in optional_input_names:
        return None
    return f"{_candidate_member_call(_iter_member_name(accessor_input), candidate_expr)}.has_value()"


def _optional_present_bit_name(input_name: str) -> str:
    """Return the cached presence bit constant name for an optional input."""
    return f"{input_name}_present_bit"


def _optional_cached_presence_expr(input_name: str, presence_expr: str) -> str:
    """Return the C++ expression for cached optional-input presence."""
    return f"{presence_expr}.has_{input_name}()"


def _optional_cached_absence_expr(input_name: str, presence_expr: str) -> str:
    """Return the C++ expression for cached optional-input absence."""
    return f"!{_optional_cached_presence_expr(input_name, presence_expr)}"


def _optional_members_for_input(
    input_name: str,
    batch_input_names: frozenset[str],
) -> tuple[str, ...]:
    """Return the ``CandidateAlignment`` members representing an optional input."""
    if input_name in batch_input_names:
        return (f"{input_name}_lo_it", f"{input_name}_hi_it")
    return (f"{input_name}_it",)


def _optional_constraint_present_guard(
    input_name: str,
    current_input: InputSelector,
    batch_input_names: frozenset[str],
) -> str:
    """Return the guard for constraints active when an optional is present."""
    if input_name not in batch_input_names:
        return f"{_candidate_member_call(f'{input_name}_it', 'candidate')}.has_value()"

    current_base = base_input_name(current_input)
    if current_base != input_name:
        return _candidate_optional_presence_expr(input_name, batch_input_names, "candidate")

    current_member = _iter_member_name(current_input)
    required_members = tuple(
        member for member in _optional_members_for_input(input_name, batch_input_names) if member != current_member
    )
    if not required_members:
        return "true"
    return " && ".join(f"{_candidate_member_call(member, 'candidate')}.has_value()" for member in required_members)


def _bound_to_cpp_literal(
    bound: primitive.UnitValue | primitive.DecimalValue,
) -> str:
    """Convert a constraint bound to a C++ literal expression."""
    match bound:
        case primitive.UnitValue(unit=unit) if isinstance(unit, TimeUnit):
            return _time_bound_to_ns_literal(bound)
        case primitive.DecimalValue():
            return _decimal_bound_to_cpp(bound)
        case primitive.UnitValue():
            msg = f"Unsupported unit type in constraint bound: {bound.unit}"
            raise NotImplementedError(msg)


def _time_bound_to_ns_literal(bound: primitive.UnitValue) -> str:
    """Convert a time-unit bound to a ``std::chrono::nanoseconds`` literal."""
    seconds = bound.as_unit(SECONDS).value
    ns = seconds * Decimal(10**9)
    if ns != ns.to_integral_value():
        msg = f"Constraint bound {bound.value}{bound.unit} cannot be exactly represented in nanoseconds"
        raise ValueError(msg)
    int_expr = int_to_cpp(int(ns), clkbuiltins.INT64)
    duration_val = CppValue(value_type=NANOSECONDS, value=int_expr)
    return duration_val.render("")


def _decimal_bound_to_cpp(bound: primitive.DecimalValue) -> str:
    """Convert a decimal constraint bound to a C++ integer literal."""
    type_info = bound.type_info
    if isinstance(type_info, typesys.InferenceVar):
        type_info = type_info.resolution()

    if not isinstance(type_info, clkbuiltins.IntegerPrimitiveType):
        msg = f"Unsupported type for decimal constraint bound: {type_info}"
        raise TypeError(msg)
    return render_value_expr(int_to_cpp(int(bound.value), type_info), "")


def _offset_op_and_literal(
    literal: str,
    bound: BoundValue,
) -> tuple[str, str]:
    """Return ``("+", literal)`` or ``("-", abs_literal)`` for readability.

    Renders ``ref - 5`` instead of ``ref + -5`` for negative offsets.
    """
    if bound.value >= 0:
        return "+", literal
    match bound:
        case primitive.UnitValue():
            negated = primitive.UnitValue.make(value=-bound.value, unit=bound.unit)
        case primitive.DecimalValue():
            negated = primitive.DecimalValue(type_info=bound.type_info, value=-bound.value)
    return "-", _bound_to_cpp_literal(negated)


@dataclass(frozen=True)
class _FieldWindows:
    """Windows grouped by target field for windowing preamble generation."""

    field_name: str
    windows: tuple[FeasibleWindow, ...]
    is_time_field: bool


def _group_windows_by_field(
    windows: tuple[FeasibleWindow, ...],
) -> list[_FieldWindows]:
    """Group windows by target field, preserving order."""
    groups: dict[str, list[FeasibleWindow]] = {}
    for w in windows:
        assert isinstance(w.target_accessor.field_name, str)
        key = w.target_accessor.field_name
        groups.setdefault(key, []).append(w)

    result: list[_FieldWindows] = []
    for field_name, field_windows in groups.items():
        first_bound = next(
            (b for w in field_windows for b in (w.lo, w.hi) if b is not None),
            None,
        )
        is_time = (
            first_bound is not None
            and isinstance(first_bound, primitive.UnitValue)
            and isinstance(first_bound.unit, TimeUnit)
        )
        result.append(
            _FieldWindows(
                field_name=field_name,
                windows=tuple(field_windows),
                is_time_field=is_time,
            ),
        )
    return result


def _offset_expr(ref_expr: str, bound: BoundValue) -> str:
    """Render a C++ reference expression with an offset, omitting ``+ 0``."""
    if bound.value == Decimal(0):
        return ref_expr
    cpp_lit = _bound_to_cpp_literal(bound)
    op, operand = _offset_op_and_literal(cpp_lit, bound)
    return f"{ref_expr} {op} {operand}"


def _emit_sentinels(
    lines: list[str],
    fg: _FieldWindows,
    lo_var: str,
    hi_var: str,
    indent: str,
) -> tuple[bool, bool]:
    """Emit sentinel initializations for window bounds.

    Returns ``(has_lo, has_hi)`` indicating which bounds are present.
    """
    has_lo = any(w.lo is not None for w in fg.windows)
    has_hi = any(w.hi is not None for w in fg.windows)
    if not has_lo and not has_hi:
        return False, False
    getter = _field_getter(fg.field_name)
    lines.append(f"{indent}// Window: {fg.field_name}")
    if fg.is_time_field:
        if has_lo:
            lines.append(f"{indent}auto {lo_var} = jewels::time::SyncTime::min();")
        if has_hi:
            lines.append(f"{indent}auto {hi_var} = jewels::time::SyncTime::max();")
    else:
        type_expr = f"std::decay_t<decltype((*view.begin()).{getter})>"
        lines.append(
            f"{indent}static_assert(std::numeric_limits<{type_expr}>::is_specialized, "
            '"Aligner range-search field types must specialize std::numeric_limits");',
        )
        if has_lo:
            lines.append(f"{indent}auto {lo_var} = std::numeric_limits<{type_expr}>::lowest();")
        if has_hi:
            lines.append(f"{indent}auto {hi_var} = (std::numeric_limits<{type_expr}>::max)();")
    return has_lo, has_hi


def _emit_window_bounds_for_field(
    lines: list[str],
    fg: _FieldWindows,
    indent: str,
) -> tuple[str, str]:
    """Emit ``win_lo`` / ``win_hi`` computation for one target field.

    Returns the C++ variable names for the window bounds.
    """
    safe_name = fg.field_name.replace(".", "_")
    lo_var = f"win_lo_{safe_name}"
    hi_var = f"win_hi_{safe_name}"

    has_lo, has_hi = _emit_sentinels(lines, fg, lo_var, hi_var, indent)
    if not has_lo and not has_hi:
        return "", ""

    # Narrow from each window.
    for w in fg.windows:
        ref_expr = _accessor_cpp(
            w.reference_accessor.input_name,
            w.reference_accessor.field_name,
            via_candidate=True,
            inout=True,
        )

        if w.lo is not None:
            lines.append(f"{indent}{lo_var} = std::max({lo_var}, {_offset_expr(ref_expr, w.lo)});")
        if w.hi is not None:
            lines.append(f"{indent}{hi_var} = std::min({hi_var}, {_offset_expr(ref_expr, w.hi)});")
    return lo_var if has_lo else "", hi_var if has_hi else ""


def _emit_lower_bound_searches(
    lines: list[str],
    lo_vars: list[tuple[str, str]],
    it_start: str,
    indent: str,
) -> None:
    """Emit binary searches for lower bounds, intersecting across fields."""
    first = True
    for lo_var, field_name in lo_vars:
        getter = _field_getter(field_name)
        if first:
            lines.append(f"{indent}auto {it_start} = std::lower_bound(view.begin(), view.end(), {lo_var},")
            lines.append(f"{indent}    [](const auto& msg, const auto& t) {{")
            lines.append(f"{indent}      return msg.{getter} < t;")
            lines.append(f"{indent}    }});")
            first = False
        else:
            lines.append(f"{indent}{it_start} = std::max({it_start},")
            lines.append(f"{indent}    std::lower_bound(view.begin(), view.end(), {lo_var},")
            lines.append(f"{indent}    [](const auto& msg, const auto& t) {{")
            lines.append(f"{indent}      return msg.{getter} < t;")
            lines.append(f"{indent}    }}));")


def _emit_upper_bound_searches(
    lines: list[str],
    hi_vars: list[tuple[str, str]],
    it_start: str,
    it_end: str,
    indent: str,
) -> None:
    """Emit binary searches for upper bounds, intersecting across fields."""
    first = True
    for hi_var, field_name in hi_vars:
        getter = _field_getter(field_name)
        if first:
            lines.append(f"{indent}auto {it_end} = std::upper_bound({it_start}, view.end(), {hi_var},")
            lines.append(f"{indent}    [](const auto& t, const auto& msg) {{")
            lines.append(f"{indent}      return t < msg.{getter};")
            lines.append(f"{indent}    }});")
            first = False
        else:
            lines.append(f"{indent}{it_end} = std::min({it_end},")
            lines.append(f"{indent}    std::upper_bound({it_start}, view.end(), {hi_var},")
            lines.append(f"{indent}    [](const auto& t, const auto& msg) {{")
            lines.append(f"{indent}      return t < msg.{getter};")
            lines.append(f"{indent}    }}));")


def _emit_default_range(
    lines: list[str],
    it_start: str,
    it_end: str,
    indent: str,
) -> tuple[str, str]:
    """Emit ``it_start = view.begin(); it_end = view.end()``."""
    lines.append(f"{indent}auto {it_start} = view.begin();")
    lines.append(f"{indent}auto {it_end} = view.end();")
    lines.append("")
    return it_start, it_end


def _integral_index_bound(bound: BoundValue) -> int:
    """Return *bound* as an integer, or raise for invalid IndexInView bounds."""
    if bound.value != int(bound.value):
        msg = f"IndexInView bound must be integral, got {bound.value}"
        raise ValueError(msg)
    return int(bound.value)


def _index_ref_expr(window: _IndexRangeWindow) -> str:
    """Return the C++ expression for an index-window reference iterator."""
    ref_member = _iter_member_name(window.reference_input)
    ref_expr = _candidate_member_call(ref_member, "*candidate")
    if window.reference_optional:
        return f"{ref_expr}.value()"
    return ref_expr


def _index_offset_expr(window: _IndexRangeWindow, offset: int) -> str:
    """Return a safely-clamped C++ expression for a reference iterator plus *offset*."""
    ref_expr = _index_ref_expr(window)
    if offset == 0:
        return ref_expr
    return f"clamp_index_offset({ref_expr}, {offset})"


def _index_lo_expr(window: _IndexRangeWindow) -> str:
    """Return the C++ expression for the lower bound of an IndexInView window."""
    if window.lo is None:
        return "view.begin()"
    return _index_offset_expr(window, _integral_index_bound(window.lo))


def _index_hi_expr(window: _IndexRangeWindow) -> str:
    """Return the C++ expression for the upper bound (exclusive) of an IndexInView window."""
    if window.hi is None:
        return "view.end()"
    hi_offset = _integral_index_bound(window.hi) + 1  # exclusive end
    return _index_offset_expr(window, hi_offset)


def _index_range_window_from_feasible_window(window: FeasibleWindow) -> _IndexRangeWindow:
    """Convert an STN-derived feasible window into the local index-window representation."""
    return _IndexRangeWindow(
        reference_input=window.reference_accessor.input_name,
        lo=window.lo,
        hi=window.hi,
        reference_optional=False,
    )


def _index_zero_bound() -> BoundValue:
    """Return a unitless zero bound for an IndexInView equality."""
    return primitive.DecimalValue(type_info=clkbuiltins.INT64, value=Decimal(0))


def _constraint_index_range_window(
    constraint: Constraint,
    current_input: InputSelector,
    *,
    optional_input: str | None,
) -> _IndexRangeWindow | None:
    """Derive an iterator-index window from one current-level constraint."""
    sides = _classify_constraint_sides(constraint, current_input)
    if not isinstance(sides.bound_field, IndexInView) or not isinstance(sides.current_field, IndexInView):
        return None
    if sides.bound_input == current_input:
        return None
    if base_input_name(sides.bound_input) != base_input_name(current_input):
        return None

    reference_optional = optional_input is not None and base_input_name(sides.bound_input) == optional_input
    if sides.is_equality:
        zero = _index_zero_bound()
        return _IndexRangeWindow(
            reference_input=sides.bound_input,
            lo=zero,
            hi=zero,
            reference_optional=reference_optional,
        )

    assert isinstance(constraint, DifferenceConstraint)
    if constraint.minuend.input_name == current_input:
        return _IndexRangeWindow(
            reference_input=sides.bound_input,
            lo=None,
            hi=constraint.bound,
            reference_optional=reference_optional,
        )
    return _IndexRangeWindow(
        reference_input=sides.bound_input,
        lo=negate_bound(constraint.bound),
        hi=None,
        reference_optional=reference_optional,
    )


def _constraint_index_range_windows(
    constraints: tuple[Constraint, ...],
    current_input: InputSelector,
    *,
    optional_input: str | None,
) -> tuple[_IndexRangeWindow, ...]:
    """Derive index windows from constraints assigned to this level."""
    return tuple(
        window
        for constraint in constraints
        if (
            window := _constraint_index_range_window(
                constraint,
                current_input,
                optional_input=optional_input,
            )
        )
        is not None
    )


def _optional_present_index_window_constraints(
    level: CodegenLevel,
    opt: _OptionalContext,
) -> tuple[Constraint, ...]:
    """Return present-branch constraints safe to use for optional-batch windowing."""
    if not opt.is_optional or not opt.required_present_members:
        return ()
    branch = level.conditional_checks.get(opt.input_name)
    if branch is None:
        return ()
    return branch.when_present


def _merge_index_range_bound_lo(existing: BoundValue | None, new: BoundValue | None) -> BoundValue | None:
    """Intersect lower bounds by keeping the tighter lower bound."""
    if existing is None:
        return new
    if new is None:
        return existing
    return new if bound_lt(existing, new) else existing


def _merge_index_range_bound_hi(existing: BoundValue | None, new: BoundValue | None) -> BoundValue | None:
    """Intersect upper bounds by keeping the tighter upper bound."""
    if existing is None:
        return new
    if new is None:
        return existing
    return new if bound_lt(new, existing) else existing


def _merge_index_range_windows(windows: tuple[_IndexRangeWindow, ...]) -> tuple[_IndexRangeWindow, ...]:
    """Return index windows in first-seen order, intersected by reference iterator."""
    result: list[_IndexRangeWindow] = []
    for window in windows:
        existing_index = next(
            (
                index
                for index, existing in enumerate(result)
                if existing.reference_input == window.reference_input
                and existing.reference_optional == window.reference_optional
            ),
            None,
        )
        if existing_index is None:
            result.append(window)
            continue
        existing = result[existing_index]
        result[existing_index] = _IndexRangeWindow(
            reference_input=existing.reference_input,
            lo=_merge_index_range_bound_lo(existing.lo, window.lo),
            hi=_merge_index_range_bound_hi(existing.hi, window.hi),
            reference_optional=existing.reference_optional,
        )
    return tuple(result)


def _index_window_needs_offset_clamp(window: _IndexRangeWindow) -> bool:
    """Return whether *window* has a finite nonzero iterator offset."""
    lo_needs_clamp = window.lo is not None and _integral_index_bound(window.lo) != 0
    hi_needs_clamp = window.hi is not None and _integral_index_bound(window.hi) + 1 != 0
    return lo_needs_clamp or hi_needs_clamp


def _emit_index_offset_clamp_helper(lines: list[str], indent: str) -> None:
    """Emit a local helper that forms only in-range offset iterators."""
    lines.append(f"{indent}const auto clamp_index_offset = [&view](auto ref, auto offset) {{")
    lines.append(f"{indent}  const auto min_offset = -std::distance(view.begin(), ref);")
    lines.append(f"{indent}  const auto max_offset = std::distance(ref, view.end());")
    lines.append(f"{indent}  const auto bounded_offset = std::clamp(")
    lines.append(f"{indent}      static_cast<decltype(max_offset)>(offset), min_offset, max_offset);")
    lines.append(f"{indent}  return ref + bounded_offset;")
    lines.append(f"{indent}}};")


def _index_range_windows_for_level(level: CodegenLevel, opt: _OptionalContext) -> tuple[_IndexRangeWindow, ...]:
    """Return the merged index windows emitted for a search level."""
    derived_index_windows = _constraint_index_range_windows(
        (*level.unconditional_checks, *_optional_present_index_window_constraints(level, opt)),
        level.join_level.input,
        optional_input=opt.input_name if opt.is_optional else None,
    )
    initial_index_windows = tuple(
        _index_range_window_from_feasible_window(window)
        for window in level.initial_windows
        if isinstance(window.target_accessor.field_name, IndexInView)
    )
    return _merge_index_range_windows((*initial_index_windows, *derived_index_windows))


def _bounded_index_range_max_span(windows: tuple[_IndexRangeWindow, ...]) -> int | None:
    """Return the smallest compile-time max span implied by index windows."""
    spans: list[int] = []
    for window in windows:
        if window.lo is None or window.hi is None:
            continue
        spans.append(_integral_index_bound(window.hi) - _integral_index_bound(window.lo) + 1)
    if not spans:
        return None
    return min(spans)


def _emit_index_windowing(
    lines: list[str],
    windows: tuple[_IndexRangeWindow, ...],
    it_start: str,
    it_end: str,
    indent: str,
) -> None:
    """Emit iterator-arithmetic range for ``IndexInView`` windows.

    Translates ``lo <= (target_idx - ref_idx) <= hi`` into:

    - ``it_start = ref_it + lo``  (clamped into the view)
    - ``it_end   = ref_it + hi + 1``  (clamped into the view, exclusive)

    Multiple windows are intersected.
    """
    lines.append(f"{indent}// IndexInView window(s).")
    if any(_index_window_needs_offset_clamp(window) for window in windows):
        _emit_index_offset_clamp_helper(lines, indent)
    first = windows[0]
    lo_expr = _index_lo_expr(first)
    hi_expr = _index_hi_expr(first)

    lines.append(f"{indent}auto {it_start} = {lo_expr};")
    lines.append(f"{indent}auto {it_end} = {hi_expr};")

    # Intersect additional windows.
    for w in windows[1:]:
        if w.lo is not None:
            lines.append(f"{indent}{it_start} = std::max({it_start}, {_index_lo_expr(w)});")
        if w.hi is not None:
            lines.append(f"{indent}{it_end} = std::min({it_end}, {_index_hi_expr(w)});")
    lines.append("")


def _bound_le(left: BoundValue, right: BoundValue) -> bool:
    """Return whether ``left <= right`` for aligner bound values."""
    return not bound_lt(right, left)


def _bound_ge(left: BoundValue, right: BoundValue) -> bool:
    """Return whether ``left >= right`` for aligner bound values."""
    return not bound_lt(left, right)


def _index_window_covers_constraint(
    window: _IndexRangeWindow,
    constraint: Constraint,
    current_input: InputSelector,
    optional_input: str | None,
) -> bool:
    """Return whether one emitted index window proves one constraint."""
    required = _constraint_index_range_window(constraint, current_input, optional_input=optional_input)
    if required is None:
        return False
    if window.reference_input != required.reference_input or window.reference_optional != required.reference_optional:
        return False
    lo_covered = required.lo is None or (window.lo is not None and _bound_ge(window.lo, required.lo))
    hi_covered = required.hi is None or (window.hi is not None and _bound_le(window.hi, required.hi))
    return lo_covered and hi_covered


def _field_window_covers_constraint(
    window: FeasibleWindow,
    constraint: Constraint,
    current_input: InputSelector,
) -> bool:
    """Return whether one emitted field window proves one difference constraint."""
    if isinstance(constraint, EqualityConstraint):
        return False

    sides = _classify_constraint_sides(constraint, current_input)
    window_matches = (
        not isinstance(sides.bound_field, IndexInView)
        and not isinstance(sides.current_field, IndexInView)
        and window.target_accessor.input_name == sides.current_input
        and window.target_accessor.field_name == sides.current_field
        and window.reference_accessor.input_name == sides.bound_input
        and window.reference_accessor.field_name == sides.bound_field
    )
    if not window_matches:
        return False

    assert isinstance(constraint, DifferenceConstraint)
    if constraint.minuend.input_name == current_input:
        return window.hi is not None and _bound_le(window.hi, constraint.bound)
    required_lo = negate_bound(constraint.bound)
    return window.lo is not None and _bound_ge(window.lo, required_lo)


def _constraint_covered_by_windows(
    constraint: Constraint,
    level: CodegenLevel,
    opt: _OptionalContext,
) -> bool:
    """Return whether generated windowing already guarantees a constraint."""
    optional_input = opt.input_name if opt.is_optional else None
    index_windows = _index_range_windows_for_level(level, opt)
    if any(
        _index_window_covers_constraint(window, constraint, level.join_level.input, optional_input)
        for window in index_windows
    ):
        return True

    field_windows = tuple(
        window for window in level.initial_windows if not isinstance(window.target_accessor.field_name, IndexInView)
    )
    return any(_field_window_covers_constraint(window, constraint, level.join_level.input) for window in field_windows)


def _remaining_constraint_checks(level: CodegenLevel, opt: _OptionalContext) -> _RemainingConstraintChecks:
    """Return constraint checks that must still be emitted for a search level."""
    unconditional = tuple(
        constraint
        for constraint in level.unconditional_checks
        if not _constraint_covered_by_windows(constraint, level, opt)
    )
    conditional_present: dict[str, tuple[Constraint, ...]] = {}
    conditional_absent: dict[str, tuple[Constraint, ...]] = {}
    for opt_name, branch in sorted(level.conditional_checks.items()):
        present = tuple(
            constraint
            for constraint in branch.when_present
            if not _constraint_covered_by_windows(constraint, level, opt)
        )
        absent = branch.when_absent
        if present:
            conditional_present[opt_name] = present
        if absent:
            conditional_absent[opt_name] = absent
    return _RemainingConstraintChecks(
        unconditional=unconditional,
        conditional_present=conditional_present,
        conditional_absent=conditional_absent,
    )


def _has_remaining_constraint_checks(level: CodegenLevel, opt: _OptionalContext) -> bool:
    """Return whether a level still needs a generated constraint checker."""
    remaining = _remaining_constraint_checks(level, opt)
    return bool(remaining.unconditional or remaining.conditional_present or remaining.conditional_absent)


def _emit_linear_field_narrowing(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    *,
    lines: list[str],
    all_lo_vars: list[tuple[str, str]],
    all_hi_vars: list[tuple[str, str]],
    it_start: str,
    it_end: str,
    indent: str,
) -> None:
    """Emit linear field-value narrowing for a tiny existing iterator range."""
    lines.append("")
    lines.append(f"{indent}// Linear field narrowing for tiny IndexInView range.")
    for lo_var, field_name in all_lo_vars:
        getter = _field_getter(field_name)
        lines.append(f"{indent}while ({it_start} < {it_end} && (*{it_start}).{getter} < {lo_var}) {{")
        lines.append(f"{indent}  ++{it_start};")
        lines.append(f"{indent}}}")
    for hi_var, field_name in all_hi_vars:
        getter = _field_getter(field_name)
        lines.append(f"{indent}while ({it_start} < {it_end} && {hi_var} < (*std::prev({it_end})).{getter}) {{")
        lines.append(f"{indent}  --{it_end};")
        lines.append(f"{indent}}}")


def _emit_field_narrowing(  # noqa: PLR0913 # Too many args mitigated by kwonly args
    *,
    lines: list[str],
    all_lo_vars: list[tuple[str, str]],
    all_hi_vars: list[tuple[str, str]],
    it_start: str,
    it_end: str,
    indent: str,
    into_existing: bool,
    use_linear: bool,
) -> None:
    """Emit field-value window narrowing.

    When ``into_existing`` is True, ``it_start/it_end`` are already declared
    (from IndexInView windowing) and we narrow with ``std::max/min``.
    Otherwise we declare them fresh via lower/upper bound helpers.
    """
    if use_linear:
        _emit_linear_field_narrowing(
            lines=lines,
            all_lo_vars=all_lo_vars,
            all_hi_vars=all_hi_vars,
            it_start=it_start,
            it_end=it_end,
            indent=indent,
        )
        return

    lines.append("")
    if into_existing:
        for lo_var, field_name in all_lo_vars:
            getter = _field_getter(field_name)
            lines.append(f"{indent}{it_start} = std::max({it_start},")
            lines.append(f"{indent}    std::lower_bound({it_start}, {it_end}, {lo_var},")
            lines.append(f"{indent}    [](const auto& msg, const auto& t) {{")
            lines.append(f"{indent}      return msg.{getter} < t;")
            lines.append(f"{indent}    }}));")
        for hi_var, field_name in all_hi_vars:
            getter = _field_getter(field_name)
            lines.append(f"{indent}{it_end} = std::min({it_end},")
            lines.append(f"{indent}    std::upper_bound({it_start}, {it_end}, {hi_var},")
            lines.append(f"{indent}    [](const auto& t, const auto& msg) {{")
            lines.append(f"{indent}      return t < msg.{getter};")
            lines.append(f"{indent}    }}));")
    else:
        if all_lo_vars:
            _emit_lower_bound_searches(lines, all_lo_vars, it_start, indent)
        else:
            lines.append(f"{indent}auto {it_start} = view.begin();")
        if all_hi_vars:
            _emit_upper_bound_searches(lines, all_hi_vars, it_start, it_end, indent)
        else:
            lines.append(f"{indent}auto {it_end} = view.end();")


def _emit_windowing_preamble(
    lines: list[str],
    level: CodegenLevel,
    opt: _OptionalContext,
    indent: str = "  ",
) -> tuple[str, str, bool]:
    """Emit the iteration windowing preamble: window computation + binary search.

    Returns ``(it_start, it_end, has_empty_range_guard)``.  When no
    windows apply, declares ``it_start`` / ``it_end`` as
    ``view.begin()`` / ``view.end()`` and reports that no guard was
    emitted.

    Args:
        lines: Output list of C++ source lines.
        level: The codegen level being emitted.
        opt: Optional-input context for the level.
        indent: Whitespace prefix for generated lines.
    """
    it_start = "it_start"
    it_end = "it_end"
    index_windows = _index_range_windows_for_level(level, opt)
    field_windows = tuple(w for w in level.initial_windows if not isinstance(w.target_accessor.field_name, IndexInView))

    if not field_windows and not index_windows:
        _emit_default_range(lines, it_start, it_end, indent)
        return it_start, it_end, False

    # Separate IndexInView windows (batch position constraints) from
    # field-value windows (regular binary-search constraints).
    # We narrow to index windows first because they're O(1), then fields narrow within that range with O(log N).
    has_index_range = bool(index_windows)
    if has_index_range:
        _emit_index_windowing(lines, index_windows, it_start, it_end, indent)

    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    field_groups = _group_windows_by_field(field_windows) if field_windows else []

    if not field_groups and not has_index_range:
        _emit_default_range(lines, it_start, it_end, indent)
        return it_start, it_end, False

    all_lo_vars: list[tuple[str, str]] = []
    all_hi_vars: list[tuple[str, str]] = []

    for fg in field_groups:
        lo_var, hi_var = _emit_window_bounds_for_field(lines, fg, indent)
        if lo_var:
            all_lo_vars.append((lo_var, fg.field_name))
        if hi_var:
            all_hi_vars.append((hi_var, fg.field_name))

    if all_lo_vars or all_hi_vars:
        bounded_index_span = _bounded_index_range_max_span(index_windows)
        use_linear_field_narrowing = (
            has_index_range
            and bounded_index_span is not None
            and bounded_index_span <= _linear_field_narrowing_max_span
        )
        _emit_field_narrowing(
            lines=lines,
            all_lo_vars=all_lo_vars,
            all_hi_vars=all_hi_vars,
            it_start=it_start,
            it_end=it_end,
            indent=indent,
            into_existing=has_index_range,
            use_linear=use_linear_field_narrowing,
        )
    elif not has_index_range:
        _emit_default_range(lines, it_start, it_end, indent)
        return it_start, it_end, False

    # For required levels, an empty narrowed range means no valid
    # candidates exist — return failure immediately.
    # For optional levels, skip the guard: the scan generators handle
    # empty views safely and will fall through to the absent path.
    if not opt.is_optional:
        lines.append("")
        lines.append(f"{indent}if ({it_start} >= {it_end}) {{")
        lines.append(f"{indent}  return jewels::failure;")
        lines.append(f"{indent}}}")
    lines.append("")

    return it_start, it_end, not opt.is_optional


def _accessor_cpp(
    accessor_input: InputSelector,
    field_name: str | IndexInView,
    *,
    via_candidate: bool,
    inout: bool = False,
    optional: bool = False,
) -> str:
    """Produce the C++ expression to access a field value.

    Args:
        accessor_input: The input selector (plain name or batch boundary).
        field_name: The field name or ``IndexInView`` sentinel.
        via_candidate: Whether to access via the ``candidate`` struct
            (True) or the local ``candidate_it`` variable (False).
        inout: Whether ``candidate`` is a ``jewels::InOut`` parameter
            (uses ``->`` instead of ``.``).
        optional: Whether the iterator is ``std::optional`` (adds
            ``.value()`` to unwrap).
    """
    if via_candidate:
        member = _iter_member_name(accessor_input)
        it_suffix = ".value()" if optional else ""
        candidate_expr = "*candidate" if inout else "candidate"
        it_expr = f"{_candidate_member_call(member, candidate_expr)}{it_suffix}"
        if isinstance(field_name, IndexInView):
            return it_expr
        return f"(*{it_expr}).{_field_getter(field_name)}"
    if isinstance(field_name, IndexInView):
        return "candidate_it"
    return f"(*candidate_it).{_field_getter(field_name)}"


def _input_view_source_expr(input_name: str, reuse: bool, receiver: str = "inputs") -> str:
    """Generate the C++ expression to construct an input view."""
    getter = _input_getter(input_name)
    if reuse:
        return f"{receiver}.{getter}.get_view()"
    return f"{receiver}.{getter}.get_cursor_view()"


def _view_expr(input_name: str) -> str:
    """Generate the C++ expression to access a cached input view."""
    return f"views.{_input_view_member_name(input_name)}"


def _cutoff_view_var(later_name: str) -> str:
    """Variable name for a pre-declared cutoff view."""
    return f"cutoff_view_{later_name}"


def _collect_cutoff_views(
    *cutoff_tuples: tuple[CutoffExpr, ...],
) -> dict[str, None]:
    """Collect distinct later-input base names across one or more cutoff tuples.

    Returns an insertion-ordered dict (keys only) for deterministic codegen.
    """
    views: dict[str, None] = {}
    for cutoffs in cutoff_tuples:
        for cutoff in cutoffs:
            name = base_input_name(cutoff.later_accessor.input_name)
            views[name] = None
    return views


def _emit_cutoff_view_decls(
    lines: list[str],
    cutoff_view_names: dict[str, None],
    indent: str,
) -> None:
    """Emit ``const auto&`` declarations for cutoff views before the scan loop.

    Cutoff expressions share the same cached input views as the current
    scan level.  Local aliases keep the generated constraint code readable.
    """
    for name in cutoff_view_names:
        var = _cutoff_view_var(name)
        lines.append(f"{indent}const auto& {var} = {_view_expr(name)};")


def _emit_right_cutoffs(
    lines: list[str],
    cutoffs: tuple[CutoffExpr, ...],
    indent: str,
    *,
    iterator_var: str = "candidate_it",
    action: str = "break",
) -> None:
    """Emit right-direction cutoff checks.

    A right cutoff fires when the current iterator's field value exceeds
    the last element of the later view plus the constraint bound.
    Used in forward scans where the scan direction is forward
    and the cutoff prunes the tail of the search space.
    """
    for cutoff in cutoffs:
        later_name = base_input_name(cutoff.later_accessor.input_name)
        later_field = cutoff.later_accessor.field_name
        assert isinstance(later_field, str)
        view_var = _cutoff_view_var(later_name)

        later_getter = _field_getter(later_field)
        current_getter = _field_getter(cutoff.current_field)

        lines.append(f"{indent}// Cutoff: no later {later_name} element can satisfy constraint")
        if cutoff.bound.value == Decimal(0):
            lines.append(f"{indent}if ({view_var}.begin() != {view_var}.end()")
            lines.append(
                f"{indent}    && (*{iterator_var}).{current_getter} > (*std::prev({view_var}.end())).{later_getter})"
            )
        else:
            lit = _bound_to_cpp_literal(cutoff.bound)
            lines.append(f"{indent}if ({view_var}.begin() != {view_var}.end()")
            rhs = f"(*std::prev({view_var}.end())).{later_getter}"
            lines.append(f"{indent}    && (*{iterator_var}).{current_getter} - {rhs} > {lit})")
        lines.append(f"{indent}{{")
        lines.append(f"{indent}  {action};")
        lines.append(f"{indent}}}")


def _emit_left_cutoffs(
    lines: list[str],
    cutoffs: tuple[CutoffExpr, ...],
    indent: str,
    *,
    iterator_var: str = "candidate_it",
    action: str = "break",
) -> None:
    """Emit left-direction cutoff checks.

    A left cutoff fires when the current iterator's field value falls
    below the first element of the later view minus the constraint bound.
    Used in reverse scans where the scan direction is backward
    and the cutoff prunes the tail of the search space.
    """
    for cutoff in cutoffs:
        later_name = base_input_name(cutoff.later_accessor.input_name)
        later_field = cutoff.later_accessor.field_name
        assert isinstance(later_field, str)
        view_var = _cutoff_view_var(later_name)

        later_getter = _field_getter(later_field)
        current_getter = _field_getter(cutoff.current_field)

        lines.append(f"{indent}// Cutoff: no later {later_name} element can satisfy constraint")
        if cutoff.bound.value == Decimal(0):
            lines.append(f"{indent}if ({view_var}.begin() != {view_var}.end()")
            lines.append(f"{indent}    && (*{iterator_var}).{current_getter} < (*{view_var}.begin()).{later_getter})")
        else:
            lit = _bound_to_cpp_literal(cutoff.bound)
            lines.append(f"{indent}if ({view_var}.begin() != {view_var}.end()")
            lines.append(
                f"{indent}    && (*{view_var}.begin()).{later_getter} - (*{iterator_var}).{current_getter} > {lit})"
            )
        lines.append(f"{indent}{{")
        lines.append(f"{indent}  {action};")
        lines.append(f"{indent}}}")


def _optional_cutoff_flag_name(
    later_input: str,
    direction: Literal["right", "left"] | None = None,
) -> str:
    """Generate the C++ variable name for an optional cutoff feasibility flag.

    Args:
        later_input: Base name of the later-level optional input.
        direction: ``"right"`` or ``"left"`` for NEAREST per-direction
            flags.  ``None`` for forward/reverse (single-direction).
    """
    if direction is not None:
        return f"{later_input}_{direction}_feasible"
    return f"{later_input}_feasible"


def _collect_optional_cutoff_inputs(
    cutoffs: tuple[CutoffExpr, ...],
) -> list[str]:
    """Get deduplicated list of later-input names referenced by optional cutoffs.

    Preserves insertion order for deterministic codegen.
    """
    seen: dict[str, None] = {}
    for cutoff in cutoffs:
        name = base_input_name(cutoff.later_accessor.input_name)
        seen[name] = None
    return list(seen)


def _emit_optional_cutoff_flags(
    lines: list[str],
    optional_cutoffs: tuple[CutoffExpr, ...],
    indent: str,
    *,
    direction: Literal["right", "left"] | None = None,
) -> list[str]:
    """Declare optional cutoff feasibility flag variables.

    Returns the list of flag variable names declared.
    """
    inputs = _collect_optional_cutoff_inputs(optional_cutoffs)
    flag_names: list[str] = []
    for inp in inputs:
        flag = _optional_cutoff_flag_name(inp, direction)
        lines.append(f"{indent}bool {flag} = true;")
        flag_names.append(flag)
    return flag_names


def _emit_optional_cutoff_checks(  # noqa: PLR0913 # Many parameters mitigated by kwonly args
    lines: list[str],
    cutoffs: tuple[CutoffExpr, ...],
    indent: str,
    *,
    direction: Literal["right", "left"],
    iterator_var: str = "it",
    flag_direction: Literal["right", "left"] | None = None,
) -> None:
    """Emit optional cutoff flag-setting checks inside the scan loop.

    Each cutoff check is guarded by the feasibility flag. When the
    cutoff fires, the flag is set to ``false``.

    Args:
        lines: Output list of C++ source lines.
        cutoffs: Cutoff expressions to emit checks for.
        indent: Whitespace prefix for generated lines.
        direction: ``"right"`` or ``"left"`` — determines comparison
            direction (right: current > last, left: current < first).
        iterator_var: C++ variable holding the current iterator.
        flag_direction: Direction suffix for the flag name (``None``
            for forward/reverse, ``"right"``/``"left"`` for NEAREST).
    """
    for cutoff in cutoffs:
        later_name = base_input_name(cutoff.later_accessor.input_name)
        later_field = cutoff.later_accessor.field_name
        assert isinstance(later_field, str)
        view_var = _cutoff_view_var(later_name)

        later_getter = _field_getter(later_field)
        current_getter = _field_getter(cutoff.current_field)
        flag = _optional_cutoff_flag_name(later_name, flag_direction)

        lines.append(f"{indent}if ({flag}) {{")
        if direction == "right":
            if cutoff.bound.value == Decimal(0):
                lines.append(f"{indent}  if ({view_var}.begin() != {view_var}.end()")
                right_rhs = f"(*std::prev({view_var}.end())).{later_getter}"
                lines.append(f"{indent}      && (*{iterator_var}).{current_getter} > {right_rhs})")
            else:
                lit = _bound_to_cpp_literal(cutoff.bound)
                lines.append(f"{indent}  if ({view_var}.begin() != {view_var}.end()")
                rhs = f"(*std::prev({view_var}.end())).{later_getter}"
                lines.append(f"{indent}      && (*{iterator_var}).{current_getter} - {rhs} > {lit})")
        elif cutoff.bound.value == Decimal(0):
            lines.append(f"{indent}  if ({view_var}.begin() != {view_var}.end()")
            lines.append(f"{indent}      && (*{iterator_var}).{current_getter} < (*{view_var}.begin()).{later_getter})")
        else:
            lit = _bound_to_cpp_literal(cutoff.bound)
            lines.append(f"{indent}  if ({view_var}.begin() != {view_var}.end()")
            lines.append(
                f"{indent}      && (*{view_var}.begin()).{later_getter} - (*{iterator_var}).{current_getter} > {lit})"
            )
        lines.append(f"{indent}  {{")
        lines.append(f"{indent}    {flag} = false;")
        lines.append(f"{indent}  }}")
        lines.append(f"{indent}}}")


def _emit_best_update(
    lines: list[str],
    indent: str,
    *,
    context: _SearchFunctionContext,
    break_on_max_optionals: bool,
    break_on_full_expr: str,
) -> None:
    """Emit code to initialize or replace the best candidate."""
    store_fn = _store_best_function_name(context)
    comparator_fn = _best_comparator_function_name(context)
    lines.append(f"{indent}if (!best.has_value()) {{")
    lines.append(f"{indent}  best.emplace();")
    lines.append(f"{indent}  {store_fn}(*candidate, *best);")
    lines.append(f"{indent}}} else if ({comparator_fn}(*candidate, *best)) {{")
    lines.append(f"{indent}  {store_fn}(*candidate, *best);")
    lines.append(f"{indent}}}")
    if break_on_max_optionals:
        lines.append(f"{indent}if ({break_on_full_expr}) {{ break; }}")


def _emit_dominating_objective_prune(
    lines: list[str],
    plan: CodegenPlan,
    context: _SearchFunctionContext,
    prune: _DominatingObjectivePrune | None,
    indent: str,
) -> None:
    """Emit tie-aware pruning for a monotone current-level objective."""
    if prune is None:
        return

    member_levels = _member_level_index_map(plan)
    level_index = context.level_index

    def best_member_expr(member_name: str) -> str:
        member_level_index = member_levels[member_name]
        if member_level_index < level_index:
            return _candidate_member_call(member_name, "*candidate")
        return _best_suffix_member_expr(member_name, level_index, member_levels).replace("best.", "best->", 1)

    obj = plan.objectives[prune.objective_index]
    active_fn = _objective_active_function_name(prune.objective_index)
    value_fn = _objective_value_function_name(prune.objective_index)
    suffix_full_expr = _optionals_present_expr(plan, context, "best->presence")
    best_value = _render_objective_value(
        obj=obj,
        optional_input_names=plan.optional_input_names,
        time_fields=plan.time_fields,
        member_expr=best_member_expr,
    )

    lines.append(f"{indent}if (best.has_value()")
    if suffix_full_expr != "true":
        lines.append(f"{indent}    && {suffix_full_expr}")
    lines.append(f"{indent}    && {active_fn}((*candidate).presence)")
    lines.append(f"{indent}    && {active_fn}(best->presence)) {{")
    lines.append(f"{indent}  const auto my_prune_obj = {value_fn}(*candidate);")
    lines.append(f"{indent}  const auto best_prune_obj = {best_value};")
    lines.append(f"{indent}  if (my_prune_obj != best_prune_obj) {{")
    lines.append(f"{indent}    break;")
    lines.append(f"{indent}  }}")
    lines.append(f"{indent}}}")


def _emit_optional_cutoff_break(
    lines: list[str],
    flag_names: list[str],
    indent: str,
) -> None:
    """Emit break when all optional flags are false and a match is in hand.

    Only emitted when ``flag_names`` is non-empty.
    """
    if not flag_names:
        return
    negated = " && ".join(f"!{f}" for f in flag_names)
    lines.append(f"{indent}if ({negated} && best.has_value()) {{")
    lines.append(f"{indent}  break;")
    lines.append(f"{indent}}}")


def _validate_plan(plan: CodegenPlan) -> None:
    """Validate that the plan is within the currently supported scope."""
    for level in plan.levels:
        if level.join_level.search_type == SearchType.NEAREST and not level.join_level.nearest_reference:
            msg = (
                f"NEAREST search type at level {level.level_index} requires a nearest_reference from objective analysis"
            )
            raise ValueError(msg)


def _objective_accessor_cpp(
    accessor_input: InputSelector,
    field_name: str | IndexInView,
    member_expr: Callable[[str], str],
    optional_input_names: frozenset[str],
) -> str:
    """Render a field accessor for objective comparison.

    Differs from _accessor_cpp just because of the context in which the access appears.

    Args:
        accessor_input: The input selector (plain name or batch boundary).
        field_name: The field name.
        member_expr: Function that maps a generated member name to a C++ iterator expression.
        optional_input_names: Names of optional inputs (need ``.value()``).
    """
    member = _iter_member_name(accessor_input)
    base_name = base_input_name(accessor_input)
    is_optional = base_name in optional_input_names
    it_suffix = ".value()" if is_optional else ""
    it_expr = f"{member_expr(member)}{it_suffix}"
    if isinstance(field_name, IndexInView):
        return it_expr
    return f"(*{it_expr}).{_field_getter(field_name)}"


def _render_objective_value_pair(
    expr: dfl.Expr,
    optional_input_names: frozenset[str],
    time_fields: frozenset[tuple[str, str]],
    current_member_expr: Callable[[str], str],
    other_member_expr: Callable[[str], str],
) -> tuple[str, str]:
    """Render the C++ value expression for *this and other.

    If the expression is a sum of terms, decomposes it and renders
    each term individually, combining with ``+``.

    Returns:
        ``(my_val_expr, other_val_expr)`` — C++ expressions for the
        objective value from ``*this`` and ``other`` respectively.
    """
    terms = decompose_additive(expr)
    if len(terms) > 1:
        my_parts: list[str] = []
        other_parts: list[str] = []
        for term in terms:
            my, other = _render_single_term_value_pair(
                term,
                optional_input_names,
                time_fields,
                current_member_expr,
                other_member_expr,
            )
            my_parts.append(my)
            other_parts.append(other)
        return " + ".join(my_parts), " + ".join(other_parts)
    return _render_single_term_value_pair(
        expr,
        optional_input_names,
        time_fields,
        current_member_expr,
        other_member_expr,
    )


def _render_objective_value(
    obj: Objective,
    optional_input_names: frozenset[str],
    time_fields: frozenset[tuple[str, str]],
    member_expr: Callable[[str], str],
) -> str:
    """Render the C++ value expression for one objective."""
    value, _ = _render_objective_value_pair(
        obj.expr,
        optional_input_names,
        time_fields,
        member_expr,
        member_expr,
    )
    return value


def _render_single_term_value_pair(
    expr: dfl.Expr,
    optional_input_names: frozenset[str],
    time_fields: frozenset[tuple[str, str]],
    current_member_expr: Callable[[str], str],
    other_member_expr: Callable[[str], str],
) -> tuple[str, str]:
    """Render a single objective term to C++ value expressions.

    Handles: squared difference, absolute difference, and simple field access.
    """
    # Pattern: squared difference (a - b) * (a - b) → render as squared value
    match expr:
        case dfl.Binary(
            op=dfl.BinaryOp.MUL,
            left=dfl.Binary(op=dfl.BinaryOp.SUB, left=l_left, right=l_right),
            right=dfl.Binary(op=dfl.BinaryOp.SUB, left=r_left, right=r_right),
        ) if l_left == r_left and l_right == r_right:
            return _render_squared_diff_pair(
                l_left,
                l_right,
                optional_input_names,
                time_fields,
                current_member_expr,
                other_member_expr,
            )
        case _:
            pass

    # Pattern: absolute difference |a - b|
    match expr:
        case dfl.Unary(
            op=dfl.UnaryOp.ABS,
            operand=dfl.Binary(op=dfl.BinaryOp.SUB, left=sub_left, right=sub_right),
        ):
            return _render_abs_diff_pair(
                sub_left,
                sub_right,
                optional_input_names,
                time_fields,
                current_member_expr,
                other_member_expr,
            )
        case _:
            pass

    # Pattern: simple field accessor
    accessor = try_field_accessor(expr)
    if accessor is not None:
        my = _objective_accessor_cpp(
            accessor.input_name,
            accessor.field_name,
            current_member_expr,
            optional_input_names,
        )
        other = _objective_accessor_cpp(
            accessor.input_name,
            accessor.field_name,
            other_member_expr,
            optional_input_names,
        )
        return my, other

    msg = f"Unrecognized objective expression pattern during rendering: {expr}"
    raise ValueError(msg)


def _is_time_diff(
    left_fa: FieldAccessor,
    right_fa: FieldAccessor,
    time_fields: frozenset[tuple[str, str]],
) -> bool:
    """Return whether both sides of a difference are time-typed fields.

    Asserts that both sides agree — mixing a time field with a non-time
    field is a bug in the objective expression.
    """
    left_is_time = (base_input_name(left_fa.input_name), left_fa.field_name) in time_fields
    right_is_time = (base_input_name(right_fa.input_name), right_fa.field_name) in time_fields
    assert left_is_time == right_is_time, "Both sides of a difference must agree on time-typing"
    return left_is_time


def _render_abs_diff_pair(  # noqa: PLR0913 # Current and best candidate accessors are both needed.
    left: dfl.Expr,
    right: dfl.Expr,
    optional_input_names: frozenset[str],
    time_fields: frozenset[tuple[str, str]],
    current_member_expr: Callable[[str], str],
    other_member_expr: Callable[[str], str],
) -> tuple[str, str]:
    """Render abs-diff value pair.

    For time-typed fields (``SyncTime``, ``Duration``), emits
    ``std::abs((a - b).count())``.  For numeric fields, emits
    ``std::abs(a - b)``.
    """
    left_fa = try_field_accessor(left)
    right_fa = try_field_accessor(right)
    assert left_fa is not None, "abs-diff sides must be field accessors"
    assert right_fa is not None, "abs-diff sides must be field accessors"

    is_time = _is_time_diff(left_fa, right_fa, time_fields)

    my_left = _objective_accessor_cpp(
        left_fa.input_name,
        left_fa.field_name,
        current_member_expr,
        optional_input_names,
    )
    my_right = _objective_accessor_cpp(
        right_fa.input_name,
        right_fa.field_name,
        current_member_expr,
        optional_input_names,
    )
    other_left = _objective_accessor_cpp(
        left_fa.input_name,
        left_fa.field_name,
        other_member_expr,
        optional_input_names,
    )
    other_right = _objective_accessor_cpp(
        right_fa.input_name,
        right_fa.field_name,
        other_member_expr,
        optional_input_names,
    )

    count = ".count()" if is_time else ""
    my_val = f"std::abs(({my_left} - {my_right}){count})"
    other_val = f"std::abs(({other_left} - {other_right}){count})"
    return my_val, other_val


def _render_squared_diff_pair(  # noqa: PLR0913 # Current and best candidate accessors are both needed.
    left: dfl.Expr,
    right: dfl.Expr,
    optional_input_names: frozenset[str],
    time_fields: frozenset[tuple[str, str]],
    current_member_expr: Callable[[str], str],
    other_member_expr: Callable[[str], str],
) -> tuple[str, str]:
    """Render squared-diff value pair.

    For time-typed fields, emits ``(a - b).count() * (a - b).count()``.
    For numeric fields, emits ``(a - b) * (a - b)``.
    """
    left_fa = try_field_accessor(left)
    right_fa = try_field_accessor(right)
    assert left_fa is not None, "squared-diff sides must be field accessors"
    assert right_fa is not None, "squared-diff sides must be field accessors"

    is_time = _is_time_diff(left_fa, right_fa, time_fields)

    my_left = _objective_accessor_cpp(
        left_fa.input_name,
        left_fa.field_name,
        current_member_expr,
        optional_input_names,
    )
    my_right = _objective_accessor_cpp(
        right_fa.input_name,
        right_fa.field_name,
        current_member_expr,
        optional_input_names,
    )
    other_left = _objective_accessor_cpp(
        left_fa.input_name,
        left_fa.field_name,
        other_member_expr,
        optional_input_names,
    )
    other_right = _objective_accessor_cpp(
        right_fa.input_name,
        right_fa.field_name,
        other_member_expr,
        optional_input_names,
    )

    count = ".count()" if is_time else ""
    my_val = f"({my_left} - {my_right}){count} * ({my_left} - {my_right}){count}"
    other_val = f"({other_left} - {other_right}){count} * ({other_left} - {other_right}){count}"
    return my_val, other_val


def _generate_candidate_iterator_aliases(plan: CodegenPlan, dial_type: str) -> list[str]:
    """Generate namespace-scope iterator type aliases."""
    lines: list[str] = []
    for name in plan.input_names:
        alias = _iter_type_name(name)
        lines.append(f"using {alias} = decltype(")
        lines.append(f"    std::declval<{dial_type}Inputs&>()")
        lines.append(f"        .{_input_getter(name)}.get_view().begin());")
    return lines


def _generate_input_views_struct(plan: CodegenPlan, dial_type: str, reuse_map: dict[str, bool]) -> list[str]:
    """Generate a per-execute cache of read-only input views."""
    lines: list[str] = ["struct InputViews {"]
    declval_inputs = f"std::declval<{dial_type}Inputs&>()"
    for name in plan.input_names:
        source_expr = _input_view_source_expr(name, reuse_map.get(name, False), receiver=declval_inputs)
        lines.append(f"  decltype({source_expr})")
        lines.append(f"      {_input_view_member_name(name)};")
    lines.append("};")
    lines.append("")
    lines.append(f"static InputViews make_input_views({dial_type}Inputs& inputs)")
    lines.append("{")
    lines.append("  return InputViews{")
    lines.extend(
        f"      .{_input_view_member_name(name)} = {_input_view_source_expr(name, reuse_map.get(name, False))},"
        for name in plan.input_names
    )
    lines.append("  };")
    lines.append("}")
    return lines


def _candidate_level_member_decl(plan: CodegenPlan, level: CodegenLevel) -> str:
    """Generate the iterator data member for one candidate suffix level."""
    input_name = base_input_name(level.join_level.input)
    alias = _iter_type_name(input_name)
    member = _iter_member_name(level.join_level.input)
    if input_name in plan.optional_input_names:
        return f"  std::optional<{alias}> {member};"
    return f"  {alias} {member};"


def _generate_candidate_suffix_structs(plan: CodegenPlan) -> list[str]:
    """Generate nested candidate suffix structs, one per search level."""
    lines: list[str] = []
    for offset, level in enumerate(reversed(plan.levels)):
        next_level = None if offset == 0 else plan.levels[len(plan.levels) - offset]
        lines.append(f"struct {_candidate_suffix_type_name(level.level_index)} {{")
        lines.append(_candidate_level_member_decl(plan, level))
        if next_level is not None:
            next_index = next_level.level_index
            lines.append(f"  {_candidate_suffix_type_name(next_index)} suffix{next_index};")
        lines.append("};")
        lines.append("")
    return lines


def _presence_count_type_cpp(compiler_context: CompilerContext, optional_count: int) -> str:
    """Return the smallest generated C++ integer type for a presence count."""
    count_type = primitive.smallest_type_to_hold_range(0, optional_count)
    return typereg.get_cpp_type(compiler_context, count_type).render("")


def _generate_presence_state(
    optional_input_names: list[str],
    compiler_context: CompilerContext,
) -> list[str]:
    """Generate cached optional-presence metadata."""
    lines: list[str] = ["struct PresenceState {"]
    if not optional_input_names:
        lines.append("};")
        return lines

    count_type = _presence_count_type_cpp(compiler_context, len(optional_input_names))
    lines.append(f"  static constexpr std::size_t optional_count = {len(optional_input_names)}U;")
    lines.append("  std::bitset<optional_count> present_mask{};")
    lines.append(f"  {count_type} present_count{{}};")
    lines.append("")
    for index, name in enumerate(optional_input_names):
        bit_name = _optional_present_bit_name(name)
        lines.append(f"  static constexpr std::size_t {bit_name} = {index}U;")
    lines.append("")
    for name in optional_input_names:
        bit_name = _optional_present_bit_name(name)
        lines.append(f"  bool has_{name}() const {{")
        lines.append(f"    return present_mask.test({bit_name});")
        lines.append("  }")
    lines.append("")
    lines.append(f"  {count_type} count_present() const {{")
    lines.append("    return present_count;")
    lines.append("  }")
    lines.append("};")
    return lines


def _generate_candidate_struct(plan: CodegenPlan) -> list[str]:
    """Generate the ``CandidateAlignment`` wrapper around the nested suffix tree."""
    lines: list[str] = ["struct CandidateAlignment {"]
    if plan.levels:
        first_index = plan.levels[0].level_index
        lines.append(f"  {_candidate_suffix_type_name(first_index)} suffix{first_index};")
    lines.append("  [[no_unique_address]] PresenceState presence;")
    lines.append("};")
    return lines


def _generate_candidate_member_accessors(plan: CodegenPlan) -> list[str]:
    """Generate accessors that preserve the old flat candidate member names."""
    member_levels = _member_level_index_map(plan)
    lines: list[str] = []
    for member_name in member_levels:
        function_name = _candidate_member_function_name(member_name)
        lines.append(f"static auto& {function_name}(CandidateAlignment& candidate)")
        lines.append("{")
        lines.append(f"  return ({_candidate_member_expr(member_name, 'candidate', member_levels)});")
        lines.append("}")
        lines.append("")
        lines.append(f"static const auto& {function_name}(const CandidateAlignment& candidate)")
        lines.append("{")
        lines.append(f"  return ({_candidate_member_expr(member_name, 'candidate', member_levels)});")
        lines.append("}")
        lines.append("")
    return lines


def _generate_presence_helpers(plan: CodegenPlan, optional_input_names: list[str]) -> list[str]:
    """Generate candidate-level presence refresh helpers."""
    lines: list[str] = []
    if not optional_input_names:
        return lines

    for name in optional_input_names:
        bit_name = _optional_present_bit_name(name)
        lines.append(f"static void refresh_{name}_presence(CandidateAlignment& candidate)")
        lines.append("{")
        presence = _candidate_optional_presence_expr(name, plan.batch_input_names, "candidate")
        lines.append(f"  const bool is_present = {presence};")
        lines.append(f"  const bool was_present = candidate.presence.has_{name}();")
        lines.append("  if (is_present == was_present) {")
        lines.append("    return;")
        lines.append("  }")
        lines.append("  if (is_present) {")
        lines.append(f"    candidate.presence.present_mask.set(PresenceState::{bit_name});")
        lines.append("    ++candidate.presence.present_count;")
        lines.append("  } else {")
        lines.append(f"    candidate.presence.present_mask.reset(PresenceState::{bit_name});")
        lines.append("    --candidate.presence.present_count;")
        lines.append("  }")
        lines.append("}")
        lines.append("")
    return lines


def _objective_active_function_name(obj_index: int) -> str:
    """Return the generated active-guard helper name for an objective."""
    return f"objective_{obj_index}_is_active"


def _objective_value_function_name(obj_index: int) -> str:
    """Return the generated value helper name for an objective."""
    return f"objective_{obj_index}_value"


def _objective_active_guard_expr(
    obj: Objective,
    optional_input_names: frozenset[str],
    presence_expr: str,
) -> str:
    """Return the C++ guard expression for evaluating an objective."""
    guard_parts: list[str] = []
    if obj.else_guard_input is not None:
        guard_parts.append(_optional_cached_absence_expr(obj.else_guard_input, presence_expr))

    present_inputs = set(referenced_base_inputs(obj.expr) & optional_input_names)
    if obj.conditional_guard_input is not None:
        present_inputs.add(obj.conditional_guard_input)
    guard_parts.extend(_optional_cached_presence_expr(name, presence_expr) for name in sorted(present_inputs))
    return " && ".join(guard_parts) if guard_parts else "true"


def _generate_objective_helpers(plan: CodegenPlan, objective_indices: frozenset[int]) -> list[str]:
    """Generate objective active-guard and value helpers."""
    lines: list[str] = []
    if not plan.objectives or not objective_indices:
        return lines

    def member_expr(member_name: str) -> str:
        return _candidate_member_call(member_name, "candidate")

    for obj_index, obj in enumerate(plan.objectives):
        if obj_index not in objective_indices:
            continue
        active_fn = _objective_active_function_name(obj_index)
        value_fn = _objective_value_function_name(obj_index)
        value_expr = _render_objective_value(
            obj=obj,
            optional_input_names=plan.optional_input_names,
            time_fields=plan.time_fields,
            member_expr=member_expr,
        )

        active_guard = _objective_active_guard_expr(obj, plan.optional_input_names, "presence")
        presence_param = "const PresenceState&" if active_guard == "true" else "const PresenceState& presence"
        lines.append(f"static bool {active_fn}({presence_param})")
        lines.append("{")
        lines.append(f"  return {active_guard};")
        lines.append("}")
        lines.append("")
        lines.append(f"static auto {value_fn}(const CandidateAlignment& candidate)")
        lines.append("{")
        lines.append(f"  return {value_expr};")
        lines.append("}")
        lines.append("")

    return lines


def _generate_best_suffix_structs(plan: CodegenPlan, best_level_indices: frozenset[int]) -> list[str]:
    """Generate per-level best-so-far suffix snapshot structs."""
    lines: list[str] = []
    for level in plan.levels:
        index = level.level_index
        if index not in best_level_indices:
            continue
        lines.append(f"struct {_best_suffix_type_name(index)} {{")
        lines.append(f"  {_candidate_suffix_type_name(index)} suffix;")
        lines.append("  [[no_unique_address]] PresenceState presence;")
        lines.append("};")
        lines.append("")
    return lines


def _generate_component_best_structs(plan: CodegenPlan, contexts: tuple[_SearchFunctionContext, ...]) -> list[str]:
    """Generate component-local best-so-far snapshot structs."""
    lines: list[str] = []
    for context in contexts:
        if not context.is_component:
            continue
        lines.append(f"struct {_component_best_type_name(context)} {{")
        for level_index in context.suffix_level_indices:
            level = plan.levels[level_index]
            lines.append(_candidate_level_member_decl(plan, level))
        lines.append("  [[no_unique_address]] PresenceState presence;")
        lines.append("};")
        lines.append("")
    return lines


def _store_best_suffix_function_name(level_index: int) -> str:
    """Return the generated function name for storing a best suffix."""
    return f"store_best_suffix_{level_index}"


def _restore_best_suffix_function_name(level_index: int) -> str:
    """Return the generated function name for restoring a best suffix."""
    return f"restore_best_suffix_{level_index}"


def _best_suffix_comparator_function_name(level_index: int) -> str:
    """Return the generated comparator function name for a best suffix."""
    return f"is_better_than_best_suffix_{level_index}"


def _objective_member_names(obj: Objective) -> frozenset[str]:
    """Return generated candidate member names referenced by an objective."""
    return frozenset(_iter_member_name(accessor.input_name) for accessor in collect_field_accessors(obj.expr))


def _objective_max_member_level_index(obj: Objective, member_levels: dict[str, int]) -> int:
    """Return the deepest generated candidate member level referenced by an objective."""
    levels: list[int] = []
    for member_name in _objective_member_names(obj):
        try:
            levels.append(member_levels[member_name])
        except KeyError as exc:
            msg = f"Objective references input member {member_name!r} that is not present in the search plan"
            raise ValueError(msg) from exc
    return max(levels, default=-1)


def _objective_member_level_indices(obj: Objective, member_levels: dict[str, int]) -> frozenset[int]:
    """Return generated search levels referenced by an objective."""
    levels: set[int] = set()
    for member_name in _objective_member_names(obj):
        try:
            levels.add(member_levels[member_name])
        except KeyError as exc:
            msg = f"Objective references input member {member_name!r} that is not present in the search plan"
            raise ValueError(msg) from exc
    return frozenset(levels)


def _objective_indices_for_context(plan: CodegenPlan, context: _SearchFunctionContext) -> tuple[int, ...]:
    """Return objective indices that can vary within a search context."""
    member_levels = _member_level_index_map(plan)
    component_levels = frozenset(context.component_level_indices)
    suffix_levels = frozenset(context.suffix_level_indices)
    indices: list[int] = []
    for obj_index, obj in enumerate(plan.objectives):
        objective_levels = _objective_member_level_indices(obj, member_levels)
        if not objective_levels & suffix_levels:
            continue
        if not objective_levels & component_levels:
            continue
        indices.append(obj_index)
    return tuple(indices)


def _objective_can_be_active_without_input(obj: Objective, input_name: str) -> bool:
    """Return whether an objective can be active when an optional input is absent."""
    if obj.conditional_guard_input == input_name:
        return False
    return input_name not in referenced_base_inputs(obj.expr)


def _nearest_optional_reference_fallback_uses_bsf(plan: CodegenPlan, context: _SearchFunctionContext) -> bool:
    """Return whether an optional-reference NEAREST fallback must compare candidates."""
    level = plan.levels[context.level_index]
    if level.join_level.search_type != SearchType.NEAREST:
        return False
    nearest_reference = level.join_level.nearest_reference
    if nearest_reference is None:
        return False

    ref_input = base_input_name(nearest_reference.reference.input_name)
    if ref_input not in plan.optional_input_names:
        return False

    return any(
        _objective_can_be_active_without_input(plan.objectives[obj_index], ref_input)
        for obj_index in _objective_indices_for_context(plan, context)
    )


def _has_downstream_objective_refs_for_context(plan: CodegenPlan, context: _SearchFunctionContext) -> bool:
    """Return whether an objective can vary below the current level in a context."""
    member_levels = _member_level_index_map(plan)
    suffix_levels = frozenset(context.suffix_level_indices)
    for obj in plan.objectives:
        objective_levels = _objective_member_level_indices(obj, member_levels)
        if any(level_index > context.level_index for level_index in objective_levels & suffix_levels):
            return True
    return False


def _suffix_optional_names(plan: CodegenPlan, level_index: int) -> tuple[str, ...]:
    """Return optional inputs whose presence can still vary at or below a level."""
    return _optional_names_for_level_indices(
        plan,
        tuple(level.level_index for level in plan.levels if level.level_index >= level_index),
    )


def _optional_names_for_level_indices(plan: CodegenPlan, level_indices: tuple[int, ...]) -> tuple[str, ...]:
    """Return optional inputs whose presence can vary in a set of levels."""
    level_index_set = frozenset(level_indices)
    suffix_inputs = {
        base_input_name(level.join_level.input)
        for level in plan.levels
        if level.level_index in level_index_set and base_input_name(level.join_level.input) in plan.optional_input_names
    }
    return tuple(name for name in plan.input_names if name in suffix_inputs)


def _suffix_optionals_present_function_name(level_index: int) -> str:
    """Return the generated suffix optional guard helper name."""
    return f"suffix_optionals_present_{level_index}"


def _component_optionals_present_function_name(context: _SearchFunctionContext) -> str:
    """Return the generated component optional guard helper name."""
    return f"component_optionals_present_{context.partition_id}_{context.component_id}_{context.level_index}"


def _optionals_present_function_name(context: _SearchFunctionContext) -> str:
    """Return the generated optional-fullness helper for a search context."""
    if context.is_component:
        return _component_optionals_present_function_name(context)
    return _suffix_optionals_present_function_name(context.level_index)


def _optionals_present_expr(plan: CodegenPlan, context: _SearchFunctionContext, presence_expr: str) -> str:
    """Return a C++ expression checking suffix optional fullness."""
    if not _optional_names_for_level_indices(plan, context.suffix_level_indices):
        return "true"
    return f"{_optionals_present_function_name(context)}({presence_expr})"


def _generate_suffix_optional_presence_helpers(plan: CodegenPlan, guard_level_indices: frozenset[int]) -> list[str]:
    """Generate helpers used to test whether a suffix has all optional inputs present."""
    lines: list[str] = []
    for level in plan.levels:
        level_index = level.level_index
        if level_index not in guard_level_indices:
            continue
        suffix_names = _suffix_optional_names(plan, level_index)
        if not suffix_names:
            continue

        checks = [f"presence.has_{name}()" for name in suffix_names]
        lines.append(
            f"static bool {_suffix_optionals_present_function_name(level_index)}(const PresenceState& presence)"
        )
        lines.append("{")
        if len(checks) == 1:
            lines.append(f"  return {checks[0]};")
        else:
            lines.append(f"  return {checks[0]}")
            lines.extend(f"      && {check}" for check in checks[1:-1])
            lines.append(f"      && {checks[-1]};")
        lines.append("}")
        lines.append("")
    return lines


def _generate_component_optional_presence_helpers(
    plan: CodegenPlan,
    contexts: tuple[_SearchFunctionContext, ...],
) -> list[str]:
    """Generate component-local optional-fullness helpers."""
    lines: list[str] = []
    for context in contexts:
        if not context.is_component:
            continue
        names = _optional_names_for_level_indices(plan, context.suffix_level_indices)
        if not names:
            continue
        checks = [f"presence.has_{name}()" for name in names]
        lines.append(
            f"static bool {_component_optionals_present_function_name(context)}(const PresenceState& presence)"
        )
        lines.append("{")
        if len(checks) == 1:
            lines.append(f"  return {checks[0]};")
        else:
            lines.append(f"  return {checks[0]}")
            lines.extend(f"      && {check}" for check in checks[1:-1])
            lines.append(f"      && {checks[-1]};")
        lines.append("}")
        lines.append("")
    return lines


def _objective_matches_level_scan(obj: Objective, level: CodegenLevel) -> bool:
    """Return whether an objective is a simple monotone field objective for this level."""
    accessor = try_field_accessor(obj.expr)
    if accessor is None or accessor.input_name != level.join_level.input:
        return False
    if not isinstance(accessor.field_name, str):
        return False

    search_type = level.join_level.search_type
    if search_type == SearchType.FIRST_IN_RANGE:
        return obj.sense == ObjectiveSense.MINIMIZE
    if search_type == SearchType.LAST_IN_RANGE:
        return obj.sense == ObjectiveSense.MAXIMIZE
    return False


def _dominating_objective_prune_for_level(
    plan: CodegenPlan,
    level: CodegenLevel,
    opt: _OptionalContext,
) -> _DominatingObjectivePrune | None:
    """Return tie-aware pruning metadata for a level, if safe."""
    if not opt.use_bsf or level.join_level.search_type not in {SearchType.FIRST_IN_RANGE, SearchType.LAST_IN_RANGE}:
        return None

    member_levels = _member_level_index_map(plan)
    for obj_index, obj in enumerate(plan.objectives):
        max_level_index = _objective_max_member_level_index(obj, member_levels)
        if max_level_index < level.level_index:
            continue
        if max_level_index > level.level_index:
            return None
        if not _objective_matches_level_scan(obj, level):
            return None
        return _DominatingObjectivePrune(objective_index=obj_index)
    return None


def _context_uses_optional_presence_helper(
    plan: CodegenPlan,
    context: _SearchFunctionContext,
    opt: _OptionalContext,
) -> bool:
    """Return whether generated code references a context optional-fullness helper."""
    if not _optional_names_for_level_indices(plan, context.suffix_level_indices):
        return False
    if opt.is_optional:
        return True
    if not opt.has_downstream_obj_refs:
        return True
    if not context.is_component:
        return _dominating_objective_prune_for_level(plan, plan.levels[context.level_index], opt) is not None
    return False


def _generate_best_context_objective_comparison(
    *,
    plan: CodegenPlan,
    context: _SearchFunctionContext,
    indent: str,
) -> list[str]:
    """Generate objective comparison blocks for a best snapshot context."""
    member_levels = _member_level_index_map(plan)
    suffix_levels = frozenset(context.suffix_level_indices)
    lines: list[str] = []

    def best_member_expr(member_name: str) -> str:
        level_index = member_levels[member_name]
        if context.is_component and level_index in suffix_levels:
            return f"best.{member_name}"
        if not context.is_component and level_index >= context.level_index:
            return _best_suffix_member_expr(member_name, context.level_index, member_levels)
        return _candidate_member_call(member_name, "candidate")

    for obj_idx in _objective_indices_for_context(plan, context):
        obj = plan.objectives[obj_idx]
        active_fn = _objective_active_function_name(obj_idx)
        value_fn = _objective_value_function_name(obj_idx)
        compare_op = "<" if obj.sense == ObjectiveSense.MINIMIZE else ">"
        other_value = _render_objective_value(
            obj=obj,
            optional_input_names=plan.optional_input_names,
            time_fields=plan.time_fields,
            member_expr=best_member_expr,
        )
        lines.append(f"{indent}if ({active_fn}(candidate.presence)")
        lines.append(f"{indent}    && {active_fn}(best.presence)) {{")
        lines.append(f"{indent}  const auto my_obj_{obj_idx} = {value_fn}(candidate);")
        lines.append(f"{indent}  const auto other_obj_{obj_idx} = {other_value};")
        lines.append(
            f"{indent}  if (my_obj_{obj_idx} != other_obj_{obj_idx}) "
            + f"{{ return my_obj_{obj_idx} {compare_op} other_obj_{obj_idx}; }}"
        )
        lines.append(f"{indent}}}")
    return lines


def _generate_best_suffix_helpers(plan: CodegenPlan, contexts: tuple[_SearchFunctionContext, ...]) -> list[str]:
    """Generate per-level best suffix store, restore, and comparison helpers."""
    lines: list[str] = []
    for context in contexts:
        index = context.level_index
        best_type = _best_suffix_type_name(index)
        candidate_suffix = _candidate_suffix_expr(index, "candidate")

        lines.append(f"static void {_store_best_suffix_function_name(index)}(")
        lines.append("    const CandidateAlignment& candidate,")
        lines.append(f"    {best_type}& best)")
        lines.append("{")
        lines.append(f"  best.suffix = {candidate_suffix};")
        lines.append("  best.presence = candidate.presence;")
        lines.append("}")
        lines.append("")

        lines.append(f"static void {_restore_best_suffix_function_name(index)}(")
        lines.append(f"    const {best_type}& best,")
        lines.append("    CandidateAlignment& candidate)")
        lines.append("{")
        lines.append(f"  {candidate_suffix} = best.suffix;")
        lines.append("  candidate.presence = best.presence;")
        lines.append("}")
        lines.append("")

        lines.append(f"static bool {_best_suffix_comparator_function_name(index)}(")
        lines.append("    const CandidateAlignment& candidate,")
        lines.append(f"    const {best_type}& best)")
        lines.append("{")
        if plan.optional_input_names:
            lines.append("  if (candidate.presence.count_present() != best.presence.count_present()) {")
            lines.append("    return candidate.presence.count_present() > best.presence.count_present();")
            lines.append("  }")
        if plan.objectives:
            lines.extend(
                _generate_best_context_objective_comparison(
                    plan=plan,
                    context=context,
                    indent="  ",
                )
            )
            lines.append("  return false;")
        else:
            lines.append("  return false;")
        lines.append("}")
        lines.append("")
    return lines


def _generate_component_best_helpers(plan: CodegenPlan, contexts: tuple[_SearchFunctionContext, ...]) -> list[str]:
    """Generate component-local best store, restore, and comparison helpers."""
    lines: list[str] = []
    for context in contexts:
        if not context.is_component:
            continue
        best_type = _component_best_type_name(context)

        lines.append(f"static void {_store_best_function_name(context)}(")
        lines.append("    const CandidateAlignment& candidate,")
        lines.append(f"    {best_type}& best)")
        lines.append("{")
        for level_index in context.suffix_level_indices:
            member_name = _iter_member_name(plan.levels[level_index].join_level.input)
            lines.append(f"  best.{member_name} = {_candidate_member_call(member_name, 'candidate')};")
        lines.append("  best.presence = candidate.presence;")
        lines.append("}")
        lines.append("")

        lines.append(f"static void {_restore_best_function_name(context)}(")
        lines.append(f"    const {best_type}& best,")
        lines.append("    CandidateAlignment& candidate)")
        lines.append("{")
        for level_index in context.suffix_level_indices:
            member_name = _iter_member_name(plan.levels[level_index].join_level.input)
            lines.append(f"  {_candidate_member_call(member_name, 'candidate')} = best.{member_name};")
        lines.append("  candidate.presence = best.presence;")
        lines.append("}")
        lines.append("")

        lines.append(f"static bool {_best_comparator_function_name(context)}(")
        lines.append("    const CandidateAlignment& candidate,")
        lines.append(f"    const {best_type}& best)")
        lines.append("{")
        if plan.optional_input_names:
            lines.append("  if (candidate.presence.count_present() != best.presence.count_present()) {")
            lines.append("    return candidate.presence.count_present() > best.presence.count_present();")
            lines.append("  }")
        lines.extend(
            _generate_best_context_objective_comparison(
                plan=plan,
                context=context,
                indent="  ",
            )
        )
        lines.append("  return false;")
        lines.append("}")
        lines.append("")
    return lines


def _classify_constraint_sides(
    constraint: Constraint,
    current_input: InputSelector,
) -> _ConstraintSides:
    """Determine which side of a constraint is the current-level iterator.

    Preserves the full ``InputSelector`` (including batch wrappers) so
    that downstream codegen resolves the correct member name.

    ``current_input`` is the full selector for the level's input (e.g.
    ``"lidar"`` or ``LastInBatch("pose")``).
    """
    match constraint:
        case DifferenceConstraint(minuend=m, subtrahend=s):
            if s.input_name == current_input:
                return _ConstraintSides(
                    bound_input=m.input_name,
                    bound_field=m.field_name,
                    current_input=s.input_name,
                    current_field=s.field_name,
                    is_equality=False,
                )
            assert m.input_name == current_input, (
                f"Neither side of constraint matches current input {current_input!r}: "
                f"{m.input_name!r} - {s.input_name!r}"
            )
            return _ConstraintSides(
                bound_input=s.input_name,
                bound_field=s.field_name,
                current_input=m.input_name,
                current_field=m.field_name,
                is_equality=False,
            )
        case EqualityConstraint(left=l, right=r):
            if r.input_name == current_input:
                return _ConstraintSides(
                    bound_input=l.input_name,
                    bound_field=l.field_name,
                    current_input=r.input_name,
                    current_field=r.field_name,
                    is_equality=True,
                )
            assert l.input_name == current_input, (
                f"Neither side of constraint matches current input {current_input!r}: "
                f"{l.input_name!r} == {r.input_name!r}"
            )
            return _ConstraintSides(
                bound_input=r.input_name,
                bound_field=r.field_name,
                current_input=l.input_name,
                current_field=l.field_name,
                is_equality=True,
            )


def _emit_constraint_check(
    lines: list[str],
    c: Constraint,
    current_input: InputSelector,
    indent: str,
    *,
    optional_input: str | None = None,
) -> None:
    """Emit one constraint check.

    Args:
        lines: Output list of C++ source lines.
        c: The constraint to emit a check for.
        current_input: Full selector for the input being scanned at this level.
        indent: Whitespace prefix for generated lines.
        optional_input: When set, bound-side accesses to this input use
            ``.value()`` to unwrap the ``std::optional`` iterator.
    """
    sides = _classify_constraint_sides(c, current_input)
    is_opt_bound = optional_input is not None and base_input_name(sides.bound_input) == optional_input
    bound_expr = _accessor_cpp(sides.bound_input, sides.bound_field, via_candidate=True, optional=is_opt_bound)
    cur_expr = _accessor_cpp(sides.current_input, sides.current_field, via_candidate=False)

    if sides.is_equality:
        lines.append(f"{indent}if ({bound_expr} != {cur_expr}) {{")
        lines.append(f"{indent}  return false;")
        lines.append(f"{indent}}}")
    else:
        assert isinstance(c, DifferenceConstraint)

        is_index = isinstance(c.minuend.field_name, IndexInView)
        assert isinstance(c.subtrahend.field_name, IndexInView) == is_index

        if c.minuend.input_name == sides.bound_input:
            lhs, rhs = bound_expr, cur_expr
        else:
            lhs, rhs = cur_expr, bound_expr

        if is_index:
            # IndexInView: compare iterator distance.
            cpp_lit = _bound_to_cpp_literal(c.bound)
            lines.append(f"{indent}if (std::distance({rhs}, {lhs}) > {cpp_lit}) {{")
            lines.append(f"{indent}  return false;")
            lines.append(f"{indent}}}")
        elif c.bound.value == Decimal(0):
            lines.append(f"{indent}if ({lhs} > {rhs}) {{")
            lines.append(f"{indent}  return false;")
            lines.append(f"{indent}}}")
        else:
            cpp_lit = _bound_to_cpp_literal(c.bound)
            lines.append(f"{indent}if ({lhs} - {rhs} > {cpp_lit}) {{")
            lines.append(f"{indent}  return false;")
            lines.append(f"{indent}}}")


def _generate_constraint_checker(
    level: CodegenLevel,
    batch_input_names: frozenset[str],
    opt: _OptionalContext,
) -> list[str]:
    """Generate ``check_constraints_N()`` for *level*."""
    remaining = _remaining_constraint_checks(level, opt)
    if not remaining.unconditional and not remaining.conditional_present and not remaining.conditional_absent:
        return []

    level_input = level.join_level.input
    current_base = base_input_name(level_input)
    it_type = _iter_type_name(current_base)
    idx = level.level_index

    lines: list[str] = []
    lines.append(f"static bool check_constraints_{idx}(")
    lines.append("    const CandidateAlignment& candidate,")
    lines.append(f"    {it_type} candidate_it)")
    lines.append("{")

    for c in remaining.unconditional:
        _emit_constraint_check(lines, c, level_input, "  ")

    for opt_name in sorted(level.conditional_checks):
        present_constraints = remaining.conditional_present.get(opt_name, ())
        absent_constraints = remaining.conditional_absent.get(opt_name, ())
        has_present = bool(present_constraints)
        has_absent = bool(absent_constraints)
        if not has_present and not has_absent:
            continue
        present_guard = _optional_constraint_present_guard(opt_name, level_input, batch_input_names)
        lines.append(f"  if ({present_guard}) {{")
        for c in present_constraints:
            _emit_constraint_check(lines, c, level_input, "    ", optional_input=opt_name)
        if has_absent:
            lines.append("  } else {")
            for c in absent_constraints:
                _emit_constraint_check(lines, c, level_input, "    ")
        lines.append("  }")

    lines.append("  return true;")
    lines.append("}")
    return lines


def _emit_optional_absent_assignments(
    lines: list[str],
    input_name: str,
    absent_members: tuple[str, ...],
    indent: str,
) -> None:
    """Emit assignments that mark an optional input absent."""
    lines.extend(f"{indent}{_candidate_member_call(member, '*candidate')} = std::nullopt;" for member in absent_members)
    lines.append(f"{indent}refresh_{input_name}_presence(*candidate);")


def _emit_candidate_assignment(
    lines: list[str],
    opt: _OptionalContext,
    member: str,
    value: str,
    indent: str,
) -> None:
    """Emit a candidate iterator assignment and refresh cached presence when needed."""
    lines.append(f"{indent}{_candidate_member_call(member, '*candidate')} = {value};")
    if opt.is_optional:
        lines.append(f"{indent}refresh_{opt.input_name}_presence(*candidate);")


def _emit_optional_batch_short_circuit(
    lines: list[str],
    next_function_name: str | None,
    opt: _OptionalContext,
) -> None:
    """Skip a batch boundary when an earlier boundary is already absent."""
    if not opt.required_present_members:
        return

    missing = " || ".join(
        f"!{_candidate_member_call(member, '*candidate')}.has_value()" for member in opt.required_present_members
    )
    lines.append(f"  if ({missing}) {{")
    _emit_optional_absent_assignments(lines, opt.input_name, opt.absent_members, "    ")
    if next_function_name is not None:
        lines.append(f"    return {next_function_name}(jewels::InOut{{*candidate}}, views);")
    else:
        lines.append("    return jewels::success;")
    lines.append("  }")
    lines.append("")


def _generate_forward_scan(  # noqa: PLR0915, C901, PLR0912 # Many lines/branches because of embedded C++
    plan: CodegenPlan,
    level: CodegenLevel,
    context: _SearchFunctionContext,
    opt: _OptionalContext,
) -> list[str]:
    """Generate ``search_level_N()`` — forward scan.

    Used for FIRST_IN_RANGE, ANY_MATCH, ENUMERATE, and EXACT_MATCH.
    """
    idx = level.level_index
    current_input = base_input_name(level.join_level.input)
    member = _iter_member_name(level.join_level.input)
    has_checker = _has_remaining_constraint_checks(level, opt)
    view = _view_expr(current_input)
    dominating_prune = None if context.is_component else _dominating_objective_prune_for_level(plan, level, opt)

    lines: list[str] = []
    lines.append(f"// Level {idx}: {current_input} ({level.join_level.search_type.value})")
    lines.append(f"static jewels::BinaryOutcome {context.function_name}(")
    lines.append("    jewels::InOut<CandidateAlignment> candidate,")
    lines.append("    const InputViews& views)")
    lines.append("{")

    if opt.use_bsf:
        lines.append(f"  std::optional<{_best_type_name(context)}> best;")
        lines.append("")

    _emit_optional_batch_short_circuit(lines, context.next_function_name, opt)

    indent = "  "
    if opt.is_optional:
        lines.append("  // Path 1: present.")
        lines.append("  {")
        indent = "    "

    lines.append(f"{indent}const auto& view = {view};")
    lines.append("")

    it_start, it_end, _ = _emit_windowing_preamble(lines, level, opt, indent=indent)

    right_cutoffs = level.direction.right_cutoffs
    opt_right_cutoffs = level.direction.optional_right_cutoffs
    cutoff_views = _collect_cutoff_views(right_cutoffs, opt_right_cutoffs if opt.use_bsf else ())
    if cutoff_views:
        _emit_cutoff_view_decls(lines, cutoff_views, indent)
        lines.append("")

    opt_flags: list[str] = []
    if opt_right_cutoffs and opt.use_bsf:
        opt_flags = _emit_optional_cutoff_flags(lines, opt_right_cutoffs, indent)
        lines.append("")

    first_candidate_returns = (
        not opt.use_bsf and context.next_function_name is None and not has_checker and not right_cutoffs
    )
    if first_candidate_returns:
        lines.append(f"{indent}if ({it_start} < {it_end}) {{")
        _emit_candidate_assignment(lines, opt, member, it_start, f"{indent}  ")
        lines.append(f"{indent}  return jewels::success;")
        lines.append(f"{indent}}}")
    else:
        lines.append(f"{indent}for (auto it = {it_start}; it < {it_end}; ++it) {{")

        if right_cutoffs:
            _emit_right_cutoffs(lines, right_cutoffs, f"{indent}  ", iterator_var="it")

        if opt_right_cutoffs and opt.use_bsf:
            _emit_optional_cutoff_checks(
                lines,
                opt_right_cutoffs,
                f"{indent}  ",
                direction="right",
                iterator_var="it",
            )
            _emit_optional_cutoff_break(lines, opt_flags, f"{indent}  ")

        if has_checker:
            lines.append(f"{indent}  if (!check_constraints_{idx}(*candidate, it)) {{")
            lines.append(f"{indent}    continue;")
            lines.append(f"{indent}  }}")

        _emit_candidate_assignment(lines, opt, member, "it", f"{indent}  ")
        _emit_dominating_objective_prune(lines, plan, context, dominating_prune, f"{indent}  ")

        if opt.use_bsf:
            if context.next_function_name is not None:
                search_call = f"{context.next_function_name}(jewels::InOut{{*candidate}}, views)"
                lines.append(f"{indent}  if (jewels::ok({search_call})) {{")
                # Without cross-level objectives, max fullness is the stopping criterion.
                _emit_best_update(
                    lines,
                    f"{indent}    ",
                    context=context,
                    break_on_max_optionals=not opt.has_downstream_obj_refs,
                    break_on_full_expr=_optionals_present_expr(plan, context, "best->presence"),
                )
                if opt.req_tighten:
                    lines.append(f"{indent}  }} else {{")
                    lines.append(f"{indent}    // Required constraints tighten: future candidates also fail.")
                    lines.append(f"{indent}    if (!best.has_value()) {{ return jewels::failure; }}")
                    lines.append(f"{indent}    break;")
                lines.append(f"{indent}  }}")
            else:
                # Innermost level with best-so-far — compare candidates.
                _emit_best_update(
                    lines,
                    f"{indent}  ",
                    context=context,
                    break_on_max_optionals=not opt.has_downstream_obj_refs,
                    break_on_full_expr=_optionals_present_expr(plan, context, "best->presence"),
                )
        elif context.next_function_name is not None:
            lines.append(
                f"{indent}  if (jewels::ok({context.next_function_name}(jewels::InOut{{*candidate}}, views))) {{"
            )
            lines.append(f"{indent}    return jewels::success;")
            if opt.req_tighten:
                lines.append(f"{indent}  }} else {{")
                lines.append(f"{indent}    return jewels::failure;")
            lines.append(f"{indent}  }}")
        else:
            lines.append(f"{indent}  return jewels::success;")

        lines.append(f"{indent}}}")  # end for

    if opt.is_optional:
        lines.append("  }")  # end present block

    _emit_scan_tail(lines, context, opt, plan)

    lines.append("}")
    return lines


def _generate_reverse_scan(  # noqa: PLR0912, PLR0915, C901 # Many lines/branches because of embedded C++
    plan: CodegenPlan,
    level: CodegenLevel,
    context: _SearchFunctionContext,
    opt: _OptionalContext,
) -> list[str]:
    """Generate ``search_level_N()`` — reverse scan from view end.

    Used for LAST_IN_RANGE.
    """
    idx = level.level_index
    current_input = base_input_name(level.join_level.input)
    view = _view_expr(current_input)
    dominating_prune = None if context.is_component else _dominating_objective_prune_for_level(plan, level, opt)

    lines: list[str] = []
    lines.append(f"// Level {idx}: {current_input} ({level.join_level.search_type.value})")
    lines.append(f"static jewels::BinaryOutcome {context.function_name}(")
    lines.append("    jewels::InOut<CandidateAlignment> candidate,")
    lines.append("    const InputViews& views)")
    lines.append("{")

    if opt.use_bsf:
        lines.append(f"  std::optional<{_best_type_name(context)}> best;")
        lines.append("")

    _emit_optional_batch_short_circuit(lines, context.next_function_name, opt)

    indent = "  "
    if opt.is_optional:
        lines.append("  // Path 1: present.")
        lines.append("  {")
        indent = "    "

    lines.append(f"{indent}const auto& view = {view};")
    lines.append("")

    it_start, it_end, preamble_has_empty_range_guard = _emit_windowing_preamble(
        lines,
        level,
        opt,
        indent=indent,
    )

    # Preamble already emits an empty-range guard when windows are present
    # (for required levels only — optional levels omit it to allow
    # fall-through to the absent path).
    # For optional levels without windows, guard the iteration to avoid
    # std::prev(it_end) UB on an empty view.
    #
    # For required levels WITH windows, the preamble's empty-range guard
    # already handles this.
    # For optional levels (with or without windows), protect std::prev(it_end).
    if opt.is_optional:
        lines.append(f"{indent}if ({it_start} < {it_end}) {{")
        iter_indent = indent + "  "
    elif not preamble_has_empty_range_guard:
        lines.append(f"{indent}if ({it_start} >= {it_end}) {{")
        lines.append(f"{indent}  return jewels::failure;")
        lines.append(f"{indent}}}")
        lines.append("")
        iter_indent = indent
    else:
        # Required level with windows: preamble already guarded.
        iter_indent = indent

    left_cutoffs = level.direction.left_cutoffs
    opt_left_cutoffs = level.direction.optional_left_cutoffs
    cutoff_views = _collect_cutoff_views(left_cutoffs, opt_left_cutoffs if opt.use_bsf else ())
    if cutoff_views:
        _emit_cutoff_view_decls(lines, cutoff_views, iter_indent)
        lines.append("")

    opt_flags: list[str] = []
    if opt_left_cutoffs and opt.use_bsf:
        opt_flags = _emit_optional_cutoff_flags(lines, opt_left_cutoffs, iter_indent)
        lines.append("")

    lines.append(f"{iter_indent}auto it = std::prev({it_end});")
    lines.append(f"{iter_indent}while (true) {{")

    if left_cutoffs:
        _emit_left_cutoffs(lines, left_cutoffs, f"{iter_indent}  ", iterator_var="it")

    if opt_left_cutoffs and opt.use_bsf:
        _emit_optional_cutoff_checks(
            lines,
            opt_left_cutoffs,
            f"{iter_indent}  ",
            direction="left",
            iterator_var="it",
        )
        _emit_optional_cutoff_break(lines, opt_flags, f"{iter_indent}  ")

    # Build on_inner_failure lines for req_tighten.
    on_inner_failure: list[str] | None = None
    if opt.req_tighten and context.next_function_name is not None:
        if opt.use_bsf:
            on_inner_failure = [
                "// Required constraints tighten: future candidates also fail.",
                "if (!best.has_value()) { return jewels::failure; }",
                "break;",
            ]
        else:
            on_inner_failure = [
                "return jewels::failure;",
            ]

    _emit_match_body(
        lines=lines,
        level=level,
        context=context,
        candidate_it_expr="it",
        opt=opt,
        indent=f"{iter_indent}  ",
        on_inner_failure=on_inner_failure,
        plan=plan,
        dominating_prune=dominating_prune,
    )
    if _match_body_can_reach_loop_step(level, opt, context.next_function_name, on_inner_failure):
        lines.append(f"{iter_indent}  if (it == {it_start}) {{")
        lines.append(f"{iter_indent}    break;")
        lines.append(f"{iter_indent}  }}")
        lines.append(f"{iter_indent}  --it;")
    lines.append(f"{iter_indent}}}")

    if opt.is_optional:
        lines.append(f"{indent}}}")  # close non-empty guard

    if opt.is_optional:
        lines.append("  }")  # close present block

    _emit_scan_tail(lines, context, opt, plan)

    lines.append("}")
    return lines


def _emit_scan_tail(
    lines: list[str],
    context: _SearchFunctionContext,
    opt: _OptionalContext,
    plan: CodegenPlan,
) -> None:
    """Emit the tail section after a scan loop's closing brace.

    Args:
        lines: Output list of C++ source lines.
        context: Search-function context for the current generated function.
        opt: Optional-input context.
        plan: Code generation plan.

    Handles four combinations of ``is_optional`` x ``use_bsf``:

    ``is_optional AND use_bsf``
        BSF (best-so-far) is active (downstream optionals or objectives).
        The absent path might produce a fuller alignment than
        the present path.  Return the present result early only if it
        achieved maximum fullness; otherwise try the absent path and
        compare via best-so-far.  When ``next_level_index`` is None
        (innermost level), the absent path always succeeds; present
        always wins on fullness so it is returned if available.

    ``is_optional AND NOT use_bsf``
        No BSF: the present path always has equal or
        higher fullness than absent.  Return immediately for a present
        match; fall through to absent otherwise.

    ``NOT is_optional AND use_bsf``
        Required input with BSF: return best-so-far
        if available, else failure.

    ``NOT is_optional AND NOT use_bsf``
        Simple first-feasible: failure if no match is available (the loop
        already returned on first success).
    """
    if opt.is_optional and opt.use_bsf:
        restore_fn = _restore_best_function_name(context)
        suffix_full_expr = _optionals_present_expr(plan, context, "best->presence")
        lines.append(f"  if (best.has_value() && {suffix_full_expr}) {{")
        lines.append(f"    {restore_fn}(*best, *candidate);")
        lines.append("    return jewels::success;")
        lines.append("  }")
        lines.append("  // Path 2: absent — compare with present via best-so-far.")
        _emit_optional_absent_assignments(lines, opt.input_name, opt.absent_members, "  ")
        if context.next_function_name is not None:
            lines.append(f"  if (jewels::ok({context.next_function_name}(jewels::InOut{{*candidate}}, views))) {{")
            _emit_best_update(
                lines,
                "    ",
                context=context,
                break_on_max_optionals=False,
                break_on_full_expr=_optionals_present_expr(plan, context, "best->presence"),
            )
            lines.append("  }")
            lines.append("  if (best.has_value()) {")
            lines.append(f"    {restore_fn}(*best, *candidate);")
            lines.append("    return jewels::success;")
            lines.append("  }")
            lines.append("  return jewels::failure;")
        else:
            lines.append("  if (best.has_value()) {")
            lines.append(f"    {restore_fn}(*best, *candidate);")
            lines.append("  }")
            lines.append("  return jewels::success;")
    elif opt.is_optional:
        lines.append("  // Path 2: absent.")
        _emit_optional_absent_assignments(lines, opt.input_name, opt.absent_members, "  ")
        if context.next_function_name is not None:
            lines.append(f"  return {context.next_function_name}(jewels::InOut{{*candidate}}, views);")
        else:
            lines.append("  return jewels::success;")
    elif opt.use_bsf:
        restore_fn = _restore_best_function_name(context)
        lines.append("  if (best.has_value()) {")
        lines.append(f"    {restore_fn}(*best, *candidate);")
        lines.append("    return jewels::success;")
        lines.append("  }")
        lines.append("  return jewels::failure;")
    else:
        lines.append("  return jewels::failure;")


def _emit_match_body(  # noqa: PLR0913 # Many parameters mitigated by kwonly args
    *,
    lines: list[str],
    level: CodegenLevel,
    context: _SearchFunctionContext,
    candidate_it_expr: str,
    opt: _OptionalContext,
    plan: CodegenPlan,
    dominating_prune: _DominatingObjectivePrune | None,
    indent: str = "  ",
    on_inner_failure: list[str] | None = None,
    on_inner_success_extra: list[str] | None = None,
    break_on_max_optionals_override: bool | None = None,
) -> None:
    """Emit the assign + optional-recurse + return/capture block.

    Common to NEAREST and reverse scan where the control flow wraps the
    body inside an ``if (check_constraints)`` block rather than using
    ``continue``.

    When ``opt.use_bsf`` is True, instead of returning immediately on
    success, the code compares against the current best and records
    a new best.  This requires ``best`` in scope.
    """
    assert not (on_inner_success_extra and not opt.use_bsf), (
        "on_inner_success_extra requires BSF tracking (use_bsf=True)"
    )

    member = _iter_member_name(level.join_level.input)
    has_checker = _has_remaining_constraint_checks(level, opt)
    idx = level.level_index

    def _emit_on_success(ind: str) -> None:
        if opt.use_bsf:
            _emit_best_update(
                lines,
                ind,
                context=context,
                break_on_max_optionals=(
                    not opt.has_downstream_obj_refs
                    if break_on_max_optionals_override is None
                    else break_on_max_optionals_override
                ),
                break_on_full_expr=_optionals_present_expr(plan, context, "best->presence"),
            )
        else:
            lines.append(f"{ind}return jewels::success;")
        if on_inner_success_extra:
            lines.extend(f"{ind}{line}" for line in on_inner_success_extra)

    def _emit_inner_call(ind: str) -> None:
        if context.next_function_name is not None:
            lines.append(f"{ind}if (jewels::ok({context.next_function_name}(jewels::InOut{{*candidate}}, views))) {{")
            _emit_on_success(f"{ind}  ")
            if on_inner_failure:
                lines.append(f"{ind}}} else {{")
                lines.extend(f"{ind}  {line}" for line in on_inner_failure)
            lines.append(f"{ind}}}")
        else:
            _emit_on_success(ind)

    if has_checker:
        lines.append(f"{indent}if (check_constraints_{idx}(*candidate, {candidate_it_expr})) {{")
        _emit_candidate_assignment(lines, opt, member, candidate_it_expr, f"{indent}  ")
        _emit_dominating_objective_prune(lines, plan, context, dominating_prune, f"{indent}  ")
        _emit_inner_call(f"{indent}  ")
        lines.append(f"{indent}}}")
    else:
        _emit_candidate_assignment(lines, opt, member, candidate_it_expr, indent)
        _emit_dominating_objective_prune(lines, plan, context, dominating_prune, indent)
        _emit_inner_call(indent)


def _match_body_can_reach_loop_step(
    level: CodegenLevel,
    opt: _OptionalContext,
    next_function_name: str | None,
    on_inner_failure: list[str] | None,
) -> bool:
    """Return whether scan-loop step code can be reached after a match body."""
    if _has_remaining_constraint_checks(level, opt) or opt.use_bsf:
        return True
    if next_function_name is None:
        return False
    return on_inner_failure != ["return jewels::failure;"]


def _nearest_ref_info(
    level: CodegenLevel,
    optional_input_names: frozenset[str],
) -> tuple[str, str, str, str, str | None]:
    """Return ``(ref_expr, target_getter, ref_input, ref_field, ref_present_guard)`` for a NEAREST level.

    Reads the ``nearest_reference`` attached during objective analysis.
    """
    nr = level.join_level.nearest_reference
    assert nr is not None, f"NEAREST at level {level.level_index} has no nearest_reference"
    ref_input = base_input_name(nr.reference.input_name)
    ref_field = nr.reference.field_name
    target_field = nr.target.field_name
    assert isinstance(ref_field, str), f"Expected str for ref_field, got {type(ref_field)}"
    assert isinstance(target_field, str), f"Expected str for target_field, got {type(target_field)}"

    ref_expr = _accessor_cpp(
        nr.reference.input_name,
        ref_field,
        via_candidate=True,
        inout=True,
        optional=ref_input in optional_input_names,
    )
    target_getter = _field_getter(target_field)
    ref_present_guard = _candidate_optional_member_presence_expr(
        nr.reference.input_name,
        optional_input_names,
        "*candidate",
    )
    return ref_expr, target_getter, ref_input, ref_field, ref_present_guard


def _generate_nearest_scan(  # noqa: PLR0915, C901, PLR0912 # Many lines/branches because of embedded codegen
    plan: CodegenPlan,
    level: CodegenLevel,
    context: _SearchFunctionContext,
    opt: _OptionalContext,
) -> list[str]:
    """Generate ``search_level_N()`` — bidirectional outward scan from reference.

    Used for NEAREST.  Binary-searches to the reference point, then alternates
    left/right by distance.
    """
    idx = level.level_index
    current_input = base_input_name(level.join_level.input)
    view = _view_expr(current_input)
    nearest_fallback_uses_bsf = _nearest_optional_reference_fallback_uses_bsf(plan, context)
    ref_expr, target_getter, ref_input, ref_field, ref_present_guard = _nearest_ref_info(
        level,
        plan.optional_input_names,
    )

    lines: list[str] = []
    lines.append(f"// Level {idx}: {current_input} (nearest: distance-ordered from {ref_input}.{ref_field})")
    lines.append(f"static jewels::BinaryOutcome {context.function_name}(")
    lines.append("    jewels::InOut<CandidateAlignment> candidate,")
    lines.append("    const InputViews& views)")
    lines.append("{")

    if opt.use_bsf:
        lines.append(f"  std::optional<{_best_type_name(context)}> best;")
        lines.append("")

    _emit_optional_batch_short_circuit(lines, context.next_function_name, opt)

    indent = "  "
    if opt.is_optional:
        lines.append("  // Path 1: present.")
        lines.append("  {")
        indent = "    "

    lines.append(f"{indent}const auto& view = {view};")
    lines.append("")

    it_start, it_end, preamble_has_empty_range_guard = _emit_windowing_preamble(
        lines,
        level,
        opt,
        indent=indent,
    )

    # No explicit empty-range guard needed for NEAREST: lower_bound on
    # an empty range returns it_end, so mid == it_end, both left and
    # right start exhausted, and the while-loop exits immediately.
    # Preamble emits the empty-range guard only for required levels;
    # optional levels omit it to allow fall-through to the absent path.
    if not preamble_has_empty_range_guard and not opt.is_optional:
        lines.append(f"{indent}if ({it_start} >= {it_end}) {{")
        lines.append(f"{indent}  return jewels::failure;")
        lines.append(f"{indent}}}")
        lines.append("")

    nearest_guard_indent: str | None = None
    if opt.is_optional:
        nearest_guard_indent = indent
        lines.append(f"{nearest_guard_indent}if ({it_start} < {it_end}) {{")
        indent = nearest_guard_indent + "  "

    nearest_ref_guard_indent: str | None = None
    if ref_present_guard is not None:
        nearest_ref_guard_indent = indent
        lines.append(f"{nearest_ref_guard_indent}if ({ref_present_guard}) {{")
        indent = nearest_ref_guard_indent + "  "

    lines.append(f"{indent}const auto ref = {ref_expr};")
    lines.append("")
    lines.append(f"{indent}// Binary search to reference point within windowed range.")
    lines.append(f"{indent}auto mid = std::lower_bound({it_start}, {it_end}, ref,")
    lines.append(f"{indent}    [](const auto& msg, const auto& r) {{")
    lines.append(f"{indent}      return msg.{target_getter} < r;")
    lines.append(f"{indent}    }});")
    lines.append("")
    lines.append(f"{indent}auto right = mid;")
    lines.append(f"{indent}auto left = (mid != {it_start})")
    lines.append(f"{indent}    ? std::prev(mid)")
    lines.append(f"{indent}    : {it_end};  // sentinel: left side exhausted")
    lines.append("")
    right_cutoffs = level.direction.right_cutoffs
    left_cutoffs = level.direction.left_cutoffs
    opt_right_cutoffs = level.direction.optional_right_cutoffs
    opt_left_cutoffs = level.direction.optional_left_cutoffs
    cutoff_views = _collect_cutoff_views(right_cutoffs, left_cutoffs, opt_right_cutoffs, opt_left_cutoffs)
    if cutoff_views:
        _emit_cutoff_view_decls(lines, cutoff_views, indent)
        lines.append("")

    opt_flags: list[str] = []
    if opt_right_cutoffs:
        opt_flags.extend(_emit_optional_cutoff_flags(lines, opt_right_cutoffs, indent, direction="right"))
    if opt_left_cutoffs:
        opt_flags.extend(_emit_optional_cutoff_flags(lines, opt_left_cutoffs, indent, direction="left"))
    if opt_flags:
        lines.append("")

    lines.append(f"{indent}while (right != {it_end} || left != {it_end}) {{")
    lines.append(f"{indent}  bool pick_right = false;")
    lines.append(f"{indent}  if (right != {it_end} && left != {it_end}) {{")
    lines.append(f"{indent}    // Pick whichever pointer is closer to the reference.")
    lines.append(f"{indent}    // Equal distance: right (later value) wins.")
    # TODO(OI-3731): This subtraction assumes signed types (e.g. chrono::nanoseconds); add validation.
    lines.append(f"{indent}    pick_right = ((*right).{target_getter} - ref")
    lines.append(f"{indent}                  <= ref - (*left).{target_getter});")
    lines.append(f"{indent}  }} else {{")
    lines.append(f"{indent}    pick_right = (right != {it_end});")
    lines.append(f"{indent}  }}")
    lines.append("")

    # Per-side cutoffs: when the pointer being picked exceeds its bound,
    # exhaust that side and re-evaluate the loop condition.
    if right_cutoffs:
        lines.append(f"{indent}  if (pick_right) {{")
        _emit_right_cutoffs(
            lines, right_cutoffs, f"{indent}    ", iterator_var="right", action=f"right = {it_end}; continue"
        )
        lines.append(f"{indent}  }} else {{" if left_cutoffs else f"{indent}  }}")
    if left_cutoffs:
        if not right_cutoffs:
            lines.append(f"{indent}  if (!pick_right) {{")
        _emit_left_cutoffs(
            lines, left_cutoffs, f"{indent}    ", iterator_var="left", action=f"left = {it_end}; continue"
        )
        lines.append(f"{indent}  }}")

    # Per-side optional cutoffs: set feasibility flags
    if opt_right_cutoffs:
        lines.append(f"{indent}  if (pick_right) {{")
        _emit_optional_cutoff_checks(
            lines,
            opt_right_cutoffs,
            f"{indent}    ",
            direction="right",
            iterator_var="right",
            flag_direction="right",
        )
        lines.append(f"{indent}  }} else {{" if opt_left_cutoffs else f"{indent}  }}")
    if opt_left_cutoffs:
        if not opt_right_cutoffs:
            lines.append(f"{indent}  if (!pick_right) {{")
        _emit_optional_cutoff_checks(
            lines,
            opt_left_cutoffs,
            f"{indent}    ",
            direction="left",
            iterator_var="left",
            flag_direction="left",
        )
        lines.append(f"{indent}  }}")
    if opt_flags and opt.use_bsf:
        _emit_optional_cutoff_break(lines, opt_flags, f"{indent}  ")

    lines.append(f"{indent}  const auto candidate_it = pick_right ? right : left;")
    lines.append("")

    # Compute per-half tighten flags for NEAREST.
    # Right half scans forward: tightens when constraint_direction == ALL_MIN.
    # Left half scans backward: tightens when constraint_direction == ALL_MAX.
    direction = level.direction
    req_tighten_right = direction.required_constraint_direction == Direction.ALL_MIN
    req_tighten_left = direction.required_constraint_direction == Direction.ALL_MAX
    opt_tighten_right = direction.optional_constraint_direction == Direction.ALL_MIN
    opt_tighten_left = direction.optional_constraint_direction == Direction.ALL_MAX

    # Build on_inner_failure for per-half req_tighten (pointer exhaustion on failure).
    on_inner_failure: list[str] | None = None
    if context.next_function_name is not None and (req_tighten_right or req_tighten_left):
        on_inner_failure_lines: list[str] = []
        if req_tighten_right and req_tighten_left:
            # Both halves tighten: exhaust whichever side was picked.
            on_inner_failure_lines.append(f"if (pick_right) {{ right = {it_end}; }}")
            on_inner_failure_lines.append(f"else {{ left = {it_end}; }}")
            on_inner_failure_lines.append("continue;")
        elif req_tighten_right:
            on_inner_failure_lines.append(f"if (pick_right) {{ right = {it_end}; continue; }}")
        else:
            on_inner_failure_lines.append(f"if (!pick_right) {{ left = {it_end}; continue; }}")
        on_inner_failure = on_inner_failure_lines

    # Build on_inner_success_extra for per-half opt_tighten (pointer exhaustion on success).
    on_inner_success_extra: list[str] | None = None
    if opt_tighten_right or opt_tighten_left:
        extra_lines: list[str] = []
        if opt_tighten_right and opt_tighten_left:
            extra_lines.append("// Optionals tighten in both directions: first match from each half is fullest.")
            extra_lines.append(f"if (pick_right) {{ right = {it_end}; }}")
            extra_lines.append(f"else {{ left = {it_end}; }}")
            extra_lines.append("continue;")
        elif opt_tighten_right:
            extra_lines.append(f"if (pick_right) {{ right = {it_end}; continue; }}")
        else:
            extra_lines.append(f"if (!pick_right) {{ left = {it_end}; continue; }}")
        on_inner_success_extra = extra_lines

    _emit_match_body(
        lines=lines,
        level=level,
        context=context,
        candidate_it_expr="candidate_it",
        opt=opt,
        plan=plan,
        dominating_prune=None,
        indent=f"{indent}  ",
        on_inner_failure=on_inner_failure,
        on_inner_success_extra=on_inner_success_extra,
    )
    if _match_body_can_reach_loop_step(level, opt, context.next_function_name, on_inner_failure):
        lines.append("")
        lines.append(f"{indent}  if (pick_right) {{")
        lines.append(f"{indent}    ++right;")
        lines.append(f"{indent}  }} else {{")
        lines.append(f"{indent}    left = (left != {it_start})")
        lines.append(f"{indent}        ? std::prev(left)")
        lines.append(f"{indent}        : {it_end};")
        lines.append(f"{indent}  }}")
    lines.append(f"{indent}}}")

    if ref_present_guard is not None:
        assert nearest_ref_guard_indent is not None
        lines.append(f"{nearest_ref_guard_indent}}} else {{")
        if nearest_fallback_uses_bsf:
            lines.append(
                f"{nearest_ref_guard_indent}  // Reference optional is absent; scan feasible candidates and compare active objectives."
            )
        else:
            lines.append(
                f"{nearest_ref_guard_indent}  // Reference optional is absent, so the nearest objective is inactive."
            )
        lines.append(f"{nearest_ref_guard_indent}  for (auto it = {it_start}; it < {it_end}; ++it) {{")
        _emit_match_body(
            lines=lines,
            level=level,
            context=context,
            candidate_it_expr="it",
            opt=opt,
            plan=plan,
            dominating_prune=None,
            indent=f"{nearest_ref_guard_indent}    ",
            break_on_max_optionals_override=False if nearest_fallback_uses_bsf else None,
        )
        lines.append(f"{nearest_ref_guard_indent}  }}")
        lines.append(f"{nearest_ref_guard_indent}}}")

    if opt.is_optional:
        assert nearest_guard_indent is not None
        lines.append(f"{nearest_guard_indent}}}")
        lines.append("  }")  # close present block

    _emit_scan_tail(lines, context, opt, plan)

    lines.append("}")
    return lines


def _search_level_tightening(level: CodegenLevel) -> tuple[bool, bool]:
    """Return whether required and optional constraints tighten for a level."""
    # Compute req_tighten and opt_tighten from direction analysis.
    # For NEAREST, per-half flags are computed locally inside the scan
    # generator — context flags stay False.
    direction = level.direction
    search_type = level.join_level.search_type
    req_tighten = False
    opt_tighten = False

    match search_type:
        case SearchType.FIRST_IN_RANGE:
            req_tighten = (
                direction.required_constraint_direction == Direction.ALL_MIN
                and direction.objective_direction == Direction.ALL_MIN
            )
            opt_tighten = (
                direction.optional_constraint_direction == Direction.ALL_MIN
                and direction.objective_direction == Direction.ALL_MIN
            )
        case SearchType.LAST_IN_RANGE:
            req_tighten = (
                direction.required_constraint_direction == Direction.ALL_MAX
                and direction.objective_direction == Direction.ALL_MAX
            )
            opt_tighten = (
                direction.optional_constraint_direction == Direction.ALL_MAX
                and direction.objective_direction == Direction.ALL_MAX
            )
        case SearchType.NEAREST | SearchType.ANY_MATCH | SearchType.ENUMERATE | SearchType.EXACT_MATCH:
            pass  # Both flags stay False.

    return req_tighten, opt_tighten


def _level_uses_bsf(
    level: CodegenLevel,
    has_downstream_obj_refs: bool,
    *,
    has_downstream_optionals: bool | None = None,
    nearest_optional_reference_fallback_uses_bsf: bool = False,
) -> bool:
    """Return whether a search level needs best-so-far tracking."""
    _, opt_tighten = _search_level_tightening(level)
    downstream_optionals = (
        level.has_downstream_optionals if has_downstream_optionals is None else has_downstream_optionals
    )
    return (
        has_downstream_obj_refs
        or nearest_optional_reference_fallback_uses_bsf
        or (downstream_optionals and not opt_tighten)
    )


def _optional_context_for_level(  # noqa: PLR0913 # Many parameters mitigated by kwonly args
    *,
    level: CodegenLevel,
    optional_input_names: frozenset[str],
    batch_input_names: frozenset[str],
    earlier_level_inputs: frozenset[InputSelector],
    has_downstream_obj_refs: bool,
    has_downstream_optionals: bool | None = None,
    nearest_optional_reference_fallback_uses_bsf: bool = False,
) -> _OptionalContext:
    """Return optional-input codegen context for a search level."""
    current_input = base_input_name(level.join_level.input)
    req_tighten, opt_tighten = _search_level_tightening(level)

    # BSF tracks best-so-far when candidates need comparison.
    # Base case: downstream optionals require fullness comparison.
    # Cross-level objectives (referencing downstream inputs) also need BSF
    # because the achievable objective residual is non-monotonic in scan
    # direction (sawtooth pattern from varying inner-level results).
    # Same-level objectives are already handled by scan direction (e.g.
    # LAST_IN_RANGE for maximize), so they don't require BSF.
    use_bsf = _level_uses_bsf(
        level,
        has_downstream_obj_refs,
        has_downstream_optionals=has_downstream_optionals,
        nearest_optional_reference_fallback_uses_bsf=nearest_optional_reference_fallback_uses_bsf,
    )

    is_optional = current_input in optional_input_names
    absent_members: tuple[str, ...] = ()
    required_present_members: tuple[str, ...] = ()
    current_selector = level.join_level.input
    if is_optional:
        if current_input in batch_input_names:
            sibling_selectors: tuple[InputSelector, ...] = (FirstInBatch(current_input), LastInBatch(current_input))
            absent_members = tuple(
                _iter_member_name(selector)
                for selector in sibling_selectors
                if selector == current_selector or selector not in earlier_level_inputs
            )
        else:
            absent_members = (_iter_member_name(current_selector),)
    if is_optional and current_input in batch_input_names:
        sibling_selectors: tuple[InputSelector, ...] = (FirstInBatch(current_input), LastInBatch(current_input))
        required_present_members = tuple(
            _iter_member_name(selector)
            for selector in sibling_selectors
            if selector != current_selector and selector in earlier_level_inputs
        )

    return _OptionalContext(
        input_name=current_input,
        is_optional=is_optional,
        total_optionals=len(optional_input_names),
        use_bsf=use_bsf,
        req_tighten=req_tighten,
        opt_tighten=opt_tighten,
        has_downstream_obj_refs=has_downstream_obj_refs,
        absent_members=absent_members,
        required_present_members=required_present_members,
    )


def _generate_search_level(
    *,
    plan: CodegenPlan,
    level: CodegenLevel,
    context: _SearchFunctionContext,
    opt: _OptionalContext,
) -> list[str]:
    """Dispatch to the appropriate scan generator based on search type."""
    match level.join_level.search_type:
        case SearchType.NEAREST:
            return _generate_nearest_scan(plan, level, context, opt)
        case SearchType.LAST_IN_RANGE:
            return _generate_reverse_scan(plan, level, context, opt)
        case SearchType.FIRST_IN_RANGE | SearchType.ANY_MATCH | SearchType.ENUMERATE | SearchType.EXACT_MATCH:
            return _generate_forward_scan(plan, level, context, opt)


def _partition_function_name(partition: PartitionPoint) -> str:
    """Return the generated partition function name."""
    return f"search_partition_{partition.start_level_index}"


def _component_function_name(partition_id: int, component_id: int, level_index: int) -> str:
    """Return the generated component search function name."""
    return f"search_component_{partition_id}_{component_id}_{level_index}"


def _root_scope(plan: CodegenPlan) -> tuple[int, ...]:
    """Return the root search scope."""
    return tuple(level.level_index for level in plan.levels)


def _partition_for_scope(plan: CodegenPlan, scope_level_indices: tuple[int, ...]) -> PartitionPoint | None:
    """Return the partition point for an exact residual scope, if available."""
    if plan.partition_plan is None:
        return None
    return plan.partition_plan.partition_for_scope(scope_level_indices)


def _scope_bind_function_name(
    partition_id: int | None,
    component_id: int | None,
    level_index: int,
) -> str:
    """Return the generated bind-search function name for a scope level."""
    if partition_id is None:
        return f"search_level_{level_index}"
    assert component_id is not None
    return _component_function_name(partition_id, component_id, level_index)


def _scope_entry_function_name(
    plan: CodegenPlan,
    scope_level_indices: tuple[int, ...],
    partition_id: int | None,
    component_id: int | None,
) -> str | None:
    """Return the generated function that solves a residual scope."""
    if not scope_level_indices:
        return None
    partition = _partition_for_scope(plan, scope_level_indices)
    if partition is not None:
        return _partition_function_name(partition)
    return _scope_bind_function_name(partition_id, component_id, scope_level_indices[0])


def _append_search_contexts_for_scope(
    *,
    builder: _ScopeSearchContextBuilder,
    scope_level_indices: tuple[int, ...],
    bound_level_indices: frozenset[int],
    partition_id: int | None,
    component_id: int | None,
) -> None:
    """Append generated bind-search contexts for a possibly partitioned scope."""
    if not scope_level_indices or scope_level_indices in builder.visited_scopes:
        return
    builder.visited_scopes.add(scope_level_indices)

    partition = _partition_for_scope(builder.plan, scope_level_indices)
    if partition is not None:
        component_bound_level_indices = set(bound_level_indices)
        for component in partition.components:
            _append_search_contexts_for_scope(
                builder=builder,
                scope_level_indices=component.level_indices,
                bound_level_indices=frozenset(component_bound_level_indices),
                partition_id=partition.partition_id,
                component_id=component.component_id,
            )
            component_bound_level_indices.update(component.level_indices)
        return

    for position, level_index in enumerate(scope_level_indices):
        suffix_level_indices = scope_level_indices[position:]
        tail_level_indices = suffix_level_indices[1:]
        current_bound_level_indices = frozenset((*bound_level_indices, *scope_level_indices[:position]))
        builder.contexts.append(
            _SearchFunctionContext(
                function_name=_scope_bind_function_name(partition_id, component_id, level_index),
                level_index=level_index,
                next_function_name=_scope_entry_function_name(
                    builder.plan,
                    tail_level_indices,
                    partition_id,
                    component_id,
                ),
                component_level_indices=scope_level_indices,
                suffix_level_indices=suffix_level_indices,
                bound_level_indices=tuple(sorted(current_bound_level_indices)),
                partition_id=partition_id,
                component_id=component_id,
            )
        )
        if _partition_for_scope(builder.plan, tail_level_indices) is not None:
            _append_search_contexts_for_scope(
                builder=builder,
                scope_level_indices=tail_level_indices,
                bound_level_indices=frozenset((*current_bound_level_indices, level_index)),
                partition_id=partition_id,
                component_id=component_id,
            )
            return


def _generated_search_contexts(plan: CodegenPlan) -> tuple[_SearchFunctionContext, ...]:
    """Return every generated search-level context."""
    builder = _ScopeSearchContextBuilder(plan=plan, contexts=[], visited_scopes=set())
    _append_search_contexts_for_scope(
        builder=builder,
        scope_level_indices=_root_scope(plan),
        bound_level_indices=frozenset(),
        partition_id=None,
        component_id=None,
    )
    return tuple(builder.contexts)


def _generated_partitions(plan: CodegenPlan) -> tuple[PartitionPoint, ...]:
    """Return every generated partition point."""
    if plan.partition_plan is None:
        return ()
    return plan.partition_plan.partitions


def _has_downstream_optionals_for_context(plan: CodegenPlan, context: _SearchFunctionContext) -> bool:
    """Return whether optional presence can still vary after the current level in a context."""
    suffix_tail = context.suffix_level_indices[1:]
    return any(
        base_input_name(plan.levels[index].join_level.input) in plan.optional_input_names for index in suffix_tail
    )


def _earlier_inputs_for_context(
    plan: CodegenPlan,
    context: _SearchFunctionContext,
) -> frozenset[InputSelector]:
    """Return inputs already bound before a generated search function executes."""
    return frozenset(plan.levels[index].join_level.input for index in context.bound_level_indices)


def _generate_search_function_declaration(function_name: str) -> list[str]:
    """Generate a forward declaration for a search function."""
    return [
        f"static jewels::BinaryOutcome {function_name}(",
        "    jewels::InOut<CandidateAlignment> candidate,",
        "    const InputViews& views);",
        "",
    ]


def _generate_search_function_declarations(
    search_contexts: tuple[_SearchFunctionContext, ...],
    partitions: tuple[PartitionPoint, ...],
) -> list[str]:
    """Generate search forward declarations for partitioned plans."""
    lines: list[str] = []
    for context in search_contexts:
        lines.extend(_generate_search_function_declaration(context.function_name))
    for partition in partitions:
        lines.extend(_generate_search_function_declaration(_partition_function_name(partition)))
    return lines


def _optional_contexts_for_search_contexts(
    plan: CodegenPlan,
    contexts: tuple[_SearchFunctionContext, ...],
) -> dict[str, _OptionalContext]:
    """Compute optional-input codegen context for each generated search function."""
    result: dict[str, _OptionalContext] = {}

    for context in contexts:
        result[context.function_name] = _optional_context_for_level(
            level=plan.levels[context.level_index],
            optional_input_names=plan.optional_input_names,
            batch_input_names=plan.batch_input_names,
            earlier_level_inputs=_earlier_inputs_for_context(plan, context),
            has_downstream_obj_refs=_has_downstream_objective_refs_for_context(plan, context),
            has_downstream_optionals=_has_downstream_optionals_for_context(plan, context),
            nearest_optional_reference_fallback_uses_bsf=_nearest_optional_reference_fallback_uses_bsf(plan, context),
        )
    return result


def _generate_partition_function(plan: CodegenPlan, partition: PartitionPoint) -> list[str]:
    """Generate a function that composes independent component searches."""
    lines: list[str] = []
    lines.append(f"static jewels::BinaryOutcome {_partition_function_name(partition)}(")
    lines.append("    jewels::InOut<CandidateAlignment> candidate,")
    lines.append("    const InputViews& views)")
    lines.append("{")
    for component in partition.components:
        entry_function = _scope_entry_function_name(
            plan,
            component.level_indices,
            partition.partition_id,
            component.component_id,
        )
        assert entry_function is not None
        lines.append(f"  if (jewels::fails({entry_function}(jewels::InOut{{*candidate}}, views))) {{")
        lines.append("    return jewels::failure;")
        lines.append("  }")
    lines.append("  return jewels::success;")
    lines.append("}")
    return lines


def _collect_generated_search_artifacts(plan: CodegenPlan) -> _GeneratedSearchArtifacts:
    """Collect reusable artifacts for top-level search code generation."""
    optional_input_names = [name for name in plan.input_names if name in plan.optional_input_names]
    search_contexts = _generated_search_contexts(plan)
    partitions = _generated_partitions(plan)
    optional_contexts = _optional_contexts_for_search_contexts(plan, search_contexts)
    flat_best_contexts = tuple(
        context
        for context in search_contexts
        if not context.is_component and optional_contexts[context.function_name].use_bsf
    )
    component_best_contexts = tuple(
        context
        for context in search_contexts
        if context.is_component and optional_contexts[context.function_name].use_bsf
    )
    optional_presence_contexts = tuple(
        context
        for context in (*flat_best_contexts, *component_best_contexts)
        if _context_uses_optional_presence_helper(plan, context, optional_contexts[context.function_name])
    )
    suffix_optional_presence_level_indices = frozenset(
        context.level_index for context in optional_presence_contexts if not context.is_component
    )
    component_optional_presence_contexts = tuple(
        context for context in optional_presence_contexts if context.is_component
    )

    used_objective_indices: set[int] = set()
    for context in (*flat_best_contexts, *component_best_contexts):
        used_objective_indices.update(_objective_indices_for_context(plan, context))
        if context.is_component:
            continue
        prune = _dominating_objective_prune_for_level(
            plan,
            plan.levels[context.level_index],
            optional_contexts[context.function_name],
        )
        if prune is not None:
            used_objective_indices.add(prune.objective_index)

    return _GeneratedSearchArtifacts(
        optional_input_names=optional_input_names,
        search_contexts=search_contexts,
        partitions=partitions,
        optional_contexts=optional_contexts,
        flat_best_contexts=flat_best_contexts,
        best_level_indices=frozenset(context.level_index for context in flat_best_contexts),
        component_best_contexts=component_best_contexts,
        suffix_optional_presence_level_indices=suffix_optional_presence_level_indices,
        component_optional_presence_contexts=component_optional_presence_contexts,
        used_objective_indices=frozenset(used_objective_indices),
    )


def _emit_search_state_types(
    lines: list[str],
    plan: CodegenPlan,
    dial_type: str,
    compiler_context: CompilerContext,
    artifacts: _GeneratedSearchArtifacts,
) -> None:
    """Emit candidate, presence, and accessor support types."""
    reuse_map: dict[str, bool] = {base_input_name(lv.join_level.input): lv.reuse for lv in plan.levels}

    lines.extend(_generate_candidate_iterator_aliases(plan, dial_type))
    lines.append("")
    lines.extend(_generate_input_views_struct(plan, dial_type, reuse_map))
    lines.append("")
    lines.extend(_generate_candidate_suffix_structs(plan))
    lines.extend(_generate_presence_state(artifacts.optional_input_names, compiler_context))
    lines.append("")
    lines.extend(_generate_candidate_struct(plan))
    lines.append("")
    lines.extend(_generate_candidate_member_accessors(plan))
    lines.extend(_generate_presence_helpers(plan, artifacts.optional_input_names))
    if artifacts.optional_input_names:
        lines.append("")


def _emit_best_search_helpers(
    lines: list[str],
    plan: CodegenPlan,
    artifacts: _GeneratedSearchArtifacts,
) -> None:
    """Emit objective and best-so-far helper functions."""
    lines.extend(_generate_objective_helpers(plan, artifacts.used_objective_indices))
    lines.extend(_generate_best_suffix_structs(plan, artifacts.best_level_indices))
    lines.extend(_generate_component_best_structs(plan, artifacts.component_best_contexts))
    lines.extend(_generate_best_suffix_helpers(plan, artifacts.flat_best_contexts))
    lines.extend(_generate_component_best_helpers(plan, artifacts.component_best_contexts))
    lines.extend(_generate_suffix_optional_presence_helpers(plan, artifacts.suffix_optional_presence_level_indices))
    lines.extend(_generate_component_optional_presence_helpers(plan, artifacts.component_optional_presence_contexts))
    lines.append("")


def _emit_constraint_checkers(
    lines: list[str],
    plan: CodegenPlan,
    artifacts: _GeneratedSearchArtifacts,
) -> None:
    """Emit generated constraint checker functions."""
    for level in reversed(plan.levels):
        checker_context = next(
            context for context in artifacts.search_contexts if context.level_index == level.level_index
        )
        checker = _generate_constraint_checker(
            level,
            plan.batch_input_names,
            artifacts.optional_contexts[checker_context.function_name],
        )
        if checker:
            lines.extend(checker)
            lines.append("")


def _emit_search_functions(
    lines: list[str],
    plan: CodegenPlan,
    artifacts: _GeneratedSearchArtifacts,
) -> None:
    """Emit declarations, component functions, partitions, and flat functions."""
    if artifacts.partitions:
        lines.extend(_generate_search_function_declarations(artifacts.search_contexts, artifacts.partitions))

    _emit_constraint_checkers(lines, plan, artifacts)

    for context in reversed(tuple(context for context in artifacts.search_contexts if context.is_component)):
        level = plan.levels[context.level_index]
        lines.extend(
            _generate_search_level(
                plan=plan,
                level=level,
                context=context,
                opt=artifacts.optional_contexts[context.function_name],
            ),
        )
        lines.append("")

    for partition in reversed(artifacts.partitions):
        lines.extend(_generate_partition_function(plan, partition))
        lines.append("")

    for context in reversed(tuple(context for context in artifacts.search_contexts if not context.is_component)):
        level = plan.levels[context.level_index]
        lines.extend(
            _generate_search_level(
                plan=plan,
                level=level,
                context=context,
                opt=artifacts.optional_contexts[context.function_name],
            ),
        )
        lines.append("")


def _generate_publish_fn(
    plan: CodegenPlan,
    dial_type: str,
) -> list[str]:
    """Generate ``publish_alignment()``."""
    lines: list[str] = []
    lines.append("static void publish_alignment(")
    lines.append(f"    {dial_type}& dial,")
    lines.append("    const CandidateAlignment& candidate)")
    lines.append("{")
    lines.append("  auto& pub = dial.get_outputs().get_alignment();")
    lines.append("  auto& inputs = dial.get_inputs();")

    for name in plan.input_names:
        if name in plan.optional_input_names and name in plan.batch_input_names:
            presence = _candidate_optional_presence_expr(name, plan.batch_input_names, "candidate")
            lo_member = _candidate_member_call(f"{name}_lo_it", "candidate")
            hi_member = _candidate_member_call(f"{name}_hi_it", "candidate")
            lines.append(f"  pub.message().set_has_{name}({presence});")
            lines.append(f"  if ({presence}) {{")
            lines.append(f"    pub.message().set_{name}_begin_seq(")
            lines.append(f"        inputs.{_input_getter(name)}.get_sequence_number(*{lo_member}));")
            lines.append(f"    pub.message().set_{name}_end_seq(")
            lines.append(f"        inputs.{_input_getter(name)}.get_sequence_number(*{hi_member}));")
            lines.append("  }")
        elif name in plan.optional_input_names:
            presence = _candidate_optional_presence_expr(name, plan.batch_input_names, "candidate")
            member = _candidate_member_call(f"{name}_it", "candidate")
            lines.append(f"  pub.message().set_has_{name}({presence});")
            lines.append(f"  if ({presence}) {{")
            lines.append(f"    pub.message().set_{name}_seq(")
            lines.append(f"        inputs.{_input_getter(name)}.get_sequence_number(*{member}));")
            lines.append("  }")
        elif name in plan.batch_input_names:
            lo_member = _candidate_member_call(f"{name}_lo_it", "candidate")
            hi_member = _candidate_member_call(f"{name}_hi_it", "candidate")
            lines.append(f"  pub.message().set_{name}_begin_seq(")
            lines.append(f"      inputs.{_input_getter(name)}.get_sequence_number({lo_member}));")
            lines.append(f"  pub.message().set_{name}_end_seq(")
            lines.append(f"      inputs.{_input_getter(name)}.get_sequence_number({hi_member}));")
        else:
            member = _candidate_member_call(f"{name}_it", "candidate")
            lines.append(f"  pub.message().set_{name}_seq(")
            lines.append(f"      inputs.{_input_getter(name)}.get_sequence_number({member}));")

    lines.append("  pub.mark_for_publish();")
    lines.append("}")
    return lines


def _generate_advance_cursors_fn(
    plan: CodegenPlan,
    dial_type: str,
) -> list[str]:
    """Generate ``advance_cursors()``."""
    lines: list[str] = []
    lines.append("static void advance_cursors(")
    lines.append(f"    {dial_type}Inputs& inputs,")
    lines.append("    const CandidateAlignment& candidate)")
    lines.append("{")

    for name in plan.input_names:
        if name in plan.optional_input_names and name in plan.batch_input_names:
            presence = _candidate_optional_presence_expr(name, plan.batch_input_names, "candidate")
            hi_member = _candidate_member_call(f"{name}_hi_it", "candidate")
            lines.append(f"  if ({presence}) {{")
            lines.append(f"    inputs.{_input_getter(name)}.set_cursor(std::next(*{hi_member}));")
            lines.append("  }")
        elif name in plan.optional_input_names:
            presence = _candidate_optional_presence_expr(name, plan.batch_input_names, "candidate")
            member = _candidate_member_call(f"{name}_it", "candidate")
            lines.append(f"  if ({presence}) {{")
            lines.append(f"    inputs.{_input_getter(name)}.set_cursor(std::next(*{member}));")
            lines.append("  }")
        elif name in plan.batch_input_names:
            # Advance past the last element in the batch.
            hi_member = _candidate_member_call(f"{name}_hi_it", "candidate")
            lines.append(f"  inputs.{_input_getter(name)}.set_cursor(std::next({hi_member}));")
        else:
            member = _candidate_member_call(f"{name}_it", "candidate")
            lines.append(f"  inputs.{_input_getter(name)}.set_cursor(std::next({member}));")

    lines.append("}")
    return lines


def _generate_timeout_constants(plan: CodegenPlan) -> list[str]:
    """Generate timeout duration constants for optional inputs."""
    return [
        f"static constexpr auto {name}_timeout = std::chrono::nanoseconds{{{timeout_ns}}};"
        for name, timeout_ns in sorted(plan.optional_timeouts.items())
    ]


def _generate_all_optionals_present(plan: CodegenPlan) -> list[str]:
    """Generate a helper that checks whether all timeout optionals are present."""
    lines: list[str] = [
        "static bool all_timeout_optionals_present(const CandidateAlignment& candidate)",
        "{",
    ]

    timeout_names = sorted(plan.optional_timeouts.keys())
    checks = [_candidate_optional_presence_expr(name, plan.batch_input_names, "candidate") for name in timeout_names]
    if len(checks) == 1:
        lines.append(f"  return {checks[0]};")
    else:
        lines.append(f"  return {checks[0]}")
        lines.extend(f"      && {check}" for check in checks[1:-1])
        lines.append(f"      && {checks[-1]};")

    lines.append("}")
    return lines


def _generate_compute_effective_deadline(plan: CodegenPlan) -> list[str]:
    """Generate a helper that computes the effective deadline across missing optionals.

    The deadline is the latest deadline among currently-missing optionals.
    It changes dynamically as optionals arrive during the waiting period, so
    it must be recomputed on each message arrival from the start time stored
    in the AlignerState.
    """
    lines: list[str] = []
    lines.append("static jewels::time::SyncTime compute_effective_deadline(")
    lines.append("    jewels::time::SyncTime t0,")
    lines.append("    const CandidateAlignment& candidate)")
    lines.append("{")
    lines.append("  auto max_remaining = std::chrono::nanoseconds{0};")
    for name in sorted(plan.optional_timeouts.keys()):
        absent = _candidate_optional_absence_expr(name, plan.batch_input_names, "candidate")
        lines.append(f"  if ({absent}) {{")
        lines.append(f"    max_remaining = std::max(max_remaining, {name}_timeout);")
        lines.append("  }")
    lines.append("  return t0 + max_remaining;")
    lines.append("}")
    return lines


def _generate_execute_cog(
    plan: CodegenPlan,
    dial_type: str,
    root_function_name: str,
) -> list[str]:
    """Generate the ``execute_cog()`` function body."""
    lines: list[str] = []
    if not plan.optional_timeouts:
        return _generate_execute_cog_no_timeout(dial_type, root_function_name)

    # Timeout protocol helpers go inside anonymous namespace.
    lines.extend(_generate_timeout_constants(plan))
    lines.append("")
    lines.extend(_generate_all_optionals_present(plan))
    lines.append("")
    lines.extend(_generate_compute_effective_deadline(plan))
    lines.append("")
    lines.append("}  // anonymous namespace")
    lines.append("")

    # execute_cog is outside anonymous namespace.
    lines.extend(_generate_execute_cog_timeout(dial_type, root_function_name))
    return lines


def _generate_execute_cog_no_timeout(
    dial_type: str,
    root_function_name: str,
) -> list[str]:
    """Generate the simple ``execute_cog()`` body (no timeout protocol)."""
    lines: list[str] = []
    lines.append(f"void execute_cog({dial_type}& dial)")
    lines.append("{")
    lines.append("  auto& inputs = dial.get_inputs();")
    lines.append("  const auto views = make_input_views(inputs);")
    lines.append("")
    lines.append("  CandidateAlignment candidate;")
    lines.append("  candidate.presence = PresenceState{};")
    lines.append(f"  if (jewels::fails({root_function_name}(")
    lines.append("          jewels::InOut{candidate}, views))) {")
    lines.append("    return;")
    lines.append("  }")
    lines.append("  publish_alignment(dial, candidate);")
    lines.append("  advance_cursors(inputs, candidate);")
    lines.append("}")
    return lines


def _generate_execute_cog_timeout(
    dial_type: str,
    root_function_name: str,
) -> list[str]:
    """Generate the ``execute_cog()`` body with timeout protocol."""
    return [
        f"void execute_cog({dial_type}& dial)",
        "{",
        "  auto& inputs = dial.get_inputs();",
        "  const auto views = make_input_views(inputs);",
        "  auto& timer = dial.get_optional_timer();",
        "  auto& state = dial.get_states().get_aligner_state();",
        "  const auto now = dial.get_start_time();",
        "  const bool timer_fired = static_cast<bool>(",
        "      dial.get_conditions().get_optional_timer());",
        "",
        "  CandidateAlignment candidate;",
        "  candidate.presence = PresenceState{};",
        f"  if (jewels::fails({root_function_name}(",
        "          jewels::InOut{candidate}, views))) {",
        "    static_cast<void>(timer.disarm());",
        "    return;",
        "  }",
        "",
        "  if (all_timeout_optionals_present(candidate)) {",
        "    if (jewels::fails(timer.disarm())) {",
        '      jewels::log_cerr_error("Failed to disarm aligner timeout timer");',
        "    }",
        "    publish_alignment(dial, candidate);",
        "    advance_cursors(inputs, candidate);",
        "    return;",
        "  }",
        "",
        "  // Partial alignment: some timeout optionals missing.",
        "",
        "  if (timer_fired) {",
        "    // Timer deadline reached — publish the partial alignment.",
        "    if (jewels::fails(timer.disarm())) {",
        '      jewels::log_cerr_error("Failed to disarm aligner timeout timer");',
        "    }",
        "    publish_alignment(dial, candidate);",
        "    advance_cursors(inputs, candidate);",
        "    return;",
        "  }",
        "",
        "  if (!timer.is_armed()) {",
        "    // Start waiting period for optional data.",
        "    state.set_partial_start_time(now);",
        "    const auto deadline = compute_effective_deadline(now, candidate);",
        "    if (jewels::fails(timer.arm(deadline))) {",
        '      jewels::log_cerr_error("Failed to arm aligner timeout timer");',
        "    }",
        "    return;",
        "  }",
        "",
        "  const auto t0 = state.get_partial_start_time();",
        "  const auto effective_deadline =",
        "      compute_effective_deadline(t0, candidate);",
        "",
        "  if (effective_deadline <= now) {",
        "    if (jewels::fails(timer.disarm())) {",
        '      jewels::log_cerr_error("Failed to disarm aligner timeout timer");',
        "    }",
        "    publish_alignment(dial, candidate);",
        "    advance_cursors(inputs, candidate);",
        "    return;",
        "  }",
        "",
        "  if (jewels::fails(timer.arm(effective_deadline))) {",
        '    jewels::log_cerr_error("Failed to arm aligner timeout timer");',
        "  }",
        "}",
    ]


def generate_search_code(
    plan: CodegenPlan,
    dial_type: str,
    compiler_context: CompilerContext,
) -> list[str]:
    """Generate the C++ search implementation for an aligner.

    Returns lines of C++ code to be emitted inside the target namespace.
    Wrapped in an anonymous namespace (except ``execute_cog`` itself).

    Raises:
        ValueError: If the plan uses NEAREST without a nearest_reference.
    """
    _validate_plan(plan)

    lines: list[str] = []

    lines.append("")
    lines.append("namespace {")
    lines.append("")

    artifacts = _collect_generated_search_artifacts(plan)
    _emit_search_state_types(lines, plan, dial_type, compiler_context, artifacts)
    _emit_best_search_helpers(lines, plan, artifacts)
    _emit_search_functions(lines, plan, artifacts)

    lines.extend(_generate_publish_fn(plan, dial_type))
    lines.append("")
    lines.extend(_generate_advance_cursors_fn(plan, dial_type))
    lines.append("")

    # When there are optional timeouts, _generate_execute_cog emits timeout
    # helpers inside the anonymous namespace, closes it, then emits
    # execute_cog outside. Otherwise we close the anonymous namespace here.
    if not plan.optional_timeouts:
        lines.append("}  // anonymous namespace")
        lines.append("")

    root_function_name = _scope_entry_function_name(plan, _root_scope(plan), None, None)
    assert root_function_name is not None
    lines.extend(_generate_execute_cog(plan, dial_type, root_function_name))

    return lines


def _plan_window_include_flags(plan: CodegenPlan) -> tuple[bool, bool]:
    """Return whether generated search windows need time and integer headers."""
    has_time_windows = False
    has_int_windows = False
    for level in plan.levels:
        for window in level.initial_windows:
            for bound in (window.lo, window.hi):
                if isinstance(bound, primitive.UnitValue) and isinstance(bound.unit, TimeUnit):
                    has_time_windows = True
                elif isinstance(bound, primitive.DecimalValue):
                    has_int_windows = True
    return has_time_windows, has_int_windows


def render_aligner_impl(
    plan: CodegenPlan,
    resolved: ResolvedAligner,
    cpp_cog: CppCog,
    target: CppTarget,
) -> CppModuleChunks:
    """Produce a complete ``CppModuleChunks`` for the aligner ``_impl`` target.

    Args:
        plan: The codegen plan for this aligner.
        resolved: The resolved aligner IR.
        cpp_cog: The CppCog for the aligner (provides the dial header).
        target: The CppTarget that owns the aligner cog.

    Returns:
        A CppModuleChunks with generated code in the implementation chunk.
    """
    dial_type = f"{resolved.name}Dial"
    lines = generate_search_code(plan=plan, dial_type=dial_type, compiler_context=target.module.context)

    mod = CppModuleChunks()
    impl = mod.implementation_chunk

    # Dial header — needed for the dial type used by execute_cog().
    assert cpp_cog.dial_header is not None
    impl.context.add_include(cpp_cog.dial_header)

    # Main _cc target header containing the alignment output schema.
    include_dir = target.module.module_id.get_base_path().parent
    schema_header_path = str(include_dir / (target.name + "_types.hh"))
    impl.context.add_include(Header(target.module.module_id.repo, schema_header_path))

    impl.context.add_include(SystemHeader("algorithm"))
    impl.context.add_include(SystemHeader("chrono"))
    impl.context.add_include(SystemHeader("iterator"))
    impl.context.add_include(SystemHeader("optional"))
    impl.context.add_include(SystemHeader("utility"))
    impl.context.add_include(Header(JEWELS_REPO, "jewels/callsig/outcome.hh"))
    impl.context.add_include(Header(JEWELS_REPO, "jewels/callsig/outparam.hh"))
    impl.context.add_include(Header(JEWELS_REPO, "jewels/log_cerr/log_cerr.hh"))

    if plan.optional_input_names:
        impl.context.add_include(SystemHeader("bitset"))
        impl.context.add_include(SystemHeader("cstdint"))
    if plan.optional_input_names or plan.objectives:
        impl.context.add_include(SystemHeader("cstddef"))

    # Windowing headers — add conditionally based on window types used.
    has_time_windows, has_int_windows = _plan_window_include_flags(plan)
    if has_time_windows:
        impl.context.add_include(Header(JEWELS_REPO, "jewels/time/sync_time.hh"))
    if has_int_windows:
        impl.context.add_include(SystemHeader("limits"))

    assert target.options is not None
    namespace = target.options.namespace
    impl.append(f"namespace {namespace}")
    impl.append("{")
    for line in lines:
        impl.append(line)
    impl.append("")
    impl.append(f"}} // namespace {namespace}")

    return mod
