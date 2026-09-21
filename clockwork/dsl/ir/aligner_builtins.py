# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner-specific DFL builtins.

Provides the types, functions, scope, and loader needed for aligner body
statements.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, compiler, dfl, dfl_traits, dfl_types, importer_registry, node, typesys
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    from fltk.fegen.pyrt.span_protocol import SpanProtocol


@dataclass
class RequireBuiltin(dfl.BuiltinFn):
    """``require(condition: Bool) -> Spec``.

    Declares a hard constraint that must hold for any valid alignment.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"require() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        if arg_types[0] is not clkbuiltins.BOOL:
            msg = f"require() expects Bool, got {arg_types[0]}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        return clkbuiltins.SPEC_TYPE


def _minmax_infer_return_type(
    arg_types: list[typesys.TypeVal | typesys.InferenceVar],
    registry: dfl_types.TraitRegistry | None = None,
    span: SpanProtocol | None = None,
    ctx: dfl.Context | None = None,
) -> typesys.TypeVal:
    if len(arg_types) != 1:
        msg = f"minimize() takes exactly 1 argument, got {len(arg_types)}"
        raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

    if registry is not None:
        dfl.verify_ord(arg_types[0], registry, span, ctx)

    return clkbuiltins.SPEC_TYPE


@dataclass
class MinimizeBuiltin(dfl.BuiltinFn):
    """``minimize(value: Ord) -> Spec``.

    Declares an optimization objective to minimize the given value.
    Accepts any type that implements the ``Ord`` trait.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        return _minmax_infer_return_type(arg_types, registry, span, ctx)


@dataclass
class MaximizeBuiltin(dfl.BuiltinFn):
    """``maximize(value: Ord) -> Spec``.

    Declares an optimization objective to maximize the given value.
    Accepts any type that implements the ``Ord`` trait.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        return _minmax_infer_return_type(arg_types, registry, span, ctx)


@dataclass
class HasCandidatesBuiltin(dfl.BuiltinFn):
    """``has_candidates(input) -> Bool``.

    Returns true if the optional input currently has candidate messages.
    Semantic validation (argument must be an optional aligner input) is
    deferred to a later analysis pass.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"has_candidates() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))
        arg_type = arg_types[0]
        is_aligner_input = arg_type is clkbuiltins.ALIGNER_INPUT_TYPE or (
            isinstance(arg_type, dfl_types.CollectionType) and arg_type.element_type is clkbuiltins.ALIGNER_INPUT_TYPE
        )
        if not is_aligner_input:
            msg = f"has_candidates() expects AlignerInput, got {arg_type}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))
        return clkbuiltins.BOOL


@dataclass
class AssumeBuiltin(dfl.BuiltinFn):
    """``assume(predicate: Bool) -> Spec``.

    Declares a compile-time assumption about a field property.
    The argument must be a Bool produced by ``is_unique``,
    ``is_strictly_increasing``, or ``is_non_decreasing``.
    No runtime check is generated.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"assume() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        if arg_types[0] is not clkbuiltins.BOOL:
            msg = f"assume() expects Bool, got {arg_types[0]}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        return clkbuiltins.SPEC_TYPE


@dataclass
class IsStrictlyIncreasingBuiltin(dfl.BuiltinFn):
    """``is_strictly_increasing(input.field) -> Bool``.

    Declares that the given field's values strictly increase across
    messages on the input view.  Implies uniqueness.
    The field type must implement the ``Ord`` trait.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"is_strictly_increasing() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        if registry is not None:
            arg_type = arg_types[0]
            if isinstance(arg_type, dfl_types.CollectionType):
                arg_type = arg_type.element_type
            dfl.verify_ord(arg_type, registry, span, ctx)

        return clkbuiltins.BOOL


