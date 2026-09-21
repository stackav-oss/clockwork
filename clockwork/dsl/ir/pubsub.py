# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Pub/Sub graph related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from enum import Enum
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.composition.channel_config_proto import ChannelTypeEnum
from clockwork.dsl.ir import (
    clkbuiltins,
    expr,
    fmt_string,
    node,
    primitive,
    representation,
    schema_reg,
    statement,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.serialization import tachyon_reg
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence


class ChannelPublishersOption(Enum):
    """Publishers option for a channel."""

    single = 0
    multiple = 1


@dataclass
class ChannelParameter(node.CstNode[cst.ChannelParameter], node.DocableEntity):
    """IR Node representing a channel parameter."""

    cur_name: str
    type_info: typesys.TypeVal | expr.TypeExpression


def _parse_parameters_block(
    parameters_block: cst.ChannelParametersBlock | None, scope: node.Scope, module: node.Module
) -> dict[str, ChannelParameter] | None:
    if module.terminals is None:
        msg = "Cannot construct IR nodes from CST without a TerminalSource"
        raise ValueError(msg)

    if not parameters_block:
        return None

    parameters: dict[str, ChannelParameter] = {}
    for cst_param in parameters_block.children_channel_parameter():
        param_name = get_span(cst_param.child_name().child_value(), module.terminals)
        if param_name in parameters:
            msg = node.append_error_line(cst_param, module, f"Duplicate parameter name '{param_name}'")
            raise ValueError(msg)
        param_doc = node.Doc.maybe_from_cst(cst_param.maybe_doc(), module)
        param_type = expr.TypeExpression.make(expr.Expr.from_cst(cst_param.child_typespec(), module))
        param = ChannelParameter(
            module=module,
            cst_node=cst_param,
            doc=param_doc,
            cur_name=param_name,
            type_info=param_type,
        )
        parameters[param_name] = param
        # Define parameter in inner scope for use by channel_name, message_type, etc.
        param_ref = ChannelParameterRef(
            name=param_name,
            scope=scope,
            type_info=typesys.InferenceVar.make(context=module, cst_node=cst_param),
            parameter_def=param,
        )
        typesys.unify(param_ref.type_info, param_type.inference_var)
        scope.define(param_name, param_ref, module.terminals)

    if not parameters:
        msg = node.append_error_line(parameters_block, module, "A channel parameter block cannot be empty.")
        raise ValueError(msg)

    return parameters


num_slots_buffer: Final = Decimal(1)
"""A small buffer to add to the number of slots.

Allows publishers to always have at least 1 message to reserve without
interfering with the subscriber.
"""


@dataclass
class Channel(node.CstNode[cst.Channel], node.DocRequiredEntity, node.NamedEntity, typesys.TypeVal):
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
    is_simplelaunch_status: bool
    enforce_backwards_compatibility: bool
    is_bulk_data: bool
    channel_type: str
    parameters: dict[str, ChannelParameter] | None
    inner_scope: node.Scope

    @classmethod
    def from_cst(cls: type[Channel], cst_node: cst.Channel, module: node.Module, scope: node.Scope) -> Channel:  # noqa: C901, PLR0912, PLR0915 simply due to handling many independent properties
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)

        inner_scope = scope.make_child_scope(name)
        parameters = _parse_parameters_block(cst_node.maybe_channel_parameters_block(), inner_scope, module)

        channel_name: expr.Expr | primitive.StringValue = (
            expr.Expr.from_cst(channel_name_cst.child_value(), module)
            if (channel_name_cst := cst_node.maybe_channel_name())
            else primitive.StringValue.make(name)
        )

        if not parameters:
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
            if not cst_publishers_option
            or cst_publishers_option.maybe_single()
            or cst_publishers_option.maybe_simplelaunch_status()
            else ChannelPublishersOption.multiple
        )
        if is_published_once and publishers_option != ChannelPublishersOption.single:
            msg = node.append_error_line(
                cst_publishers_option, module, "Cannot have multiple publishers when channel is published once"
            )
            raise ValueError(msg)
        if publishers_option != ChannelPublishersOption.single and parameters:
            msg = node.append_error_line(
                cst_publishers_option,
                module,
                "Generic channels are currently only supported for single publishers.",
            )
            raise ValueError(msg)
        is_diagnostics = bool(cst_publishers_option and cst_publishers_option.maybe_diagnostics())
        is_bridge_status = bool(cst_publishers_option and cst_publishers_option.maybe_bridge_status())
        is_c2c_bridge_status = bool(cst_publishers_option and cst_publishers_option.maybe_c2c_bridge_status())
        is_simplelaunch_status = bool(cst_publishers_option and cst_publishers_option.maybe_simplelaunch_status())
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
        channel_type = "unspecified"
        if maybe_channel_type := cst_node.maybe_channel_option_channel_type():
            channel_type = get_span(maybe_channel_type.child_identifier().child_value(), module.terminals)
            if channel_type not in ChannelTypeEnum.__annotations__:
                msg = node.append_error_line(
                    maybe_channel_type,
                    module,
                    "channel_type must be one of: unspecified, shared_memory, gpu",
                )
                raise ValueError(msg)
        result = cls(
            type_info=clkbuiltins.TYPE_TYPE if parameters else clkbuiltins.CHANNEL_TYPE,
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
            is_simplelaunch_status=is_simplelaunch_status,
            enforce_backwards_compatibility=enforce_backwards_compatibility,
            is_bulk_data=is_bulk_data,
            channel_type=channel_type,
            parameters=parameters,
            inner_scope=inner_scope,
        )
        scope.define(name, result, module.terminals)
        return result

    def is_generic(self) -> bool:
        """Return True if this channel has parameters (is generic)."""
        return self.parameters is not None and len(self.parameters) > 0

    @override
    def concrete_type_info(self) -> typesys.TypeVal | typesys.InferenceVar:
        """Return CHANNEL_TYPE regardless of whether this channel is generic."""
        return clkbuiltins.CHANNEL_TYPE

    @override
    def generic_parameters(self) -> Sequence[typesys.Parameter] | None:
        """Get the generic parameters for the channel.

        Returns:
            The generic parameters, or None if the channel is not generic.
        """
        if not self.parameters:
            return None

        params = []
        for _, param in sorted(self.parameters.items()):
            type_info = param.type_info
            if isinstance(type_info, expr.TypeExpression):
                type_info = type_info.evaluate()
            assert isinstance(type_info, typesys.TypeVal)
            optional = isinstance(type_info, typesys.Instantiation) and type_info.instantiates is clkbuiltins.OPTIONAL
            params.append(
                typesys.Parameter(
                    name=param.cur_name,
                    type_bound=type_info,
                    default=None,
                    is_optional=optional,
                )
            )
        return params

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

    def _validate_simplelaunch_status(self) -> bool:
        if not self.parameters or len(self.parameters) != 1 or "cpu" not in self.parameters:
            return False
        type_info = self.parameters["cpu"].type_info
        if isinstance(type_info, expr.TypeExpression):
            type_info = type_info.evaluate()
        return type_info is clkbuiltins.CPU_DOMAIN_TYPE

    def _register_generic(self) -> None:
        # Most generic channels are not registered - they serve as templates
        if self.is_simplelaunch_status:
            if not self._validate_simplelaunch_status():
                msg = self.append_error_line(
                    "Simplelaunch status channels must have single 'cpu' parameter with type CpuDomain."
                )
                raise ValueError(msg)
            registry = self.module.context[CHANNEL_REGISTRY_KEY]
            if registry.simplelaunch_status_channel:
                msg = self.append_error_line(
                    f"Attempt to define multiple simplelaunch status channels {self.channel_name} and {registry.simplelaunch_status_channel.channel_name}"
                )
                raise ValueError(msg)
            registry.simplelaunch_status_channel = self

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.message_size, expr.Expr | None):
            msg = self.append_error_line(f"Attempt to resolve Channel twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)

        if self.is_generic():
            self._register_generic()
            return

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
            type_info=self.num_slots_expr.type_info, value=self.num_slots_expr.value + num_slots_buffer
        )

    def _resolve_message_type(self) -> primitive.DecimalValue | None:
        assert isinstance(self.message_type, expr.TypeExpression)
        message_type = self.message_type.evaluate()
        if isinstance(message_type, typesys.DeferrableType):
            msg = self.message_type.append_error_line("Channel type may not be a generic parameter.")
            raise TypeError(msg)
        repr_info = get_message_repr_info(message_type, self.message_type, self.module.context)
        expected_message_size = get_message_type_size(message_type, self.message_type, self.module.context)
        self.message_type = message_type
        self.message_repr = repr_info.representation_ir
        return expected_message_size

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        # For generic channels, use the channel name from the module scope
        if self.is_generic():
            return f"Channel({self.module.inner_scope.uniq_path}::{self.name})"
        if not isinstance(self.channel_name, primitive.StringValue):
            msg = self.append_error_line("Attempt to generate value key for unresolved Channel")
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved channel is a runtime error)
        return f"Channel({self.channel_name.value})"


