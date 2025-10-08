# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Representation of an entire Clockwork system."""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Final, Generic, TypeAlias, TypeVar
from uuid import UUID, uuid3

from clockwork.dsl.composition import (
    graphir,
    logger_config,
    logger_config_proto,
)
from clockwork.dsl.composition.str_manip import snake_from_camel
from clockwork.dsl.ir import (
    audio,
    box,
    clkbuiltins,
    clkenum,
    cog,
    diagnostics,
    hardware,
    node,
    policy,
    primitive,
    pubsub,
    schema_reg,
    typesys,
    udp,
)
from clockwork.dsl.ir.uuid_reg import lookup_uuid, register_entity_with_stable_key

if TYPE_CHECKING:
    from collections.abc import Iterable

    from clockwork.dsl.compiler_context import CompilerContext


@dataclass(slots=True)
class LogProducer(typesys.NamedAttribute):
    """Represents a producer that publishes from a log or other external source."""

    source_name: str


EndpointType: TypeAlias = (
    cog.CogInstanceMember[Any]
    | udp.UdpSocketEndpointInstance
    | audio.AudioSourceInstance
    | LogProducer
    | diagnostics.DiagnosticsInstance
)

ProducerType: TypeAlias = (
    cog.CogInstanceMember[cog.OutputDef]
    | udp.UdpSocketEndpointInstance
    | cog.CogInstanceMember[cog.MetricsOutputDef]
    | audio.AudioSourceInstance
    | LogProducer
    | diagnostics.DiagnosticsInstance
)

ObserverType: TypeAlias = cog.CogInstanceMember[cog.InputDef] | udp.UdpSocketEndpointInstance

MetricsProducerType: TypeAlias = cog.CogInstanceMember[cog.MetricsOutputDef]


@dataclass(slots=True)
class MetricsChannel:
    """A Metrics Channel with related graph information."""

    channel: graphir.MetricsChannel
    producers: dict[UUID, Endpoint[ProducerType, None]]
    observers: dict[UUID, Endpoint[ObserverType, None]]

    def is_valid(self) -> bool:
        """Determine if this is a valid channel."""
        return bool(self.producers) and len(self.producers) == 1

    def is_multi_producer(self) -> bool:
        """Determine if this is a multi-producer channel."""
        return False

    def is_diagnostics(self) -> bool:
        """Determine if this is a diagnostics channel."""
        return False

    def is_bridge_status(self) -> bool:
        """Determine if this is a bridge status channel."""
        return False

    def is_single_producer(self) -> bool:
        """Determine if this is a single-producer channel."""
        return True

    def enforce_backwards_compatibility(self) -> bool:
        """Determine whether to test metadata for backwards compability."""
        return False


@dataclass(slots=True)
class Channel:
    """A Channel with related graph information."""

    channel: graphir.Channel
    producers: dict[UUID, Endpoint[ProducerType, None]]
    observers: dict[UUID, Endpoint[ObserverType, None]]

    def is_single_producer(self) -> bool:
        """Determine if this is a single-producer channel."""
        return not self.channel.is_multi_publisher

    def is_multi_producer(self) -> bool:
        """Determine if this is a multi-producer channel."""
        return self.channel.is_multi_publisher

    def is_diagnostics(self) -> bool:
        """Determine if this is a diagnostics channel."""
        return self.channel.is_diagnostics

    def is_bridge_status(self) -> bool:
        """Determine if this is a bridge status channel."""
        return self.channel.is_bridge_status

    def is_valid(self) -> bool:
        """Determine if this is a valid channel."""
        return bool(self.producers) and (self.channel.is_multi_publisher or len(self.producers) == 1)

    def enforce_backwards_compatibility(self) -> bool:
        """Determine whether to validate metadata for backwards compability."""
        return self.channel.enforce_backwards_compatibility


ConnectableType = TypeVar("ConnectableType")
EndpointEntityType = TypeVar("EndpointEntityType", bound=EndpointType)


@dataclass(slots=True)
class Endpoint(Generic[EndpointEntityType, ConnectableType]):
    """A connectable endpoint."""

    entity: EndpointEntityType
    connected_to: Channel | MetricsChannel | Connectable[ConnectableType, EndpointEntityType] | None
    process: UUID


@dataclass(slots=True)
class Connectable(Generic[ConnectableType, EndpointEntityType]):
    """A memory resource instance."""

    entity: ConnectableType
    endpoints: dict[UUID, Endpoint[EndpointEntityType, ConnectableType]]


