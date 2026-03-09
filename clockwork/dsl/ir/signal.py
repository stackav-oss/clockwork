# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Signal related IR nodes."""

from __future__ import annotations

import enum
from ast import literal_eval
from dataclasses import dataclass

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import clkbuiltins, clkenum, cst_util, expr, node, primitive, strongtypes, typesys
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override


class AggregationType(enum.Enum):
    """Aggregation types for signals.

    Defines how signal values should be aggregated before storage/transmission or after collection.
    """

    VALUE = "value"  # Default: no aggregation
    MIN = "min"
    MAX = "max"
    SUM = "sum"
    COUNT = "count"
    MEAN = "mean"
    FINAL_VALUE = "final_value"
    FIRST_VALUE = "first_value"


def _is_valid_signal_type(type_val: typesys.TypeVal) -> bool:
    """Check if a type is valid for use as a signal type (scalar types only)."""
    if isinstance(
        type_val,
        clkbuiltins.IntegerPrimitiveType
        | clkbuiltins.FloatingPointPrimitiveType
        | clkenum.ClkEnum
        | clkenum.ResolvedEnum,
    ):
        return True
    if type_val in (clkbuiltins.DURATION, clkbuiltins.SYNC_TIME, clkbuiltins.BOOL):
        return True
    # Allow strong types with scalar underlying types (recursive check)
    if isinstance(type_val, strongtypes.StrongType):
        underlying = type_val.get_underlying_type()
        return _is_valid_signal_type(underlying)
    return False

    return False


@dataclass
class _SignalOptions:
    """Parsed signal options from a signal block."""

    signal_name: expr.Expr | None
    multi_instance: bool
    metadata: expr.TypeExpression | None
    pre_aggregation: set[AggregationType]
    pre_aggregation_text: str
    post_aggregation: set[AggregationType] | None
    post_aggregation_text: str | None


def parse_aggregation(
    agg_cst: cst.SignalPreAggregation | cst.SignalPostAggregation | cst.SignalOptionPostAggregation,
    module: node.Module,
    aggregation_type_name: str = "aggregation",
) -> tuple[set[AggregationType], str]:
    """Parse aggregation option from CST node.

    Args:
        agg_cst: CST node for pre or post aggregation.
        module: Module containing the CST node.
        aggregation_type_name: Name to use in error messages (e.g., "pre-aggregation", "post-aggregation").

    Returns:
        Tuple of (aggregation types set, original DSL text).
    """
    if module.terminals is None:
        msg = "Cannot construct IR nodes from CST without a TerminalSource"
        raise ValueError(msg)

    aggregation: set[AggregationType] = set()
    agg_list = agg_cst.child_aggregation_list()

    # Extract original DSL text from the aggregation list and add brackets to match DSL format
    agg_text = "[" + get_span(agg_list.span, module.terminals) + "]"

    for string_lit_cst in agg_list.children_string_literal():
        literal_str = get_span(string_lit_cst.child_value(), module.terminals)
        agg_value = literal_eval(literal_str)
        try:
            aggregation.add(AggregationType(agg_value))
        except ValueError as e:
            valid_types = ", ".join(a.value for a in AggregationType)
            error_msg = f"Invalid {aggregation_type_name} type: {agg_value}. Valid types are: {valid_types}"
            formatted_error = cst_util.format_line_with_error(
                string_lit_cst.child_value(), module.terminals, module.module_id
            )
            msg = f"{error_msg}{formatted_error}"
            raise TypeError(msg) from e
    return aggregation, agg_text