def get_message_repr_info(
    message_type: typesys.Value, cst_node: node.CstNode[cst.Expr], context: CompilerContext
) -> schema_reg.RepresentationInfo:
    """Helper function to lookup the RepresentationInfo for a type."""
    repr_ref = representation.RepresentationReference.from_typespec(message_type)
    if isinstance(repr_ref, str):
        msg = cst_node.append_error_line(repr_ref)
        raise TypeError(msg)
    repr_info = schema_reg.lookup_representation(context, repr_ref)
    if repr_info is None:
        msg = cst_node.append_error_line("Representation not instantiated or instantiation not visible here.")
        raise ValueError(msg)
    return repr_info


def get_message_type_size(
    message_type: typesys.Value, cst_node: node.CstNode[cst.Expr], context: CompilerContext
) -> primitive.DecimalValue | None:
    """Helper function to lookup the size of a message type."""
    repr_info = get_message_repr_info(message_type, cst_node, context)
    assert isinstance(repr_info.representation_ir.typespec, typesys.Instantiation)
    if repr_info.representation_ir.typespec.instantiates is clkbuiltins.TACHYON:
        schema_type = repr_info.representation_ir.schema_ir
        constraint = tachyon_reg.constraint_for_type(context, schema_type)
        if constraint is None:
            # Should be impossible except for a bug in the compiler
            msg = cst_node.append_error_line(f"Missing Tachyon constraint for {repr_info}")
            raise RuntimeError(msg)
        return primitive.DecimalValue(type_info=clkbuiltins.UINT32, value=Decimal(constraint.size))
    return None


