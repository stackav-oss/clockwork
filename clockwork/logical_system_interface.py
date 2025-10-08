# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Public-facing interface for processing and modification of LogicalSystems."""

from __future__ import annotations

import contextlib
import logging
from copy import copy
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Any, Final, final

from clockwork.dsl.composition import logger_config
from clockwork.dsl.composition.graphir import Channel as GraphirChannel
from clockwork.dsl.composition.graphir import MetricsChannel as GraphirMetricsChannel
from clockwork.dsl.composition.system import (
    LOG_PRODUCER_TYPE,
    Channel,
    Connectable,
    Endpoint,
    LogicalSystem,
    LogProducer,
    MetricsChannel,
    ObserverType,
    ProducerType,
    make_system,
)
from clockwork.dsl.composition.systemgen import gen_system_from_logical_system
from clockwork.dsl.ir import primitive, system_target
from clockwork.dsl.ir.box import (
    HOST_CPU_DOMAIN_POLICY,
    HOST_PROCESS_POLICY,
    MemoryResourceInstance,
    ProcessInstance,
    SerializedDataFileInstance,
    StateInstance,
)
from clockwork.dsl.ir.clkenum import ResolvedEnum, ResolvedValueDef, ValueRef
from clockwork.dsl.ir.cog import CogInstance, CogInstanceMember, InputDef, OutputDef
from clockwork.dsl.ir.compiler import compile_source_file, create_filesystem_importer
from clockwork.dsl.ir.hardware import CpuDomain
from clockwork.dsl.ir.policy import (
    _POLICY_KEY,  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    POLICY_DATA_TYPE,
    PolicyClass,
    PolicyData,
    PolicyInstance,
    UnboundPolicyData,
    bind_policy_data,
    lookup_all_policies,
    lookup_policy,
)
from clockwork.dsl.ir.schema import SchemaInstance
from clockwork.dsl.ir.typesys import TypeVal, Value
from clockwork.dsl.ir.udp import UdpSocketEndpointInstance
from clockwork.dsl.ir.uuid_reg import lookup_uuid, register_entity_with_stable_key

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path
    from uuid import UUID

    from clockwork.dsl.ir.module_id import ModuleID
    from clockwork.dsl.ir.node import Importer

_logger: Final = logging.getLogger(__name__)


@dataclass(slots=True)
class GeneratedSystemFiles:
    """Return structure for gen_system."""

    process_description_files: list[Path]
    telemetry_logger_config_files: list[Path]
    event_logger_config_files: list[Path]
    bridge_config_files: list[Path]
    simplelaunch_config_files: list[Path]
    multi_subscriber_config_files: list[Path]
    channel_publisher_config_files: list[Path]
    channel_allocation_report_files: list[Path]
    channel_spy_config_files: list[Path]
    diagnostics_database_config_files: list[Path]
    logged_channel_metadata_files: list[Path]
    metrics_channel_metadata_files: list[Path]