def _parse_signal_options(
    signal_block: cst.SignalBlock,
    cst_node: cst.ModuleScopeSignal | cst.CogScopeSignal,
    module: node.Module,
) -> _SignalOptions:
    """Parse signal options from a signal block."""
    signal_name: expr.Expr | None = None
    multi_instance = False
    metadata: expr.TypeExpression | None = None
    pre_aggregation: set[AggregationType] = {AggregationType.VALUE}
    pre_aggregation_text: str = "value"
    post_aggregation: set[AggregationType] | None = None
    post_aggregation_text: str | None = None

    for option in signal_block.children_signal_option():
        if name_cst := option.maybe_signal_name():
            signal_name = expr.Expr.from_cst(name_cst.child_value(), module)
            typesys.unify(signal_name.type_info, clkbuiltins.STRING)
        elif multi_instance_cst := option.maybe_signal_multi_instance():
            multi_instance_value = multi_instance_cst.child_boolean()
            multi_instance = multi_instance_value.maybe_true() is not None
        elif metadata_cst := option.maybe_signal_metadata():
            metadata = expr.TypeExpression.make(expr.Expr.from_cst(metadata_cst.child_typespec(), module))
        elif pre_agg_cst := option.maybe_signal_pre_aggregation():
            pre_aggregation, pre_aggregation_text = parse_aggregation(pre_agg_cst, module, "pre-aggregation")
        elif post_agg_cst := option.maybe_signal_option_post_aggregation():
            # Validate that post_aggregation is only allowed on cog-scope signals
            if isinstance(cst_node, cst.ModuleScopeSignal):
                if module.terminals is None:
                    msg = "Cannot construct IR nodes from CST without a TerminalSource"
                    raise ValueError(msg)
                msg = cst_util.format_line_with_error(post_agg_cst.span, module.terminals, module.module_id)
                msg = f"post_aggregation is only allowed on cog-scope signals, not module-scope signals{msg}"
                raise TypeError(msg)
            post_aggregation, post_aggregation_text = parse_aggregation(post_agg_cst, module, "post-aggregation")

    return _SignalOptions(
        signal_name=signal_name,
        multi_instance=multi_instance,
        metadata=metadata,
        pre_aggregation=pre_aggregation,
        pre_aggregation_text=pre_aggregation_text,
        post_aggregation=post_aggregation,
        post_aggregation_text=post_aggregation_text,
    )


@dataclass
class ResolvedSignal(
    node.CstNode[cst.ModuleScopeSignal | cst.CogScopeSignal],
    node.DocableEntity,
    node.NamedEntity,
    typesys.Value,
):
    """Fully resolved version of a Signal node."""

    signal_name: str
    signal_type: typesys.TypeVal
    multi_instance: bool
    pre_aggregation: set[AggregationType]
    pre_aggregation_text: str
    post_aggregation: set[AggregationType] | None
    post_aggregation_text: str | None
    metadata: typesys.TypeVal | None
    source: Signal | None

    @override
    def value_key(self) -> str:
        """Get the value key for this signal."""
        return f"Signal({self.fqn})"