@dataclass
class LogicalSystem:
    """A Clockwork system."""

    module: node.Module
    require_logging_policies: bool
    all_entities: dict[UUID, Any] = field(default_factory=dict)
    config_endpoints: dict[UUID, Endpoint[cog.CogInstanceMember[cog.ConfigDef], box.SerializedDataFileInstance]] = (
        field(default_factory=dict)
    )
    memres_endpoints: dict[UUID, Endpoint[cog.CogInstanceMember[cog.ResourceDef], box.MemoryResourceInstance]] = field(
        default_factory=dict
    )
    observer_endpoints: dict[UUID, Endpoint[ObserverType, Any]] = field(default_factory=dict)
    producer_endpoints: dict[UUID, Endpoint[ProducerType, Any]] = field(default_factory=dict)
    state_endpoints: dict[UUID, Endpoint[cog.CogInstanceMember[cog.StateDef], box.StateInstance]] = field(
        default_factory=dict
    )
    channels: dict[str, Channel] = field(default_factory=dict)
    metrics_channels: dict[str, MetricsChannel] = field(default_factory=dict)
    cogs: dict[UUID, cog.CogInstance] = field(default_factory=dict)
    init_deps: dict[UUID, set[UUID]] = field(default_factory=dict)
    configs: dict[UUID, Connectable[box.SerializedDataFileInstance, cog.CogInstanceMember[cog.ConfigDef]]] = field(
        default_factory=dict
    )
    states: dict[UUID, Connectable[box.StateInstance, cog.CogInstanceMember[cog.StateDef]]] = field(
        default_factory=dict
    )
    mem_resources: dict[UUID, Connectable[box.MemoryResourceInstance, cog.CogInstanceMember[cog.ResourceDef]]] = field(
        default_factory=dict
    )
    udp_sockets: dict[UUID, udp.UdpSocketInstance] = field(default_factory=dict)
    audio_sources: dict[UUID, audio.AudioSourceInstance] = field(default_factory=dict)
    log_producers: dict[UUID, LogProducer] = field(default_factory=dict)
    cpu_domains: dict[UUID, hardware.CpuDomain] = field(default_factory=dict)
    processes: dict[UUID, box.ProcessInstance] = field(default_factory=dict)
    process_to_domain: dict[UUID, UUID] = field(default_factory=dict)
    entity_to_process: dict[UUID, UUID] = field(default_factory=dict)
    ignored_producer_endpoints: dict[UUID, int] = field(default_factory=dict)
    ignored_observer_endpoints: dict[UUID, int] = field(default_factory=dict)

    def add_entity(self, entity: Any, uuid: UUID | None = None) -> UUID:  # noqa: ANN401 (Any required for polymorphism)
        """Add a new entity to the system, ensuring uniqueness.

        Raises:
            KeyError if the UUID is already registered.
        """
        if uuid is None:
            uuid = lookup_uuid(self.module.context, entity)
        if uuid in self.all_entities:
            msg = f"Entity already present in system: {entity.value_key() if hasattr(entity, 'value_key') else entity}"
            raise KeyError(msg)
        self.all_entities[uuid] = entity
        return uuid

    def _require_entity(self, entity: Any, uuid: UUID | None = None) -> UUID:  # noqa: ANN401 (Any required for polymorphism)
        if uuid is None:
            uuid = lookup_uuid(self.module.context, entity)
        found = self.all_entities[uuid]
        if found is not entity:
            msg = f"Entity with duplicate UUID {uuid}: {found} is not {entity}"
            raise RuntimeError(msg)
        return uuid

    def _ensure_process(self, instance: typesys.Value) -> UUID:
        """Determine the process for an entity and register it if needed."""
        policy_data = policy.lookup_policy(self.module, box.HOST_PROCESS_POLICY, instance)
        if policy_data is None:
            msg = f"No host process associated with instance {instance.value_key()}"
            raise ValueError(msg)
        process = policy_data.data.data["process"]
        assert isinstance(process, box.ProcessInstance)
        uuid = lookup_uuid(self.module.context, process)
        if uuid not in self.processes:
            assert self.add_process(process) == uuid
        return uuid

    def add_process(self, instance: box.ProcessInstance) -> UUID:
        """Add a process to the system."""
        uuid = self.add_entity(instance)
        domain = self._ensure_cpu_domain(instance)
        self.processes[uuid] = instance
        self.process_to_domain[uuid] = domain
        return uuid

    def _ensure_cpu_domain(self, instance: typesys.Value) -> UUID:
        """Determine the CPU domain for an entity and register it if needed."""
        policy_data = policy.lookup_policy(self.module, box.HOST_CPU_DOMAIN_POLICY, instance)
        if policy_data is None:
            msg = f"No CPU domain associated with instance {instance.value_key()}"
            raise ValueError(msg)
        cpu_domain = policy_data.data.data["cpu_domain"]
        assert isinstance(cpu_domain, hardware.CpuDomain)
        uuid = lookup_uuid(self.module.context, cpu_domain)
        if uuid not in self.cpu_domains:
            assert self.add_cpu_domain(cpu_domain) == uuid
        return uuid

    def add_cpu_domain(self, instance: hardware.CpuDomain) -> UUID:
        """Add a CPU Domain to the system."""
        uuid = self.add_entity(instance)
        self.cpu_domains[uuid] = instance
        if instance.logging_backup:
            assert isinstance(instance.logging_backup, hardware.CpuDomain)
            logging_backup_uuid = lookup_uuid(self.module.context, instance.logging_backup)
            if logging_backup_uuid not in self.all_entities:
                assert self.add_entity(instance.logging_backup) == logging_backup_uuid
                self.cpu_domains[logging_backup_uuid] = instance.logging_backup
        return uuid

    def add_cog(self, instance: cog.CogInstance) -> UUID:
        """Add a Cog instance to the system."""
        uuid = self.add_entity(instance)
        process = self._ensure_process(instance)
        self.entity_to_process[uuid] = process
        self.cogs[uuid] = instance
        for endpoint in instance.members:
            self.add_cog_endpoint(endpoint, process)
        self.add_metrics_channels(instance)
        return uuid

    def add_cog_endpoint(self, endpoint: cog.CogInstanceMember[Any], process: UUID) -> UUID:
        """Add an endpoint to the system."""
        uuid = self.add_entity(endpoint)
        if isinstance(endpoint.member, cog.ConditionDef):
            # These are not connectable in the system graph
            pass
        elif isinstance(endpoint.member, cog.ConfigDef):
            self.config_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        elif isinstance(endpoint.member, cog.InputDef):
            self.observer_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        elif isinstance(
            endpoint.member,
            cog.OutputDef | diagnostics.DiagnosticsDef | diagnostics.InfraDiagnosticsDef | cog.MetricsOutputDef,
        ):
            self.producer_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        elif isinstance(endpoint.member, cog.ResourceDef):
            self.memres_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        elif isinstance(endpoint.member, cog.StateDef):
            self.state_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        else:
            msg = f"Unrecognized endpoint type: {type(endpoint.member)}"
            raise TypeError(msg)
        self.entity_to_process[uuid] = process
        return uuid

    def add_observer_endpoint(self, endpoint: ObserverType, process: UUID) -> UUID:
        """Add an endpoint to the system."""
        uuid = self.add_entity(endpoint)
        self.observer_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        self.entity_to_process[uuid] = process
        return uuid

    def add_producer_endpoint(self, endpoint: ProducerType, process: UUID) -> UUID:
        """Add an endpoint to the system."""
        uuid = self.add_entity(endpoint)
        self.producer_endpoints[uuid] = Endpoint(entity=endpoint, connected_to=None, process=process)
        self.entity_to_process[uuid] = process
        return uuid

    def add_config(self, instance: box.SerializedDataFileInstance) -> UUID:
        """Add a config file to the system."""
        uuid = self.add_entity(instance)
        process = self._ensure_process(instance)
        self.entity_to_process[uuid] = process
        self.configs[uuid] = Connectable(instance, {})
        return uuid

    def add_state(self, instance: box.StateInstance) -> UUID:
        """Add a state instance to the system."""
        uuid = self.add_entity(instance)
        process = self._ensure_process(instance)
        self.entity_to_process[uuid] = process
        self.states[uuid] = Connectable(instance, {})
        return uuid

    def add_udp_socket(self, instance: udp.UdpSocketInstance) -> UUID:
        """Add a UDP socket instance to the system."""
        process = self._ensure_process(instance)
        instance_uuid = self.add_entity(instance)
        self.udp_sockets[instance_uuid] = instance
        self.entity_to_process[instance_uuid] = process
        # Each endpoint can only be connected to one item.  UDP
        # sockets can be bidirectional which means they can be both an
        # observer and a producer.  Instead of passing in the socket
        # instance for the endpoint entity, we pass in a unique
        # observer / producer endpoint.
        if instance.observer_endpoint:
            observer_uuid = self.add_entity(instance.observer_endpoint)
            endpoint: Endpoint[udp.UdpSocketEndpointInstance, Any] = Endpoint(
                entity=instance.observer_endpoint, connected_to=None, process=process
            )
            self.observer_endpoints[observer_uuid] = endpoint  # pyright: ignore[reportArgumentType] # False Positive, UdpSocketEndpointInstance is a ProducerType
            self.entity_to_process[observer_uuid] = process
        if instance.producer_endpoint:
            producer_uuid = self.add_entity(instance.producer_endpoint)
            endpoint: Endpoint[udp.UdpSocketEndpointInstance, Any] = Endpoint(
                entity=instance.producer_endpoint, connected_to=None, process=process
            )
            self.producer_endpoints[producer_uuid] = endpoint  # pyright: ignore[reportArgumentType] # False Positive, UdpSocketEndpointInstance is a ProducerType
            self.entity_to_process[producer_uuid] = process
        return instance_uuid

    def add_audio_source(self, instance: audio.AudioSourceInstance) -> UUID:
        """Add a Audio source instance to the system."""
        process = self._ensure_process(instance)
        uuid = self.add_entity(instance)
        endpoint: Endpoint[ProducerType, None] = Endpoint(entity=instance, connected_to=None, process=process)
        self.producer_endpoints[uuid] = endpoint
        self.entity_to_process[uuid] = process
        self.audio_sources[uuid] = instance

        self.add_diagnostics_source(instance.diagnostics, process)

        return uuid

    def add_diagnostics_source(self, instance: diagnostics.DiagnosticsInstance, process: UUID) -> UUID:
        """Add diagnostics instance to the system."""
        uuid = self.add_entity(instance)
        endpoint: Endpoint[ProducerType, None] = Endpoint(entity=instance, connected_to=None, process=process)
        self.producer_endpoints[uuid] = endpoint
        self.entity_to_process[uuid] = process

        return uuid

    def add_log_producer(self, instance: LogProducer, channel: graphir.Channel) -> UUID:
        """Add a log producer to the system."""
        # We have to create log producers with an invalid process UUID,
        # otherwise the process hosting the log reader will try to create a
        # normal producer endpoint for it.
        process = UUID(int=0)
        if len(self.cpu_domains) != 1:
            msg = "Log producers are only valid in single-domain systems"
            raise ValueError(msg)
        (domain,) = self.cpu_domains.keys()
        self.process_to_domain[process] = domain
        uuid = self.add_producer_endpoint(instance, process)
        self.entity_to_process[uuid] = process
        self.log_producers[uuid] = instance
        self.connect_channel_producer(channel, instance)
        return uuid

    def add_memory_resource(self, instance: box.MemoryResourceInstance) -> UUID:
        """Add a memory resource instance to the system."""
        uuid = self.add_entity(instance)
        process = self._ensure_process(instance)
        self.entity_to_process[uuid] = process
        self.mem_resources[uuid] = Connectable(instance, {})
        return uuid

    def ensure_channel(self, channel: graphir.Channel | graphir.MetricsChannel) -> Channel | MetricsChannel:
        """Ensure that the channel is in the channels map."""
        if isinstance(channel, graphir.MetricsChannel):
            return self.ensure_metrics_channel(channel)
        name = channel.channel_name
        try:
            found = self.channels[name]
            if found.channel is not channel:
                msg = f"Duplicate GraphIR Channel: {found.channel} is not {channel}"
                raise RuntimeError(msg)
        except KeyError:
            found = Channel(channel=channel, producers={}, observers={})
            self.channels[name] = found
        return found

    def ensure_metrics_channel(self, channel: graphir.MetricsChannel) -> MetricsChannel:
        """Ensure that the channel is in the channels map."""
        name = channel.channel_name
        try:
            found = self.metrics_channels[name]
            if found.channel is not channel:
                msg = f"Duplicate GraphIR Metrics Channel: {found.channel} is not {channel}"
                raise RuntimeError(msg)
        except KeyError:
            found = MetricsChannel(channel=channel, producers={}, observers={})
            self.metrics_channels[name] = found
        return found

    def connect_channel_observer(
        self, channel: graphir.Channel | graphir.MetricsChannel, observer: ObserverType
    ) -> None:
        """Connect an observer to a channel."""
        uuid = self._require_entity(observer)
        endpoint = self.observer_endpoints[uuid]
        our_channel = self.ensure_channel(channel)
        if endpoint.connected_to and endpoint.connected_to is not our_channel:
            msg = f"Endpoint connected to multiple channels: {endpoint} {our_channel}"
            raise ValueError(msg)
        endpoint.connected_to = our_channel
        try:
            found = our_channel.observers[uuid]
            if found is not endpoint:
                msg = f"Duplicate observer UUID {uuid}: {found} is not {endpoint}"
                raise RuntimeError(msg)
        except KeyError:
            our_channel.observers[uuid] = endpoint

    def connect_channel_producer(
        self, channel: graphir.Channel | graphir.MetricsChannel, producer: ProducerType
    ) -> None:
        """Connect a producer to a channel."""
        uuid = self._require_entity(producer)
        endpoint = self.producer_endpoints[uuid]
        our_channel = self.ensure_channel(channel)
        if endpoint.connected_to and endpoint.connected_to is not our_channel:
            msg = f"Endpoint connected to multiple channels: {endpoint} {our_channel}"
            raise ValueError(msg)
        endpoint.connected_to = our_channel
        try:
            found = our_channel.producers[uuid]
            if found is not endpoint:
                msg = f"Duplicate producer UUID {uuid}: {found} is not {endpoint}"
                raise RuntimeError(msg)
        except KeyError:
            our_channel.producers[uuid] = endpoint

    def add_metrics_channels(self, instance: cog.CogInstance) -> None:
        """Add metrics channels to the system."""
        for channel in instance.members:
            if isinstance(channel.member, cog.MetricsOutputDef):
                if channel.member.log_type == cog.MetricsLogType.event:
                    name_prefix = "/_clockwork/journal/execution-metrics/"
                else:
                    name_prefix = "/_clockwork/journal/aggregated-execution-metrics/"
                uuid = uuid3(clkbuiltins.CLOCKWORK_NAMESPACE_UUID, instance.fqn)
                message_size = graphir.get_representation_size(
                    self.module.context, channel.member.get_resolved_representation_instantiation()
                )
                # If we can't find a message size it means the cog is not in a cpp target, in which case this becomes
                # irrelevant anyway so set the size to 0.
                if message_size is None:
                    message_size = 0
                channel_obj = graphir.MetricsChannel(
                    channel_name=f"{name_prefix}{instance.cog_class.name}/{uuid}",
                    message_repr=channel.member.get_resolved_representation_instantiation(),
                    log_type=channel.member.log_type,
                    message_size=message_size,
                    num_slots=5,
                    uuid=uuid,
                    cog_path=instance.cog_class.fqn,
                    cog_instance_path=instance.fqn,
                )
                self.ensure_metrics_channel(channel_obj)
                self.connect_metrics_channel_producer(channel_obj, channel)  # pyright: ignore[reportArgumentType] this is effectively checked by this isinstance call above.

    def connect_metrics_channel_producer(
        self, metrics_channel: graphir.MetricsChannel, producer: MetricsProducerType
    ) -> None:
        """Connect a producer to a metrics channel."""
        uuid = self._require_entity(producer)
        endpoint = self.producer_endpoints[uuid]
        our_channel = self.ensure_metrics_channel(metrics_channel)
        try:
            found = our_channel.producers[uuid]
            if found is not endpoint:
                msg = f"Duplicate producer UUID {uuid}: {found} is not {endpoint}"
                raise RuntimeError(msg)
        except KeyError:
            our_channel.producers[uuid] = endpoint

    def connect_memory_resource(
        self, memory_resource: box.MemoryResourceInstance, endpoint: cog.CogInstanceMember[cog.ResourceDef]
    ) -> None:
        """Connect a memory resource to a Cog endpoint."""
        mem_uuid = self._require_entity(memory_resource)
        mem_res = self.mem_resources[mem_uuid]
        ep_uuid = self._require_entity(endpoint)
        ep = self.memres_endpoints[ep_uuid]
        try:
            found = mem_res.endpoints[ep_uuid]
            if found is not ep:
                msg = f"Duplicate endpoint UUID {ep_uuid}: {found} is not {ep}"
                raise RuntimeError(msg)
        except KeyError:
            mem_res.endpoints[ep_uuid] = ep

    def connect_config(
        self, config: box.SerializedDataFileInstance, endpoint: cog.CogInstanceMember[cog.ConfigDef]
    ) -> None:
        """Connect a config to a Cog endpoint."""
        cfg_uuid = self._require_entity(config)
        cfg = self.configs[cfg_uuid]
        ep_uuid = self._require_entity(endpoint)
        ep = self.config_endpoints[ep_uuid]
        try:
            found = cfg.endpoints[ep_uuid]
            if found is not ep:
                msg = f"Duplicate endpoint UUID {ep_uuid}: {found} is not {ep}"
                raise RuntimeError(msg)
        except KeyError:
            cfg.endpoints[ep_uuid] = ep

    def connect_state(self, state: box.StateInstance, endpoint: cog.CogInstanceMember[cog.StateDef]) -> None:
        """Connect a state instance to a Cog endpoint."""
        st_uuid = self._require_entity(state)
        st = self.states[st_uuid]
        ep_uuid = self._require_entity(endpoint)
        ep = self.state_endpoints[ep_uuid]
        try:
            found = st.endpoints[ep_uuid]
            if found is not ep:
                msg = f"Duplicate endpoint UUID {ep_uuid}: {found} is not {ep}"
                raise RuntimeError(msg)
        except KeyError:
            st.endpoints[ep_uuid] = ep

    def _get_cog_endpoint_message_size(self, cog_endpoint: cog.InputDef | cog.OutputDef) -> int:
        representation_reference = cog_endpoint.get_representation_reference()
        representation_instantiation = schema_reg.lookup_representation(self.module.context, representation_reference)
        if not representation_instantiation:
            msg = node.enrich_error_if_possible(
                representation_reference,
                "Failed to resolve representation instantiation from representation reference",
            )
            raise RuntimeError(msg)
        size = graphir.get_representation_size(self.module.context, representation_instantiation.representation_ir)
        if not size:
            msg = node.enrich_error_if_possible(
                representation_instantiation.representation_ir,
                "Failed to resolve message size from representation instantiation",
            )
            raise RuntimeError(msg)
        return size

    def validate_connections(self) -> None:
        """Validate all connections in the system."""
        for uuid, producer in self.producer_endpoints.items():
            if producer.connected_to is None:
                if isinstance(producer.entity, cog.CogInstanceMember):
                    if isinstance(producer.entity.member, cog.MetricsOutputDef):
                        continue
                    if producer.entity.member.is_optional:
                        self.ignored_producer_endpoints[uuid] = self._get_cog_endpoint_message_size(
                            producer.entity.member
                        )
                        continue
                msg = node.enrich_error_if_possible(
                    producer.entity, f"Producer endpoint not connected to channel: {producer.entity.name}"
                )
                raise ValueError(msg)
        for uuid, observer in self.observer_endpoints.items():
            if observer.connected_to is None:
                if isinstance(observer.entity, cog.CogInstanceMember) and (
                    observer.entity.member.view_params.is_optional
                ):
                    self.ignored_observer_endpoints[uuid] = self._get_cog_endpoint_message_size(observer.entity.member)
                    continue
                msg = node.enrich_error_if_possible(
                    observer.entity, f"Observer endpoint not connected to channel: {observer.entity.name}"
                )
                raise ValueError(msg)