class LogicalSystemInterface:
    """Public interface to Clockwork LogicalSystem data."""

    def __init__(self, module_id: ModuleID, module_importer: Importer | None = None) -> None:
        """Constructor for the interface to a LogicalSystem.

        Parameters:
            module_id: Module containing a system target to interface with.
            module_importer: Optional importer instance to use when compiling Clockwork modules.
                         Use the same importer if you are compiling and combining multiple logical system interfaces in the same process.
        """
        # Maybe turn down the logger because the clk compiler debug is too verbose
        previous_level = logging.getLogger().getEffectiveLevel()
        logging.getLogger().setLevel("DEBUG")
        debug_level = logging.getLogger().getEffectiveLevel()
        logging.getLogger().setLevel("INFO" if previous_level == debug_level else previous_level)
        self._logical_system = self._logical_system_from_system_target_config(module_id, module_importer)
        logging.getLogger().setLevel(previous_level)

    def import_context_from(self, other: LogicalSystemInterface) -> None:
        """Import the context from another LogicalSystemInterface.

        This imports various registries from CompilerContext from one logical
        system to another. This is to support the use case of combining pieces
        of one logical system (loaded from one set of clk files) with another
        logical system (loaded from another set of clk files).
        """
        self._logical_system.module.context.import_from(other._logical_system.module.context)

    def write_execution_configs(self, root_dir: Path) -> GeneratedSystemFiles:
        """Generate execution config files for a LogicalSystem."""
        include_dir = root_dir
        write_files = True
        write_json_files = False
        system_fqn = "path.prefix.System"
        system_key = system_fqn
        generated_system_files, _output_targets_by_domain, _physical_system = gen_system_from_logical_system(
            include_dir=include_dir,
            root_dir=root_dir,
            logical_system=self._logical_system,
            write_files=write_files,
            write_json_files=write_json_files,
            system_fqn=system_fqn,
            system_key=system_key,
        )
        return GeneratedSystemFiles(
            process_description_files=generated_system_files.process_description_files,
            telemetry_logger_config_files=generated_system_files.telemetry_logger_config_files,
            event_logger_config_files=generated_system_files.event_logger_config_files,
            bridge_config_files=generated_system_files.bridge_config_files,
            simplelaunch_config_files=generated_system_files.simplelaunch_config_files,
            multi_subscriber_config_files=generated_system_files.multi_subscriber_config_files,
            channel_publisher_config_files=generated_system_files.channel_publisher_config_files,
            channel_allocation_report_files=generated_system_files.channel_allocation_report_files,
            channel_spy_config_files=generated_system_files.channel_spy_config_files,
            diagnostics_database_config_files=generated_system_files.diagnostics_database_config_files,
            logged_channel_metadata_files=generated_system_files.logged_channel_metadata_files,
            metrics_channel_metadata_files=generated_system_files.metrics_channel_metadata_files,
        )

    def get_channels(self) -> list[ChannelInterface]:
        """Get the channels in the system."""
        return [
            ChannelInterface(self._logical_system, channel_name)
            for channel_name in (self._logical_system.channels | self._logical_system.metrics_channels)
        ]

    def get_channel(self, channel_name: str) -> ChannelInterface | None:
        """Get a channel from the system."""
        channel = (self._logical_system.channels | self._logical_system.metrics_channels).get(channel_name)
        if not channel:
            return None
        return ChannelInterface(self._logical_system, channel_name)

    def add_channel(self, channel: ChannelInterface, alternative_channel_name: str | None = None) -> ChannelInterface:
        """Add a channel to the system."""
        channel_name = channel.get_name()
        output_channel = channel._channel.channel  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        if alternative_channel_name:
            channel_name = alternative_channel_name
            if channel_name in (self._logical_system.channels | self._logical_system.metrics_channels):
                return ChannelInterface(self._logical_system, channel_name)
            output_channel = _get_channel_with_alternate_name(channel._channel, alternative_channel_name)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        self._logical_system.ensure_channel(output_channel)
        if channel.is_metrics_channel():
            new_channel = self._logical_system.metrics_channels[channel_name]
        else:
            new_channel = self._logical_system.channels[channel_name]

        new_channel.producers.clear()
        new_channel.observers.clear()
        return ChannelInterface(self._logical_system, channel_name)

    def remove_channel(self, channel: ChannelInterface) -> None:
        """Remove a channel from the system."""
        channel.remove_self_from_system()

    def get_io_connections(self) -> list[IoConnectionInterface]:
        """Get the io connections in the system."""
        io_connections = []
        io_connections.extend(
            IoConnectionInterface(self._logical_system, uuid) for uuid in self._logical_system.udp_sockets
        )
        io_connections.extend(
            IoConnectionInterface(self._logical_system, uuid) for uuid in self._logical_system.audio_sources
        )
        return io_connections

    def get_cogs(self) -> list[CogInterface]:
        """Get the cogs in the system."""
        return [CogInterface(self._logical_system, cog_uuid) for cog_uuid in self._logical_system.cogs]

    def add_cog(self, cog: CogInterface) -> CogInterface:
        """Add a cog to the system. Return the new cog."""
        return cog._add_self_to_new_system(self._logical_system)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def remove_cog(self, cog: CogInterface) -> None:
        """Remove a cog from the system."""
        cog.remove_self_from_system()

    def clear_policies(self) -> None:
        """Remove all policies from the system."""
        self._logical_system.module.context[_POLICY_KEY].registry.clear()

    def clear_udp_sockets(self) -> None:
        """Clear all UDP sockets and their endpoints from the system."""
        for channel in self.get_channels():
            for producer in channel.get_producers():
                if producer.is_udp_socket():
                    producer.delete()
            for observer in channel.get_observers():
                if observer.is_udp_socket():
                    observer.delete()
        self._logical_system.udp_sockets.clear()

    def get_processes(self) -> list[ProcessInterface]:
        """Get all processes in the system."""
        return [ProcessInterface(self._logical_system, process_uuid) for process_uuid in self._logical_system.processes]

    def add_process(self, process: ProcessInterface, cpu_domain: CpuDomainInterface) -> ProcessInterface:
        """Add a process to the system."""
        process_uuid = self._logical_system.add_process(process._process)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        self._logical_system.process_to_domain = {process_uuid: cpu_domain._cpu_domain_uuid}  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        return ProcessInterface(self._logical_system, process_uuid)

    def remove_process(self, process: ProcessInterface) -> None:
        """Remove a process from the system."""
        process.remove_self_from_system()

    def assign_all_entities_to_process(self, process: ProcessInterface) -> None:
        """Assign all entities to the same process."""
        for entity_uuid in self._logical_system.entity_to_process:
            self._logical_system.entity_to_process[entity_uuid] = process._process_uuid  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def assign_endpoint_process(self, endpoint: EndpointInterface, process: ProcessInterface) -> None:
        """Assign an endpoint to a process."""
        endpoint._endpoint.process = process._process_uuid  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def get_cpu_domains(self) -> list[CpuDomainInterface]:
        """Get all CPU domains in the system."""
        return [
            CpuDomainInterface(self, self._logical_system, cpu_domain_uuid)
            for cpu_domain_uuid in self._logical_system.cpu_domains
        ]

    def get_cpu_domain_for_entity(self, entity_uuid: UUID) -> CpuDomainInterface:
        """Get the cpu domain where an entity resides."""
        process_uuid = self._logical_system.entity_to_process[entity_uuid]
        cpu_domain_uuid = self._logical_system.process_to_domain[process_uuid]
        return CpuDomainInterface(self, self._logical_system, cpu_domain_uuid)

    def add_cpu_domain(self, cpu_domain: CpuDomainInterface) -> CpuDomainInterface:
        """Add a CPU Domain to the system."""
        new_domain_uuid = self._logical_system.add_cpu_domain(cpu_domain._cpu_domain)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        return CpuDomainInterface(self, self._logical_system, new_domain_uuid)

    def remove_cpu_domain(self, cpu_domain: CpuDomainInterface) -> None:
        """Remove a CPU domain from the system."""
        cpu_domain.remove_self_from_system()

    def bind_cpu_domain_policy_to_process(self, cpu_domain: CpuDomainInterface, process: ProcessInterface) -> None:
        """Bind a CPU domain to a process."""
        cpu_domain_policy = cpu_domain._get_policy()  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        if isinstance(cpu_domain_policy.source, PolicyInstance):
            error_str = "CPU Domain policy source is of incorrect type for binding a new policy."
            raise TypeError(error_str)
        internal_process = process._process  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        current_policy = lookup_policy(self._logical_system.module, HOST_CPU_DOMAIN_POLICY, internal_process)
        if current_policy and current_policy.data == cpu_domain_policy.data:
            return
        bind_policy_data(
            self._logical_system.module,
            UnboundPolicyData(
                policy_class=HOST_CPU_DOMAIN_POLICY,
                data=cpu_domain_policy.data,
                source=cpu_domain_policy.source,
                type_info=POLICY_DATA_TYPE,
            ),
            internal_process,
        )

    def add_log_producer(
        self,
        channel: ChannelInterface,
        alternative_output_channel_name: str | None = None,
        alternative_input_channel_name: str | None = None,
    ) -> None:
        """Add a log producer to read onto a channel."""
        channel.add_log_producer(
            alternative_output_channel_name=alternative_output_channel_name,
            alternative_input_channel_name=alternative_input_channel_name,
        )

    def _get_policy_field_value(self, policy: PolicyClass, field_name: str) -> ResolvedValueDef:
        """Helper to get a field value from a policy class."""
        field_value = None
        for field_def in policy.schema.fields.values():
            if not isinstance(field_def.type_info, ResolvedEnum):
                error_str = "Unexpected type info type for policy class"
                raise TypeError(error_str)
            for value in field_def.type_info.values.values():
                if value.name == field_name:
                    field_value = value
        if field_value is None:
            error_str = f"Couldn't find {field_name} value in policy enum"
            raise RuntimeError(error_str)
        return field_value

    def add_telemetry_log_observers(self, channels: list[ChannelInterface]) -> None:
        """Set a channel to be written to the telemetry log."""
        channel_logging_policy_class = logger_config.get_channel_logging_policy()
        telemetry_value = self._get_policy_field_value(channel_logging_policy_class, "telemetry")
        if telemetry_value.source is None:
            error_str = "Can't find telemetry value source value in order to assign to a logging policy."
            raise RuntimeError(error_str)
        persistent_value = self._get_policy_field_value(channel_logging_policy_class, "persistent")
        if persistent_value.source is None:
            error_str = "Can't find persistent value source value in order to assign to a logging policy."
            raise RuntimeError(error_str)

        for channel in channels:
            if isinstance(channel._channel, MetricsChannel):  # pyright: ignore[reportPrivateUsage]  TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
                continue
            type_infos = list(channel_logging_policy_class.target_bound)
            if (
                isinstance(channel._channel.channel.ir_node.type_info, TypeVal)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
                and channel._channel.channel.ir_node.type_info not in type_infos  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
            ):
                type_infos.append(channel._channel.channel.ir_node.type_info)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
            channel_logging_policy_class.target_bound = list(type_infos)
            schema_args = [("log_type", ValueRef.make(telemetry_value.source))]
            if channel.is_persistent():
                _logger.debug("Marking channel %s as persistent", channel.get_name())
                schema_args.append(("channel_type", ValueRef.make(persistent_value.source)))
            bind_policy_data(
                self._logical_system.module,
                UnboundPolicyData(
                    policy_class=channel_logging_policy_class,
                    data=SchemaInstance.from_args(
                        schema_ir=channel_logging_policy_class.schema,
                        args=schema_args,
                        error_report_node=None,
                        error_report_module=self._logical_system.module,
                    ),
                    source=None,
                    type_info=POLICY_DATA_TYPE,
                ),
                channel._channel.channel.ir_node,  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
            )

    def is_endpoint_connected_to_cog(self, endpoint: EndpointInterface, cog: CogInterface) -> bool:
        """Determine whether an endpoint is connected to a cog."""
        return (
            isinstance(endpoint._endpoint.entity, CogInstanceMember)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
            and endpoint._endpoint.entity.cog_instance is cog._cog  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # noqa: SLF001
        )

    def connect_channel_observer(self, channel: ChannelInterface, observer: EndpointInterface) -> None:
        """Connect a channel to a channel observer."""
        self._logical_system.connect_channel_observer(channel._channel.channel, observer._endpoint.entity)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def connect_channel_producer(self, channel: ChannelInterface, producer: EndpointInterface) -> None:
        """Connect a channel to a channel producer."""
        if channel.is_metrics_channel():
            self._logical_system.connect_metrics_channel_producer(
                channel._channel.channel, producer._endpoint.entity)  # pyright: ignore[reportArgumentType, reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        else:
            self._logical_system.connect_channel_producer(channel._channel.channel, producer._endpoint.entity)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def _get_cpu_domain_policy(self, cpu_domain: CpuDomainInterface) -> PolicyData:
        """Get the CPU domain policy data associated with this CPU domain."""
        for policy in lookup_all_policies(self._logical_system.module, HOST_CPU_DOMAIN_POLICY):
            if (
                policy.data.data.get("cpu_domain")
                and isinstance(policy.data.data["cpu_domain"], CpuDomain)
                and policy.data.data["cpu_domain"].name == cpu_domain.get_name()
            ):
                return policy
        error_str = f"Couldn't find CPU domain policy data for a CPU domain named {cpu_domain.get_name()}"
        raise RuntimeError(error_str)

    def _logical_system_from_system_target_config(
        self, module_id: ModuleID, module_importer: Importer | None = None
    ) -> LogicalSystem:
        """Generate a LogicalSystem representing the system target defined in the given clockwork config file."""
        if module_importer is None:
            module_importer = create_filesystem_importer()
        module = compile_source_file(
            module_id,
            importer=module_importer,
        )
        logical_system: LogicalSystem | None = None
        for obj in module.inner_scope.names.values():
            if isinstance(obj, system_target.UnresolvedSystemTarget):
                if logical_system is not None:
                    error_str = (
                        f"Clockwork configuration '{module_id.get_base_path()}' specifies multiple system targets."
                        " Expected 1."
                    )
                    raise RuntimeError(error_str)
                system_target_ir = obj.get_resolved()
                logical_system = make_system(
                    [system_target_ir.box_instance], system_target_ir.module, system_target_ir.require_logging_policies
                )
        if logical_system is None:
            error_str = f"Clockwork configuration '{module_id.get_base_path()}' did not specify any system targets."
            raise RuntimeError(error_str)
        return logical_system


