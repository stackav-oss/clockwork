# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate ProcessDescriptions from Process IR."""

from __future__ import annotations

import itertools
from dataclasses import dataclass
from typing import TYPE_CHECKING, cast
from uuid import UUID, uuid4

from clockwork.dsl.composition import pdf, system
from clockwork.dsl.ir import (
    box,
    cog,
    diagnostics,
    extern_type,
    node,
    primitive,
    representation,
    schema_reg,
    typesys,
    udp,
)
from clockwork.dsl.ir.cog_components import InputDef, MetricsOutputDef, OutputDef
from clockwork.dsl.ir.uuid_reg import lookup_uuid
from clockwork.dsl.serialization import tachyon_layout_reg

if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping

    from clockwork.dsl.composition import pdfproto


@dataclass
class _ProcessGraph:
    """Helper class to hold data for a process during ProcessDescription generation."""

    pdf: pdfproto.ProcessDescription
    cog_names: dict[UUID, str]
    init_cog_deps: dict[UUID, set[UUID]]
    data_source_map: dict[UUID, int]  # data_source_uuid to index in pdf.data_sources

    @staticmethod
    def make(process_id: UUID) -> _ProcessGraph:
        return _ProcessGraph(
            pdf=pdf.ProcessDescription(
                process_id=process_id,
                cog_instances=[],
                state_graph=pdf.StateGraph(state_instances=[], connections=[]),
                config_graph=pdf.ConfigGraph(config_instances=[], connections=[]),
                pubsub_graph=pdf.PubSubGraph(publish_endpoints=[], connections=[]),
                memory_resource_graph=pdf.MemoryResourceGraph(memory_resources=[], connections=[]),
                timers=[],
                init_cogs=[],
                log_cog=uuid4(),
                io_connections=[],
                not_connected_endpoints=[],
                snapshot_configs=[],
                data_sources=[],
            ),
            cog_names={},
            init_cog_deps={},
            data_source_map={},
        )

    def add_data_source(
        self,
        data_source_uuid: UUID,
        data_source: box.FirstMessageInstance | box.SerializedDataFileInstance,
        sys: system.LogicalSystem,
    ) -> int:
        """Add a data source to the process description if not already present, returning its index."""
        if data_source_uuid in self.data_source_map:
            return self.data_source_map[data_source_uuid]

        fallback_index = pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL
        if data_source_uuid in sys.data_source_fallbacks:
            fallback_uuid = sys.data_source_fallbacks[data_source_uuid]
            fallback_index = self.add_data_source(fallback_uuid, sys.data_sources[fallback_uuid], sys)
        elif isinstance(data_source, box.FirstMessageInstance):
            if data_source.allow_default:
                fallback_index = pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

        index = len(self.pdf.data_sources)
        self.data_source_map[data_source_uuid] = index

        if isinstance(data_source, box.SerializedDataFileInstance):
            data_source_type = pdf.DataSourceType.file
            source_path_or_name = str(data_source.file_path)
            repr_id = lookup_uuid(data_source.module.context, data_source.repr_typespec)
        else:  # FirstMessageInstance
            data_source_type = pdf.DataSourceType.log_first_message
            assert isinstance(data_source.channel.channel_name, primitive.StringValue)
            source_path_or_name = str(data_source.channel.channel_name.value)
            assert data_source.channel.message_repr is not None
            repr_id = lookup_uuid(data_source.module.context, data_source.channel.message_repr.typespec)

        self.pdf.data_sources.append(
            pdf.DataSource(
                representation_id=repr_id,
                data_source_type=data_source_type,
                source_path_or_name=source_path_or_name,
                fallback_source=fallback_index,
            )
        )
        return index


