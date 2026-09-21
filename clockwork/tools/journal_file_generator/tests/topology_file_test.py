# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for loading journal topology protobuf text files."""

from pathlib import Path

import pytest
from clockwork.journal import journal_topology_pb2
from clockwork.tools.journal_file_generator import topology_file as topology_file_module
from clockwork.tools.journal_file_generator.topology_file import (
    JournalTopologyError,
    find_cog_instance_paths,
    load_cog_channel_maps,
    topology_file_for_log,
)
from google.protobuf import text_format


def _topology_contents(*, duplicate_cog: bool = False) -> str:
    cog = journal_topology_pb2.CogTopology(
        cog_instance_path="runtime.Box.Cog",
        input_channels=[journal_topology_pb2.CogChannel(member_name="input", channel_name="InputChannel")],
        output_channels=[journal_topology_pb2.CogChannel(member_name="output", channel_name="OutputChannel")],
        has_clockwork_state=True,
        snapshot_channels=[journal_topology_pb2.CogChannel(member_name="state", channel_name="SnapshotChannel")],
    )
    topology = journal_topology_pb2.JournalTopology(
        format_version=1,
        system_target_name="runtime.System",
        cogs=[cog, cog] if duplicate_cog else [cog],
    )
    return text_format.MessageToString(topology)


def _write_topology(path: Path, *, duplicate_cog: bool = False) -> None:
    path.write_text(_topology_contents(duplicate_cog=duplicate_cog))


def test_load_cog_channel_maps(tmp_path: Path) -> None:
    """A valid topology is converted to the existing channel-map model."""
    topology_file = tmp_path / "journal_topology.pbtxt"
    _write_topology(topology_file)

    channel_map = load_cog_channel_maps(topology_file)["runtime.Box.Cog"]

    assert [(ref.cog_member_name, ref.channel_name) for ref in channel_map.input_channels] == [
        ("input", "InputChannel")
    ]
    assert [(ref.cog_member_name, ref.channel_name) for ref in channel_map.output_channels] == [
        ("output", "OutputChannel")
    ]
    assert channel_map.has_clockwork_state
    assert [(ref.cog_member_name, ref.channel_name) for ref in channel_map.snapshot_channels] == [
        ("state", "SnapshotChannel")
    ]


def test_find_cogs_loads_topology_from_s3(monkeypatch: pytest.MonkeyPatch) -> None:
    """S3 telemetry URIs load topology from their timestamp directory."""
    expected_uri = "s3://test-bucket/telemetry/log/journal_topology.pbtxt"

    def read_log_file(uri: str) -> bytes:
        assert uri == expected_uri
        return _topology_contents().encode()

    monkeypatch.setattr(topology_file_module, "read_log_file", read_log_file)

    assert find_cog_instance_paths(
        name_contains="Box",
        log_uri="s3://test-bucket/telemetry/log/telemetry/",
    ) == ("runtime.Box.Cog",)


@pytest.mark.parametrize(
    ("log_uri", "expected"),
    [
        ("/logs/timestamp/telemetry", Path("/logs/timestamp/journal_topology.pbtxt")),
        ("/logs/timestamp", Path("/logs/timestamp/journal_topology.pbtxt")),
        (
            "s3://bucket/logs/timestamp/telemetry/",
            "s3://bucket/logs/timestamp/journal_topology.pbtxt",
        ),
        ("s3://bucket/logs/timestamp", "s3://bucket/logs/timestamp/journal_topology.pbtxt"),
    ],
)
def test_topology_file_for_log(log_uri: str, expected: Path | str) -> None:
    """Topology discovery supports stream URIs and legacy timestamp roots."""
    assert topology_file_for_log(log_uri) == expected


@pytest.mark.parametrize("case", ["missing", "malformed", "duplicate"])
def test_invalid_topology(case: str, tmp_path: Path) -> None:
    """Missing, malformed, and duplicate-cog files fail clearly."""
    topology_file = tmp_path / "journal_topology.pbtxt"
    if case == "malformed":
        topology_file.write_text("not protobuf text")
    elif case == "duplicate":
        _write_topology(topology_file, duplicate_cog=True)

    with pytest.raises(JournalTopologyError):
        load_cog_channel_maps(topology_file)