class EndpointInterface:
    """Public interface to clockwork logical system endpoint."""

    def __init__(self, logical_system: LogicalSystem, uuid: UUID, endpoint: Endpoint[Any, Any]) -> None:
        """Constructor."""
        self._logical_system: LogicalSystem = logical_system
        self._uuid: UUID = uuid
        self._endpoint: Endpoint[Any, Any] = endpoint

    def is_udp_socket(self) -> bool:
        """Determine whether this endpoint is a udp socket."""
        return isinstance(self._endpoint.entity, UdpSocketEndpointInstance)

    def is_log_producer(self) -> bool:
        """Determine whether this endpoint is a log producer."""
        return isinstance(self._endpoint.entity, LogProducer)

    def get_log_producer_source_name(self) -> str:
        """Get the source name of a log producer."""
        if not self.is_log_producer():
            error_str = "Trying to get source name of a non-log producer endpoint."
            raise TypeError(error_str)
        return str(self._endpoint.entity.source_name)

    def get_connected_channel(self) -> ChannelInterface | None:
        """Get the channel connected to this endpoint, if any."""
        if isinstance(self._endpoint.connected_to, Channel):
            return ChannelInterface(self._logical_system, self._endpoint.connected_to.channel.channel_name)
        return None

    def update_observed_channel(self, old_channel: ChannelInterface, new_channel: ChannelInterface) -> None:
        """Update an endpoint connection to a different channel."""
        if not isinstance(self._endpoint.connected_to, Channel):
            error_str = "Trying to update connected channel for a non-channel endpoint"
            raise TypeError(error_str)
        old_channel._channel.observers.pop(self._uuid)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        self._endpoint.connected_to = None
        self._logical_system.connect_channel_observer(new_channel._channel.channel, self._endpoint.entity)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def update_produced_channel(self, old_channel: ChannelInterface, new_channel: ChannelInterface) -> None:
        """Update an endpoint connection to a different channel."""
        old_channel._channel.producers.pop(self._uuid)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
        self._endpoint.connected_to = None
        self._logical_system.connect_channel_producer(new_channel._channel.channel, self._endpoint.entity)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

    def delete(self) -> None:
        """Remove this endpoint from the system."""
        if isinstance(self._endpoint.entity, CogInstanceMember):
            error_str = "Cog member endpoints cannot be deleted from the system. Consider deleting the cog instead."
            raise TypeError(error_str)
        if isinstance(self._endpoint.entity, UdpSocketEndpointInstance):
            _remove_entity(self._logical_system, self._uuid)
            _remove_endpoint(self._logical_system, self._uuid)
        elif isinstance(self._endpoint.entity, LogProducer):
            _remove_entity(self._logical_system, self._uuid)
            _remove_endpoint(self._logical_system, self._uuid)
            self._logical_system.log_producers.pop(self._uuid)
        else:
            error_str = "Unhandled endpoint type while trying to delete an endpoint."
            raise TypeError(error_str)


