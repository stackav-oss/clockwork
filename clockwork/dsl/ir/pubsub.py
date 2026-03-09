# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Pub/Sub graph related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from enum import Enum
from typing import Final, final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, representation, schema_reg, typesys
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.serialization import tachyon_reg
from typing_extensions import override


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
    num_slots_expr: expr.Expr | primitive.DecimalValue | None
    is_published_once: bool
    publishers_option: ChannelPublishersOption
    is_diagnostics: bool
    is_bridge_status: bool
    is_c2c_bridge_status: bool
    enforce_backwards_compatibility: bool
    is_bulk_data: bool

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
        if channel_option_num_slots := cst_node.maybe_channel_option_num_slots():
            num_slots = expr.Expr.from_cst(channel_option_num_slots.child_value(), module)
            is_published_once = False
        else:
            channel_option_is_published_once = cst_node.maybe_channel_option_published_once()
            assert channel_option_is_published_once is not None
            num_slots = None
            is_published_once = channel_option_is_published_once.child_value().maybe_true() is not None
            if not is_published_once:
                msg = node.append_error_line(
                    channel_option_is_published_once, module, "published_once option must be set to true"
                )
                raise ValueError(msg)
        cst_publishers_option: cst.ChannelOptionPublishers | None = cst_node.maybe_channel_option_publishers()
        publishers_option = (
            ChannelPublishersOption.single
            if not cst_publishers_option or cst_publishers_option.maybe_single()
            else ChannelPublishersOption.multiple
        )
        if is_published_once and publishers_option != ChannelPublishersOption.single:
            msg = node.append_error_line(
                cst_publishers_option, module, "Cannot have multiple publishers when channel is published once"
            )
            raise ValueError(msg)
        is_diagnostics = bool(cst_publishers_option and cst_publishers_option.maybe_diagnostics())
        is_bridge_status = bool(cst_publishers_option and cst_publishers_option.maybe_bridge_status())
        is_c2c_bridge_status = bool(cst_publishers_option and cst_publishers_option.maybe_c2c_bridge_status())
        enforce_backwards_compatibility: bool = True
        is_bulk_data: bool = False
        if (
            maybe_enforce_backwards_compatibility_option
            := cst_node.maybe_channel_option_enforce_backwards_compatibility()
        ):
            enforce_backwards_compatibility_value = maybe_enforce_backwards_compatibility_option.child_boolean()
            enforce_backwards_compatibility = enforce_backwards_compatibility_value.maybe_true() is not None
        if maybe_bulk_data_option := cst_node.maybe_channel_option_bulk_data():
            bulk_data_option_value = maybe_bulk_data_option.child_boolean()
            is_bulk_data = bulk_data_option_value.maybe_true() is not None
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
            is_published_once=is_published_once,
            publishers_option=publishers_option,
            is_diagnostics=is_diagnostics,
            is_bridge_status=is_bridge_status,
            is_c2c_bridge_status=is_c2c_bridge_status,
            enforce_backwards_compatibility=enforce_backwards_compatibility,
            is_bulk_data=is_bulk_data,
        )
        scope.define(name, result, module.terminals)
        return result

    def _register(self) -> None:
        assert isinstance(self.channel_name, primitive.StringValue)
        registry = self.module.context[CHANNEL_REGISTRY_KEY]
        registry.channel_registry[self.channel_name.value] = self
        if self.is_diagnostics:
            if registry.diagnostics_channel:
                msg = self.append_error_line(
                    f"Attempt to define multiple diagnostics channels {self.channel_name} and {registry.diagnostics_channel.channel_name}"
                )
                raise ValueError(msg)
            registry.diagnostics_channel = self
        if self.is_bridge_status:
            if registry.bridge_status_channel:
                msg = self.append_error_line(
                    f"Attempt to define multiple bridge status channels {self.channel_name} and {registry.bridge_status_channel.channel_name}"
                )
                raise ValueError(msg)
            registry.bridge_status_channel = self
        if self.is_c2c_bridge_status:
            if registry.c2c_bridge_status_channel:
                msg = self.append_error_line(
                    f"Attempt to define multiple C2C bridge status channels {self.channel_name} and {registry.c2c_bridge_status_channel.channel_name}"
                )
                raise ValueError(msg)
            registry.c2c_bridge_status_channel = self

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.message_size, expr.Expr | None):
            msg = self.append_error_line(f"Attempt to resolve Channel twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        if isinstance(self.channel_name, expr.Expr):
            channel_name = self.channel_name.evaluate()
            assert isinstance(channel_name, primitive.StringValue)
            self.channel_name = channel_name
        self._register()

        expected_message_size = self._resolve_message_type()

        if isinstance(self.message_size, expr.Expr):
            message_size = self.message_size.evaluate()
            assert isinstance(message_size, primitive.DecimalValue)
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
            assert isinstance(num_slots, primitive.DecimalValue)
            if num_slots.value < 1:
                msg = self.num_slots_expr.append_error_line("Must specify at least one slot in the channel.")
                raise ValueError(msg)
            self.num_slots_expr = num_slots

    @property
    def num_slots(self) -> primitive.DecimalValue:
        """Get the buffer slot count."""
        if self.is_published_once:
            return primitive.DecimalValue(type_info=clkbuiltins.UINT32, value=Decimal(1))
        if not isinstance(self.num_slots_expr, primitive.DecimalValue):
            assert self.num_slots_expr
            msg = self.num_slots_expr.append_error_line("Num slots not resolved before access.")
            raise TypeError(msg)
        # Add one so the requested max number of messages is always available.
        return primitive.DecimalValue(
            type_info=self.num_slots_expr.type_info, value=self.num_slots_expr.value + Decimal(1)
        )

    def _resolve_message_type(self) -> primitive.DecimalValue | None:
        assert isinstance(self.message_type, expr.TypeExpression)
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
        assert isinstance(repr_info.representation_ir.typespec, typesys.Instantiation)
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

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        if not isinstance(self.channel_name, primitive.StringValue):
            msg = self.append_error_line("Attempt to generate value key for unresolved Channel")
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved channel is a runtime error)
        return f"Channel({self.channel_name.value})"


