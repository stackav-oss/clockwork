# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate real-time playback conversion configurations."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, Protocol, cast

from clockwork.dsl.cog.metrics_policy_loader import load_policy_module
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.composition import pdf, publisher_config
from clockwork.dsl.ir import box, primitive, representation
from clockwork.logging.realtime_playback import nb_xxh3_checksum
from clockwork.serialization.py import tachyon_dyn
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping
    from uuid import UUID

    from clockwork.dsl.composition import pdfproto, system
    from clockwork.serialization.py.protocol import Tachyon


_MODULE_PATH: Final = Path("clockwork/logging/realtime_playback/realtime_playback_conversion_config.clk")


class _StreamKind(Protocol):
    """Dynamic stream-kind enum lookup exposed by the generated Tachyon type."""

    def __getitem__(self, name: str) -> object:
        """Return the enum member with the given name."""


class _InitializationRequirement(Protocol):
    """Dynamic initialization requirement fields used during generation."""

    process_uuid: UUID


@dataclass
class Entities:
    """Real-time playback conversion configuration Tachyon types."""

    playback_assignment: Callable[..., object]
    domain_plan: Callable[..., object]
    initialization_requirement: Callable[..., object]
    stream_kind: _StreamKind
    conversion_config: Callable[..., Tachyon[object]]


class Registry(Context):
    """Registry for real-time playback conversion configuration types."""

    def __init__(self, name: str | None, entities: Entities) -> None:
        """Create a new real-time playback conversion configuration registry."""
        self.name = name
        self.entities = entities

    @override
    def import_from(self, other: Registry) -> None:
        """Merge another registry into this one."""
        if self.entities != other.entities:
            msg = (
                f"Real-time playback conversion configuration registries have conflicting entities: "
                f"{self.entities} vs {other.entities}\nWhen merging {other.name} into {self.name}"
            )
            raise RuntimeError(msg)

    def get_entities(self) -> Entities:
        """Get all real-time playback conversion configuration types."""
        return self.entities


def _load_all_entities(compiler_context: CompilerContext) -> Entities:
    """Load real-time playback conversion configuration types from the CLK module."""
    module = load_policy_module(compiler_context, _MODULE_PATH)
    module_context = module.context

    return Entities(
        playback_assignment=tachyon_dyn.get_instantiation_dataclass(
            module_context, module, "PlaybackChannelAssignment"
        )[0],
        domain_plan=tachyon_dyn.get_instantiation_dataclass(module_context, module, "PlaybackCpuDomainPlan")[0],
        initialization_requirement=tachyon_dyn.get_instantiation_dataclass(
            module_context, module, "InitializationRequirement"
        )[0],
        stream_kind=tachyon_dyn.get_enum(module_context, module, "RealtimePlaybackStreamKind")[0],
        conversion_config=tachyon_dyn.get_instantiation_dataclass(
            module_context, module, "RealtimePlaybackConversionConfig"
        )[0],
    )