@dataclass
class ChannelParameterRef(node.NamedEntity, typesys.DeferrableType):
    """A reference to a generic channel parameter."""

    parameter_def: ChannelParameter

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        msg = f"Attempt to generate a value key for an unsubstituted channel parameter: {self}"
        raise RuntimeError(msg)


@dataclass
class InstantiatedChannel(typesys.Value):
    """A fully instantiated channel with all parameter substitutions resolved.

    This represents a generic channel that has been instantiated with concrete
    values for all of its parameters. All fields that depend on parameters have
    been resolved to their final values.
    """

    channel_name: primitive.StringValue
    message_type: typesys.TypeVal
    message_repr: representation.ResolvedReprInstantiation | None
    message_size: primitive.DecimalValue | None
    num_slots: primitive.DecimalValue
    is_published_once: bool
    publishers_option: ChannelPublishersOption
    is_diagnostics: bool
    is_bridge_status: bool
    is_c2c_bridge_status: bool
    is_simplelaunch_status: bool
    enforce_backwards_compatibility: bool
    is_bulk_data: bool
    channel_type: str
    channel: Channel
    arguments: dict[str, typesys.Value]

    @classmethod
    def from_instantiation(
        cls: type[InstantiatedChannel],
        instantiation: typesys.Instantiation,
        calling_context: CompilerContext | None = None,
    ) -> InstantiatedChannel:
        """Create an InstantiatedChannel from a channel instantiation.

        Args:
            instantiation: An Instantiation whose .instantiates is a Channel.
            calling_context: Optional compiler context from the module where the
                channel is being instantiated. When provided, this context is used
                for representation lookups, allowing the message type to reference
                schemas imported by the calling module but not by the channel's
                defining module.

        Returns:
            A fully resolved InstantiatedChannel.

        Raises:
            TypeError: If the instantiation is not for a Channel.
            ValueError: If required parameters are missing or invalid.
        """
        if not isinstance(instantiation.instantiates, Channel):
            msg = f"Expected Channel instantiation, got {type(instantiation.instantiates)}"
            raise TypeError(msg)

        channel = instantiation.instantiates
        if not channel.is_generic():
            msg = "Cannot instantiate a non-generic channel"
            raise ValueError(msg)

        args = instantiation.arguments
        # Evaluate any immutable bindings in the arguments. Module-level constants
        # are represented as ImmutableBinding nodes that wrap the underlying value.
        args = {name: (val.value if isinstance(val, statement.ImmutableBinding) else val) for name, val in args.items()}
        assert channel.inner_scope.parent is not None
        scope = channel.inner_scope.parent.make_anon_child_scope(channel.name)
        for name, value in args.items():
            # Define the actual parameter values so they're looked up when
            # resolving any fmt strings as opposed to getting the parameters
            # themselves.
            scope.define(
                name,
                node.NamedBindingRef(name=name, value=value, scope=scope.make_child_scope(name)),
                channel.module.terminals,
            )

        channel_name = _resolve_channel_name(channel, args, scope)

        message_type = _resolve_message_type_from_args(channel, args)

        # Resolve message representation. Use the calling module's context when
        # available, because the message type may reference schemas imported by the
        # calling module but not by the channel's defining module.
        lookup_context = calling_context if calling_context is not None else channel.module.context

        repr_ref = representation.RepresentationReference.from_typespec(message_type)
        if isinstance(repr_ref, str):
            msg = f"Failed to create representation reference: {repr_ref}"
            raise TypeError(msg)
        repr_info = schema_reg.lookup_representation(lookup_context, repr_ref)
        message_repr = repr_info.representation_ir if repr_info else None

        num_slots = _resolve_num_slots(channel, args)

        assert isinstance(channel.message_type, expr.TypeExpression)

        instantiated_channel = cls(
            type_info=clkbuiltins.CHANNEL_TYPE,
            channel_name=channel_name,
            message_type=message_type,
            message_repr=message_repr,
            message_size=get_message_type_size(message_type, channel.message_type, lookup_context),
            num_slots=num_slots,
            is_published_once=channel.is_published_once,
            publishers_option=channel.publishers_option,
            is_diagnostics=channel.is_diagnostics,
            is_bridge_status=channel.is_bridge_status,
            is_c2c_bridge_status=channel.is_c2c_bridge_status,
            is_simplelaunch_status=channel.is_simplelaunch_status,
            enforce_backwards_compatibility=channel.enforce_backwards_compatibility,
            is_bulk_data=channel.is_bulk_data,
            channel_type=channel.channel_type,
            channel=channel,
            arguments=dict(args),
        )

        registry = channel.module.context[CHANNEL_REGISTRY_KEY].channel_registry
        registry.setdefault(instantiated_channel.channel_name.value, instantiated_channel)
        entry = registry[instantiated_channel.channel_name.value]
        assert isinstance(entry, InstantiatedChannel)
        if entry.value_key() != instantiated_channel.value_key():
            msg = node.enrich_error_if_possible(
                instantiation,
                "Instantiated channel produces a channel name that is already registered with different parameters.",
            )
            raise RuntimeError(msg)
        return entry

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return f"Channel({self.channel_name.value})"

    @property
    def doc(self) -> node.Doc:
        """Get the underlying channel doc string."""
        return self.channel.doc


