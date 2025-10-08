# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""System Graph IR (Intermediate Representation).

The classes here implement a graph representation of a Clockwork system.  The
information contained here is entirely derived from (and so somewhat redundant
with) the information from the main compiler IR in clockwork.dsl.ir,
but that IR is still fairly coupled to the syntax.  The representation here
sheds most of the syntax ties, deals only in fully resolved entities, and has
better type information.  This is a representation that is more reasonable as a
starting point for tooling that wants to query and manipulate a system graph.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, TypeAlias, final

from clockwork.dsl.compiler_context import Context, ContextKey
from clockwork.dsl.ir import (
    audio,
    box,
    clkbuiltins,
    cog,
    diagnostics,
    extern_type,
    node,
    primitive,
    pubsub,
    representation,
    schema_reg,
    typesys,
    udp,
)
from clockwork.dsl.serialization import tachyon_reg
from typing_extensions import override

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl import compiler_context
    from clockwork.dsl.compiler_context import CompilerContext


@final
class GraphirChannelRegistry(Context):
    """GraphIR Channel Registry."""

    def __init__(self, name: str | None) -> None:
        """Create a new GraphIR channel registry."""
        self.name = name
        self.channel_registry: dict[str, Channel] = {}

    @override
    def import_from(self, other: GraphirChannelRegistry) -> None:
        """Combine this channel registry with cached channels from another registry.

        Raises:
            RuntimeError: If a channel already exists with a different definition.
        """
        for key, channel in other.channel_registry.items():
            if key in self.channel_registry and self.channel_registry[key] != channel:
                msg = f"Channel registry entry {key} has conflicting entry: {self.channel_registry[key]} vs {channel}\nWhen merging {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.channel_registry[key] = channel


class GraphirChannelRegistryKey(ContextKey[GraphirChannelRegistry]):
    """CompilerContext Key for GraphIR Channel Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> GraphirChannelRegistry:
        """Create a default instance of a GraphIR Channel Registry."""
        return GraphirChannelRegistry(compiler_context.name)


GRAPHIR_CHANNEL_REGISTRY_KEY: Final = GraphirChannelRegistryKey("GraphirChannelRegistryKey")


@dataclass(slots=True, frozen=True)
class DocableEntity:
    """Base class for things which can optionally have a Doc node."""

    doc: node.Doc | None


@dataclass(slots=True, frozen=True)
class DocRequiredEntity:
    """Base class for things which must have a Doc node."""

    doc: node.Doc


@dataclass(slots=True, frozen=True)
class MetricsChannel:
    """GraphIR Node representing a metrics channel."""

    channel_name: str
    message_repr: representation.ResolvedReprInstantiation
    log_type: cog.MetricsLogType
    message_size: int
    num_slots: int
    uuid: UUID
    cog_path: str
    cog_instance_path: str

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}({self.channel_name})"


def get_representation_size(
    compiler_context: compiler_context.CompilerContext, message_repr: representation.ResolvedReprInstantiation
) -> int | None:
    """Get the size of a representation."""
    if message_repr.typespec.instantiates is clkbuiltins.TACHYON:
        schema_type = message_repr.schema_ir
        constraint = tachyon_reg.constraint_for_type(compiler_context, schema_type)
        if constraint is None:
            return None
        return constraint.size
    msg = f"Unsupported message type {message_repr.typespec}"
    raise RuntimeError(msg)


@dataclass(slots=True, frozen=True)
class Channel(DocRequiredEntity):
    """GraphIR Node representing a pub/sub channel."""

    channel_name: str
    message_repr: representation.ResolvedReprInstantiation
    message_size: int
    num_slots: int
    is_multi_publisher: bool
    is_diagnostics: bool
    is_bridge_status: bool
    enforce_backwards_compatibility: bool
    ir_node: pubsub.Channel

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}({self.channel_name})"

    @classmethod
    def from_ir(cls: type[Channel], ir_node: pubsub.Channel) -> Channel:
        """Construct a GraphIR node from an IR node."""
        if (
            not isinstance(ir_node.channel_name, primitive.StringValue)
            or not isinstance(ir_node.message_repr, representation.ResolvedReprInstantiation)
            or not isinstance(ir_node.message_size, primitive.DecimalValue)
            or not isinstance(ir_node.num_slots, primitive.DecimalValue)  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        ):
            msg = f"Attempt to convert unresolved Channel: {ir_node}"
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is appropriate because this is a compiler bug)
        return cls(
            doc=ir_node.doc,
            channel_name=ir_node.channel_name.value,
            message_repr=ir_node.message_repr,
            message_size=int(ir_node.message_size.value),
            num_slots=int(ir_node.num_slots.value),
            is_multi_publisher=ir_node.publishers_option == pubsub.ChannelPublishersOption.multiple,
            is_diagnostics=ir_node.is_diagnostics,
            is_bridge_status=ir_node.is_bridge_status,
            enforce_backwards_compatibility=ir_node.enforce_backwards_compatibility,
            ir_node=ir_node,
        )


def lookup_channel(key: str | pubsub.Channel, compiler_context: CompilerContext, create_if_new: bool = True) -> Channel:
    """Look up a GraphIR channel for an IR channel.

    Args:
        key: Either a channel name or an IR Channel object.
        compiler_context: Context to look up channel in.
        create_if_new: If True, create and register and return a new GraphIR node.

    Raises:
        KeyError if create_if_new is False and there's no GraphIR node yet, or if there's no underlying IR Channel.
    """
    registry = compiler_context[GRAPHIR_CHANNEL_REGISTRY_KEY]
    if isinstance(key, pubsub.Channel):
        if not isinstance(key.channel_name, primitive.StringValue):
            msg = f"Attempt to look up unresolved channel {key}"
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is appropriate because this is a compiler bug)
        key = key.channel_name.value
    try:
        return registry.channel_registry[key]

    except KeyError:
        if not create_if_new:
            raise
    result = Channel.from_ir(pubsub.lookup_channel(key, compiler_context))
    registry.channel_registry[key] = result
    return result


def diagnostics_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the GraphIR diagnostics channel for the IR diagnostics channel."""
    ir_channel = pubsub.diagnostics_channel(compiler_context)
    if not ir_channel:
        return None
    return lookup_channel(ir_channel, compiler_context)