def make_system(
    boxes: Iterable[box.ResolvedBox], system_module: node.Module, require_logging_policies: bool
) -> LogicalSystem:
    """Construct a System from a set of boxes."""
    result = LogicalSystem(module=system_module, require_logging_policies=require_logging_policies)
    for a_box in boxes:
        _add_box_to_system(a_box, result)
    log_reader_policy_class = logger_config.get_log_reader_policy()
    for log_reader_policy_data in policy.lookup_all_policies(result.module, log_reader_policy_class):
        ir_channel = log_reader_policy_data.target
        assert isinstance(ir_channel, pubsub.Channel)
        channel = graphir.lookup_channel(ir_channel, result.module.context)
        if len(result.processes) > 1:
            msg = f"LogReaderPolicy can only be used in single-process systems (channel: {channel.channel_name})"
            raise ValueError(msg)
        policy_source = log_reader_policy_data.data.data["source_name"]
        if isinstance(policy_source, clkbuiltins.Nullopt):
            # Default to channel name
            source_name = channel.channel_name
        else:
            assert isinstance(policy_source, primitive.StringValue)
            source_name = policy_source.value
        log_producer = LogProducer(
            name=f"logreader-({source_name})-to-({channel.channel_name})",
            scope=result.module.inner_scope,
            type_info=LOG_PRODUCER_TYPE,
            source_name=source_name,
        )
        register_entity_with_stable_key(system_module.context, log_producer)
        result.add_log_producer(log_producer, channel)
    return result