def gen_pd_sys(sys: system.PhysicalSystem) -> dict[UUID, pdfproto.ProcessDescription]:
    """Generate a ProcessDescription for a System.

    Returns:
        Mapping from process UUID to ProcessDescription for that process.
    """
    processes = {process_uuid: _ProcessGraph.make(process_uuid) for process_uuid in sys.system.processes}
    _gen_cogs_sys(sys, processes)
    _gen_udp_sys(sys, processes)
    _gen_audio_sys(sys, processes)
    _gen_state_sys(sys, processes)
    _gen_config_sys(sys, processes)
    _gen_memres_sys(sys, processes)
    _gen_pubsub_sys(sys, processes)
    _gen_non_connected_endpoints(sys, processes)
    _gen_snapshot_configs(sys, processes)
    return {process_uuid: process.pdf for process_uuid, process in processes.items()}


def _gen_cogs_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    log_cog_set = False
    for uuid, cog_instance in sys.system.cogs.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        cog_desc, cog_timers = gen_cog(cog_instance)
        assert uuid == cog_desc.cog_instance_id
        process_desc.cog_instances.append(cog_desc)
        process_graph.cog_names[uuid] = cog_desc.instance_path_name
        process_desc.timers.extend(cog_timers)
        if cog_instance.cog_class.is_init():
            process_graph.init_cog_deps[cog_desc.cog_instance_id] = set()
        if cog_instance.cog_class.is_log():
            if log_cog_set:
                msg = "Only one log playing cog is allowed in a system"
                raise ValueError(msg)
            process_graph.pdf.log_cog = cog_desc.cog_instance_id
            log_cog_set = True


def _gen_udp_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for uuid, udp_instance in sys.system.udp_sockets.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        endpoints = [
            pdf.EndpointInstanceDescription(
                endpoint_class_id=lookup_uuid(sys.system.module.context, entity.endpoint),
                endpoint_instance_id=lookup_uuid(sys.system.module.context, entity),
            )
            for entity in (udp_instance.producer_endpoint, udp_instance.observer_endpoint)
            if entity
        ]
        udp_desc = pdf.IoConnectionInstanceDescription(
            instance_path_name=udp_instance.value_key(),
            class_id=lookup_uuid(sys.system.module.context, udp_instance.socket),
            instance_id=lookup_uuid(sys.system.module.context, udp_instance),
            endpoints=endpoints,
            diags_endpoint_id=None,
        )
        process_desc.io_connections.append(udp_desc)


def _gen_audio_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for uuid, audio_instance in sys.system.audio_sources.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        # The class and instance ID is also reused as the endpoint
        # class and instance ID for audio as every instance only has
        # one endpoint.
        class_id = lookup_uuid(sys.system.module.context, audio_instance.source)
        instance_id = lookup_uuid(sys.system.module.context, audio_instance)
        audio_desc = pdf.IoConnectionInstanceDescription(
            instance_path_name=audio_instance.value_key(),
            class_id=class_id,
            instance_id=instance_id,
            endpoints=[
                pdf.EndpointInstanceDescription(
                    endpoint_class_id=class_id,
                    endpoint_instance_id=instance_id,
                )
            ],
            diags_endpoint_id=lookup_uuid(sys.system.module.context, audio_instance.diagnostics),
        )
        process_desc.io_connections.append(audio_desc)


def _gen_state_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for uuid, state_instance in sys.system.states.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        state_desc = gen_state_instance(state_instance.entity)
        if uuid in sys.system.init_data_sources:
            data_source_uuid = sys.system.init_data_sources[uuid]
            index = process_graph.add_data_source(
                data_source_uuid, sys.system.data_sources[data_source_uuid], sys.system
            )
            state_desc.init_data_source = index
        process_desc.state_graph.state_instances.append(state_desc)
        if state_instance.entity.init_cog_endpoint:
            init_cog_id = lookup_uuid(sys.system.module.context, state_instance.entity.init_cog_endpoint.cog_instance)
        else:
            init_cog_id = None
        for ep_uuid, endpoint in state_instance.endpoints.items():
            if endpoint.process != process_uuid:
                msg = f"State {state_desc} connected to endpoint in different process: {endpoint.entity}"
                raise ValueError(msg)
            process_desc.state_graph.connections.append(pdf.StateConnection(state_id=uuid, endpoint_id=ep_uuid))
            if endpoint.entity.cog_instance.cog_class.is_init():
                cog_instance_id = lookup_uuid(sys.system.module.context, endpoint.entity.cog_instance)
                if init_cog_id is not None and init_cog_id != cog_instance_id:
                    # Another init cog depends on this state but doesn't
                    # initialize it; that's an init dependency.
                    process_graph.init_cog_deps[cog_instance_id].add(init_cog_id)


