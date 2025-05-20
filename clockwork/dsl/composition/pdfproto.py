# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to Process Description Files (PDF)."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from uuid import UUID


class MemoryResourceType(Protocol):
    """Fake enum type for values of MemoryResourceType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_memory_resource_type: None


class MemoryResourceTypeEnum(Protocol):
    """Type of memory resource."""

    new_delete: MemoryResourceType


@dataclass(kw_only=True)
class ProcessDescription(Tachyon["ProcessDescription"]):
    """Describes the clockwork graph for a particular process."""

    process_id: UUID
    cog_instances: list[CogInstanceDescription]
    state_graph: StateGraph
    config_graph: ConfigGraph
    pubsub_graph: PubSubGraph
    memory_resource_graph: MemoryResourceGraph
    timers: list[TimerInstanceDescription]
    init_cogs: list[UUID]
    log_cog: UUID
    io_connections: list[IoConnectionInstanceDescription]


@dataclass(kw_only=True)
class PubSubGraph(Tachyon["PubSubGraph"]):
    """Description of publishers, subscribers, and the connections between them."""

    publish_endpoints: list[PublishEndpoint]
    connections: list[PubSubConnection]


@dataclass(kw_only=True)
class CogInstanceDescription(Tachyon["CogInstanceDescription"]):
    """Protocol for dynamic CogInstanceDescription dataclass."""

    cog_class_id: UUID
    cog_instance_id: UUID
    endpoints: list[EndpointInstanceDescription]
    instance_path_name: str


@dataclass(kw_only=True)
class EndpointInstanceDescription(Tachyon["EndpointInstanceDescription"]):
    """Typing protocol for dynamic EndpointInstanceDescription dataclass."""

    endpoint_class_id: UUID
    endpoint_instance_id: UUID


@dataclass(kw_only=True)
class PublishEndpoint(Tachyon["PublishEndpoint"]):
    """Additional information about a publish endpoint.

    We need information about both local and remote publishers.  For local
    publishers we create the buffer; for remote ones we need to connect to it
    and validate that it has the expected layout.
    """

    process_id: UUID
    publisher_id: UUID
    buffer_layout: PinionBufferLayout
    num_subscribers: int


@dataclass(kw_only=True)
class PinionBufferLayout(Tachyon["PinionBufferLayout"]):
    """Describes layout of a Pinion buffer."""

    num_slots: int
    message_size: int


@dataclass(kw_only=True)
class PubSubConnection(Tachyon["PubSubConnection"]):
    """Describes a link between a publisher/subscriber pair."""

    subscriber_process_id: UUID
    subscriber_id: UUID
    publisher_id: UUID


@dataclass(kw_only=True)
class StateGraph(Tachyon["StateGraph"]):
    """Describes state instances and how they connect to Cog instances."""

    state_instances: list[StateInstanceDescription]
    connections: list[StateConnection]


@dataclass(kw_only=True)
class StateInstanceDescription(Tachyon["StateInstanceDescription"]):
    """Describes a specific instance of persistent state."""

    representation_id: UUID
    state_instance_id: UUID
    instance_path_name: str
    maybe_buffer_layout: PinionBufferLayout | None
    maybe_memory_resource: UUID | None


@dataclass(kw_only=True)
class StateConnection(Tachyon["StateConnection"]):
    """A connection between a state instance and a state endpoint on a Cog."""

    state_id: UUID
    endpoint_id: UUID


@dataclass(kw_only=True)
class ConfigGraph(Tachyon["ConfigGraph"]):
    """Describes Config instances and their connections to Cog instances."""

    config_instances: list[ConfigInstanceDescription]
    connections: list[ConfigConnection]


@dataclass(kw_only=True)
class ConfigInstanceDescription(Tachyon["ConfigInstanceDescription"]):
    """Describes a Config instances."""

    representation_id: UUID
    config_instance_id: UUID
    instance_path_name: str
    config_file_path: str


@dataclass(kw_only=True)
class ConfigConnection(Tachyon["ConfigConnection"]):
    """Connection between a Config instance and an endpoint on a Cog instance."""

    config_id: UUID
    endpoint_id: UUID


@dataclass(kw_only=True)
class MemoryResource(Tachyon["MemoryResource"]):
    """Represents a distinct memory resource to instantiate."""

    memory_resource_id: UUID
    instance_path_name: str
    resource_type: MemoryResourceType
    resource_max_size: int


@dataclass(kw_only=True)
class MemoryResourceConnection(Tachyon["MemoryResourceConnection"]):
    """Connection between a MemoryResource instance and an endpoint on a Cog instance."""

    memory_resource_id: UUID
    endpoint_id: UUID


@dataclass(kw_only=True)
class MemoryResourceGraph(Tachyon["MemoryResourceGraph"]):
    """Describes MemoryResource instances and their connections to Cog instances."""

    memory_resources: list[MemoryResource]
    connections: list[MemoryResourceConnection]


@dataclass(kw_only=True)
class TimerInstanceDescription(Tachyon["TimerInstanceDescription"]):
    """An instance of a Cog timer."""

    timer_id: UUID
    instance_path_name: str


@dataclass(kw_only=True)
class IoConnectionInstanceDescription(Tachyon["IoConnectionInstanceDescription"]):
    """An instance of an IO Connection."""

    class_id: UUID
    instance_id: UUID
    endpoints: list[EndpointInstanceDescription]
    diags_endpoint_id: UUID | None
    instance_path_name: str