@dataclass
class Signal(
    node.CstNode[cst.ModuleScopeSignal | cst.CogScopeSignal],
    node.DocableEntity,
    node.NamedEntity,
    typesys.Value,
    typesys.SubscriptableEntity,
):
    """IR Node representing a signal declaration."""

    signal_name: expr.Expr | str | None
    signal_type: expr.TypeExpression | typesys.TypeVal
    multi_instance: bool
    pre_aggregation: set[AggregationType]
    pre_aggregation_text: str
    post_aggregation: set[AggregationType] | None
    post_aggregation_text: str | None
    metadata: expr.TypeExpression | typesys.TypeVal | None
    resolved: ResolvedSignal | None = None

    @classmethod
    def from_cst(
        cls: type[Signal], cst_node: cst.ModuleScopeSignal | cst.CogScopeSignal, module: node.Module, scope: node.Scope
    ) -> Signal:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        cst_doc = cst_node.maybe_doc()
        doc = node.Doc.from_cst(cst_doc, module) if cst_doc else None
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)

        signal_type = expr.TypeExpression.make(expr.Expr.from_cst(cst_node.child_signal_type(), module))

        signal_name: expr.Expr | None = None
        multi_instance = False
        metadata: expr.TypeExpression | None = None
        pre_aggregation: set[AggregationType] = {AggregationType.VALUE}  # Default aggregation
        pre_aggregation_text: str = "value"
        post_aggregation: set[AggregationType] | None = None
        post_aggregation_text: str | None = None

        if signal_block := cst_node.maybe_signal_block():
            options = _parse_signal_options(signal_block, cst_node, module)
            signal_name = options.signal_name
            multi_instance = options.multi_instance
            metadata = options.metadata
            pre_aggregation = options.pre_aggregation
            pre_aggregation_text = options.pre_aggregation_text
            post_aggregation = options.post_aggregation
            post_aggregation_text = options.post_aggregation_text

        result = cls(
            type_info=clkbuiltins.SIGNAL_TYPE,
            doc=doc,
            name=name,
            scope=scope,
            module=module,
            cst_node=cst_node,
            signal_name=signal_name,
            signal_type=signal_type,
            multi_instance=multi_instance,
            metadata=metadata,
            pre_aggregation=pre_aggregation,
            pre_aggregation_text=pre_aggregation_text,
            post_aggregation=post_aggregation,
            post_aggregation_text=post_aggregation_text,
            resolved=None,
        )
        if isinstance(cst_node, cst.ModuleScopeSignal):
            scope.define(name, result, module.terminals)
        return result

    def resolve(self) -> ResolvedSignal:
        """Perform finalization of the signal IR."""
        if self.resolved:
            return self.resolved

        # Invariant. If this has not been resolved yet, signal_type must be a TypeExpression
        assert isinstance(self.signal_type, expr.TypeExpression)

        resolved_signal_name: str
        if isinstance(self.signal_name, expr.Expr):
            signal_name_val = self.signal_name.evaluate()
            if not isinstance(signal_name_val, primitive.StringValue):
                msg = self.signal_name.append_error_line(
                    f"Signal name must evaluate to a string, but got {signal_name_val!r}"
                )
                raise TypeError(msg)
            resolved_signal_name = signal_name_val.value
        elif isinstance(self.signal_name, str):
            resolved_signal_name = self.signal_name
        else:
            resolved_signal_name = self.fqn

        signal_type = self.signal_type.evaluate()
        if not isinstance(signal_type, typesys.TypeVal):
            msg = self.signal_type.append_error_line(f"Signal type must evaluate to a type, got {signal_type}")
            raise TypeError(msg)

        if not _is_valid_signal_type(signal_type):
            type_repr = getattr(signal_type, "name", str(signal_type))
            error_msg = (
                f"Signal type must be a scalar value (integer, float, bool, enum, or strong type with scalar "
                f"underlying type). Got {type_repr}. Schemas, arrays, optionals, and other complex types are "
                f"not valid signal types."
            )
            msg = self.signal_type.append_error_line(error_msg)
            raise TypeError(msg)

        resolved_metadata: typesys.TypeVal | None = None
        if self.metadata is not None:
            # Invariant. If this has not been resolved yet, metadata must be a TypeExpression
            assert isinstance(self.metadata, expr.TypeExpression)
            metadata = self.metadata.evaluate()
            if not isinstance(metadata, typesys.TypeVal):
                msg = self.metadata.append_error_line(f"Metadata type must evaluate to a type, got {metadata}")
                raise TypeError(msg)
            resolved_metadata = metadata

        self.resolved = ResolvedSignal(
            name=self.name,
            scope=self.scope,
            module=self.module,
            cst_node=self.cst_node,
            doc=self.doc,
            type_info=self.type_info,
            signal_name=resolved_signal_name,
            signal_type=signal_type,
            multi_instance=self.multi_instance,
            pre_aggregation=self.pre_aggregation,
            pre_aggregation_text=self.pre_aggregation_text,
            post_aggregation=self.post_aggregation,
            post_aggregation_text=self.post_aggregation_text,
            metadata=resolved_metadata,
            source=self,
        )
        return self.resolved

    def get_resolved(self) -> ResolvedSignal:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    @override
    def value_key(self) -> str:
        """Get the value key for this signal."""
        return f"Signal({self.fqn})"

    @override
    def evaluate_subscript(
        self,
        *,
        index: typesys.Value,
        cst_node: node.CstNode[cst.Expr],
        module: node.Module,
    ) -> SignalInstanceSpec:
        """Evaluate subscript operation on this signal.

        Args:
            index: The subscript index (WildcardValue for wildcard '*', StringValue for instance name,
                   Cog/CogInstance for cog-based filtering)
            cst_node: The CST node for error reporting
            module: The module for context

        Returns:
            SignalInstanceSpec with instance_key set to None for wildcard, or the Value otherwise
        """
        instance_key = index

        return SignalInstanceSpec(
            type_info=clkbuiltins.SIGNAL_TYPE,
            signal=self,
            instance_key=instance_key,
        )


@dataclass
class SignalInstanceSpec(typesys.Value):
    """Represents a specific instance or a group of instances of a signal (e.g., signal["instance_name"] or signal[CogType])."""

    signal: Signal
    instance_key: typesys.Value  # Can be a string, a cog type, or wildcard.

    def is_wildcard(self) -> bool:
        """Check if this spec represents a wildcard (all instances)."""
        return self.instance_key is expr.WILDCARD

    @override
    def value_key(self) -> str:
        """Get the value key for this signal instance."""
        signal_key = self.signal.fqn
        return f"SignalInstance({signal_key},[{self.instance_key.value_key() if self.instance_key is not expr.WILDCARD else '*'}])"