def _gen_config_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for uuid, config_instance in sys.system.configs.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        config_desc = gen_config_instance(config_instance.entity)
        config_desc.init_data_source = process_graph.add_data_source(uuid, config_instance.entity, sys.system)
        # Set init_data_source if present
        if uuid in sys.system.init_data_sources:
            data_source_uuid = sys.system.init_data_sources[uuid]
            index = process_graph.add_data_source(
                data_source_uuid, sys.system.data_sources[data_source_uuid], sys.system
            )
            config_desc.init_data_source = index
        process_desc.config_graph.config_instances.append(config_desc)
        for ep_uuid, endpoint in config_instance.endpoints.items():
            if endpoint.process != process_uuid:
                msg = f"Config {config_desc} connected to endpoint in different process: {endpoint.entity}"
                raise ValueError(msg)
            process_desc.config_graph.connections.append(pdf.ConfigConnection(config_id=uuid, endpoint_id=ep_uuid))


def _gen_memres_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for uuid, memres_instance in sys.system.mem_resources.items():
        process_uuid = sys.system.entity_to_process[uuid]
        process_graph = processes[process_uuid]
        process_desc = process_graph.pdf
        memres_desc = pdf.MemoryResource(
            memory_resource_id=uuid,
            instance_path_name=memres_instance.entity.value_key(),
            resource_type=pdf.MemoryResourceType.new_delete,
            resource_max_size=memres_instance.entity.max_size,
        )
        process_desc.memory_resource_graph.memory_resources.append(memres_desc)
        for ep_uuid, endpoint in memres_instance.endpoints.items():
            if endpoint.process != process_uuid:
                msg = f"Memory resource {memres_desc} connected to endpoint in different process: {endpoint.entity}"
                raise ValueError(msg)
            process_desc.memory_resource_graph.connections.append(
                pdf.MemoryResourceConnection(memory_resource_id=uuid, endpoint_id=ep_uuid)
            )


def _add_non_connected_endpoint(
    entity: cog.CogInstanceMember[InputDef]
    | cog.CogInstanceMember[OutputDef]
    | cog.CogInstanceMember[MetricsOutputDef],
    message_size: int,
    process_desc: pdfproto.ProcessDescription,
) -> None:
    """Add a non-connected endpoint to the process description along with whether the endpoint is a publisher or subscriber and the message size."""
    uuid = lookup_uuid(entity.cog_instance.module.context, entity)
    if isinstance(entity.member, InputDef):
        endpoint_type = pdf.NotConnectedEndpointType.subscriber
    else:
        endpoint_type = pdf.NotConnectedEndpointType.publisher
    process_desc.not_connected_endpoints.append(
        pdf.NotConnectedEndpoint(
            endpoint_id=uuid,
            endpoint_type=endpoint_type,
            # The buffer will not be connected so it just needs one slot
            buffer_layout=pdf.PinionBufferLayout(num_slots=1, message_size=message_size, is_published_once=False),
        )
    )