def _add_box_to_system(a_box: box.ResolvedBox, system: LogicalSystem) -> None:
    for instance in a_box.instances:
        _add_instance_to_system(a_box, system, instance)
    for connection in a_box.connections:
        _add_connection_to_system(a_box, system, graphir.from_ir_connection(connection, system.module.context))


def _add_instance_to_system(a_box: box.ResolvedBox, system: LogicalSystem, instance: node.NamedEntity) -> None:  # noqa: C901 (branches for type dispatch)
    if isinstance(instance, cog.CogInstance):
        system.add_cog(instance)
    elif isinstance(instance, box.SerializedDataFileInstance):
        system.add_config(instance)
    elif isinstance(instance, box.StateInstance):
        system.add_state(instance)
    elif isinstance(instance, udp.UdpSocketInstance):
        system.add_udp_socket(instance)
    elif isinstance(instance, audio.AudioSourceInstance):
        system.add_audio_source(instance)
    elif isinstance(instance, box.MemoryResourceInstance):
        system.add_memory_resource(instance)
    elif isinstance(instance, box.Box):
        _add_box_to_system(instance.get_resolved(), system)
    elif isinstance(instance, box.ProcessInstance):
        if lookup_uuid(system.module.context, instance) not in system.processes:
            system.add_process(instance)
    elif isinstance(instance, hardware.CpuDomain):
        if lookup_uuid(system.module.context, instance) not in system.cpu_domains:
            system.add_cpu_domain(instance)
    else:
        msg = a_box.append_error_line(f"Member {instance.name} is of unsupported type: {instance}")
        raise NotImplementedError(msg)


def _add_connection_to_system(
    a_box: box.ResolvedBox, system: LogicalSystem, connection: graphir.ConnectionType
) -> None:
    if isinstance(connection, graphir.ChannelToCogPublishConnection):
        system.connect_channel_producer(connection.channel, connection.cog_instance_member)
    elif isinstance(connection, graphir.ChannelToUdpPublishConnection):
        system.connect_channel_producer(connection.channel, connection.socket_endpoint)
    elif isinstance(connection, graphir.ChannelToAudioPublishConnection):
        system.connect_channel_producer(connection.channel, connection.source_instance)
    elif isinstance(connection, graphir.ChannelToCogSubscribeConnection):
        system.connect_channel_observer(connection.channel, connection.cog_instance_member)
    elif isinstance(connection, graphir.ChannelToUdpSubscribeConnection):
        system.connect_channel_observer(connection.channel, connection.socket_endpoint)
    elif isinstance(connection, graphir.MemoryResourceConnection):
        system.connect_memory_resource(connection.memory_resource, connection.cog_instance_member)
    elif isinstance(connection, graphir.ConfigConnection):
        system.connect_config(connection.config_instance, connection.cog_instance_member)
    elif isinstance(connection, graphir.StateConnection):
        system.connect_state(connection.state_instance, connection.cog_instance_member)
    elif isinstance(connection, graphir.ChannelToDiagnosticsPublish):
        system.connect_channel_producer(connection.channel, connection.diagnostics_instance)
    else:
        msg = a_box.append_error_line(f"Unsupported connection type: {type(connection)}")
        raise NotImplementedError(msg)