@dataclass(kw_only=True)
class ChannelLoggingPolicy:
    """Public interface for clockwork logged channel config."""

    # The ChannelType and LogType enums are dynamically defined, so
    # they are variables not type.  Just storing names for now.
    log_type: str  # none, telemetry, event
    channel_type: str  # regular, persistent


class ChannelInterface:
    """Public interface to clockwork logical system channel."""

    def __init__(self, logical_system: LogicalSystem, channel_name: str) -> None:
        """Constructor."""
        self._logical_system: LogicalSystem = logical_system
        self._channel_name: str = channel_name
        self._channel: Channel | MetricsChannel = (
            self._logical_system.channels | self._logical_system.metrics_channels
        )[self._channel_name]
        self._logging_policy: ChannelLoggingPolicy | None = self._lookup_logging_policy()

    def is_metrics_channel(self) -> bool:
        """Check if the channel is a metrics channel."""
        return isinstance(self._channel, MetricsChannel)

    def is_multi_producer(self) -> bool:
        """Check if the channel is a multi producer."""
        return self._channel.is_multi_producer()

    def get_producers(self) -> list[EndpointInterface]:
        """Get the inputs into the channel."""
        return [
            EndpointInterface(self._logical_system, uuid, endpoint)
            for uuid, endpoint in self._channel.producers.items()
        ]

    def get_observers(self) -> list[EndpointInterface]:
        """Get the outputs out of the channel."""
        return [
            EndpointInterface(self._logical_system, uuid, endpoint)
            for uuid, endpoint in self._channel.observers.items()
        ]

    def get_name(self) -> str:
        """Get the channel name."""
        return self._channel_name

    def has_log_writer_policy(self) -> bool:
        """Determine whether a channel is configured to be written to an output log."""
        if isinstance(self._channel, MetricsChannel):
            return True
        channel_logging_policy_class = logger_config.get_channel_logging_policy()
        return (
            lookup_policy(self._logical_system.module, channel_logging_policy_class, self._channel.channel.ir_node)
            is not None
        )

    def get_optimal_queue_size(self) -> int:
        """Get the smallest queue size for the channel that still meets the requirements for all observers."""
        optimal_queue_size = 1
        for endpoint in self.get_observers():
            if isinstance(endpoint._endpoint.entity, CogInstanceMember):  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
                view_max_messages = endpoint._endpoint.entity.member.view_params.max_msgs + 1  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001
                if isinstance(view_max_messages, int):
                    optimal_queue_size = max(optimal_queue_size, view_max_messages)
        return optimal_queue_size

    def _lookup_logging_policy(self) -> ChannelLoggingPolicy | None:
        """Lookup the channel log writer policy, or None if one is not set."""
        if isinstance(self._channel, MetricsChannel):
            return ChannelLoggingPolicy(log_type=self._channel.channel.log_type.name, channel_type="regular")
        channel_logging_policy_class = logger_config.get_channel_logging_policy()
        policy = lookup_policy(self._logical_system.module, channel_logging_policy_class, self._channel.channel.ir_node)
        if policy is None:
            return None

        schema_data = policy.data.data
        log_type = schema_data.get("log_type")
        channel_type = schema_data.get("channel_type")
        if log_type is None or channel_type is None:
            return None

        # Return the logging policy
        # Linter doesn't know how to get the attributes of these dynamic enums
        return ChannelLoggingPolicy(
            log_type=log_type.name,  # pyright: ignore[reportAttributeAccessIssue] Dynamic Enum
            channel_type=channel_type.name,  # pyright: ignore[reportAttributeAccessIssue] Dynamic Enum
        )

    def get_log_writer_policy(self) -> None | ChannelLoggingPolicy:
        """Get the channel logging policy."""
        return self._logging_policy

    def is_persistent(self) -> bool:
        """Get whether the logging policy is persistent."""
        if self._logging_policy is None:
            return False
        return self._logging_policy.channel_type == logger_config.ChannelType.persistent.name  # pyright: ignore[reportAttributeAccessIssue] Dynamic Type

    def remove_self_from_system(self) -> None:
        """Remove this channel from its system."""
        if self.is_metrics_channel():
            removed_channel = self._logical_system.metrics_channels.pop(self._channel_name)
        else:
            removed_channel = self._logical_system.channels.pop(self._channel_name)
        for endpoint_dict in _get_endpoint_dicts(self._logical_system):
            for endpoint_uuid in list(endpoint_dict.keys()):
                endpoint = endpoint_dict[endpoint_uuid]
                if (
                    isinstance(endpoint.connected_to, Channel | MetricsChannel)
                    and removed_channel.channel.channel_name == endpoint.connected_to.channel.channel_name
                ):
                    _remove_endpoint(self._logical_system, endpoint_uuid)

    def add_log_producer(
        self, alternative_output_channel_name: str | None = None, alternative_input_channel_name: str | None = None
    ) -> None:
        """Add a log producer to the system that produces this channel."""
        if isinstance(self._channel, MetricsChannel):
            error_str = (
                "Trying to add a log producer for a metrics channel. Metrics channels are not supported for log "
                "producers."
            )
            raise TypeError(error_str)
        channel_name = self.get_name()
        # cant read schema from the log if it doesn't have a uuid
        if (
            not self._channel.channel.ir_node.message_repr
            or not self._channel.channel.ir_node.message_repr
            or not self._channel.channel.ir_node.message_repr.schema_ir.schema.uuid
        ):
            error_str = (
                f"Trying to add a log producer for channel {channel_name}, but don't have a UUID for the "
                "channel's message representation."
            )
            raise RuntimeError(error_str)
        output_channel_name = channel_name
        output_channel = self._channel.channel
        if alternative_output_channel_name:
            if alternative_output_channel_name in self._logical_system.channels:
                output_channel = self._logical_system.channels[alternative_output_channel_name].channel
            else:
                output_channel_name = alternative_output_channel_name
                output_channel = _get_channel_with_alternate_name(self._channel, alternative_output_channel_name)
                # A Graphir Channel was the argument in so that type should be the output of _get_channel_with_alternate_name
                assert isinstance(output_channel, GraphirChannel)
            self._logical_system.ensure_channel(output_channel)
        input_channel_name = alternative_input_channel_name if alternative_input_channel_name else channel_name
        log_producer = LogProducer(
            name=f"logreader-({input_channel_name})-to-({output_channel_name})",
            scope=self._logical_system.module.inner_scope,
            type_info=LOG_PRODUCER_TYPE,
            source_name=input_channel_name,
        )
        register_entity_with_stable_key(self._logical_system.module.context, log_producer)
        self._logical_system.add_log_producer(
            log_producer,
            output_channel,
        )

    def get_message_repr_name(self) -> str:
        """Get the name of the message representation."""
        return self._channel.channel.message_repr.schema_ir.schema.fqn

    def get_message_size_bytes(self) -> int:
        """Get the size of messages on this channel."""
        return self._channel.channel.message_size

    def get_queue_size(self) -> int:
        """Get the message queue size for the channel."""
        return self._channel.channel.num_slots

    def set_queue_size(self, queue_size: int) -> None:
        """Set the message queue size for the channel."""
        self._channel.channel = replace(self._channel.channel, num_slots=queue_size)  # pyright: ignore[reportAttributeAccessIssue] # False positive, pyright doesn't understand replace() is a dataclass method


