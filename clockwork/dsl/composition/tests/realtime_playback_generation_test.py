# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for real-time playback conversion-configuration generation."""

from __future__ import annotations

from copy import copy
from pathlib import Path
from typing import TYPE_CHECKING, Final, Protocol, cast
from uuid import UUID

import pytest
from clockwork.dsl.composition import genpd, pdfproto, realtime_playback_generation, system
from clockwork.dsl.ir import compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

if TYPE_CHECKING:
    from collections.abc import Sequence

_FIXTURE_PATH: Final = Path("clockwork/dsl/composition/tests/support/realtime_playback_system_target.clk")


class _GeneratedChannel(Protocol):
    channel_name: str


class _GeneratedPublisherConfig(Protocol):
    channels: Sequence[_GeneratedChannel]


class _GeneratedEnum(Protocol):
    name: str


class _GeneratedAssignment(Protocol):
    source_channel_name: str
    destination_channel_name: str
    stream_kind: _GeneratedEnum


class _GeneratedDomainPlan(Protocol):
    cpu_domain_name: str
    simplelaunch_node_name: str
    assignments: Sequence[_GeneratedAssignment]


class _GeneratedInitializationRequirement(Protocol):
    process_uuid: UUID
    source_channel_name: str
    cpu_domain_name: str
    allow_missing: bool


class _GeneratedConfig(Protocol):
    publishers: _GeneratedPublisherConfig
    cpu_domains: Sequence[_GeneratedDomainPlan]
    initialization_requirements: Sequence[_GeneratedInitializationRequirement]
    platform_config_xxh3: int


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture()
def fixture_data(
    fs_importer: FilesystemImporter,
) -> tuple[system.LogicalSystem, dict[UUID, pdfproto.ProcessDescription]]:
    module = compiler.compile_source_file(ModuleID.from_path(CLK_REPO, _FIXTURE_PATH), fs_importer)
    target = module.inner_scope.lookup("RealtimePlaybackSystem", recursive=False)
    assert isinstance(target, system_target.UnresolvedSystemTarget)
    logical_system = system.make_system(
        [target.get_resolved().box_instance], module, target.require_logging_policies, target.use_simplelaunch
    )
    process_descriptions = genpd.gen_pd_sys(system.make_physical_system(logical_system))
    return logical_system, process_descriptions


def test_platform_checksum_matches_the_cpp_process_framing_vector() -> None:
    """Keep Python generation byte-for-byte compatible with the C++ XXH3 vector."""
    payloads = {
        UUID("ffeeddcc-bbaa-9988-7766-554433221100"): bytes.fromhex("aabb"),
        UUID("00112233-4455-6677-8899-aabbccddeeff"): bytes.fromhex("00010203"),
    }
    assert realtime_playback_generation._platform_config_xxh3(payloads) == 0x6DE46A6DB67B3963


def test_make_conversion_config_captures_playback_configuration(
    fixture_data: tuple[system.LogicalSystem, dict[UUID, pdfproto.ProcessDescription]],
) -> None:
    """Generate the documented two-domain fixture and resolve shared initialization metadata."""
    logical_system, process_descriptions = fixture_data
    config = cast(
        "_GeneratedConfig", realtime_playback_generation.make_conversion_config(logical_system, process_descriptions)
    )

    assert sorted(channel.channel_name for channel in config.publishers.channels) == [
        "/camera",
        "/regular",
        "/shared_init",
    ]
    assert config.platform_config_xxh3 != 0

    assignments_by_domain = {
        plan.cpu_domain_name: {
            (
                assignment.source_channel_name,
                assignment.destination_channel_name,
                assignment.stream_kind.name,
                plan.simplelaunch_node_name,
            )
            for assignment in plan.assignments
        }
        for plan in config.cpu_domains
    }
    assert assignments_by_domain == {
        "DomainOne": {("/recorded_regular", "/regular", "regular", "c1")},
        "DomainTwo": {("/recorded_camera", "/camera", "camera", "c2")},
    }

    assert {requirement.source_channel_name for requirement in config.initialization_requirements} == {"/shared_init"}
    assert {requirement.cpu_domain_name for requirement in config.initialization_requirements} == {
        "DomainOne",
        "DomainTwo",
    }
    assert all(requirement.allow_missing for requirement in config.initialization_requirements)


def _add_initialization_only_process(
    logical_system: system.LogicalSystem,
    process_descriptions: dict[UUID, pdfproto.ProcessDescription],
    index: int,
) -> tuple[UUID, UUID]:
    """Add a process on a new CPU domain whose only playback input is initialization data."""
    source_process_uuid, source_process = next(iter(logical_system.processes.items()))
    source_domain = next(iter(logical_system.cpu_domains.values()))
    domain_uuid = UUID(int=1000 + index)
    process_uuid = UUID(int=2000 + index)
    init_only_domain = copy(source_domain)
    init_only_domain.name = f"InitializationOnly{index}"
    init_only_domain.simplelaunch_node = f"init{index}"
    logical_system.cpu_domains[domain_uuid] = init_only_domain
    logical_system.processes[process_uuid] = source_process
    logical_system.process_to_domain[process_uuid] = domain_uuid
    process_descriptions[process_uuid] = process_descriptions[source_process_uuid]
    return domain_uuid, process_uuid


def test_generation_includes_initialization_only_cpu_domain(
    fixture_data: tuple[system.LogicalSystem, dict[UUID, pdfproto.ProcessDescription]],
) -> None:
    """Keep a CPU domain plan when its process has no regular or camera assignments."""
    logical_system, process_descriptions = fixture_data
    _, process_uuid = _add_initialization_only_process(logical_system, process_descriptions, 0)
    config = cast(
        "_GeneratedConfig", realtime_playback_generation.make_conversion_config(logical_system, process_descriptions)
    )

    plan = next(plan for plan in config.cpu_domains if plan.cpu_domain_name == "InitializationOnly0")
    assert plan.simplelaunch_node_name == "init0"
    assert plan.assignments == []
    requirement = next(item for item in config.initialization_requirements if item.process_uuid == process_uuid)
    assert requirement.cpu_domain_name == "InitializationOnly0"


def test_generation_rejects_initialization_only_domain_without_simplelaunch_node(
    fixture_data: tuple[system.LogicalSystem, dict[UUID, pdfproto.ProcessDescription]],
) -> None:
    """Require an explicit SimpleLaunch owner for initialization-only outputs."""
    logical_system, process_descriptions = fixture_data
    domain_uuid, _ = _add_initialization_only_process(logical_system, process_descriptions, 0)
    logical_system.cpu_domains[domain_uuid].simplelaunch_node = None

    with pytest.raises(TypeError, match="no explicit simplelaunch node"):
        realtime_playback_generation.make_conversion_config(logical_system, process_descriptions)