def _gen_non_connected_endpoints(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    """Lookup the non-connected endpoints for each process and write them to the process description.

    This is ultimately used for tracking which cog inputs and outputs are valid as "non-connected"
    even though they are not in the pubsub graph.
    """
    for process_uuid, process_graph in processes.items():
        process_desc = process_graph.pdf
        for observer_uuid, message_size in sys.system.ignored_observer_endpoints.items():
            if sys.system.observer_endpoints[observer_uuid].process == process_uuid:
                observer_entity = sys.system.observer_endpoints[observer_uuid].entity
                if not isinstance(observer_entity, cog.CogInstanceMember):
                    msg = node.enrich_error_if_possible(observer_entity, "Non-cog observer endpoint  cannot be ignored")
                    raise ValueError(msg)
                _add_non_connected_endpoint(observer_entity, message_size, process_desc)

        for producer_uuid, message_size in sys.system.ignored_producer_endpoints.items():
            if sys.system.producer_endpoints[producer_uuid].process == process_uuid:
                producer_entity = sys.system.producer_endpoints[producer_uuid].entity
                if not isinstance(producer_entity, cog.CogInstanceMember):
                    msg = node.enrich_error_if_possible(producer_entity, "Non-cog producer endpoint cannot be ignored")
                    raise ValueError(msg)
                if not isinstance(producer_entity.member, InputDef | OutputDef | MetricsOutputDef):
                    msg = node.enrich_error_if_possible(
                        producer_entity, "Only cog input/output/metrics endpoints can be ignored"
                    )
                    raise ValueError(msg)
                _add_non_connected_endpoint(
                    cast(
                        "cog.CogInstanceMember[InputDef] | cog.CogInstanceMember[OutputDef] | cog.CogInstanceMember[MetricsOutputDef]",
                        producer_entity,
                    ),
                    message_size=message_size,
                    process_desc=process_desc,
                )


def _gen_snapshot_configs(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    """Generate snapshot configurations from snapshot producers.

    This function iterates over snapshot metadata in the logical system and
    populates the snapshot_configs field in the process descriptions with the
    information needed by the scaffolding layer to implement snapshot/restore.
    """
    for endpoint_uuid, snapshot_producer in sys.system.snapshot_metadata.items():
        if endpoint_uuid not in sys.system.entity_to_process:
            msg = f"Snapshot producer endpoint {endpoint_uuid} not assigned to any process"
            raise ValueError(msg)

        process_uuid = sys.system.entity_to_process[endpoint_uuid]
        process_desc = processes[process_uuid].pdf

        snapshot_cfg = pdf.SnapshotConfig(
            endpoint_id=endpoint_uuid,
            snapshot_publisher_id=endpoint_uuid,
            interval=snapshot_producer.interval_ns,
            cycles=snapshot_producer.cycles,
        )
        process_desc.snapshot_configs.append(snapshot_cfg)


def _gen_pubsub_sys(sys: system.PhysicalSystem, processes: dict[UUID, _ProcessGraph]) -> None:
    for process_uuid, process_graph in processes.items():
        process_desc = process_graph.pdf
        cpu_uuid = sys.system.process_to_domain[process_uuid]
        cpu_domain = sys.cpu_domains[cpu_uuid]
        for buffer_uuid, buffer in itertools.chain(cpu_domain.buffers.items(), cpu_domain.metrics_buffers.items()):
            pub_ep = pdf.PublishEndpoint(
                process_id=sys.system.entity_to_process.get(buffer_uuid, UUID(int=0)),
                publisher_id=buffer_uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=buffer.layout.num_slots,
                    message_size=buffer.layout.message_size,
                    is_published_once=buffer.layout.is_published_once,
                ),
                num_subscribers=buffer.num_subscribers,
                channel_name=buffer.channel.channel.channel_name,
                is_bulk_data=buffer.channel.is_bulk_data(),
            )
            # We include this buffer's publish endpoint only if this process publishes it or subscribes to it.
            # If we subscribe but don't publish, that's handled below.
            if pub_ep.process_id == process_uuid and not isinstance(buffer.producer, system.LogProducer):
                process_desc.pubsub_graph.publish_endpoints.append(pub_ep)
                inserted_pub = True
            else:
                inserted_pub = False
            for observer_uuid, observer in buffer.observers.items():
                if isinstance(observer, cog.CogInstanceMember | udp.UdpSocketEndpointInstance):
                    # Only include subscriptions for single producer channels,
                    # multi-producer channels are handled separately.
                    if (
                        buffer.channel.is_single_producer()
                        and sys.system.entity_to_process[observer_uuid] == process_uuid
                    ):
                        process_desc.pubsub_graph.connections.append(
                            pdf.PubSubConnection(
                                subscriber_process_id=process_uuid,
                                subscriber_id=observer_uuid,
                                publisher_id=buffer_uuid,
                            )
                        )
                        if not inserted_pub:
                            process_desc.pubsub_graph.publish_endpoints.append(pub_ep)
                            inserted_pub = True
                elif isinstance(observer, system.BridgeObserver | system.LogObserver):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                    # Not part of the process description file
                    pass
                else:
                    msg = f"Unrecognized observer type {type(observer)}"
                    raise NotImplementedError(msg)

        process_desc.init_cogs = gen_init_list(
            init_cog_deps=process_graph.init_cog_deps, cog_names=process_graph.cog_names
        )


def gen_cog(cog_ir: cog.CogInstance) -> tuple[pdfproto.CogInstanceDescription, list[pdfproto.TimerInstanceDescription]]:
    """Generate a CogInstanceDescription for a CogInstance."""
    cog_desc = pdf.CogInstanceDescription(
        cog_class_id=lookup_uuid(cog_ir.module.context, cog_ir.cog_class),
        cog_instance_id=lookup_uuid(cog_ir.module.context, cog_ir),
        endpoints=[gen_cog_endpoint(x) for x in cog_ir.members],
        instance_path_name=cog_ir.value_key(),
    )
    timers = list(gen_cog_timers(cog_ir))
    return cog_desc, timers


def gen_cog_endpoint(
    member_instance: cog.CogInstanceMember[
        cog.ResourceDef
        | cog.ConfigDef
        | cog.StateDef
        | cog.InputDef
        | cog.OutputDef
        | cog.MetricsOutputDef
        | cog.ReportGroupDef
        | cog.ConditionDef
        | diagnostics.DiagnosticsDef
        | diagnostics.InfraDiagnosticsDef
    ],
) -> pdfproto.EndpointInstanceDescription:
    """Generate an EndpointInstanceDescription for a CogInstanceMember."""
    return pdf.EndpointInstanceDescription(
        endpoint_class_id=lookup_uuid(member_instance.cog_instance.module.context, member_instance.member),
        endpoint_instance_id=lookup_uuid(member_instance.cog_instance.module.context, member_instance),
    )


def gen_cog_timers(cog_ir: cog.CogInstance) -> Iterable[pdfproto.TimerInstanceDescription]:
    """Generate a list of timer instances for a Cog instance."""
    for member in cog_ir.members:
        if isinstance(member.member, cog.ConditionDef):
            if not isinstance(member.member.condition, cog.TimeSinceLastExec):
                msg = cog_ir.append_error_line(f"Unsupported condition member type: {member}")
                raise NotImplementedError(msg)
            yield pdf.TimerInstanceDescription(
                timer_id=lookup_uuid(cog_ir.module.context, member), instance_path_name=member.value_key()
            )


def gen_config_instance(
    data_file: box.FirstMessageInstance | box.SerializedDataFileInstance,
) -> pdfproto.ConfigInstanceDescription:
    """Generate a ConfigInstanceDescription for a TextProto file."""
    return pdf.ConfigInstanceDescription(
        config_instance_id=lookup_uuid(data_file.module.context, data_file),
        instance_path_name=data_file.value_key(),
        init_data_source=pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL,  # to be filled in later
    )


def gen_state_instance(state_instance: box.StateInstance) -> pdfproto.StateInstanceDescription:
    """Generate a StateInstanceDescription for a TextProto file."""
    repr_typespec = state_instance.repr_typespec
    if isinstance(repr_typespec, typesys.Instantiation):
        return _gen_state_instance_schema(state_instance, repr_typespec)
    if isinstance(repr_typespec, extern_type.ExternType):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return _gen_state_instance_extern(state_instance, repr_typespec)
    msg = state_instance.append_error_line(f"Unsupported state representation {state_instance}")
    raise RuntimeError(msg)


def _gen_state_instance_schema(
    state_instance: box.StateInstance, repr_typespec: typesys.Instantiation
) -> pdfproto.StateInstanceDescription:
    repr_ref = representation.RepresentationReference.from_typespec(repr_typespec)
    if isinstance(repr_ref, str):
        msg = state_instance.append_error_line(f"Unsupported representation {repr_typespec.value_key()}")
        raise ValueError(msg)  # noqa: TRY004 (ValueError is more appropriate here than TypeError)
    state_repr = schema_reg.lookup_representation(state_instance.module.context, repr_ref)
    if state_repr is None:
        msg = state_instance.append_error_line(f"No representation registered for {repr_typespec.value_key()}")
        raise ValueError(msg)
    schema_typespec = repr_typespec.arguments["schema"]
    assert isinstance(schema_typespec, typesys.TypeVal)
    layout = tachyon_layout_reg.layout_for_type(state_instance.module.context, schema_typespec)
    if layout is None:
        msg = state_instance.append_error_line(f"No Tachyon layout for {repr_typespec.value_key()}")
        raise ValueError(msg)
    return pdf.StateInstanceDescription(
        representation_id=lookup_uuid(state_instance.module.context, repr_typespec),
        state_instance_id=lookup_uuid(state_instance.module.context, state_instance),
        instance_path_name=state_instance.value_key(),
        maybe_buffer_layout=pdf.PinionBufferLayout(num_slots=1, message_size=layout.size, is_published_once=False),
        maybe_memory_resource=None,
        init_data_source=pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL,
    )


def _gen_state_instance_extern(
    state_instance: box.StateInstance, repr_typespec: extern_type.ExternType
) -> pdfproto.StateInstanceDescription:
    assert state_instance.memory_resource is not None
    return pdf.StateInstanceDescription(
        representation_id=lookup_uuid(state_instance.module.context, repr_typespec),
        state_instance_id=lookup_uuid(state_instance.module.context, state_instance),
        instance_path_name=state_instance.value_key(),
        maybe_buffer_layout=None,
        maybe_memory_resource=lookup_uuid(state_instance.module.context, state_instance.memory_resource),
        init_data_source=pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL,
    )


def gen_init_list(init_cog_deps: Mapping[UUID, set[UUID]], cog_names: Mapping[UUID, str]) -> list[UUID]:
    """Generate a topologically-sorted list of init cogs.

    This sorts the init cogs according to their dependencies, so that each init
    cog runs after all init cogs that init any state they depend on.

    Args:
        init_cog_deps: Adjacency list of init cogs IDs with IDs of cogs they depend on.
        cog_names: Human-readable names of Cogs (for error message purposes)

    Returns: The sorted list of cogs, in execution order.

    Raises:
        ValueError: If there is a cycle in the dependency graph.
    """
    path: set[UUID] = set()
    visited = set()
    result = []

    def _dfs(node: UUID) -> None:
        if node in visited:
            return
        if node in path:
            msg = f"Init Cog dependency cycle: {[cog_names[x] for x in path]}"
            raise ValueError(msg)
        path.add(node)
        visited.add(node)
        for dep in init_cog_deps[node]:
            _dfs(dep)
        path.remove(node)
        result.append(node)

    for cog_id in init_cog_deps:
        _dfs(cog_id)

    return result