class ConnectableInterface:
    """Public interface to clockwork logical system connectable."""

    def __init__(
        self, logical_system: LogicalSystem, connectable_uuid: UUID, connectable_dict: dict[UUID, Any]
    ) -> None:
        """Constructor."""
        self._logical_system: LogicalSystem = logical_system
        self._connectable_uuid: UUID = connectable_uuid
        self._connectable_dict: dict[UUID, Any] = connectable_dict
        self._connectable: Connectable[Any, CogInstanceMember[Any]] = self._connectable_dict[self._connectable_uuid]

    def get_name(self) -> str:
        """Get the name of the connectable."""
        if not hasattr(self._connectable.entity, "fqn") or not isinstance(self._connectable.entity.fqn, str):
            error_str = "Could not get a name for a connectable"
            raise RuntimeError(error_str)
        return self._connectable.entity.fqn

    def get_endpoints(self) -> list[EndpointInterface]:
        """Get the endpoints on a connectable."""
        return [
            EndpointInterface(self._logical_system, endpoint_uuid, endpoint)
            for endpoint_uuid, endpoint in self._connectable.endpoints.items()
        ]

    def _add_self_to_new_system(self, new_logical_system: LogicalSystem, process_policy_data: PolicyData) -> None:
        """Add this connectable to a new system (and make the appropriate connections)."""
        self._try_set_connectable_process_in_new_system(new_logical_system, process_policy_data)
        self._try_add_connectable_entity_to_new_system(new_logical_system)
        self._make_connections_to_cogs(new_logical_system)

    def _try_set_connectable_process_in_new_system(
        self, new_logical_system: LogicalSystem, process_policy_data: PolicyData
    ) -> None:
        """Set the process for the connectable, if needed."""
        if lookup_policy(new_logical_system.module, HOST_PROCESS_POLICY, self._connectable.entity) is None:
            if isinstance(process_policy_data.source, PolicyInstance):
                error_str = "Policy data source is of incorrect type for binding a new policy."
                raise RuntimeError(error_str)
            bind_policy_data(
                new_logical_system.module,
                UnboundPolicyData(
                    policy_class=HOST_PROCESS_POLICY,
                    data=process_policy_data.data,
                    source=process_policy_data.source,
                    type_info=POLICY_DATA_TYPE,
                ),
                self._connectable.entity,
            )

    def _try_add_connectable_entity_to_new_system(self, new_logical_system: LogicalSystem) -> None:
        """Add this connectables entity to the new system, if needed."""
        if isinstance(self._connectable.entity, SerializedDataFileInstance):
            with contextlib.suppress(KeyError):
                new_logical_system.add_config(self._connectable.entity)
        elif isinstance(self._connectable.entity, StateInstance):
            with contextlib.suppress(KeyError):
                new_logical_system.add_state(self._connectable.entity)
        elif isinstance(self._connectable.entity, MemoryResourceInstance):
            with contextlib.suppress(KeyError):
                new_logical_system.add_memory_resource(self._connectable.entity)

    def _make_connections_to_cogs(self, new_logical_system: LogicalSystem) -> None:
        """Make connections to cogs in the new system."""
        for endpoint in self._connectable.endpoints.values():
            for cog in new_logical_system.cogs.values():
                if endpoint.entity.cog_instance is cog:
                    if isinstance(self._connectable.entity, SerializedDataFileInstance):
                        new_logical_system.connect_config(self._connectable.entity, endpoint.entity)
                    elif isinstance(self._connectable.entity, StateInstance):
                        new_logical_system.connect_state(self._connectable.entity, endpoint.entity)
                    elif isinstance(self._connectable.entity, MemoryResourceInstance):
                        new_logical_system.connect_memory_resource(self._connectable.entity, endpoint.entity)
                    else:
                        error_str = f"Unhandled connectable type {type(self._connectable.entity)}"
                        raise TypeError(error_str)