@dataclass(slots=True)
class BridgeProducer(typesys.NamedAttribute):
    """Represents a producer (destination) endpoint of a cross-domain link."""

    source_domain: UUID
    dest_domain: UUID
    dest_pinion_buffer: UUID
    remote_source: UUID


@dataclass(slots=True)
class BridgeObserver(typesys.NamedAttribute):
    """Represents a observer (source) endpoint of a cross-domain link."""

    source_domain: UUID
    dest_domain: UUID
    source_pinion_buffer: UUID
    remote_producers: list[UUID]
    lan_port: int


BRIDGE_PRODUCER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="BridgeProducer", type_info=clkbuiltins.TYPE_TYPE
)
BRIDGE_OBSERVER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="BridgeObserver", type_info=clkbuiltins.TYPE_TYPE
)


@dataclass(slots=True)
class PlatformDiagnosticsProducer(typesys.NamedAttribute):
    """Represents a clockwork platform component that publishes diagnostics."""

    uuid: UUID
    pinion_buffer: UUID
    group_id: str
    instance_id: str


PLATFORM_DIAGNOSTICS_PRODUCER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="PlatformDiagnosticsProducer", type_info=clkbuiltins.TYPE_TYPE
)


@dataclass(slots=True)
class PlatformProducer(typesys.NamedAttribute):
    """Represents a clockwork platform component that publishes messages."""

    uuid: UUID
    pinion_buffer: UUID


PLATFORM_PRODUCER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="PlatformProducer", type_info=clkbuiltins.TYPE_TYPE
)


@dataclass(slots=True)
class LogObserver(typesys.NamedAttribute):
    """Represents a buffer observer in the logger."""

    pinion_buffer: UUID
    log_type: logger_config_proto.LogType
    channel_type: logger_config_proto.ChannelType
    is_redundant: bool


LOG_PRODUCER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="LogProducer", type_info=clkbuiltins.TYPE_TYPE
)

LOG_OBSERVER_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="LogObserver", type_info=clkbuiltins.TYPE_TYPE
)

PinionProducerType: TypeAlias = (
    ProducerType | BridgeProducer | LogProducer | PlatformDiagnosticsProducer | PlatformProducer
)
PinionObserverType: TypeAlias = ObserverType | BridgeObserver | LogObserver


@dataclass(slots=True, frozen=True, kw_only=True)
class PinionBufferLayout:
    """Describes layout of a Pinion buffer."""

    num_slots: int
    message_size: int


@dataclass(slots=True)
class PinionBufferBase:
    """Base class for Pinion buffers."""

    uuid: UUID
    cpu_domain_uuid: UUID
    producer: PinionProducerType
    observers: dict[UUID, PinionObserverType]
    layout: PinionBufferLayout
    num_subscribers: int

    def add_observer(self, compiler_context: CompilerContext, observer: PinionObserverType) -> UUID:
        """Add an observer to this buffer."""
        uuid = lookup_uuid(compiler_context, observer)
        if uuid in self.observers:
            msg = f"Observer already registered: {uuid}"
            raise KeyError(msg)
        self.num_subscribers += (
            2
            if isinstance(observer, LogObserver)
            and observer.log_type in (logger_config.LogType.telemetry, logger_config.LogType.redundant_telemetry)
            else 1
        )
        self.observers[uuid] = observer
        return uuid


@dataclass(slots=True)
class MetricsPinionBuffer(PinionBufferBase):
    """A Pinion buffer for metrics channels."""

    channel: MetricsChannel


@dataclass(slots=True)
class PinionBuffer(PinionBufferBase):
    """A Pinion buffer."""

    channel: Channel

    def has_local_endpoints(self) -> bool:
        """Test if the pinion buffer has any local system endpoints."""
        return any(
            isinstance(observer, cog.CogInstanceMember) and isinstance(observer.member, cog.InputDef)  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            for observer in self.observers.values()
        )


