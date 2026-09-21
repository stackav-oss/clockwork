# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to Process Description Files (PDF)."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from clockwork.serialization.py.protocol import Tachyon

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl.composition.channel_config_proto import ChannelType


class MemoryResourceType(Protocol):
    """Fake enum type for values of MemoryResourceType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  So this just needs to be a protocol that
    # nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_memory_resource_type: None


class NotConnectedEndpointType(Protocol):
    """Fake enum type for values of NotConnectedEndpointType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  The type here is just to be able to disambiguate
    # between different enum types. The real enum values get set at runtime.
    # So this just needs to be a protocol that nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_not_connected_endpoint_type: None


class DataSourceType(Protocol):
    """Fake enum type for values of DataSourceType."""

    # The point is that the only way to get an instance of the enum is via the
    # dynamic runtime enum class.  The type here is just to be able to disambiguate
    #  between different enum types. The real enum values get set at runtime.
    #  So this just needs to be a protocol that nothing can ever satisfy.
    _please_never_define_a_class_with_this_attribute_data_source_type: None


class MemoryResourceTypeEnum(Protocol):
    """Type of memory resource."""

    new_delete: MemoryResourceType


class NotConnectedEndpointTypeEnum(Protocol):
    """Type of not-connected endpoint."""

    publisher: NotConnectedEndpointType
    subscriber: NotConnectedEndpointType


class DataSourceTypeEnum(Protocol):
    """Type of data source."""

    file: DataSourceType
    log_first_message: DataSourceType


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ProcessDescription(Tachyon["ProcessDescription"]):
# fmt: on
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
    not_connected_endpoints: list[NotConnectedEndpoint]
    snapshot_configs: list[SnapshotConfig]
    data_sources: list[DataSource]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PubSubGraph(Tachyon["PubSubGraph"]):
# fmt: on
    """Description of publishers, subscribers, and the connections between them."""

    publish_endpoints: list[PublishEndpoint]
    connections: list[PubSubConnection]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class CogInstanceDescription(Tachyon["CogInstanceDescription"]):
# fmt: on
    """Protocol for dynamic CogInstanceDescription dataclass."""

    cog_class_id: UUID
    cog_instance_id: UUID
    endpoints: list[EndpointInstanceDescription]
    instance_path_name: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class EndpointInstanceDescription(Tachyon["EndpointInstanceDescription"]):
# fmt: on
    """Typing protocol for dynamic EndpointInstanceDescription dataclass."""

    endpoint_class_id: UUID
    endpoint_instance_id: UUID


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PublishEndpoint(Tachyon["PublishEndpoint"]):
# fmt: on
    """Additional information about a publish endpoint.

    We need information about both local and remote publishers.  For local
    publishers we create the buffer; for remote ones we need to connect to it
    and validate that it has the expected layout.
    """

    process_id: UUID
    publisher_id: UUID
    buffer_layout: PinionBufferLayout
    num_subscribers: int
    channel_name: str
    is_bulk_data: bool
    channel_type: ChannelType


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PinionBufferLayout(Tachyon["PinionBufferLayout"]):
# fmt: on
    """Describes layout of a Pinion buffer."""

    num_slots: int
    message_size: int
    is_published_once: bool
    max_msgs_per_exec: int = 1


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class PubSubConnection(Tachyon["PubSubConnection"]):
# fmt: on
    """Describes a link between a publisher/subscriber pair."""

    subscriber_process_id: UUID
    subscriber_id: UUID
    publisher_id: UUID
    subscriber_key: str
    publisher_key: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class StateGraph(Tachyon["StateGraph"]):
# fmt: on
    """Describes state instances and how they connect to Cog instances."""

    state_instances: list[StateInstanceDescription]
    connections: list[StateConnection]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class StateInstanceDescription(Tachyon["StateInstanceDescription"]):
# fmt: on
    """Describes a specific instance of persistent state."""

    representation_id: UUID
    state_instance_id: UUID
    instance_path_name: str
    snapshot_representation_id: UUID | None
    maybe_buffer_layout: PinionBufferLayout | None
    maybe_memory_resource: UUID | None
    init_data_source: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class StateConnection(Tachyon["StateConnection"]):
# fmt: on
    """A connection between a state instance and a state endpoint on a Cog."""

    state_id: UUID
    endpoint_id: UUID


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ConfigGraph(Tachyon["ConfigGraph"]):
# fmt: on
    """Describes Config instances and their connections to Cog instances."""

    config_instances: list[ConfigInstanceDescription]
    connections: list[ConfigConnection]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ConfigInstanceDescription(Tachyon["ConfigInstanceDescription"]):
# fmt: on
    """Describes a Config instance."""

    config_instance_id: UUID
    instance_path_name: str
    init_data_source: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class ConfigConnection(Tachyon["ConfigConnection"]):
# fmt: on
    """Connection between a Config instance and an endpoint on a Cog instance."""

    config_id: UUID
    endpoint_id: UUID


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class DataSource(Tachyon["DataSource"]):
# fmt: on
    """Describes a data source for config or state initialization."""

    representation_id: UUID
    data_source_type: DataSourceType
    source_path_or_name: str
    fallback_source: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MemoryResource(Tachyon["MemoryResource"]):
# fmt: on
    """Represents a distinct memory resource to instantiate."""

    memory_resource_id: UUID
    instance_path_name: str
    resource_type: MemoryResourceType
    resource_max_size: int


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MemoryResourceConnection(Tachyon["MemoryResourceConnection"]):
# fmt: on
    """Connection between a MemoryResource instance and an endpoint on a Cog instance."""

    memory_resource_id: UUID
    endpoint_id: UUID


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class MemoryResourceGraph(Tachyon["MemoryResourceGraph"]):
# fmt: on
    """Describes MemoryResource instances and their connections to Cog instances."""

    memory_resources: list[MemoryResource]
    connections: list[MemoryResourceConnection]


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class TimerInstanceDescription(Tachyon["TimerInstanceDescription"]):
# fmt: on
    """An instance of a Cog timer."""

    timer_id: UUID
    instance_path_name: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class IoConnectionInstanceDescription(Tachyon["IoConnectionInstanceDescription"]):
# fmt: on
    """An instance of an IO Connection."""

    class_id: UUID
    instance_id: UUID
    endpoints: list[EndpointInstanceDescription]
    diags_endpoint_id: UUID | None
    instance_path_name: str


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class NotConnectedEndpoint(Tachyon["NotConnectedEndpoint"]):
# fmt: on
    """Describes an endpoint that is not connected to any publisher or subscriber."""

    endpoint_id: UUID
    endpoint_type: NotConnectedEndpointType
    buffer_layout: PinionBufferLayout


# fmt: off
@dataclass(kw_only=True)
# pyrefly: ignore[implicit-abstract-class] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
class SnapshotConfig(Tachyon["SnapshotConfig"]):
# fmt: on
    """Configuration for snapshotting a state or config endpoint.

    For state snapshots (TakeSnapshots policy), at least one of interval_ns or cycles must be set.
    For config snapshots (SnapshotOnce policy), both interval_ns and cycles will be None.
    """

    endpoint_id: UUID
    snapshot_publisher_id: UUID
    interval: int | None
    cycles: int | None