class ProcessInterface:
    """Public interface to clockwork logical system process."""

    def __init__(self, logical_system: LogicalSystem, process_uuid: UUID) -> None:
        """Constructor."""
        self._logical_system: LogicalSystem = logical_system
        self._process_uuid: UUID = process_uuid
        self._process: ProcessInstance = self._logical_system.processes[self._process_uuid]

    def remove_self_from_system(self) -> None:
        """Remove the process from the system. Entities and endpoints may be left with dangling references."""
        _remove_entity(self._logical_system, self._process_uuid)
        self._logical_system.processes.pop(self._process_uuid)
        self._logical_system.process_to_domain.pop(self._process_uuid)


class CpuDomainInterface:
    """Public interface to clockwork logical system CPU domains."""

    def __init__(
        self,
        logical_system_interface: LogicalSystemInterface,
        logical_system: LogicalSystem,
        cpu_domain_uuid: UUID,
    ) -> None:
        """Constructor."""
        self._logical_system_interface: LogicalSystemInterface = logical_system_interface
        self._logical_system: LogicalSystem = logical_system
        self._cpu_domain_uuid: UUID = cpu_domain_uuid
        self._cpu_domain: CpuDomain = self._logical_system.cpu_domains[self._cpu_domain_uuid]

    def remove_self_from_system(self) -> None:
        """Remove the process from the system. Processes may be left with dangling references."""
        _remove_entity(self._logical_system, self._cpu_domain_uuid)
        self._logical_system.cpu_domains.pop(self._cpu_domain_uuid)

    def get_name(self) -> str:
        """Get the name of the CPU Domain."""
        return self._cpu_domain.name

    def _get_policy(self) -> PolicyData:
        """Get the CPU Domain policy."""
        return self._logical_system_interface._get_cpu_domain_policy(self)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001


@final
class IoConnectionInterface:
    """Public interface to a clockwork IoConnection."""

    def __init__(self, logical_system: LogicalSystem, uuid: UUID) -> None:
        """Initialize the class."""
        self._logical_system = logical_system
        self._uuid = uuid

    def get_uuid(self) -> UUID:
        """Get UUID for the io connection instance."""
        return self._uuid

    def get_name(self) -> str:
        """Get name for the io connection instance."""
        if udp_socket := self._logical_system.udp_sockets.get(self._uuid):
            return udp_socket.fqn
        if audio_source := self._logical_system.audio_sources.get(self._uuid):
            return audio_source.fqn
        msg = "Not supported"
        raise NotImplementedError(msg)

    def _entities_to_channel_names(
        self,
        entities: list[Value],
        mapping: dict[UUID, Endpoint[ObserverType, Any]] | dict[UUID, Endpoint[ProducerType, Any]],
    ) -> list[str]:
        channel_names = []
        for entity in entities:
            entity_uuid = lookup_uuid(self._logical_system.module.context, entity)
            endpoint = mapping[entity_uuid]
            if isinstance(endpoint.connected_to, Channel):
                channel_names.append(endpoint.connected_to.channel.channel_name)
        return channel_names

    def get_output_channel_names(self) -> list[str]:
        """Names of any output channels."""
        entities: list[Value] = []
        if udp_socket := self._logical_system.udp_sockets.get(self._uuid):
            if udp_socket.producer_endpoint:
                entities.append(udp_socket.producer_endpoint)
        elif audio_source := self._logical_system.audio_sources.get(self._uuid):
            entities.append(audio_source)
        else:
            msg = "Not supported"
            raise NotImplementedError(msg)
        return self._entities_to_channel_names(entities, self._logical_system.producer_endpoints)

    def get_input_channel_names(self) -> list[str]:
        """Names of any input channels."""
        entities: list[Value] = []
        if udp_socket := self._logical_system.udp_sockets.get(self._uuid):
            if udp_socket.observer_endpoint:
                entities.append(udp_socket.observer_endpoint)
        elif self._logical_system.audio_sources.get(self._uuid):
            # Audio sources only have outputs.
            pass
        else:
            msg = "Not supported"
            raise NotImplementedError(msg)
        return self._entities_to_channel_names(entities, self._logical_system.observer_endpoints)


