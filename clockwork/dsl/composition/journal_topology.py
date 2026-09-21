# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Journal-relevant projection of a resolved Clockwork system."""

from __future__ import annotations

from typing import TYPE_CHECKING, Final

from clockwork.dsl.ir import cog as cog_ir
from clockwork.journal import journal_topology_pb2
from clockwork.tools.journal_file_generator.cog_channel import (
    CogChannelMap,
    CogChannelRef,
    DiscoveredSnapshotChannelRef,
)
from google.protobuf import text_format

if TYPE_CHECKING:
    from uuid import UUID

    from clockwork.dsl.composition import system as composition_system

# The topology payload is materialized as a standalone pbtxt file downstream, so retain
# the standard header that identifies its schema and message type.
_FORMAT_VERSION: Final = 1
_PBTXT_HEADER: Final = "# proto-file: clockwork/journal/journal_topology.proto\n# proto-message: JournalTopology\n"


def make_cog_channel_maps(logical_system: composition_system.LogicalSystem) -> dict[str, CogChannelMap]:
    """Project a logical system into deterministic journal-relevant cog channel maps."""
    input_refs_by_cog: dict[str, list[CogChannelRef]] = {}
    output_refs_by_cog: dict[str, list[CogChannelRef]] = {}
    snapshot_refs_by_cog: dict[str, list[DiscoveredSnapshotChannelRef]] = {}
    stateful_cog_paths = frozenset(
        endpoint.entity.cog_instance.fqn for endpoint in logical_system.state_endpoints.values()
    )
    for channel in logical_system.channels.values():
        channel_name = channel.channel.channel_name
        for observer in channel.observers.values():
            input_ref = _input_channel_ref(observer.entity, channel_name)
            if input_ref is not None:
                cog_instance_path, channel_ref = input_ref
                input_refs_by_cog.setdefault(cog_instance_path, []).append(channel_ref)
        for endpoint_uuid, producer in channel.producers.items():
            output_ref = _output_channel_ref(producer.entity, channel_name)
            if output_ref is not None:
                cog_instance_path, channel_ref = output_ref
                output_refs_by_cog.setdefault(cog_instance_path, []).append(channel_ref)
            snapshot_ref = _snapshot_channel_ref(
                endpoint_uuid=endpoint_uuid,
                entity=producer.entity,
                channel_name=channel_name,
                logical_system=logical_system,
            )
            if snapshot_ref is not None:
                cog_instance_path, channel_ref = snapshot_ref
                snapshot_refs_by_cog.setdefault(cog_instance_path, []).append(channel_ref)

    return {
        cog_instance.fqn: CogChannelMap(
            cog_instance_path=cog_instance.fqn,
            input_channels=_unique_channel_refs(input_refs_by_cog.get(cog_instance.fqn, [])),
            output_channels=_unique_channel_refs(output_refs_by_cog.get(cog_instance.fqn, [])),
            has_clockwork_state=cog_instance.fqn in stateful_cog_paths,
            snapshot_channels=_unique_snapshot_channel_refs(snapshot_refs_by_cog.get(cog_instance.fqn, [])),
        )
        for cog_instance in sorted(logical_system.cogs.values(), key=lambda instance: instance.fqn)
    }


def render_journal_topology(*, logical_system: composition_system.LogicalSystem, system_target_name: str) -> str:
    """Render deterministic protobuf text for a resolved system."""
    channel_maps = make_cog_channel_maps(logical_system)
    topology = journal_topology_pb2.JournalTopology(
        format_version=_FORMAT_VERSION,
        system_target_name=system_target_name,
        cogs=[
            journal_topology_pb2.CogTopology(
                cog_instance_path=channel_map.cog_instance_path,
                input_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.input_channels
                ],
                output_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.output_channels
                ],
                has_clockwork_state=channel_map.has_clockwork_state,
                snapshot_channels=[
                    journal_topology_pb2.CogChannel(
                        member_name=channel_ref.cog_member_name,
                        channel_name=channel_ref.channel_name,
                    )
                    for channel_ref in channel_map.snapshot_channels
                ],
            )
            for channel_map in channel_maps.values()
        ],
    )
    return _PBTXT_HEADER + text_format.MessageToString(topology)


def _input_channel_ref(entity: object, channel_name: str) -> tuple[str, CogChannelRef] | None:
    """Return the runtime path and channel ref for a regular or aligned input."""
    if not isinstance(entity, cog_ir.CogInstanceMember):
        return None
    if not isinstance(entity.member, (cog_ir.InputDef, cog_ir.CogAlignedInputDef)):
        return None
    return (
        entity.cog_instance.fqn,
        CogChannelRef(cog_member_name=_input_metric_cog_member_name(entity), channel_name=channel_name),
    )


def _output_channel_ref(entity: object, channel_name: str) -> tuple[str, CogChannelRef] | None:
    """Return the runtime path and channel ref for an output endpoint."""
    if not isinstance(entity, cog_ir.CogInstanceMember):
        return None
    if not isinstance(entity.member, cog_ir.OutputDef):
        return None
    return entity.cog_instance.fqn, CogChannelRef(cog_member_name=entity.name, channel_name=channel_name)


def _snapshot_channel_ref(
    *,
    endpoint_uuid: UUID,
    entity: object,
    channel_name: str,
    logical_system: composition_system.LogicalSystem,
) -> tuple[str, DiscoveredSnapshotChannelRef] | None:
    """Return the runtime path and channel ref for a journaled state snapshot."""
    if endpoint_uuid not in logical_system.snapshot_metadata:
        return None
    if not isinstance(entity, cog_ir.CogInstanceMember) or not isinstance(entity.member, cog_ir.StateDef):
        return None
    return (
        entity.cog_instance.fqn,
        DiscoveredSnapshotChannelRef(cog_member_name=entity.name, channel_name=channel_name),
    )


def _unique_channel_refs(channel_refs: list[CogChannelRef]) -> tuple[CogChannelRef, ...]:
    """Deduplicate channel refs and sort them for deterministic topology output."""
    return tuple(sorted(set(channel_refs), key=lambda ref: (ref.channel_name, ref.cog_member_name)))


def _unique_snapshot_channel_refs(
    channel_refs: list[DiscoveredSnapshotChannelRef],
) -> tuple[DiscoveredSnapshotChannelRef, ...]:
    """Deduplicate snapshot refs and sort them for deterministic topology output."""
    return tuple(sorted(set(channel_refs), key=lambda ref: (ref.channel_name, ref.cog_member_name)))


def _input_metric_cog_member_name(
    entity: cog_ir.CogInstanceMember[cog_ir.InputDef | cog_ir.CogAlignedInputDef],
) -> str:
    """Return the member name used by cog metrics for an input endpoint."""
    if isinstance(entity.member, cog_ir.InputDef):
        return entity.name.rsplit(".", maxsplit=1)[-1]
    return entity.name
