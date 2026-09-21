# Copyright 2025-2026 Stack AV Co.
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
from clockwork.dsl.composition.channel_config import ChannelType
from clockwork.dsl.ir import (
    aligner,
    audio,
    box,
    clkbuiltins,
    cog,
    cog_components,
    diagnostics,
    extern_type,
    hardware,
    node,
    primitive,
    pubsub,
    representation,
    schema,
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
    from clockwork.dsl.composition.channel_config_proto import ChannelType as ChannelTypeProto


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


def get_schema_size(
    compiler_context: compiler_context.CompilerContext, schema: schema.InstantiatedSchema
) -> int | None:
    """Get the size of a schema.

    Args:
        compiler_context: Compiler context to look up tachyon constraints in.
        schema: Schema to get size of.

    Returns: Size of the schema in bytes, or None if the size cannot be determined .
    """
    constraint = tachyon_reg.constraint_for_type(compiler_context, schema)
    if constraint is None:
        return None
    return constraint.size


@dataclass(slots=True, frozen=True)
class Channel(DocRequiredEntity):
    """GraphIR Node representing a pub/sub channel."""

    channel_name: str
    message_repr: representation.ResolvedReprInstantiation
    message_size: int
    num_slots: int
    is_published_once: bool
    is_multi_publisher: bool
    is_diagnostics: bool
    is_bridge_status: bool
    is_c2c_bridge_status: bool
    is_simplelaunch_status: bool
    enforce_backwards_compatibility: bool
    is_bulk_data: bool
    channel_type: ChannelTypeProto
    ir_node: pubsub.Channel | pubsub.InstantiatedChannel

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}({self.channel_name})"

    @classmethod
    def from_ir(cls: type[Channel], ir_node: pubsub.Channel | pubsub.InstantiatedChannel) -> Channel:
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
            is_published_once=ir_node.is_published_once,
            is_multi_publisher=ir_node.publishers_option == pubsub.ChannelPublishersOption.multiple,
            is_diagnostics=ir_node.is_diagnostics,
            is_bridge_status=ir_node.is_bridge_status,
            is_c2c_bridge_status=ir_node.is_c2c_bridge_status,
            is_simplelaunch_status=ir_node.is_simplelaunch_status,
            enforce_backwards_compatibility=ir_node.enforce_backwards_compatibility,
            is_bulk_data=ir_node.is_bulk_data,
            channel_type=getattr(ChannelType, ir_node.channel_type),
            ir_node=ir_node,
        )


def lookup_channel(
    key: str | pubsub.Channel | pubsub.InstantiatedChannel,
    compiler_context: CompilerContext,
    create_if_new: bool = True,
) -> Channel:
    """Look up a GraphIR channel for an IR channel.

    Args:
        key: Either a channel name or an IR Channel object.
        compiler_context: Context to look up channel in.
        create_if_new: If True, create and register and return a new GraphIR node.

    Raises:
        KeyError if create_if_new is False and there's no GraphIR node yet, or if there's no underlying IR Channel.
    """
    orig_key = key
    registry = compiler_context[GRAPHIR_CHANNEL_REGISTRY_KEY]
    if isinstance(key, pubsub.Channel | pubsub.InstantiatedChannel):
        if isinstance(key, pubsub.Channel) and key.generic_parameters():
            msg = f"Attempt to look up generic channel without being instantiated: {key}"
            raise RuntimeError(msg)
        if not isinstance(key.channel_name, primitive.StringValue):
            msg = f"Attempt to look up unresolved channel {key}"
            raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is appropriate because this is a compiler bug)
        key = key.channel_name.value
    try:
        return registry.channel_registry[key]

    except KeyError:
        if not create_if_new:
            raise

    if isinstance(orig_key, str):
        result = Channel.from_ir(pubsub.lookup_channel(key, compiler_context))
    else:
        result = Channel.from_ir(orig_key)
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


def c2c_bridge_status_channel(compiler_context: CompilerContext) -> Channel | None:
    """Get the GraphIR C2C bridge status channel for the IR C2C bridge status channel."""
    ir_channel = pubsub.c2c_bridge_status_channel(compiler_context)
    if not ir_channel:
        return None
    return lookup_channel(ir_channel, compiler_context)