def _resolve_channel_name(
    channel: Channel, args: Mapping[str, typesys.Value], scope: node.Scope
) -> primitive.StringValue:
    """Resolve the channel name, substituting parameters as needed.

    Args:
        channel: The generic channel being instantiated.
        args: Channel parameters.
        scope: Scope used to evaluate any substitutions.

    Returns:
        The resolved channel name as a StringValue.
    """
    if isinstance(channel.channel_name, primitive.StringValue):
        return channel.channel_name

    # channel_name is an expr.Expr that needs to be evaluated
    assert isinstance(channel.channel_name, expr.Expr)
    evaluated = channel.channel_name.evaluate()

    if isinstance(evaluated, primitive.StringValue):
        return evaluated
    if isinstance(evaluated, fmt_string.UnevaluatedFmtString):
        return evaluated.evaluate_from_scope(scope)

    if isinstance(evaluated, ChannelParameterRef):
        param_name = evaluated.parameter_def.cur_name
        if param_name not in args:
            msg = f"Parameter '{param_name}' not found in instantiation arguments"
            raise ValueError(msg)
        result = args[param_name]
        if not isinstance(result, primitive.StringValue):
            msg = f"Parameter '{param_name}' must be a string value, got {type(result)}"
            raise TypeError(msg)
        return primitive.StringValue(type_info=result.type_info, value=result.value)

    msg = f"Cannot resolve channel name from {type(evaluated)}"
    raise TypeError(msg)