class RegistryKey(ContextKey[Registry]):
    """CompilerContext key for real-time playback conversion configuration types."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> Registry:
        """Create a default registry by loading the conversion configuration module."""
        return Registry(compiler_context.name, _load_all_entities(compiler_context))


REGISTRY_KEY: Final = RegistryKey("RealtimePlaybackConversionConfigRegistryKey")


def get_entities(compiler_context: CompilerContext) -> Entities:
    """Get all real-time playback conversion configuration types."""
    registry = compiler_context[REGISTRY_KEY]
    return registry.get_entities()


def make_conversion_config(
    logical_system: system.LogicalSystem,
    process_descriptions: Mapping[UUID, pdfproto.ProcessDescription],
) -> Tachyon[object]:
    """Create the immutable conversion input for one playback-enabled system."""
    entities = get_entities(logical_system.module.context)
    assignments_by_domain: dict[UUID, list[object]] = {}
    publisher_channels: dict[str, publisher_config.PublishedChannelConfig] = {}
    for assignment in logical_system.realtime_playback_assignments:
        message_repr = assignment.channel.message_repr
        assert isinstance(message_repr, representation.ResolvedReprInstantiation)
        if message_repr.typespec.instantiates.value_key() != "::Tachyon":
            msg = f"Playback destination is not Tachyon: {assignment.destination_channel_name}"
            raise ValueError(msg)
        _add_publisher_channel(publisher_channels, assignment.destination_channel_name, message_repr, logical_system)
        assignments_by_domain.setdefault(assignment.cpu_domain_uuid, []).append(
            entities.playback_assignment(
                source_channel_name=assignment.source_channel_name,
                destination_channel_name=assignment.destination_channel_name,
                stream_kind=entities.stream_kind[assignment.stream_kind_name],
            )
        )

    process_payloads = {
        process_uuid: _serialize_process_description(process_description)
        for process_uuid, process_description in process_descriptions.items()
    }
    requirements = _initialization_requirements(logical_system, process_descriptions, entities)
    for data_source in logical_system.data_sources.values():
        if isinstance(data_source, box.FirstMessageInstance):
            channel_name = data_source.channel.channel_name
            assert isinstance(channel_name, primitive.StringValue)
            assert data_source.channel.message_repr is not None
            _add_publisher_channel(
                publisher_channels, channel_name.value, data_source.channel.message_repr, logical_system
            )
    for requirement in requirements:
        assignments_by_domain.setdefault(logical_system.process_to_domain[requirement.process_uuid], [])

    plans = []
    for domain_uuid, assignments in sorted(assignments_by_domain.items(), key=lambda item: item[0].bytes):
        domain = logical_system.cpu_domains[domain_uuid]
        if not isinstance(domain.simplelaunch_node, str):
            msg = f"Playback domain has no explicit simplelaunch node: {domain.name}"
            raise TypeError(msg)
        if (
            domain.simplelaunch_node in {"", ".", ".."}
            or "/" in domain.simplelaunch_node
            or "\\" in domain.simplelaunch_node
        ):
            msg = f"Playback domain has an invalid simplelaunch node: {domain.name}"
            raise ValueError(msg)
        plans.append(
            entities.domain_plan(
                cpu_domain_name=domain.name,
                simplelaunch_node_name=domain.simplelaunch_node,
                assignments=sorted(assignments, key=lambda item: item.destination_channel_name),
            )
        )
    return entities.conversion_config(
        platform_id=str(logical_system.module.module_id),
        platform_config_xxh3=_platform_config_xxh3(process_payloads),
        publishers=publisher_config.ChannelPublisherConfig(
            channels=[publisher_channels[name] for name in sorted(publisher_channels)]
        ),
        cpu_domains=plans,
        initialization_requirements=requirements,
    )


def _add_publisher_channel(
    publisher_channels: dict[str, publisher_config.PublishedChannelConfig],
    channel_name: str,
    message_repr: representation.ResolvedReprInstantiation,
    logical_system: system.LogicalSystem,
) -> None:
    """Add one target publisher entry, rejecting conflicting duplicate names."""
    config = publisher_config.make_unbuffered_published_channel_config(
        logical_system.module.context, channel_name, message_repr
    )
    existing = publisher_channels.setdefault(channel_name, config)
    if existing != config:
        msg = f"Conflicting publisher metadata for channel: {channel_name}"
        raise ValueError(msg)


def _initialization_requirements(
    logical_system: system.LogicalSystem,
    process_descriptions: Mapping[UUID, pdfproto.ProcessDescription],
    entities: Entities,
) -> list[_InitializationRequirement]:
    """Flatten logged state and configuration initialization requirements."""
    requirements: list[_InitializationRequirement] = []
    for process_uuid, process_description in sorted(process_descriptions.items(), key=lambda item: item[0].bytes):
        instances = [
            *process_description.state_graph.state_instances,
            *process_description.config_graph.config_instances,
        ]
        for instance in instances:
            source_index = instance.init_data_source
            if source_index >= len(process_description.data_sources):
                continue
            source = process_description.data_sources[source_index]
            if source.data_source_type != pdf.DataSourceType.log_first_message:
                continue
            allow_missing = source.fallback_source == pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL
            if not allow_missing and source.fallback_source < len(process_description.data_sources):
                fallback = process_description.data_sources[source.fallback_source]
                allow_missing = fallback.data_source_type == pdf.DataSourceType.file
            requirements.append(
                cast(
                    "_InitializationRequirement",
                    entities.initialization_requirement(
                        process_uuid=process_uuid,
                        source_channel_name=source.source_path_or_name,
                        cpu_domain_name=logical_system.cpu_domains[logical_system.process_to_domain[process_uuid]].name,
                        allow_missing=allow_missing,
                    ),
                )
            )
    return requirements


def _serialize_process_description(process_description: pdfproto.ProcessDescription) -> bytes:
    """Return the exact fixed-size Tachyon payload written for a process."""
    payload = bytearray(process_description.get_tachyon_constraint().size)
    process_description.serialize_tachyon(memoryview(payload))
    return bytes(payload)


def _platform_config_xxh3(process_payloads: Mapping[UUID, bytes]) -> int:
    """Hash UUID-and-length-framed process payloads in raw UUID order."""
    checksum = nb_xxh3_checksum.Xxh3Checksum()
    for process_uuid, payload in sorted(process_payloads.items(), key=lambda item: item[0].bytes):
        checksum.update(memoryview(process_uuid.bytes))
        checksum.update(memoryview(struct.pack("<Q", len(payload))))
        checksum.update(memoryview(payload))
    return checksum.digest()