@dataclass
class IsNonDecreasingBuiltin(dfl.BuiltinFn):
    """``is_non_decreasing(input.field) -> Bool``.

    Declares that the given field's values are non-decreasing across
    messages on the input view.  Equal consecutive values are permitted
    (unlike ``is_strictly_increasing``).  The field type must implement
    the ``Ord`` trait.  Combine with ``is_unique`` for
    strictly-increasing semantics.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"is_non_decreasing() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        if registry is not None:
            arg_type = arg_types[0]
            if isinstance(arg_type, dfl_types.CollectionType):
                arg_type = arg_type.element_type
            dfl.verify_ord(arg_type, registry, span, ctx)

        return clkbuiltins.BOOL


@dataclass
class IsUniqueBuiltin(dfl.BuiltinFn):
    """``is_unique(input.field) -> Bool``.

    Declares that each value of the given field appears at most once
    across messages on the input view.  No ordering guarantee is made.
    """

    @override
    def infer_return_type(
        self,
        arg_types: list[typesys.TypeVal | typesys.InferenceVar],
        arg_exprs: list[object] | None = None,
        registry: dfl_types.TraitRegistry | None = None,
        span: SpanProtocol | None = None,
        ctx: dfl.Context | None = None,
    ) -> typesys.TypeVal:
        if len(arg_types) != 1:
            msg = f"is_unique() takes exactly 1 argument, got {len(arg_types)}"
            raise dfl.TypeCheckError(dfl.format_error(msg, span, ctx))

        return clkbuiltins.BOOL


def _create_aligner_builtins_scope() -> node.Scope:
    """Create scope with aligner-specific and DFL builtins."""
    scope = node.Scope(
        parent=None,
        uniq_path="__aligner_builtins__",
        module_id_for_errors=None,
    )

    builtins: list[dfl.BuiltinFn] = [
        RequireBuiltin(name="require", scope=scope, variadic=False, min_args=1),
        MinimizeBuiltin(name="minimize", scope=scope, variadic=False, min_args=1),
        MaximizeBuiltin(name="maximize", scope=scope, variadic=False, min_args=1),
        HasCandidatesBuiltin(name="has_candidates", scope=scope, variadic=False, min_args=1),
        AssumeBuiltin(name="assume", scope=scope, variadic=False, min_args=1),
        IsStrictlyIncreasingBuiltin(name="is_strictly_increasing", scope=scope, variadic=False, min_args=1),
        IsNonDecreasingBuiltin(name="is_non_decreasing", scope=scope, variadic=False, min_args=1),
        IsUniqueBuiltin(name="is_unique", scope=scope, variadic=False, min_args=1),
    ]

    for builtin in builtins:
        scope.define(builtin.name, builtin, None)

    return scope


ALIGNER_BUILTINS_SCOPE: Final = _create_aligner_builtins_scope()


@dataclass
class AlignerBuiltinsEntities:
    """Entities extracted from std/aligners/builtins.clk.

    Attributes:
        aligner_scope_template: Scope template containing aligner builtins
            (require, minimize, maximize, has_candidates) and spread.
            Parent is ``None``; callers clone with a real parent.
    """

    aligner_scope_template: node.Scope


@final
class AlignerBuiltinsRegistry(Context):
    """Registry for aligner builtins (compiler context entry).

    Attributes:
        name: Debug name for the registry.
        entities: The loaded entities.
    """

    def __init__(self, name: str | None, entities: AlignerBuiltinsEntities) -> None:
        """Create a new aligner builtins registry.

        Args:
            name: Debug name.
            entities: The loaded entities.
        """
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: AlignerBuiltinsRegistry) -> None:
        """Merge another registry (must be identical).

        Args:
            other: The registry to merge.

        Raises:
            RuntimeError: If entities differ.
        """
        if self.entities is not other.entities:
            msg = "Conflicting aligner builtins registries"
            raise RuntimeError(msg)


def _load_aligner_builtins(compiler_context: CompilerContext) -> AlignerBuiltinsEntities:
    """Load std/aligners/builtins.clk and extract entities.

    Args:
        compiler_context: The compiler context to use.

    Returns:
        The loaded entities.

    Raises:
        RuntimeError: If no importer is registered or spread cannot be loaded.
    """
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise RuntimeError(msg)

    module_id = ModuleID.from_path(CLK_REPO, Path("std/aligners/builtins.clk"))
    module = compiler.compile_source_file(module_id, importer_reg.importer)
    compiler_context.import_from(module.context)

    # Insert DFL builtins (map, sum, filter, etc.) into the builtins.clk module's
    # scope chain so that fn bodies like spread() can reference them.
    # The scope chain before: clkbuiltins → externs → inner → fn.body_scope
    # After:                  clkbuiltins → dfl_builtins → externs → ...
    # Guard against double-insertion when the importer cache returns the same
    # already-mutated module on subsequent calls.
    externs_scope = module.inner_scope.parent
    assert externs_scope is not None
    if externs_scope.lookup("map") is None:
        dfl_clone = dfl.BUILTINS_SCOPE.clone(parent=externs_scope.parent)
        externs_scope.parent = dfl_clone

    spread_fn = module.inner_scope.lookup("spread")
    if not isinstance(spread_fn, dfl.FnDef):
        msg = "spread function not found in std/aligners/builtins.clk"
        raise TypeError(msg)

    aligner_template = ALIGNER_BUILTINS_SCOPE.clone(parent=None)
    aligner_template.define("spread", spread_fn, None)
    return AlignerBuiltinsEntities(aligner_scope_template=aligner_template)


class _AlignerBuiltinsRegistryKey(ContextKey[AlignerBuiltinsRegistry]):
    """CompilerContext key for the aligner builtins registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> AlignerBuiltinsRegistry:
        """Create a default instance by loading builtins.clk.

        Args:
            compiler_context: The compiler context.

        Returns:
            A new AlignerBuiltinsRegistry with all entities loaded.
        """
        dfl_traits.ensure_traits_loaded(compiler_context)
        entities = _load_aligner_builtins(compiler_context)
        return AlignerBuiltinsRegistry(compiler_context.name, entities)


_ALIGNER_BUILTINS_KEY: Final = _AlignerBuiltinsRegistryKey("AlignerBuiltinsRegistryKey")


def ensure_aligner_builtins_loaded(compiler_context: CompilerContext) -> AlignerBuiltinsRegistry:
    """Ensure std/aligners/builtins.clk is loaded and return the registry.

    Idempotent — calling multiple times returns the same registry.

    Args:
        compiler_context: The compiler context to use.

    Returns:
        The AlignerBuiltinsRegistry with all entities loaded.
    """
    return compiler_context[_ALIGNER_BUILTINS_KEY]


def make_body_parent_scope(
    compiler_context: CompilerContext,
    base_scope: node.Scope,
) -> node.Scope:
    """Build the builtin scope chain for aligner body type-checking.

    Clones the DFL builtins and aligner builtins (including ``spread``) into
    a two-level scope chain parented on *base_scope*::

        base_scope → dfl_builtins_clone → aligner_builtins_clone

    Args:
        compiler_context: The active compiler context.
        base_scope: The scope to use as chain root (typically ``aligner.inner_scope``).

    Returns:
        The aligner builtins clone, suitable for use as ``body_scope.parent``.
    """
    registry = ensure_aligner_builtins_loaded(compiler_context)
    dfl_clone = dfl.BUILTINS_SCOPE.clone(parent=base_scope)
    return registry.entities.aligner_scope_template.clone(parent=dfl_clone)