def _resolve_message_type_from_args(channel: Channel, args: Mapping[str, typesys.Value]) -> typesys.TypeVal:
    """Resolve the message type, substituting parameters as needed.

    Args:
        channel: The generic channel being instantiated.
        args: The instantiation arguments.

    Returns:
        The resolved message type.
    """
    if isinstance(channel.message_type, typesys.TypeVal):
        # Check if it's an instantiation that depends on parameters
        if isinstance(channel.message_type, typesys.Instantiation):
            return _substitute_channel_params(channel.message_type, args, channel)
        return channel.message_type

    # message_type is an expr.TypeExpression that needs to be evaluated
    assert isinstance(channel.message_type, expr.TypeExpression)
    evaluated = channel.message_type.evaluate()

    if isinstance(evaluated, ChannelParameterRef):
        param_name = evaluated.parameter_def.cur_name
        if param_name not in args:
            msg = f"Parameter '{param_name}' not found in instantiation arguments"
            raise ValueError(msg)
        result = args[param_name]
        if not isinstance(result, typesys.TypeVal):
            msg = f"Parameter '{param_name}' must be a type, got {type(result)}"
            raise TypeError(msg)
        return result

    if isinstance(evaluated, typesys.Instantiation):
        return _substitute_channel_params(evaluated, args, channel)

    if isinstance(evaluated, typesys.TypeVal):
        return evaluated

    msg = f"Cannot resolve message type from {type(evaluated)}"
    raise TypeError(msg)


def _substitute_channel_params(
    type_info: typesys.TypeVal,
    args: Mapping[str, typesys.Value],
    error_node: node.CstNode[cst.Channel],
) -> typesys.TypeVal:
    """Substitute ChannelParameterRef values in types.

    Similar to schema.substitute_parameter_refs, but handles ChannelParameterRef
    instead of schema.ParameterRef.

    Args:
        type_info: Any type, possibly containing ChannelParameterRef values.
        args: Parameter names and values from the channel instantiation.
        error_node: Node to use for error reporting.

    Returns:
        A type with all parameters substituted.
    """
    if isinstance(type_info, ChannelParameterRef):
        param_name = type_info.parameter_def.cur_name
        if param_name not in args:
            msg = f"Parameter '{param_name}' not found in instantiation arguments"
            raise ValueError(msg)
        result = args[param_name]
        if not isinstance(result, typesys.TypeVal):
            msg = f"Parameter '{param_name}' must be a type, got {type(result)}"
            raise TypeError(msg)
        return result

    if not isinstance(type_info, typesys.Instantiation):
        return type_info

    # Recursively substitute in arguments
    result_args: dict[str, typesys.Value] = {}
    for arg_name, arg_val in type_info.arguments.items():
        if isinstance(arg_val, ChannelParameterRef):
            param_name = arg_val.parameter_def.cur_name
            if param_name not in args:
                msg = f"Parameter '{param_name}' not found in instantiation arguments"
                raise ValueError(msg)
            result = args[param_name]
            result_args[arg_name] = result
        elif isinstance(arg_val, typesys.Instantiation):
            result_args[arg_name] = _substitute_channel_params(arg_val, args, error_node)
        else:
            result_args[arg_name] = arg_val

    return typesys.Instantiation(
        type_info=type_info.type_info,
        instantiates=type_info.instantiates,
        arguments=result_args,
    )