@dataclass(slots=True)
class PhysicalCpuDomain:
    """Represents a physical CPU domain."""

    uuid: UUID
    logical: hardware.CpuDomain
    system: PhysicalSystem
    scope: node.Scope
    bridge_observers: dict[UUID, BridgeObserver] = field(default_factory=dict)
    bridge_producers: dict[UUID, BridgeProducer] = field(default_factory=dict)
    log_observers: dict[UUID, LogObserver] = field(default_factory=dict)
    log_producers: dict[UUID, tuple[LogProducer, UUID]] = field(default_factory=dict)
    buffers: dict[UUID, PinionBuffer] = field(default_factory=dict)
    metrics_buffers: dict[UUID, MetricsPinionBuffer] = field(default_factory=dict)
    lan_connection: hardware.EthernetNode | None = None
    lan_port_range: tuple[int, int] | None = None
    lan_port_producers: list[UUID] = field(default_factory=list)
    pinion_buffer_bridge_observers: dict[UUID, BridgeObserver] = field(default_factory=dict)
    platform_diagnostics_producers: dict[UUID, PlatformDiagnosticsProducer] = field(default_factory=dict)
    bridge_diagnostics_producer: UUID | None = field(default=None)
    platform_bridge_status_producers: dict[UUID, PlatformProducer] = field(default_factory=dict)
    bridge_status_producer: UUID | None = field(default=None)
    logging_backup: UUID | None = field(default=None)

    def lookup_observer(self, observer_uuid: UUID) -> PinionObserverType:
        """Retrieve an observer on this domain.

        This exists instead of direct dictionary looking because the observer
        might be locally-defined only in the physical CPU domain, or it might be
        from the logical system.  This function will check in both.

        Raises: KeyError if the observer is not found.
        """
        try:
            return self.bridge_observers[observer_uuid]
        except KeyError:
            pass
        try:
            return self.log_observers[observer_uuid]
        except KeyError:
            pass
        return self.system.system.observer_endpoints[observer_uuid].entity

    def add_bridge_producer(self, result: BridgeProducer) -> UUID:
        """Add a new bridge producer.

        Unlike most of the add_x() methods, this takes a pre-constructed
        instance.  That's because it's not generally possible to construct a
        valid instance on its own, due to cyclic relationships with buffers and
        producers.  These need to be constructed together in a method which can
        satisfy the invariants (specifically in
        PhysicalSystem.add_bridge_link()).
        """
        uuid = register_entity_with_stable_key(self.system.system.module.context, result)
        if uuid in self.bridge_producers:
            msg = f"Duplicate bridge producer UUID {uuid}:\n{self.bridge_producers[uuid]}\n{result}"
            raise KeyError(msg)
        self.bridge_producers[uuid] = result
        return uuid

    def add_bridge_observer(self, observer: BridgeObserver) -> UUID:
        """Add a new bridge observer.

        Unlike most of the add_x() methods, this takes a pre-constructed
        instance.  That's because it's not generally possible to construct a
        valid instance on its own, due to cyclic relationships with buffers and
        producers.  These need to be constructed together in a method which can
        satisfy the invariants (specifically in
        PhysicalSystem.add_bridge_link()).
        """
        uuid = register_entity_with_stable_key(self.system.system.module.context, observer)
        if uuid in self.bridge_observers:
            msg = f"Duplicate bridge observer UUID {uuid}:\n{self.bridge_observers[uuid]}\n{observer}"
            raise KeyError(msg)
        self.bridge_observers[uuid] = observer
        if observer.source_pinion_buffer in self.pinion_buffer_bridge_observers:
            msg = f"Duplicate bridge observer for buffer UUID {uuid}:\n{self.pinion_buffer_bridge_observers[observer.source_pinion_buffer]}\n{observer}"
            raise KeyError(msg)
        self.pinion_buffer_bridge_observers[observer.source_pinion_buffer] = observer
        return uuid

    def add_log_observer(
        self,
        buffer_uuid: UUID,
        log_type: logger_config_proto.LogType,
        channel_type: logger_config_proto.ChannelType,
        is_redundant: bool,
    ) -> UUID:
        """Add a new log observer."""
        observer = LogObserver(
            name=f"log-{log_type}-{buffer_uuid}",
            scope=self.scope,
            type_info=LOG_OBSERVER_TYPE,
            pinion_buffer=buffer_uuid,
            log_type=log_type,
            channel_type=channel_type,
            is_redundant=is_redundant,
        )
        uuid = register_entity_with_stable_key(self.system.system.module.context, observer)
        self.log_observers[uuid] = observer
        buffer = (self.buffers | self.metrics_buffers)[buffer_uuid]
        buffer.add_observer(self.system.system.module.context, observer)
        return uuid

    def add_platform_diagnostics_producer(self, result: PlatformDiagnosticsProducer) -> UUID:
        """Add a platform diagnostics producer.

        Unlike most of the add_x() methods, this takes a pre-constructed
        instance.  That's because it's not generally possible to construct a
        valid instance on its own, due to cyclic relationships with buffers and
        producers.  These need to be constructed together in a method which can
        satisfy the invariants (specifically in
        PhysicalSystem.add_bridge_link()).
        """
        uuid = register_entity_with_stable_key(self.system.system.module.context, result)
        if uuid in self.platform_diagnostics_producers:
            msg = f"Duplicate platform diagnostics producer UUID {uuid}:\n{self.platform_diagnostics_producers[uuid]}\n{result}"
            raise KeyError(msg)
        self.platform_diagnostics_producers[uuid] = result
        return uuid

    def add_platform_bridge_status_producer(self, result: PlatformProducer) -> UUID:
        """Add a platform bridge status producer.

        Unlike most of the add_x() methods, this takes a pre-constructed
        instance.  That's because it's not generally possible to construct a
        valid instance on its own, due to cyclic relationships with buffers and
        producers.  These need to be constructed together in a method which can
        satisfy the invariants (specifically in
        PhysicalSystem.add_bridge_link()).
        """
        uuid = register_entity_with_stable_key(self.system.system.module.context, result)
        if uuid in self.platform_bridge_status_producers:
            msg = f"Duplicate platform bridge status producer UUID {uuid}:\n{self.platform_bridge_status_producers[uuid]}\n{result}"
            raise KeyError(msg)
        self.platform_bridge_status_producers[uuid] = result
        return uuid

    def add_pinion_buffer(self, producer: PinionProducerType, channel: Channel) -> UUID:
        """Add a new Pinion buffer."""
        assert self.system.system.ensure_channel(channel.channel) is channel
        uuid = lookup_uuid(self.system.system.module.context, producer)
        if uuid in self.buffers:
            msg = f"Pinion buffer already exists: {self.buffers[uuid]}"
            raise KeyError(msg)
        result = PinionBuffer(
            uuid=uuid,
            cpu_domain_uuid=self.uuid,
            producer=producer,
            observers={},
            layout=PinionBufferLayout(num_slots=channel.channel.num_slots, message_size=channel.channel.message_size),
            channel=channel,
            num_subscribers=0,
        )
        self.buffers[uuid] = result
        return uuid

    def add_metrics_pinion_buffer(self, producer: PinionProducerType, metrics_channel: MetricsChannel) -> UUID:
        """Add a new Metrics Pinion buffer."""
        assert self.system.system.ensure_metrics_channel(metrics_channel.channel) is metrics_channel
        uuid = lookup_uuid(self.system.system.module.context, producer)
        result = MetricsPinionBuffer(
            uuid=uuid,
            cpu_domain_uuid=self.uuid,
            producer=producer,
            observers={},
            layout=PinionBufferLayout(
                num_slots=metrics_channel.channel.num_slots, message_size=metrics_channel.channel.message_size
            ),
            channel=metrics_channel,
            num_subscribers=0,
        )
        self.metrics_buffers[uuid] = result
        return uuid


@dataclass
class PhysicalSystem:
    """Represents a physical system."""

    system: LogicalSystem
    cpu_domains: dict[UUID, PhysicalCpuDomain] = field(default_factory=dict)
    bridge_domains: set[UUID] = field(default_factory=set)

    def add_cpu_domain(self, logical: hardware.CpuDomain) -> UUID:
        """Add a new CPU Domain."""
        uuid = lookup_uuid(self.system.module.context, logical)
        if uuid in self.cpu_domains:
            msg = f"Duplicate CPU domain {uuid}:\n{self.cpu_domains[uuid]}\n{logical}"
            raise KeyError(msg)
        scope = node.Scope(
            parent=None,
            uniq_path=f"{logical.scope.uniq_path}::{logical.name}",
            module_id_for_errors=self.system.module.module_id,
        )

        lan_conn = hardware.get_cpu_domain_connection(system_module=self.system.module, cpu_domain=logical)
        port_range = lan_conn.bridge_ports if lan_conn else None
        result = PhysicalCpuDomain(
            uuid=uuid,
            logical=logical,
            system=self,
            scope=scope,
            lan_connection=lan_conn,
            lan_port_range=port_range,
        )
        self.cpu_domains[uuid] = result
        if logical.logging_backup:
            assert isinstance(logical.logging_backup, hardware.CpuDomain)
            result.logging_backup = lookup_uuid(self.system.module.context, logical.logging_backup)
        return uuid

    def add_bridge_link(self, source_domain_uuid: UUID, source_buffer_uuid: UUID, dest_domain_uuid: UUID) -> UUID:
        """Add a new bridge link.

        On the source domain, this creates and registers a new bridge observer.  On the destination domain, it creates and registers a new bridge producer and buffer.  It links them all together correctly.

        Returns: The new destination pinion buffer UUID.
        """
        source_domain = self.cpu_domains[source_domain_uuid]
        source_buffer = source_domain.buffers[source_buffer_uuid]
        dest_domain = self.cpu_domains[dest_domain_uuid]

        if (
            source_domain.lan_connection is None
            or dest_domain.lan_connection is None
            or source_domain.lan_connection.lan is not dest_domain.lan_connection.lan
        ):
            msg = f"No Ethernet LAN connection between domains: {source_domain.logical.value_key()} and {dest_domain.logical.value_key()}"
            raise ValueError(msg)

        observer = source_domain.pinion_buffer_bridge_observers.get(source_buffer_uuid, None)
        if observer:
            observer_uuid = lookup_uuid(self.system.module.context, observer)
        else:
            assert source_domain.lan_port_range is not None
            # We allocate port numbers sequentially starting at the beginning of the
            # port range, which is in lan_port_range[0].  So we just add to that the
            # number of producers made so far to get the next port number to
            # allocate.
            observer_port = source_domain.lan_port_range[0] + len(source_domain.lan_port_producers)
            observer = BridgeObserver(
                name=f"bridge-{source_buffer_uuid}-to-{dest_domain_uuid}",
                scope=source_domain.scope,
                type_info=BRIDGE_OBSERVER_TYPE,
                source_domain=source_domain_uuid,
                dest_domain=dest_domain_uuid,
                source_pinion_buffer=source_buffer_uuid,
                remote_producers=[],
                lan_port=observer_port,
            )
            observer_uuid = source_domain.add_bridge_observer(observer)
            assert source_buffer.add_observer(self.system.module.context, observer) == observer_uuid
        producer = BridgeProducer(
            name=f"bridge-{source_buffer_uuid}-from-{source_domain_uuid}",
            scope=dest_domain.scope,
            type_info=BRIDGE_PRODUCER_TYPE,
            source_domain=source_domain_uuid,
            dest_domain=dest_domain_uuid,
            dest_pinion_buffer=UUID(int=0),  # Replaced below once we know it
            remote_source=observer_uuid,
        )
        producer_uuid = dest_domain.add_bridge_producer(producer)
        source_domain.lan_port_producers.append(producer_uuid)
        dest_buffer_uuid = dest_domain.add_pinion_buffer(producer=producer, channel=source_buffer.channel)

        observer.remote_producers.append(producer_uuid)
        producer.dest_pinion_buffer = dest_buffer_uuid

        self.bridge_domains.add(source_domain_uuid)
        self.bridge_domains.add(dest_domain_uuid)

        return dest_buffer_uuid