class CogInterface:
    """Public interface to clockwork logical system cog."""

    def __init__(self, logical_system: LogicalSystem, cog_uuid: UUID) -> None:
        """Constructor."""
        self._logical_system: LogicalSystem = logical_system
        self._cog_uuid: UUID = cog_uuid
        self._cog: CogInstance = self._logical_system.cogs[self._cog_uuid]

    def get_name(self) -> str:
        """Get the cog name."""
        return self._cog.fqn

    def get_uuid(self) -> UUID:
        """Get the cog UUID."""
        return self._cog_uuid

    def is_init(self) -> bool:
        """Whether or not this is an init cog."""
        return self._cog.cog_class.is_init()

    def get_output_channel_names(self) -> list[str]:
        """Names of any output channels."""
        channel_names = []
        for entity in self._cog.members:
            if not isinstance(entity.member, OutputDef):
                continue
            endpoint_uuid = lookup_uuid(self._logical_system.module.context, entity)
            endpoint = self._logical_system.producer_endpoints[endpoint_uuid]
            if not isinstance(endpoint.connected_to, Channel):
                continue
            channel_names.append(endpoint.connected_to.channel.channel_name)
        return channel_names

    def get_endpoint_by_output_name(self, name: str) -> EndpointInterface | None:
        """Get an endpoint by its name in the cog outputs."""
        for entity in self._cog.members:
            if not isinstance(entity.member, OutputDef):
                continue
            if entity.name == name:
                endpoint_uuid = lookup_uuid(self._logical_system.module.context, entity)
                endpoint = self._logical_system.producer_endpoints[endpoint_uuid]
                return EndpointInterface(self._logical_system, endpoint_uuid, endpoint)
        return None

    def get_input_channel_names(self) -> list[str]:
        """Names of any input channels."""
        channel_names = []
        for entity in self._cog.members:
            if not isinstance(entity.member, InputDef):
                continue
            endpoint_uuid = lookup_uuid(self._logical_system.module.context, entity)
            endpoint = self._logical_system.observer_endpoints[endpoint_uuid]
            if not isinstance(endpoint.connected_to, Channel):
                continue
            channel_names.append(endpoint.connected_to.channel.channel_name)
        return channel_names

    def get_endpoint_by_input_name(self, name: str) -> EndpointInterface | None:
        """Get an endpoint by its name in the cog inputs."""
        for entity in self._cog.members:
            if not isinstance(entity.member, InputDef):
                continue
            if entity.name == name:
                endpoint_uuid = lookup_uuid(self._logical_system.module.context, entity)
                endpoint = self._logical_system.observer_endpoints[endpoint_uuid]
                return EndpointInterface(self._logical_system, endpoint_uuid, endpoint)
        return None

    def get_connectables(self) -> list[ConnectableInterface]:
        """Get the connectables that feed into this cog."""
        return [
            *self._get_connectables(self._logical_system.configs),
            *self._get_connectables(self._logical_system.states),
            *self._get_connectables(self._logical_system.mem_resources),
        ]

    def remove_self_from_system(self) -> None:
        """Remove this cog from its system."""
        self._logical_system.all_entities.pop(self._cog_uuid)
        self._logical_system.entity_to_process.pop(self._cog_uuid)
        removed_cog = self._logical_system.cogs.pop(self._cog_uuid)

        for endpoint_dict in _get_endpoint_dicts(self._logical_system):
            for endpoint_uuid in list(endpoint_dict.keys()):
                endpoint = endpoint_dict.get(endpoint_uuid)
                if (
                    endpoint
                    and isinstance(endpoint.entity, CogInstanceMember)
                    and endpoint.entity in removed_cog.members
                ):
                    _remove_endpoint(self._logical_system, endpoint_uuid)

        connectable_infos: list[tuple[dict[UUID, Connectable[Any, Any]], Callable[[LogicalSystem, UUID], None]]] = [
            (self._logical_system.configs, _remove_config),
            (self._logical_system.states, _remove_state),
            (self._logical_system.mem_resources, _remove_mem_resource),
        ]
        for connectable_info in connectable_infos:
            connectable_dict = connectable_info[0]
            removal_fn = connectable_info[1]
            for connectable_uuid in list(connectable_dict.keys()):
                connectable = connectable_dict[connectable_uuid]
                for endpoint_uuid in list(connectable.endpoints.keys()):
                    endpoint = connectable.endpoints[endpoint_uuid]
                    if (
                        endpoint
                        and isinstance(endpoint.entity, CogInstanceMember)
                        and endpoint.entity.cog_instance is removed_cog
                    ):
                        _remove_endpoint(self._logical_system, endpoint_uuid)
                        if not len(connectable.endpoints):
                            removal_fn(self._logical_system, connectable_uuid)

    def _get_connectables(
        self, connectable_dict: dict[UUID, Connectable[Any, CogInstanceMember[Any]]]
    ) -> list[ConnectableInterface]:
        """Get the connectables this cog depends on."""
        connectable_interfaces: list[ConnectableInterface] = []
        for connectable_uuid, connectable in connectable_dict.items():
            connectable_feeds_cog = False
            for endpoint in connectable.endpoints.values():
                if isinstance(endpoint.entity, CogInstanceMember) and endpoint.entity.cog_instance is self._cog:  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                    connectable_feeds_cog = True
                    break
            if connectable_feeds_cog:
                # LogicalSystem doesn't count mem resources for states as being connected to the cogs that use the state
                if isinstance(connectable.entity, StateInstance) and connectable.entity.memory_resource is not None:
                    for (
                        mem_connectable_uuid,
                        mem_resource_connectable,
                    ) in self._logical_system.mem_resources.items():
                        if mem_resource_connectable.entity is connectable.entity.memory_resource:
                            connectable_interfaces.append(
                                ConnectableInterface(
                                    self._logical_system, mem_connectable_uuid, self._logical_system.mem_resources
                                )
                            )
                connectable_interfaces.append(
                    ConnectableInterface(self._logical_system, connectable_uuid, connectable_dict)
                )
        return connectable_interfaces

    def _add_self_to_new_system(self, new_logical_system: LogicalSystem) -> CogInterface:
        """Add this cog to a different logical system from the original one."""
        process_policy_data = lookup_policy(self._logical_system.module, HOST_PROCESS_POLICY, self._cog)
        if process_policy_data is None or isinstance(process_policy_data.source, PolicyInstance):
            error_str = "While adding cog to a new system, couldn't find associated process policy in old system."
            raise RuntimeError(error_str)
        bind_policy_data(
            new_logical_system.module,
            UnboundPolicyData(
                policy_class=HOST_PROCESS_POLICY,
                data=process_policy_data.data,
                source=process_policy_data.source,
                type_info=POLICY_DATA_TYPE,
            ),
            self._cog,
        )
        new_cog_uuid = new_logical_system.add_cog(self._cog)
        for old_system_connectable in self.get_connectables():
            old_system_connectable._add_self_to_new_system(new_logical_system, process_policy_data)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: SLF001

        return CogInterface(new_logical_system, new_cog_uuid)