def simplelaunch_status_channel(cpu_domain: hardware.CpuDomain, compiler_context: CompilerContext) -> Channel | None:
    """Get the GraphIR simplelaunch status channel for the IR simplelaunch status channel."""
    ir_channel = pubsub.simplelaunch_status_channel(compiler_context)
    if not ir_channel:
        return None
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=ir_channel,
        arguments={"cpu": cpu_domain},
    )
    instantiated_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation, compiler_context)
    return lookup_channel(instantiated_channel, compiler_context)


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
class MultiChannelSubscribeConnection:
    """A connection that subscribes to multiple channels."""

    channels: list[Channel]
    ir_node: box.Connection


@dataclass(slots=True, frozen=True)
class MultiChannelToCogSubscribeConnection(MultiChannelSubscribeConnection):
    """A connection for a Cog to subscribe to multiple channels."""

    cog_instance_member: cog.CogInstanceMember[cog.InputDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(channels={self.channels}, cog_instance_member={self.cog_instance_member.member.fqn})"


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
class ChannelToAlignedInputConnection(ChannelSubscribeConnection):
    """A connection from an alignment channel to a cog's aligned input.

    Represents a connection between an alignment output channel and a cog's aligned input.
    At system composition time, this expands into N+1 subscriptions:
    one for the alignment channel and one for each of the aligner's upstream channels.
    """

    cog_instance_member: cog.CogInstanceMember[cog_components.CogAlignedInputDef]

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return (
            f"{type(self).__name__}(channel={self.channel}, cog_instance_member={self.cog_instance_member.member.fqn})"
        )


@dataclass(slots=True, frozen=True)
class ConfigConnection:
    """A connection between a config file and a Cog config endpoint."""

    config_instance: box.FirstMessageInstance | box.SerializedDataFileInstance
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


@dataclass(slots=True, frozen=True)
class DataSourceFallbackConnection:
    """A connection to a data source's fallback endpoint."""

    data_source: box.FirstMessageInstance
    fallback_data_source: box.FirstMessageInstance | box.SerializedDataFileInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return (
            f"{type(self).__name__}(data_source={self.data_source}, fallback_data_source={self.fallback_data_source})"
        )


@dataclass(slots=True, frozen=True)
class InitDataSourceConnection:
    """A connection between a data source and a state or config instance."""

    data_source: box.FirstMessageInstance | box.SerializedDataFileInstance
    # For legacy reasons, there currently is no box.ConfigInstance; instead this will be a data source
    target_instance: box.StateInstance | box.FirstMessageInstance | box.SerializedDataFileInstance

    @override
    def __repr__(self) -> str:
        """User-friendly printable representation."""
        return f"{type(self).__name__}(data_source={self.data_source}, target_instance={self.target_instance})"


ConnectionType: TypeAlias = (
    ChannelSubscribeConnection
    | MultiChannelSubscribeConnection
    | ChannelPublishConnection
    | ConfigConnection
    | DataSourceFallbackConnection
    | InitDataSourceConnection
    | MemoryResourceConnection
    | StateConnection
)


ConnectedCogInstanceMemberType: TypeAlias = (
    cog.CogInstanceMember[cog.InputDef]
    | cog.CogInstanceMember[cog.OutputDef]
    | cog.CogInstanceMember[cog.ResourceDef]
    | cog.CogInstanceMember[cog.ConfigDef]
    | cog.CogInstanceMember[cog.StateDef]
    | cog.CogInstanceMember[cog_components.CogAlignedInputDef]
)


@final
class ConnectedCogInstanceMemberRegistry(Context):
    """GraphIR Connected Cog Member Registry."""

    def __init__(self, name: str | None) -> None:
        """Create a new GraphIR connected cog instance member registry."""
        self.name = name
        self.connected_cog_instance_member_registry: dict[str, node.NamedEntity | Channel | list[Channel]] = {}

    @override
    def import_from(self, other: ConnectedCogInstanceMemberRegistry) -> None:
        """Combine this connected cog instance member registry with cached connected cog instance members from another registry.

        Raises:
            RuntimeError: If a connected cog instance member already exists with a different definition.
        """
        for key, value in other.connected_cog_instance_member_registry.items():
            if (
                key in self.connected_cog_instance_member_registry
                and self.connected_cog_instance_member_registry[key] != value
            ):
                msg = f"Connected cog instance member registry entry {key} has conflicting entry: {self.connected_cog_instance_member_registry[key]} vs {value}\nWhen merging {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.connected_cog_instance_member_registry[key] = value


class ConnectedCogInstanceMemberRegistryKey(ContextKey[ConnectedCogInstanceMemberRegistry]):
    """CompilerContext Key for GraphIR Connected Cog Member Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ConnectedCogInstanceMemberRegistry:
        """Create a default instance of a GraphIR Connected Cog Member Registry."""
        return ConnectedCogInstanceMemberRegistry(compiler_context.name)


CONNECTED_COG_MEMBER_REGISTRY_KEY: Final = ConnectedCogInstanceMemberRegistryKey(
    "ConnectedCogInstanceMemberRegistryKey"
)


def lookup_connected_cog_instance_member(
    key: ConnectedCogInstanceMemberType,
    compiler_context: CompilerContext,
) -> node.NamedEntity | Channel | list[Channel]:
    """Look up a the entity connected to a cog instance member.

    Args:
        key: Cog instance member
        compiler_context: Context to use for the lookup

    Raises:
        KeyError if no value found for key
    """
    registry = compiler_context[CONNECTED_COG_MEMBER_REGISTRY_KEY]
    return registry.connected_cog_instance_member_registry[key.value_key()]


def register_connected_cog_instance_member(
    key: ConnectedCogInstanceMemberType,
    value: node.NamedEntity | Channel | list[Channel],
    ir_node: box.Connection,
    compiler_context: CompilerContext,
) -> None:
    """Register an entity connected to a cog instance member.

    Args:
        key: Connected cog instance member
        value: Connected cog instance member value
        ir_node: IR node for error messages
        compiler_context: Context to use for the lookup

    Raises:
        RuntimeError if a value is already registered for key
    """
    registry = compiler_context[CONNECTED_COG_MEMBER_REGISTRY_KEY]
    value_key = key.value_key()
    if (
        value_key in registry.connected_cog_instance_member_registry
        and registry.connected_cog_instance_member_registry[value_key] != value
    ):
        value = registry.connected_cog_instance_member_registry[value_key]
        value_name = (
            value.channel_name
            if isinstance(value, Channel)
            else f"[{','.join([element.channel_name for element in value])}]"
            if isinstance(value, list)
            else value.fqn
        )
        msg = ir_node.append_error_line(f"{key.name} is already connected to {value_name}")
        raise RuntimeError(msg)
    registry.connected_cog_instance_member_registry[value_key] = value


def _handle_cog_output_to_channel_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: cog.CogInstanceMember[cog.OutputDef],
    target: pubsub.Channel | pubsub.InstantiatedChannel,
) -> ChannelToCogPublishConnection:
    channel = lookup_channel(target, compiler_context)
    if channel.message_repr.typespec.value_key() != source.member.get_representation_typespec().value_key():
        msg = ir_node.append_error_line(
            f"Attempt to connect channel {channel.channel_name} type {channel.message_repr.typespec.value_key()} to output type {source.member.get_representation_typespec().value_key()}"
        )
        raise TypeError(msg)
    register_connected_cog_instance_member(source, channel, ir_node, compiler_context)
    return ChannelToCogPublishConnection(channel=channel, cog_instance_member=source, ir_node=ir_node)


def _handle_cog_output_to_cog_input_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: cog.CogInstanceMember[cog.OutputDef],
    target: cog.CogInstanceMember[cog.InputDef],
) -> ChannelToCogSubscribeConnection | None:
    try:
        channel = lookup_connected_cog_instance_member(source, compiler_context)
    except KeyError:
        return None
    assert isinstance(channel, Channel)
    return _handle_channel_to_cog_input_connection(ir_node, compiler_context, channel, target)


def _handle_cog_output_to_aligned_cog_input_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: cog.CogInstanceMember[cog.OutputDef],
    target: cog.CogInstanceMember[cog.CogAlignedInputDef],
) -> ChannelToAlignedInputConnection | None:
    try:
        channel = lookup_connected_cog_instance_member(source, compiler_context)
    except KeyError:
        return None
    assert isinstance(channel, Channel)
    return _handle_channel_to_aligned_cog_input_connection(ir_node, compiler_context, channel, target)


def _handle_channel_to_udp_socket_connection(
    ir_node: box.Connection,
    channel: Channel,
    target: udp.UdpSocketInstance,
) -> ChannelToUdpSubscribeConnection:
    if message_error := udp.validate_message_type(channel.message_repr.typespec, target.socket, channel.channel_name):
        msg = ir_node.append_error_line(message_error)
        raise TypeError(msg)
    if not target.observer_endpoint:
        msg = ir_node.append_error_line("Attempting to connect a UDP socket to a channel without an observer endpoint.")
        raise TypeError(msg)
    return ChannelToUdpSubscribeConnection(channel=channel, socket_endpoint=target.observer_endpoint, ir_node=ir_node)


def _handle_channel_to_aligned_cog_input_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    channel: Channel,
    target: cog.CogInstanceMember[cog_components.CogAlignedInputDef],
) -> ChannelToAlignedInputConnection:
    aligned_def = target.member
    aligner_type = aligned_def.aligned_type
    if not isinstance(aligner_type, aligner.Aligner):
        msg = ir_node.append_error_line(f"Aligned input type must be an aligner, got {type(aligner_type).__name__}")
        raise TypeError(msg)
    if aligner_type.alignment_iface is None:
        msg = ir_node.append_error_line(f"Aligner '{aligner_type.name}' has no alignment interface")
        raise TypeError(msg)
    alignment_repr = aligner_type.alignment_iface.representation
    if alignment_repr is None:
        msg = ir_node.append_error_line(f"Aligner '{aligner_type.name}' alignment interface has no representation")
        raise TypeError(msg)
    expected_key = alignment_repr.typespec.value_key()
    if channel.message_repr.typespec.value_key() != expected_key:
        msg = ir_node.append_error_line(
            f"Channel type {channel.message_repr.typespec.value_key()} does not match "
            + f"alignment schema type {expected_key}"
        )
        raise TypeError(msg)
    register_connected_cog_instance_member(target, channel, ir_node, compiler_context)
    return ChannelToAlignedInputConnection(channel=channel, cog_instance_member=target, ir_node=ir_node)


def _handle_channel_to_cog_input_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    channel: Channel,
    target: cog.CogInstanceMember[cog_components.InputDef],
) -> ChannelToCogSubscribeConnection:
    if channel.message_repr.typespec.value_key() != target.member.get_representation_typespec().value_key():
        msg = ir_node.append_error_line(
            f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to input type {target.member.get_representation_typespec().value_key()}"
        )
        raise TypeError(msg)
    if channel.is_multi_publisher and not target.member.view_params.no_dial:
        msg = ir_node.append_error_line(
            f"Attempt to connect multi publisher channel '{channel.channel_name}' to dial input '{target.member.name}'"
        )
        raise TypeError(msg)
    register_connected_cog_instance_member(target, channel, ir_node, compiler_context)
    return ChannelToCogSubscribeConnection(channel=channel, cog_instance_member=target, ir_node=ir_node)


def _handle_values_to_cog_input_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    values: typesys.Values,
    target: cog.CogInstanceMember[cog_components.InputDef],
) -> MultiChannelToCogSubscribeConnection | None:
    if target.elements is None:
        msg = ir_node.append_error_line(
            f"Attempt to connect multiple channels to non multi_connect input '{target.member.name}'"
        )
        raise TypeError(msg)
    if len(values.elements) > len(target.elements):
        msg = ir_node.append_error_line(
            f"Attempt to connect {len(values.elements)} channels to multi_connect input with size {len(target.elements)}"
        )
        raise TypeError(msg)
    channels: list[Channel] = []
    for element in values.elements:
        if isinstance(element, cog.CogInstanceMember) and isinstance(element.member, cog.OutputDef):
            try:
                channel = lookup_connected_cog_instance_member(element, compiler_context)
            except KeyError:
                return None
            assert isinstance(channel, Channel)
        elif isinstance(element, pubsub.Channel | pubsub.InstantiatedChannel):
            channel = lookup_channel(element, compiler_context)
        else:
            msg = ir_node.append_error_line(
                f"Attempt to connect invalid type '{type(element)}' to dial input '{target.member.name}'"
            )
            raise TypeError(msg)
        if channel.is_multi_publisher:
            msg = ir_node.append_error_line(
                f"Attempt to connect multi publisher channel '{channel.channel_name}' to dial input '{target.member.name}'"
            )
            raise TypeError(msg)
        if channel.message_repr.typespec.value_key() != target.member.get_representation_typespec().value_key():
            msg = ir_node.append_error_line(
                f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to input type {target.member.get_representation_typespec().value_key()}"
            )
            raise TypeError(msg)
        if channel.is_multi_publisher:
            msg = ir_node.append_error_line(
                f"Attempt to connect multi publisher channel '{channel.channel_name}' to dial input '{target.member.name}'"
            )
            raise TypeError(msg)
        channels.append(channel)
    register_connected_cog_instance_member(target, channels, ir_node, compiler_context)
    return MultiChannelToCogSubscribeConnection(channels=channels, cog_instance_member=target, ir_node=ir_node)


def _handle_audio_source_to_channel_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: audio.AudioSourceInstance,
    target: pubsub.Channel | pubsub.InstantiatedChannel,
) -> ChannelToAudioPublishConnection:
    channel = lookup_channel(target, compiler_context)
    if message_error := audio.validate_message_type(channel.message_repr.typespec, source.source, channel.channel_name):
        msg = ir_node.append_error_line(message_error)
        raise TypeError(msg)
    return ChannelToAudioPublishConnection(channel=channel, source_instance=source, ir_node=ir_node)


def _handle_udp_socket_to_channel_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: udp.UdpSocketInstance,
    target: pubsub.Channel | pubsub.InstantiatedChannel,
) -> ChannelToUdpPublishConnection:
    channel = lookup_channel(target, compiler_context)
    if message_error := udp.validate_message_type(channel.message_repr.typespec, source.socket, channel.channel_name):
        msg = ir_node.append_error_line(message_error)
        raise TypeError(msg)
    if not source.producer_endpoint:
        msg = ir_node.append_error_line("Attempting to connect a UDP socket to a channel without a producer endpoint.")
        raise TypeError(msg)
    return ChannelToUdpPublishConnection(channel=channel, socket_endpoint=source.producer_endpoint, ir_node=ir_node)


def _handle_data_source_to_fallback_connection(
    source: box.FirstMessageInstance | box.SerializedDataFileInstance,
    target: box.FallbackEndpoint,
) -> DataSourceFallbackConnection:
    return DataSourceFallbackConnection(fallback_data_source=source, data_source=target.parent_data_source)


def _handle_data_source_to_init_connection(
    ir_node: box.Connection,
    source: box.FirstMessageInstance | box.SerializedDataFileInstance,
    target: box.StateInstance | box.FirstMessageInstance | box.SerializedDataFileInstance,
) -> InitDataSourceConnection:
    if (
        isinstance(source, box.FirstMessageInstance)
        and isinstance(target, box.StateInstance)
        and isinstance(target.repr_typespec, extern_type.ExternType)
    ):
        serialized_form = target.repr_typespec.serialized_form
        if serialized_form is None:
            msg = ir_node.append_error_line(
                "FirstMessage cannot initialize an external state without a serialized form"
            )
            raise TypeError(msg)
        assert source.channel.message_repr is not None
        source_key = source.channel.message_repr.typespec.value_key()
        target_key = serialized_form.value_key()
        if source_key != target_key:
            msg = ir_node.append_error_line(f"Data source type {source_key} does not match state type {target_key}")
            raise TypeError(msg)
    return InitDataSourceConnection(data_source=source, target_instance=target)


def _handle_data_source_to_cog_config_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: box.FirstMessageInstance | box.SerializedDataFileInstance,
    target: cog.CogInstanceMember[cog.ConfigDef],
) -> ConfigConnection:
    # TODO(OI-2013): Validate config representation/type; blocked on Protobuf converters landing
    register_connected_cog_instance_member(target, source, ir_node, compiler_context)
    return ConfigConnection(config_instance=source, cog_instance_member=target)


def _handle_cog_config_to_cog_config_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: cog.CogInstanceMember[cog.ConfigDef],
    target: cog.CogInstanceMember[cog.ConfigDef],
) -> ConfigConnection | None:
    try:
        config_instance = lookup_connected_cog_instance_member(source, compiler_context)
    except KeyError:
        return None
    assert isinstance(config_instance, box.FirstMessageInstance | box.SerializedDataFileInstance)
    return _handle_data_source_to_cog_config_connection(ir_node, compiler_context, config_instance, target)


def _handle_state_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: box.StateInstance,
    target: cog.CogInstanceMember[cog.StateDef],
) -> StateConnection:
    if isinstance(target.member.message_type, schema_reg.InterfaceInfo):
        assert isinstance(
            target.member.message_type.interface_ir.representation, representation.RepresentationReference
        )
        if (source_key := source.repr_typespec.value_key()) != (
            target_key := target.member.message_type.interface_ir.representation.typespec.value_key()
        ):
            msg = ir_node.append_error_line(f"State type {source_key} does not match endpoint type {target_key}")
            raise TypeError(msg)
    elif isinstance(target.member.message_type, extern_type.ExternType):
        if (source_key := source.repr_typespec.value_key()) != (target_key := target.member.message_type.value_key()):
            msg = ir_node.append_error_line(f"State type {source_key} does not match endpoint type {target_key}")
            raise TypeError(msg)
    else:
        msg = ir_node.append_error_line(f"Unsupported message type {target.member.message_type}")
        raise RuntimeError(msg)  # noqa: TRY004 (RuntimeError is appropriate because this is a compiler bug)
    register_connected_cog_instance_member(target, source, ir_node, compiler_context)
    return StateConnection(state_instance=source, cog_instance_member=target)


def _handle_cog_state_to_cog_state_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: cog.CogInstanceMember[cog.StateDef],
    target: cog.CogInstanceMember[cog.StateDef],
) -> StateConnection | None:
    try:
        state_instance = lookup_connected_cog_instance_member(source, compiler_context)
    except KeyError:
        return None
    assert isinstance(state_instance, box.StateInstance)
    return _handle_state_connection(ir_node, compiler_context, state_instance, target)


def _handle_memory_resource_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: box.MemoryResourceInstance,
    target: cog.CogInstanceMember[cog.ResourceDef],
) -> MemoryResourceConnection:
    register_connected_cog_instance_member(target, source, ir_node, compiler_context)
    return MemoryResourceConnection(memory_resource=source, cog_instance_member=target)


def _handle_diagnostics_instance_to_channel_connection(
    ir_node: box.Connection,
    compiler_context: CompilerContext,
    source: diagnostics.DiagnosticsInstance,
    target: pubsub.Channel | pubsub.InstantiatedChannel,
) -> ChannelToDiagnosticsPublish:
    channel = lookup_channel(target, compiler_context)
    if channel.message_repr.typespec.value_key() != source.diagnostics.get_representation_typespec().value_key():
        msg = ir_node.append_error_line(
            f"Attempt to connect channel type {channel.message_repr.typespec.value_key()} to output type {source.diagnostics.get_representation_typespec().value_key()}"
        )
        raise TypeError(msg)
    return ChannelToDiagnosticsPublish(channel=channel, diagnostics_instance=source, ir_node=ir_node)


def from_ir_connection(  # noqa: C901, PLR0911, PLR0912  TODO(OI-3057): Refactor this function to reduce complexity
    ir_node: box.Connection, compiler_context: CompilerContext
) -> ConnectionType | None:
    """Construct a GraphIR connection of the appropriate class from an IR Connection.

    Args:
        ir_node: IR Connection to generate GraphIR from.
        compiler_context: Compiler context to use for looking up channels.

    Returns:
        Connection from source to target or None if the source has not been connected yet
    """
    if isinstance(ir_node.source, cog.CogInstanceMember):
        if isinstance(ir_node.source.member, cog.OutputDef | diagnostics.DiagnosticsDef) and isinstance(
            ir_node.target, pubsub.Channel | pubsub.InstantiatedChannel
        ):
            return _handle_cog_output_to_channel_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
        if (
            isinstance(ir_node.source.member, cog.OutputDef)
            and isinstance(ir_node.target, cog.CogInstanceMember)
            and isinstance(ir_node.target.member, cog.InputDef)
        ):
            return _handle_cog_output_to_cog_input_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
        if (
            isinstance(ir_node.source.member, cog.OutputDef)
            and isinstance(ir_node.target, cog.CogInstanceMember)
            and isinstance(ir_node.target.member, cog.CogAlignedInputDef)
        ):
            return _handle_cog_output_to_aligned_cog_input_connection(
                ir_node, compiler_context, ir_node.source, ir_node.target
            )
        if (
            isinstance(ir_node.source.member, cog.StateDef)
            and isinstance(ir_node.target, cog.CogInstanceMember)
            and isinstance(ir_node.target.member, cog.StateDef)
        ):
            return _handle_cog_state_to_cog_state_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
        if (
            isinstance(ir_node.source.member, cog.ConfigDef)
            and isinstance(ir_node.target, cog.CogInstanceMember)
            and isinstance(ir_node.target.member, cog.ConfigDef)
        ):
            return _handle_cog_config_to_cog_config_connection(
                ir_node, compiler_context, ir_node.source, ir_node.target
            )
    elif isinstance(ir_node.source, pubsub.Channel | pubsub.InstantiatedChannel):
        channel = lookup_channel(ir_node.source, compiler_context)
        if isinstance(ir_node.target, udp.UdpSocketInstance):
            return _handle_channel_to_udp_socket_connection(ir_node, channel, ir_node.target)
        if isinstance(ir_node.target, cog.CogInstanceMember) and isinstance(
            ir_node.target.member, cog_components.CogAlignedInputDef
        ):
            return _handle_channel_to_aligned_cog_input_connection(ir_node, compiler_context, channel, ir_node.target)
        if isinstance(ir_node.target, cog.CogInstanceMember) and isinstance(ir_node.target.member, cog.InputDef):
            return _handle_channel_to_cog_input_connection(ir_node, compiler_context, channel, ir_node.target)
    elif isinstance(ir_node.source, audio.AudioSourceInstance) and isinstance(
        ir_node.target, pubsub.Channel | pubsub.InstantiatedChannel
    ):
        return _handle_audio_source_to_channel_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
    elif isinstance(ir_node.source, udp.UdpSocketInstance) and isinstance(
        ir_node.target, pubsub.Channel | pubsub.InstantiatedChannel
    ):
        return _handle_udp_socket_to_channel_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
    elif isinstance(ir_node.source, box.FirstMessageInstance | box.SerializedDataFileInstance):
        if isinstance(ir_node.target, box.FallbackEndpoint):
            return _handle_data_source_to_fallback_connection(ir_node.source, ir_node.target)
        if isinstance(ir_node.target, box.StateInstance | box.FirstMessageInstance | box.SerializedDataFileInstance):
            return _handle_data_source_to_init_connection(ir_node, ir_node.source, ir_node.target)
        if isinstance(ir_node.target, cog.CogInstanceMember) and isinstance(ir_node.target.member, cog.ConfigDef):
            return _handle_data_source_to_cog_config_connection(
                ir_node, compiler_context, ir_node.source, ir_node.target
            )
    elif (
        isinstance(ir_node.source, box.StateInstance)
        and isinstance(ir_node.target, cog.CogInstanceMember)
        and isinstance(ir_node.target.member, cog.StateDef)
    ):
        return _handle_state_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
    elif (
        isinstance(ir_node.source, box.MemoryResourceInstance)
        and isinstance(ir_node.target, cog.CogInstanceMember)
        and isinstance(ir_node.target.member, cog.ResourceDef)
    ):
        return _handle_memory_resource_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
    elif isinstance(ir_node.source, diagnostics.DiagnosticsInstance) and isinstance(
        ir_node.target, pubsub.Channel | pubsub.InstantiatedChannel
    ):
        return _handle_diagnostics_instance_to_channel_connection(
            ir_node, compiler_context, ir_node.source, ir_node.target
        )
    elif (
        isinstance(ir_node.source, typesys.Values)
        and isinstance(ir_node.target, cog.CogInstanceMember)
        and isinstance(ir_node.target.member, cog.InputDef)
    ):
        return _handle_values_to_cog_input_connection(ir_node, compiler_context, ir_node.source, ir_node.target)
    msg = ir_node.append_error_line(f"Unsupported connection type: {ir_node}")
    raise NotImplementedError(msg)