def make_physical_system(system: LogicalSystem) -> PhysicalSystem:
    """Construct a physical realization of a logical system."""
    system.validate_connections()
    result = PhysicalSystem(system=system)
    for domain_uuid, domain in system.cpu_domains.items():
        assert result.add_cpu_domain(domain) == domain_uuid
    for channel in system.channels.values():
        _make_physical_channel(result, channel)
    _add_bridge_channel_producers(result)
    for metrics_channel in system.metrics_channels.values():
        _make_metrics_channel(result, metrics_channel)
    return result


def _make_metrics_channel(system: PhysicalSystem, metrics_channel: MetricsChannel) -> None:
    if not metrics_channel.producers:
        msg = f"Metrics channels must have a producer; channel {metrics_channel.channel.channel_name} has none"
        raise ValueError(msg)
    assert metrics_channel.is_valid()
    for producer in metrics_channel.producers.values():
        producer_domain_uuid = system.system.process_to_domain[producer.process]
        producer_domain = system.cpu_domains[producer_domain_uuid]
        producer_domain.add_metrics_pinion_buffer(producer.entity, metrics_channel)


def _make_physical_channel(system: PhysicalSystem, channel: Channel) -> None:
    if not channel.producers:
        msg = f"Channels must have a producer; channel {channel.channel.channel_name} has none"
        raise ValueError(msg)
    if channel.is_single_producer() and not channel.is_valid():
        msg = f"Single producer channels must have one producer; channel {channel.channel.channel_name} has {len(channel.producers)}"
        raise ValueError(msg)
    assert channel.is_valid()
    for producer in channel.producers.values():
        producer_domain_uuid = system.system.process_to_domain[producer.process]
        producer_domain = system.cpu_domains[producer_domain_uuid]
        # First create a buffer on the producer's node.
        producer_buffer_uuid = producer_domain.add_pinion_buffer(producer.entity, channel)
        if isinstance(producer.entity, LogProducer):
            log_producer_uuid = lookup_uuid(system.system.module.context, producer.entity)
            assert system.system.log_producers[log_producer_uuid] is producer.entity
            producer_domain.log_producers[log_producer_uuid] = (producer.entity, producer_buffer_uuid)

        redundant_logging_domain_uuid, redundant_logging_buffer_uuid = _add_logging_observers(
            system, channel, producer_domain_uuid, producer_buffer_uuid
        )

        observers_by_domain: dict[UUID, list[UUID]] = defaultdict(list)
        for observer_uuid, observer in channel.observers.items():
            observers_by_domain[system.system.process_to_domain[observer.process]].append(observer_uuid)
        for domain_uuid, observers in observers_by_domain.items():
            if domain_uuid != producer_domain_uuid:
                if domain_uuid != redundant_logging_domain_uuid:
                    domain_buffer_uuid = system.add_bridge_link(
                        source_domain_uuid=producer_domain_uuid,
                        source_buffer_uuid=producer_buffer_uuid,
                        dest_domain_uuid=domain_uuid,
                    )
                else:
                    assert isinstance(redundant_logging_buffer_uuid, UUID)
                    domain_buffer_uuid = redundant_logging_buffer_uuid
            else:
                domain_buffer_uuid = producer_buffer_uuid
            domain = system.cpu_domains[domain_uuid]
            domain_buffer = domain.buffers[domain_buffer_uuid]
            for domain_observer_uuid in observers:
                domain_observer = domain.lookup_observer(domain_observer_uuid)
                domain_buffer.add_observer(system.system.module.context, domain_observer)


def _add_logging_observers(
    system: PhysicalSystem, channel: Channel, producer_domain_uuid: UUID, producer_buffer_uuid: UUID
) -> tuple[UUID | None, UUID | None]:
    channel_logging_policy = logger_config.get_channel_logging_policy()
    policy_data = policy.lookup_policy(system.system.module, channel_logging_policy, channel.channel.ir_node)
    if policy_data is None:
        if system.system.require_logging_policies:
            msg = channel.channel.ir_node.append_error_line(
                f"No logging policy defined for {channel.channel.channel_name} in @{system.system.module.module_id.repo}::{system.system.module.module_id.name}"
            )
            raise ValueError(msg)
        return (None, None)

    redundant_logging_domain_uuid: UUID | None = None
    redundant_logging_buffer_uuid: UUID | None = None

    log_type_ref = policy_data.data.data["log_type"]
    assert isinstance(log_type_ref, clkenum.ValueRef)
    log_type = logger_config.LogType[log_type_ref.name]  # pyright: ignore[reportInvalidTypeArguments] # This is an Enum, but linter doesn't know that
    channel_type_ref = policy_data.data.data["channel_type"]
    assert isinstance(channel_type_ref, clkenum.ValueRef)
    channel_type = logger_config.ChannelType[
        channel_type_ref.name  # pyright: ignore[reportInvalidTypeArguments] # This is an Enum, but linter doesn't know that
    ]

    producer_domain = system.cpu_domains[producer_domain_uuid]
    assert log_type in (
        logger_config.LogType.none,
        logger_config.LogType.telemetry,
        logger_config.LogType.event,
        logger_config.LogType.redundant_telemetry,
    )
    if log_type != logger_config.LogType.none:
        producer_domain.add_log_observer(buffer_uuid=producer_buffer_uuid, log_type=log_type, channel_type=channel_type, is_redundant=False)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    if log_type == logger_config.LogType.redundant_telemetry and isinstance(producer_domain.logging_backup, UUID):
        redundant_logging_domain_uuid = producer_domain.logging_backup
        redundant_logging_domain = system.cpu_domains[redundant_logging_domain_uuid]
        redundant_logging_buffer_uuid = system.add_bridge_link(
            source_domain_uuid=producer_domain_uuid,
            source_buffer_uuid=producer_buffer_uuid,
            dest_domain_uuid=redundant_logging_domain_uuid,
        )
        redundant_logging_domain.add_log_observer(buffer_uuid=redundant_logging_buffer_uuid, log_type=log_type, channel_type=channel_type, is_redundant=True)  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    return (redundant_logging_domain_uuid, redundant_logging_buffer_uuid)