def bridge_status_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the GraphIR bridge status channel for the IR bridge status channel."""
    ir_channel = pubsub.bridge_status_channel(compiler_context)
    if not ir_channel:
        return None
    return lookup_channel(ir_channel, compiler_context)


@dataclass(slots=True, frozen=True)
class ChannelSubscribeConnection:
    """A connection that subscribes to a Channel."""

    channel: Channel
    ir_node: box.Connection


@dataclass(slots=True, frozen=True)
class ChannelToCogSubscribeConnection(ChannelSubscribeConnection):
    """A connection for a Cog to subscribe to a channel."""

    cog_instance_member: cog.CogInstanceMember[cog.InputDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return (
            f"{type(self).__name__}(channel={self.channel}, cog_instance_member={self.cog_instance_member.member.fqn})"
        )


@dataclass(slots=True, frozen=True)
class ChannelPublishConnection:
    """A connection that publishes to a Channel."""

    channel: Channel
    ir_node: box.Connection


@dataclass(slots=True, frozen=True)
class ChannelToCogPublishConnection(ChannelPublishConnection):
    """A connection for a Cog to publish to a channel."""

    cog_instance_member: cog.CogInstanceMember[cog.OutputDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return (
            f"{type(self).__name__}(channel={self.channel}, cog_instance_member={self.cog_instance_member.member.fqn})"
        )


@dataclass(slots=True, frozen=True)
class ChannelToUdpPublishConnection(ChannelPublishConnection):
    """A connection for a UDP socket to publish to a channel."""

    socket_endpoint: udp.UdpSocketEndpointInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(channel={self.channel}, socket_instance={self.socket_endpoint.socket.endpoint})"


@dataclass(slots=True, frozen=True)
class ChannelToUdpSubscribeConnection(ChannelSubscribeConnection):
    """A connection for a UDP socket to subscribe to a channel."""

    socket_endpoint: udp.UdpSocketEndpointInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(channel={self.channel}, socket_instance={self.socket_endpoint.socket.endpoint})"


@dataclass(slots=True, frozen=True)
class ChannelToAudioPublishConnection(ChannelPublishConnection):
    """A connection for an audio source to publish to a channel."""

    source_instance: audio.AudioSourceInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(channel={self.channel}, source_instance={self.source_instance.source.endpoint})"


@dataclass(slots=True, frozen=True)
class ChannelToDiagnosticsPublish(ChannelPublishConnection):
    """A connection for a diagnostics source to publish to a channel."""

    diagnostics_instance: diagnostics.DiagnosticsInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        group_id, instance_id = (
            self.diagnostics_instance.diagnostics.group_id,
            self.diagnostics_instance.diagnostics.instance_id,
        )
        return f"{type(self).__name__}(channel={self.channel}, diagnostics_instance=({group_id}, {instance_id}))"


@dataclass(slots=True, frozen=True)
class ConfigConnection:
    """A connection between a config file and a Cog config endpoint."""

    config_instance: box.SerializedDataFileInstance
    cog_instance_member: cog.CogInstanceMember[cog.ConfigDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(config={self.config_instance}, cog_instance_member={self.cog_instance_member.member.fqn})"


@dataclass(slots=True, frozen=True)
class StateConnection:
    """A connection between a state instance and a Cog state endpoint."""

    state_instance: box.StateInstance
    cog_instance_member: cog.CogInstanceMember[cog.StateDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(state={self.state_instance}, cog_instance_member={self.cog_instance_member.member.fqn})"


@dataclass(slots=True, frozen=True)
class MemoryResourceConnection:
    """A connection between a memory resource and a Cog memory endpoint."""

    memory_resource: box.MemoryResourceInstance
    cog_instance_member: cog.CogInstanceMember[cog.ResourceDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(memory_resource={self.memory_resource}, cog_instance_member={self.cog_instance_member.member.fqn})"


ConnectionType: TypeAlias = (
    ChannelSubscribeConnection
    | ChannelPublishConnection
    | ConfigConnection
    | StateConnection
    | MemoryResourceConnection
)


def from_ir_connection(  # noqa: C901, PLR0911, PLR0912, PLR0915  TODO(OI-3057): Refactor this function to reduce complexity
    ir_node: box.Connection, compiler_context: CompilerContext
) -> ConnectionType:
    """Construct a GraphIR connection of the appropriate class from an IR Connection.

    Parameters:
        ir_node: IR Connection to generate GraphIR from.
        compiler_context: Compiler context to use for looking up channels.
    """
    if isinstance(ir_node.source, cog.CogInstanceMember):
        if not isinstance(ir_node.source.member, cog.OutputDef | diagnostics.DiagnosticsDef):
            msg = ir_node.append_error_line(
                f"Connection source must be a Cog output or diagnostics, not {ir_node.source.member}"
            )
            raise TypeError(msg)
        if not isinstance(ir_node.target, pubsub.Channel):
            msg = ir_node.append_error_line(f"Connection target must be a Channel, not {ir_node.target}")
            raise TypeError(msg)
        channel = lookup_channel(ir_node.target, compiler_context)
        if channel.message_repr.typespec.value_key() != ir_node.source.member.get_representation_typespec().value_key():
            msg = f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to output type {ir_node.source.member.get_representation_typespec().value_key()}"
            raise TypeError(msg)
        return ChannelToCogPublishConnection(channel=channel, cog_instance_member=ir_node.source, ir_node=ir_node)
    if isinstance(ir_node.source, pubsub.Channel):
        channel = lookup_channel(ir_node.source, compiler_context)
        if isinstance(ir_node.target, udp.UdpSocketInstance):
            if not isinstance(channel.message_repr.typespec, typesys.TypeVal):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                msg = "Unable to connect unresolved channel to udp socket."
                raise TypeError(msg)
            if message_error := udp.validate_message_type(
                channel.message_repr.typespec, ir_node.target.socket, channel.channel_name
            ):
                msg = ir_node.append_error_line(message_error)
                raise TypeError(msg)
            if not ir_node.target.observer_endpoint:
                msg = ir_node.append_error_line(
                    "Attempting to connect a UDP socket to a channel without an observer endpoint."
                )
                raise TypeError(msg)
            return ChannelToUdpSubscribeConnection(
                channel=channel, socket_endpoint=ir_node.target.observer_endpoint, ir_node=ir_node
            )
        if not isinstance(ir_node.target, cog.CogInstanceMember) or not isinstance(ir_node.target.member, cog.InputDef):
            target_name = (
                ir_node.target.name if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = ir_node.append_error_line(f"Connection target must be a Cog input, not {target_name}")
            raise TypeError(msg)
        if channel.message_repr.typespec.value_key() != ir_node.target.member.get_representation_typespec().value_key():
            msg = f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to input type {ir_node.target.member.get_representation_typespec().value_key()}"
            raise TypeError(msg)
        if channel.is_multi_publisher and not ir_node.target.member.view_params.no_dial:
            msg = f"Attempt to connect multi publisher channel '{channel.channel_name}' to dial input '{ir_node.target.member.name}'"
            raise TypeError(msg)
        return ChannelToCogSubscribeConnection(channel=channel, cog_instance_member=ir_node.target, ir_node=ir_node)
    if isinstance(ir_node.source, audio.AudioSourceInstance):
        if not isinstance(ir_node.target, pubsub.Channel):
            target_name = (
                ir_node.target.name if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = ir_node.append_error_line(f"Connection target must be a Channel, not {target_name}")
            raise TypeError(msg)
        channel = lookup_channel(ir_node.target, compiler_context)
        if not isinstance(channel.message_repr.typespec, typesys.TypeVal):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Unable to connect unresolved channel to audio source."
            raise TypeError(msg)
        if message_error := audio.validate_message_type(
            channel.message_repr.typespec, ir_node.source.source, channel.channel_name
        ):
            msg = ir_node.append_error_line(message_error)
            raise TypeError(msg)
        return ChannelToAudioPublishConnection(channel=channel, source_instance=ir_node.source, ir_node=ir_node)
    if isinstance(ir_node.source, udp.UdpSocketInstance):
        if not isinstance(ir_node.target, pubsub.Channel):
            target_name = (
                ir_node.target.name if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = ir_node.append_error_line(f"Connection target must be a Channel, not {target_name}")
            raise TypeError(msg)
        channel = lookup_channel(ir_node.target, compiler_context)
        if not isinstance(channel.message_repr.typespec, typesys.TypeVal):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Unable to connect unresolved channel to udp socket."
            raise TypeError(msg)
        if message_error := udp.validate_message_type(
            channel.message_repr.typespec, ir_node.source.socket, channel.channel_name
        ):
            msg = ir_node.append_error_line(message_error)
            raise TypeError(msg)
        if not ir_node.source.producer_endpoint:
            msg = ir_node.append_error_line(
                "Attempting to connect a UDP socket to a channel without a producer endpoint."
            )
            raise TypeError(msg)
        return ChannelToUdpPublishConnection(
            channel=channel, socket_endpoint=ir_node.source.producer_endpoint, ir_node=ir_node
        )
    if isinstance(ir_node.source, box.SerializedDataFileInstance):
        if not isinstance(ir_node.target, cog.CogInstanceMember) or not isinstance(
            ir_node.target.member, cog.ConfigDef
        ):
            target_name = (
                ir_node.target.name if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = f"SerializedDataFile can only be connected to a config endpoint, not {target_name}"
            raise TypeError(msg)
        # TODO(OI-2013): Validate config representation/type; blocked on Protobuf converters landing
        return ConfigConnection(config_instance=ir_node.source, cog_instance_member=ir_node.target)
    if isinstance(ir_node.source, box.StateInstance):
        if not isinstance(ir_node.target, cog.CogInstanceMember) or not isinstance(ir_node.target.member, cog.StateDef):
            target_name = (
                ir_node.target.name if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = f"State can only be connected to a state endpoint, not {target_name}"
            raise TypeError(msg)
        return _validate_state_connection(ir_node, ir_node.source, ir_node.target)
    if isinstance(ir_node.source, box.MemoryResourceInstance):
        if not isinstance(ir_node.target, cog.CogInstanceMember) or not isinstance(
            ir_node.target.member, cog.ResourceDef
        ):
            target_name = (
                ir_node.target.fqn if isinstance(ir_node.target, node.NamedEntity) else str(type(ir_node.target))
            )
            msg = f"Memory resource can only be connected to a memory endpoint, not {target_name}"
            raise TypeError(msg)
        return MemoryResourceConnection(memory_resource=ir_node.source, cog_instance_member=ir_node.target)
    if isinstance(ir_node.source, diagnostics.DiagnosticsInstance):
        if not isinstance(ir_node.target, pubsub.Channel):
            msg = ir_node.append_error_line(f"Connection target must be a Channel, not {ir_node.target}")
            raise TypeError(msg)
        channel = lookup_channel(ir_node.target, compiler_context)
        if (
            channel.message_repr.typespec.value_key()
            != ir_node.source.diagnostics.get_representation_typespec().value_key()
        ):
            msg = f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to output type {ir_node.source.diagnostics.get_representation_typespec().value_key()}"
            raise TypeError(msg)
        return ChannelToDiagnosticsPublish(channel=channel, diagnostics_instance=ir_node.source, ir_node=ir_node)
    msg = ir_node.append_error_line(f"Unsupported connection type: {ir_node}")
    raise NotImplementedError(msg)


def _validate_state_connection(
    ir_node: box.Connection, state_ir: box.StateInstance, instance_member: cog.CogInstanceMember[cog.StateDef]
) -> StateConnection:
    if isinstance(instance_member.member.message_type, schema_reg.InterfaceInfo):
        assert isinstance(
            instance_member.member.message_type.interface_ir.representation, representation.RepresentationReference
        )
        if (source_key := state_ir.repr_typespec.value_key()) != (
            target_key := instance_member.member.message_type.interface_ir.representation.typespec.value_key()
        ):
            msg = ir_node.append_error_line(f"State type {source_key} does not match endpoint type {target_key}")
            raise TypeError(msg)
    elif isinstance(instance_member.member.message_type, extern_type.ExternType):
        if (source_key := state_ir.repr_typespec.value_key()) != (
            target_key := instance_member.member.message_type.value_key()
        ):
            msg = ir_node.append_error_line(f"State type {source_key} does not match endpoint type {target_key}")
            raise TypeError(msg)
    else:
        msg = ir_node.append_error_line(f"Unsupported message type {instance_member.member.message_type}")
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is appropriate because this is a compiler bug)
    return StateConnection(state_instance=state_ir, cog_instance_member=instance_member)
