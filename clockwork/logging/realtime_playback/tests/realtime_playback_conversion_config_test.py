# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the generated real-time playback conversion configuration."""

import uuid
from dataclasses import asdict

import pytest
from clockwork.logging import channel_publisher_config_clk_py as publisher_config
from clockwork.logging.realtime_playback import realtime_playback_conversion_config_clk_py as config
from clockwork.logging.realtime_playback import realtime_playback_stream_kind_clk_py as stream_kind


def _empty_publishers() -> publisher_config.ChannelPublisherConfig:
    return publisher_config.ChannelPublisherConfig(channels=[])


@pytest.fixture()
def minimal_config() -> config.RealtimePlaybackConversionConfig:
    """Build a small populated conversion configuration."""
    regular_assignment = config.PlaybackChannelAssignment(
        source_channel_name="/source",
        destination_channel_name="/destination_regular",
        stream_kind=stream_kind.RealtimePlaybackStreamKind.regular,
    )
    camera_assignment = config.PlaybackChannelAssignment(
        source_channel_name="/source",
        destination_channel_name="/destination_camera",
        stream_kind=stream_kind.RealtimePlaybackStreamKind.camera,
    )
    domain0 = config.PlaybackCpuDomainPlan(
        cpu_domain_name="domain0",
        simplelaunch_node_name="c1",
        assignments=[regular_assignment],
    )
    domain1 = config.PlaybackCpuDomainPlan(
        cpu_domain_name="domain1",
        simplelaunch_node_name="c2",
        assignments=[camera_assignment],
    )
    requirement = config.InitializationRequirement(
        process_uuid=uuid.UUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
        source_channel_name="/state",
        cpu_domain_name="domain0",
        allow_missing=True,
    )
    return config.RealtimePlaybackConversionConfig(
        platform_id="platform",
        platform_config_xxh3=456,
        publishers=_empty_publishers(),
        cpu_domains=[domain0, domain1],
        initialization_requirements=[requirement],
    )


def test_minimal_config_round_trips(minimal_config: config.RealtimePlaybackConversionConfig) -> None:
    """Verify defaults, bounds, and Tachyon serialization."""
    assert minimal_config.format_version == 1
    assert (
        config.MAX_CPU_DOMAINS,
        config.MAX_CHANNEL_ASSIGNMENTS_PER_DOMAIN,
        config.MAX_INITIALIZATION_REQUIREMENTS,
    ) == (16, 555, 8192)
    assert [
        (assignment.source_channel_name, assignment.destination_channel_name)
        for domain in minimal_config.cpu_domains
        for assignment in domain.assignments
    ] == [
        ("/source", "/destination_regular"),
        ("/source", "/destination_camera"),
    ]

    constraint_size = config.RealtimePlaybackConversionConfig.get_tachyon_constraint().size
    assert constraint_size < 256 * 1024 * 1024
    assert constraint_size < 2**32

    buffer = bytearray(constraint_size)
    minimal_config.serialize_tachyon(memoryview(buffer))
    round_tripped = config.RealtimePlaybackConversionConfig.deserialize_tachyon(memoryview(buffer))

    assert asdict(minimal_config) == asdict(round_tripped)