def _add_bridge_diagnostics_producers(
    system: PhysicalSystem, diagnostics_channel: Channel, observers_by_domain: dict[UUID, list[UUID]]
) -> None:
    for producer_domain_uuid in system.bridge_domains:
        producer_domain = system.cpu_domains[producer_domain_uuid]
        diagnostics_producer = PlatformDiagnosticsProducer(
            name=f"bridge-diagnostics-{producer_domain_uuid}",
            scope=producer_domain.scope,
            type_info=PLATFORM_DIAGNOSTICS_PRODUCER_TYPE,
            uuid=UUID(int=0),  # Replaced below once we know it
            pinion_buffer=UUID(int=0),  # Replaced below once we know it
            group_id="tcp_bridge",
            instance_id=snake_from_camel(producer_domain.logical.name),
        )
        diagnostics_producer_uuid = producer_domain.add_platform_diagnostics_producer(diagnostics_producer)
        producer_buffer_uuid = producer_domain.add_pinion_buffer(diagnostics_producer, diagnostics_channel)
        diagnostics_producer.uuid = diagnostics_producer_uuid
        diagnostics_producer.pinion_buffer = producer_buffer_uuid
        producer_domain.bridge_diagnostics_producer = diagnostics_producer_uuid

        redundant_logging_domain_uuid, redundant_logging_buffer_uuid = _add_logging_observers(
            system, diagnostics_channel, producer_domain_uuid, producer_buffer_uuid
        )

        for domain_uuid, observers in observers_by_domain.items():
            if domain_uuid != producer_domain_uuid:
                if domain_uuid != redundant_logging_domain_uuid:
                    domain_buffer_uuid = system.add_bridge_link(
                        source_domain_uuid=producer_domain_uuid,
                        source_buffer_uuid=producer_buffer_uuid,
                        dest_domain_uuid=domain_uuid,
                    )
                else:
                    assert isinstance(redundant_logging_buffer_uuid, UUID)
                    domain_buffer_uuid = redundant_logging_buffer_uuid
            else:
                domain_buffer_uuid = producer_buffer_uuid
            domain = system.cpu_domains[domain_uuid]
            domain_buffer = domain.buffers[domain_buffer_uuid]
            for domain_observer_uuid in observers:
                domain_observer = domain.lookup_observer(domain_observer_uuid)
                domain_buffer.add_observer(system.system.module.context, domain_observer)


def _add_bridge_status_producers(
    system: PhysicalSystem, bridge_status_channel: Channel, observers_by_domain: dict[UUID, list[UUID]]
) -> None:
    for producer_domain_uuid in system.bridge_domains:
        producer_domain = system.cpu_domains[producer_domain_uuid]
        bridge_status_producer = PlatformProducer(
            name=f"bridge-status-{producer_domain_uuid}",
            scope=producer_domain.scope,
            type_info=PLATFORM_PRODUCER_TYPE,
            uuid=UUID(int=0),  # Replaced below once we know it
            pinion_buffer=UUID(int=0),  # Replaced below once we know it
        )
        bridge_status_producer_uuid = producer_domain.add_platform_bridge_status_producer(bridge_status_producer)
        producer_buffer_uuid = producer_domain.add_pinion_buffer(bridge_status_producer, bridge_status_channel)
        bridge_status_producer.uuid = bridge_status_producer_uuid
        bridge_status_producer.pinion_buffer = producer_buffer_uuid
        producer_domain.bridge_status_producer = bridge_status_producer_uuid

        redundant_logging_domain_uuid, redundant_logging_buffer_uuid = _add_logging_observers(
            system, bridge_status_channel, producer_domain_uuid, producer_buffer_uuid
        )

        for domain_uuid, observers in observers_by_domain.items():
            if domain_uuid != producer_domain_uuid:
                if domain_uuid != redundant_logging_domain_uuid:
                    domain_buffer_uuid = system.add_bridge_link(
                        source_domain_uuid=producer_domain_uuid,
                        source_buffer_uuid=producer_buffer_uuid,
                        dest_domain_uuid=domain_uuid,
                    )
                else:
                    assert isinstance(redundant_logging_buffer_uuid, UUID)
                    domain_buffer_uuid = redundant_logging_buffer_uuid
            else:
                domain_buffer_uuid = producer_buffer_uuid
            domain = system.cpu_domains[domain_uuid]
            domain_buffer = domain.buffers[domain_buffer_uuid]
            for domain_observer_uuid in observers:
                domain_observer = domain.lookup_observer(domain_observer_uuid)
                domain_buffer.add_observer(system.system.module.context, domain_observer)


def _add_bridge_channel_producers(system: PhysicalSystem) -> None:
    """Add producers for the channels published by the TCP bridge."""
    ir_diagnostics_channel = graphir.diagnostics_channel(system.system.module.context)
    ir_bridge_status_channel = graphir.bridge_status_channel(system.system.module.context)
    if (ir_diagnostics_channel or ir_bridge_status_channel) and system.bridge_domains:
        diagnostics_observers_by_domain: dict[UUID, list[UUID]] = defaultdict(list)
        bridge_status_observers_by_domain: dict[UUID, list[UUID]] = defaultdict(list)
        if ir_diagnostics_channel:
            diagnostics_channel = system.system.ensure_channel(ir_diagnostics_channel)
            for observer_uuid, observer in diagnostics_channel.observers.items():
                system.bridge_domains.add(system.system.process_to_domain[observer.process])
                diagnostics_observers_by_domain[system.system.process_to_domain[observer.process]].append(observer_uuid)
        if ir_bridge_status_channel:
            bridge_status_channel = system.system.ensure_channel(ir_bridge_status_channel)
            for observer_uuid, observer in bridge_status_channel.observers.items():
                system.bridge_domains.add(system.system.process_to_domain[observer.process])
                bridge_status_observers_by_domain[system.system.process_to_domain[observer.process]].append(
                    observer_uuid
                )
        if ir_diagnostics_channel and system.bridge_domains:
            diagnostics_channel = system.system.ensure_channel(ir_diagnostics_channel)
            assert isinstance(diagnostics_channel, Channel)
            _add_bridge_diagnostics_producers(system, diagnostics_channel, diagnostics_observers_by_domain)
        if ir_bridge_status_channel and system.bridge_domains:
            bridge_status_channel = system.system.ensure_channel(ir_bridge_status_channel)
            assert isinstance(bridge_status_channel, Channel)
            _add_bridge_status_producers(system, bridge_status_channel, bridge_status_observers_by_domain)


def add_metrics_logging_observers(system: PhysicalSystem) -> None:
    """Add metrics logging observers to a system."""
    for domain in system.cpu_domains.values():
        for buffer_uuid, buffer in domain.metrics_buffers.items():
            if buffer.channel.channel.log_type == cog.MetricsLogType.event:
                log_type = logger_config.LogType.event
            elif buffer.channel.channel.log_type == cog.MetricsLogType.telemetry:
                log_type = logger_config.LogType.telemetry
            else:
                msg = f"Unsupported log type {buffer.channel.channel.log_type} for metrics channel {buffer.channel.channel.channel_name}"
                raise ValueError(msg)
            channel_type = logger_config.ChannelType.regular
            domain.add_log_observer(
                buffer_uuid=buffer_uuid, log_type=log_type, channel_type=channel_type, is_redundant=False
            )