def lookup_channel(channel_name: str, compiler_context: CompilerContext) -> Channel:
    """Look up a channel by name.

    Raises:
        KeyError if no such channel is defined.
    """
    registry = compiler_context[CHANNEL_REGISTRY_KEY]
    return registry.channel_registry[channel_name]


def diagnostics_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the diagnostics channel."""
    registry = compiler_context[CHANNEL_REGISTRY_KEY]
    return registry.diagnostics_channel


def bridge_status_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the bridge status channel."""
    registry = compiler_context[CHANNEL_REGISTRY_KEY]
    return registry.bridge_status_channel


def c2c_bridge_status_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the C2C bridge status channel."""
    registry = compiler_context[CHANNEL_REGISTRY_KEY]
    return registry.c2c_bridge_status_channel


@final
class ChannelRegistry(Context):
    """Channel Registry."""

    def __init__(self, name: str | None) -> None:
        """Create a new channel registry."""
        self.name = name

        # Channels are not namespaced; in any given system there can be only one channel
        # of each name.  This prevents confusion.  This registry enforces that.
        # Unlike other registries, this one isn't primarily meant to allow lookups of
        # channels, but it could also be used for that if (later) exposed by a lookup
        # function.  The way of referencing a channel within Clockwork source is by
        # importing the module that defines it, so looking it up by channel name should
        # not be needed.
        self.channel_registry: dict[str, Channel] = {}

        # The system can only have one diagnostics channel and one bridge status channel.
        # These variables are used to store the diagnostics and bridge status channel
        # definitions as they are encountered during parsing.
        self.diagnostics_channel: Channel | None = None
        self.bridge_status_channel: Channel | None = None
        self.c2c_bridge_status_channel: Channel | None = None

    @override
    def import_from(self, other: ChannelRegistry) -> None:
        """Combine this registry with cached channels from another registry.

        Raises:
            RuntimeError: If a channel already exists with a different definition
        """
        for key, channel in other.channel_registry.items():
            if key in self.channel_registry and self.channel_registry[key] != channel:
                msg = f"Channel registry entry {key} has conflicting entry: {self.channel_registry[key]} vs {channel}\nWhen merging {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.channel_registry[key] = channel

        if other.bridge_status_channel and not self.bridge_status_channel:
            self.bridge_status_channel = other.bridge_status_channel

        if other.c2c_bridge_status_channel and not self.c2c_bridge_status_channel:
            self.c2c_bridge_status_channel = other.c2c_bridge_status_channel

        if other.diagnostics_channel and not self.diagnostics_channel:
            self.diagnostics_channel = other.diagnostics_channel


class ChannelRegistryKey(ContextKey[ChannelRegistry]):
    """CompilerContext Key for Channel Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ChannelRegistry:
        """Create a default instance of a Channel Registry."""
        return ChannelRegistry(compiler_context.name)


CHANNEL_REGISTRY_KEY: Final = ChannelRegistryKey("ChannelRegistryKey")