def _remove_entity(logical_system: LogicalSystem, entity_uuid: UUID) -> None:
    """Remove an entity from a logical system."""
    logical_system.all_entities.pop(entity_uuid, None)
    logical_system.entity_to_process.pop(entity_uuid, None)


def _remove_endpoint(logical_system: LogicalSystem, endpoint_to_remove_uuid: UUID) -> None:
    """Remove an endpoint from a LogicalSystem."""
    _remove_entity(logical_system, endpoint_to_remove_uuid)
    for endpoint_dict in _get_endpoint_dicts(logical_system):
        endpoint_dict.pop(endpoint_to_remove_uuid, None)

    for channel in (logical_system.channels | logical_system.metrics_channels).values():
        channel.producers.pop(endpoint_to_remove_uuid, None)
        channel.observers.pop(endpoint_to_remove_uuid, None)

    connectable_infos: list[tuple[dict[UUID, Connectable[Any, Any]], Callable[[LogicalSystem, UUID], None]]] = [
        (logical_system.configs, _remove_config),
        (logical_system.states, _remove_state),
        (logical_system.mem_resources, _remove_mem_resource),
    ]
    for connectable_info in connectable_infos:
        connectable_dict = connectable_info[0]
        removal_fn = connectable_info[1]
        for connectable_uuid in list(connectable_dict.keys()):
            connectable = connectable_dict[connectable_uuid]
            if connectable.endpoints.pop(endpoint_to_remove_uuid, None) and not len(connectable.endpoints):
                removal_fn(logical_system, connectable_uuid)


def _remove_config(logical_system: LogicalSystem, config_uuid: UUID) -> None:
    """Remove a Config from a LogicalSystem."""
    _remove_entity(logical_system, config_uuid)
    logical_system.configs.pop(config_uuid)


def _remove_state(logical_system: LogicalSystem, state_uuid: UUID) -> None:
    """Remove a State from a LogicalSystem."""
    _remove_entity(logical_system, state_uuid)
    logical_system.states.pop(state_uuid)


def _remove_mem_resource(logical_system: LogicalSystem, mem_resource_uuid: UUID) -> None:
    """Remove a MemResource from a LogicalSystem."""
    _remove_entity(logical_system, mem_resource_uuid)
    logical_system.mem_resources.pop(mem_resource_uuid)


def _get_endpoint_dicts(logical_system: LogicalSystem) -> list[dict[UUID, Endpoint[Any, Any]]]:
    """Get the endpoint dicts in a logical system."""
    return [
        logical_system.state_endpoints,
        logical_system.config_endpoints,
        logical_system.memres_endpoints,
        logical_system.producer_endpoints,
        logical_system.observer_endpoints,
    ]


def _get_channel_with_alternate_name(
    channel: Channel | MetricsChannel, alternative_channel_name: str
) -> GraphirChannel | GraphirMetricsChannel:
    """Make a copy of a channel, except with a different name."""
    if isinstance(channel, MetricsChannel):
        return _get_metrics_channel_with_alternate_name(channel, alternative_channel_name)
    updated_ir_node = copy(channel.channel.ir_node)
    updated_ir_node.name = alternative_channel_name
    updated_ir_node.channel_name = primitive.StringValue.make(alternative_channel_name)
    return GraphirChannel(
        channel.channel.doc,
        alternative_channel_name,
        channel.channel.message_repr,
        channel.channel.message_size,
        channel.channel.num_slots,
        channel.channel.is_multi_publisher,
        channel.channel.is_diagnostics,
        channel.channel.is_bridge_status,
        channel.channel.enforce_backwards_compatibility,
        updated_ir_node,
    )


def _get_metrics_channel_with_alternate_name(
    channel: MetricsChannel, alternative_channel_name: str
) -> GraphirMetricsChannel:
    """Make a copy of a metrics channel, except with a different name."""
    return GraphirMetricsChannel(
        alternative_channel_name,
        channel.channel.message_repr,
        channel.channel.log_type,
        channel.channel.message_size,
        channel.channel.num_slots,
        channel.channel.uuid,
        channel.channel.cog_path,
        channel.channel.cog_instance_path,
    )
