# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Pub/Sub graph related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from enum import Enum

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, representation, schema_reg, typesys
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.serialization import tachyon_reg


class ChannelPublishersOption(Enum):
    """Publishers option for a channel."""

    single = 0
    multiple = 1


@dataclass
class Channel(node.CstNode[cst.Channel], node.DocRequiredEntity, node.NamedEntity, typesys.Value):
    """IR Node representing a pub/sub channel declaration."""

    channel_name: expr.Expr | primitive.StringValue
    message_type: expr.TypeExpression | typesys.TypeVal
    message_repr: representation.ResolvedReprInstantiation | None
    message_size: expr.Expr | None | primitive.DecimalValue
    num_slots_expr: expr.Expr | primitive.DecimalValue
    publishers_option: ChannelPublishersOption
    is_diagnostics: bool
    is_bridge_status: bool

    @classmethod
    def from_cst(cls: type[Channel], cst_node: cst.Channel, module: node.Module, scope: node.Scope) -> Channel:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)

        channel_name: expr.Expr | primitive.StringValue = (
            expr.Expr.from_cst(channel_name_cst.child_value(), module)
            if (channel_name_cst := cst_node.maybe_channel_name())
            else primitive.StringValue.make(name)
        )
        typesys.unify(channel_name.type_info, clkbuiltins.STRING)
        message_type = expr.TypeExpression.make(
            expr.Expr.from_cst(cst_node.child_channel_message_type().child_typespec(), module)
        )
        if message_size_cst := cst_node.maybe_channel_option_message_size():
            message_size = expr.Expr.from_cst(message_size_cst.child_value(), module)
            typesys.unify(message_size.type_info, clkbuiltins.UINT32)
        else:
            message_size = None
        num_slots = expr.Expr.from_cst(cst_node.child_channel_option_num_slots().child_value(), module)
        cst_publishers_option: cst.ChannelOptionPublishers | None = cst_node.maybe_channel_option_publishers()
        publishers_option = (
            ChannelPublishersOption.single
            if not cst_publishers_option or cst_publishers_option.maybe_single()
            else ChannelPublishersOption.multiple
        )
        is_diagnostics = bool(cst_publishers_option and cst_publishers_option.maybe_diagnostics())
        is_bridge_status = bool(cst_publishers_option and cst_publishers_option.maybe_bridge_status())
        result = cls(
            type_info=clkbuiltins.CHANNEL_TYPE,
            doc=doc,
            name=name,
            scope=module.inner_scope,
            module=module,
            cst_node=cst_node,
            channel_name=channel_name,
            message_type=message_type,
            message_repr=None,
            message_size=message_size,
            num_slots_expr=num_slots,
            publishers_option=publishers_option,
            is_diagnostics=is_diagnostics,
            is_bridge_status=is_bridge_status,
        )
        scope.define(name, result, module.terminals)
        return result

    def _register(self) -> None:
        assert isinstance(self.channel_name, primitive.StringValue)  # noqa: S101  (Guaranteed by resolve)
        _CHANNEL_REG[self.channel_name.value] = self
        if self.is_diagnostics:
            global _DIAGNOSTICS_CHANNEL  # noqa: PLW0603 (Only one diagnostics channel per system is allowed)
            if _DIAGNOSTICS_CHANNEL:
                msg = self.append_error_line(
                    f"Attempt to define multiple diagnostics channels {self.channel_name} and {_DIAGNOSTICS_CHANNEL.channel_name}"
                )
                raise ValueError(msg)
            _DIAGNOSTICS_CHANNEL = self  # pyright: ignore[reportConstantRedefinition] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        if self.is_bridge_status:
            global _BRIDGE_STATUS_CHANNEL  # noqa: PLW0603 (Only one bridge status channel per system is allowed)
            if _BRIDGE_STATUS_CHANNEL:
                msg = self.append_error_line(
                    f"Attempt to define multiple bridge status channels {self.channel_name} and {_BRIDGE_STATUS_CHANNEL.channel_name}"
                )
                raise ValueError(msg)
            _BRIDGE_STATUS_CHANNEL = self  # pyright: ignore[reportConstantRedefinition] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.message_size, expr.Expr | None):
            msg = self.append_error_line(f"Attempt to resolve Channel twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        if isinstance(self.channel_name, expr.Expr):
            channel_name = self.channel_name.evaluate()
            assert isinstance(channel_name, primitive.StringValue)  # noqa: S101  (Should be guaranteed by type system)
            self.channel_name = channel_name
        self._register()

        expected_message_size = self._resolve_message_type()

        if isinstance(self.message_size, expr.Expr):
            message_size = self.message_size.evaluate()
            assert isinstance(message_size, primitive.DecimalValue)  # noqa: S101  (Should be guaranteed by type system)
            if expected_message_size is not None and message_size.value != expected_message_size.value:
                msg = self.message_size.append_error_line(
                    f"Specified representation has size {expected_message_size.value}; either remove the explicit size or modify it to match."
                )
                raise ValueError(msg)
            self.message_size = message_size
        elif self.message_size is None:
            if expected_message_size is None:
                msg = self.append_error_line("Cannot auto-size this channel; please specify message_size explicitly")
                raise ValueError(msg)
            self.message_size = expected_message_size
        if isinstance(self.num_slots_expr, expr.Expr):
            num_slots = self.num_slots_expr.evaluate()
            assert isinstance(num_slots, primitive.DecimalValue)  # noqa: S101  (Should be guaranteed by type system)
            if num_slots.value < 1:
                msg = self.num_slots_expr.append_error_line("Must specify at least one slot in the channel.")
                raise ValueError(msg)
            self.num_slots_expr = num_slots

    @property
    def num_slots(self) -> primitive.DecimalValue:
        """Get the buffer slot count."""
        if not isinstance(self.num_slots_expr, primitive.DecimalValue):
            msg = self.num_slots_expr.append_error_line("Num slots not resolved before access.")
            raise TypeError(msg)
        # Add one so the requested max number of messages is always available.
        return primitive.DecimalValue(
            type_info=self.num_slots_expr.type_info, value=self.num_slots_expr.value + Decimal(1)
        )

    def _resolve_message_type(self) -> primitive.DecimalValue | None:
        assert isinstance(self.message_type, expr.TypeExpression)  # noqa: S101  (for mypy)
        message_type = self.message_type.evaluate()
        if isinstance(message_type, typesys.DeferrableType):
            msg = self.message_type.append_error_line("Channel type may not be a generic parameter.")
            raise TypeError(msg)
        repr_ref = representation.RepresentationReference.from_typespec(message_type)
        if isinstance(repr_ref, str):
            msg = self.message_type.append_error_line(repr_ref)
            raise TypeError(msg)
        repr_info = schema_reg.lookup_representation(self.module.context, repr_ref)
        if repr_info is None:
            msg = self.message_type.append_error_line(
                "Representation not instantiated or instantiation not visible here."
            )
            raise ValueError(msg)
        assert isinstance(repr_info.representation_ir.typespec, typesys.Instantiation)  # noqa: S101 (for mypy)
        if repr_info.representation_ir.typespec.instantiates is clkbuiltins.TACHYON:
            schema_type = repr_info.representation_ir.schema_ir
            constraint = tachyon_reg.constraint_for_type(self.module.context, schema_type)
            if constraint is None:
                # Should be impossible except for a bug in the compiler
                msg = self.message_type.append_error_line(f"Missing Tachyon constraint for {repr_info}")
                raise RuntimeError(msg)
            expected_message_size = primitive.DecimalValue(type_info=clkbuiltins.UINT32, value=Decimal(constraint.size))
        else:
            expected_message_size = None
        self.message_type = message_type
        self.message_repr = repr_info.representation_ir
        return expected_message_size

    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        if not isinstance(self.channel_name, primitive.StringValue):
            msg = self.append_error_line("Attempt to generate value key for unresolved Channel")
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved channel is a runtime error)
        return f"Channel({self.channel_name.value})"


def lookup_channel(channel_name: str) -> Channel:
    """Look up a channel by name.

    Raises:
        KeyError if no such channel is defined.
    """
    return _CHANNEL_REG[channel_name]


def diagnostics_channel() -> Channel | None:
    """Get the diagnostics channel."""
    return _DIAGNOSTICS_CHANNEL


def bridge_status_channel() -> Channel | None:
    """Get the bridge status channel."""
    return _BRIDGE_STATUS_CHANNEL


# Channels are not namespaced; in any given system there can be only one channel
# of each name.  This prevents confusion.  This global registry enforces that.
# Unlike other registries, this one isn't primarily meant to allow lookups of
# channels, but it could also be used for that if (later) exposed by a lookup
# function.  The way of referencing a channel within Clockwork source is by
# importing the module that defines it, so looking it up by channel name should
# not be needed.
_CHANNEL_REG: dict[str, Channel] = {}


# The system can only have one diagnostics channel and one bridge status channel.
# These variables are used to store the diagnostics and bridge status channel
# definitins as they are encountered during parsing.
_DIAGNOSTICS_CHANNEL: Channel | None = None
_BRIDGE_STATUS_CHANNEL: Channel | None = None
