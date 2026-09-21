# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Access to the real-time playback policy definition."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, Protocol, cast, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.composition import graphir
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    compiler,
    hardware,
    importer_registry,
    node,
    policy,
    primitive,
    pubsub,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.ir.uuid_reg import lookup_uuid
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Mapping
    from uuid import UUID


class _LogicalSystemChannel(Protocol):
    """Channel view needed by playback assignment resolution."""

    producers: object


@final
class RealtimePlaybackRegistry(Context):
    """Registry for real-time playback policy entities."""

    def __init__(self, name: str | None, realtime_playback_policy: policy.PolicyClass) -> None:
        """Initialize the registry with the compiled policy."""
        self.name = name
        self.realtime_playback_policy = realtime_playback_policy

    @override
    def import_from(self, other: RealtimePlaybackRegistry) -> None:
        """Merge another real-time playback registry."""
        if self.realtime_playback_policy is not other.realtime_playback_policy:
            msg = f"Real-time playback registries conflict: {other.name} and {self.name}"
            raise RuntimeError(msg)


def _load_realtime_playback_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    importer_reg = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if importer_reg.importer is None:
        msg = "No importer registered in compiler context"
        raise TypeError(msg)
    policy_module = compiler.compile_source_file(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/logging/realtime_playback/realtime_playback_policy.clk"),
        ),
        importer_reg.importer,
    )
    compiler_context.import_from(policy_module.context)
    policy_def = policy_module.inner_scope.lookup("RealtimePlaybackPolicy")
    if not isinstance(policy_def, policy.PolicyDef):
        msg = "RealtimePlaybackPolicy was not found in the real-time playback policy module"
        raise TypeError(msg)
    return policy_def.get_resolved()


class RealtimePlaybackRegistryKey(ContextKey[RealtimePlaybackRegistry]):
    """Compiler-context key for real-time playback entities."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> RealtimePlaybackRegistry:
        """Create the registry after compiling its policy module."""
        return RealtimePlaybackRegistry(compiler_context.name, _load_realtime_playback_policy(compiler_context))


REGISTRY_KEY: Final = RealtimePlaybackRegistryKey("RealtimePlaybackRegistryKey")


@dataclass(frozen=True)
class RealtimePlaybackAssignment:
    """Resolved placement for a real-time playback channel."""

    destination_channel_name: str
    source_channel_name: str
    stream_kind_name: str
    cpu_domain_uuid: UUID
    cpu_domain_name: str
    simplelaunch_node_name: str
    channel: graphir.Channel


def resolve_assignments(
    module: node.Module,
    channels: Mapping[str, object],
    cpu_domains: Mapping[UUID, hardware.CpuDomain],
) -> tuple[RealtimePlaybackAssignment, ...]:
    """Resolve policies without deriving placement from channel names or consumers."""
    assignments = []
    for policy_data in policy.lookup_all_policies(module, get_realtime_playback_policy(module.context)):
        target = policy_data.target
        if not isinstance(target, (pubsub.Channel, pubsub.InstantiatedChannel)):
            msg = "RealtimePlaybackPolicy must target a channel"
            raise TypeError(msg)
        channel = graphir.lookup_channel(target, module.context)
        live_channel = channels.get(channel.channel_name)
        if live_channel is not None and cast("_LogicalSystemChannel", live_channel).producers:
            msg = f"Playback channel has a live publisher: {channel.channel_name}"
            raise ValueError(msg)
        source = policy_data.data.data["source_name"]
        if isinstance(source, clkbuiltins.Nullopt):
            source_name = channel.channel_name
        else:
            if not isinstance(source, primitive.StringValue):
                msg = "source_name must be a StringValue"
                raise TypeError(msg)
            source_name = source.value
        stream_kind = policy_data.data.data["stream_kind"]
        if not isinstance(stream_kind, clkenum.ValueRef):
            msg = "stream_kind must be an enum value"
            raise TypeError(msg)
        domain = policy_data.data.data["playback_cpu_domain"]
        if not isinstance(domain, hardware.CpuDomain):
            msg = "playback_cpu_domain must be a CpuDomain"
            raise TypeError(msg)
        domain_uuid = lookup_uuid(module.context, domain)
        if domain_uuid not in cpu_domains:
            msg = f"Playback domain is not in the system: {domain.name}"
            raise ValueError(msg)
        if not isinstance(domain.simplelaunch_node, str):
            msg = f"Playback domain has no explicit simplelaunch node: {domain.name}"
            raise TypeError(msg)
        assignments.append(
            RealtimePlaybackAssignment(
                channel.channel_name,
                source_name,
                stream_kind.name,
                domain_uuid,
                domain.name,
                domain.simplelaunch_node,
                channel,
            )
        )
    return tuple(sorted(assignments, key=lambda value: (value.cpu_domain_uuid.bytes, value.destination_channel_name)))


def get_realtime_playback_policy(compiler_context: CompilerContext) -> policy.PolicyClass:
    """Get the RealtimePlaybackPolicy definition."""
    return compiler_context[REGISTRY_KEY].realtime_playback_policy