def _resolve_num_slots(channel: Channel, args: Mapping[str, typesys.Value]) -> primitive.DecimalValue:
    """Resolve the number of slots.

    Args:
        channel: The generic channel being instantiated.
        args: The instantiation arguments.

    Returns:
        The resolved number of slots.
    """
    if channel.is_published_once:
        return primitive.DecimalValue(type_info=clkbuiltins.UINT32, value=Decimal(1))

    if isinstance(channel.num_slots_expr, primitive.DecimalValue):
        # Add one so the requested max number of messages is always available.
        return primitive.DecimalValue(
            type_info=channel.num_slots_expr.type_info, value=channel.num_slots_expr.value + num_slots_buffer
        )

    if channel.num_slots_expr is None:
        msg = "num_slots_expr is None but channel is not published_once"
        raise ValueError(msg)

    assert isinstance(channel.num_slots_expr, expr.Expr)
    evaluated = channel.num_slots_expr.evaluate()

    if isinstance(evaluated, primitive.DecimalValue):
        return primitive.DecimalValue(type_info=evaluated.type_info, value=evaluated.value + Decimal(1))

    if isinstance(evaluated, ChannelParameterRef):
        param_name = evaluated.parameter_def.cur_name
        if param_name not in args:
            msg = f"Parameter '{param_name}' not found in instantiation arguments"
            raise ValueError(msg)
        result = args[param_name]
        if not isinstance(result, primitive.DecimalValue):
            msg = f"Parameter '{param_name}' must be a decimal value, got {type(result)}"
            raise TypeError(msg)
        return primitive.DecimalValue(type_info=result.type_info, value=result.value + num_slots_buffer)

    msg = f"Cannot resolve num_slots from {type(evaluated)}"
    raise TypeError(msg)


def lookup_channel(channel_name: str, compiler_context: CompilerContext) -> Channel | InstantiatedChannel:
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


def simplelaunch_status_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the simplelaunch status channel."""
    registry = compiler_context[CHANNEL_REGISTRY_KEY]
    return registry.simplelaunch_status_channel


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
        self.channel_registry: dict[str, Channel | InstantiatedChannel] = {}

        # The system can only have one diagnostics channel and one bridge status channel.
        # These variables are used to store the diagnostics and bridge status channel
        # definitions as they are encountered during parsing.
        self.diagnostics_channel: Channel | None = None
        self.bridge_status_channel: Channel | None = None
        self.c2c_bridge_status_channel: Channel | None = None
        self.simplelaunch_status_channel: Channel | None = None

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

        if other.simplelaunch_status_channel and not self.simplelaunch_status_channel:
            self.simplelaunch_status_channel = other.simplelaunch_status_channel

        if other.diagnostics_channel and not self.diagnostics_channel:
            self.diagnostics_channel = other.diagnostics_channel


class ChannelRegistryKey(ContextKey[ChannelRegistry]):
    """CompilerContext Key for Channel Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ChannelRegistry:
        """Create a default instance of a Channel Registry."""
        return ChannelRegistry(compiler_context.name)


CHANNEL_REGISTRY_KEY: Final = ChannelRegistryKey("ChannelRegistryKey")
