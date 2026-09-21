# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Load journal topology files and discover runtime cog paths."""

from __future__ import annotations

from pathlib import Path
from typing import final
from urllib.parse import unquote, urlparse

from clockwork.journal import journal_topology_pb2
from clockwork.logging.readers.nb_log_reader import log_file_exists, read_log_file
from clockwork.tools.journal_file_generator.cog_channel import (
    CogChannelMap,
    CogChannelRef,
    DiscoveredSnapshotChannelRef,
)
from google.protobuf import text_format

JOURNAL_TOPOLOGY_FILE_NAME = "journal_topology.pbtxt"
_FORMAT_VERSION = 1


@final
class JournalTopologyError(ValueError):
    """Raised when a journal topology source is missing or invalid."""


def load_cog_channel_maps(topology_file: Path | str) -> dict[str, CogChannelMap]:
    """Load and validate cog channel maps from a journal topology file."""
    try:
        contents = (
            topology_file.read_text() if isinstance(topology_file, Path) else read_log_file(topology_file).decode()
        )
    except (OSError, RuntimeError, UnicodeDecodeError) as exc:
        msg = f"Cannot read journal topology file {topology_file}: {exc}"
        raise JournalTopologyError(msg) from exc

    try:
        topology = text_format.Parse(contents, journal_topology_pb2.JournalTopology())
    except text_format.ParseError as exc:
        msg = f"Cannot parse journal topology file {topology_file}: {exc}"
        raise JournalTopologyError(msg) from exc

    _validate_topology(topology)
    return {
        cog.cog_instance_path: CogChannelMap(
            cog_instance_path=cog.cog_instance_path,
            input_channels=tuple(
                CogChannelRef(cog_member_name=channel.member_name, channel_name=channel.channel_name)
                for channel in cog.input_channels
            ),
            output_channels=tuple(
                CogChannelRef(cog_member_name=channel.member_name, channel_name=channel.channel_name)
                for channel in cog.output_channels
            ),
            has_clockwork_state=cog.has_clockwork_state,
            snapshot_channels=tuple(
                DiscoveredSnapshotChannelRef(
                    cog_member_name=channel.member_name,
                    channel_name=channel.channel_name,
                )
                for channel in cog.snapshot_channels
            ),
        )
        for cog in topology.cogs
    }


def find_cog_instance_paths(
    *,
    name_contains: str,
    log_uri: str | None = None,
    journal_topology_file: Path | None = None,
) -> tuple[str, ...]:
    """Return sorted cog instance paths containing a literal substring."""
    if not name_contains:
        msg = "--name-contains must not be empty."
        raise JournalTopologyError(msg)
    topology_file = journal_topology_file or topology_file_for_log(log_uri)
    return tuple(path for path in sorted(load_cog_channel_maps(topology_file)) if name_contains in path)


def topology_file_exists(topology_file: Path | str) -> bool:
    """Return whether a local or S3 topology file exists."""
    try:
        return topology_file.exists() if isinstance(topology_file, Path) else log_file_exists(topology_file)
    except RuntimeError as exc:
        msg = f"Cannot check journal topology file {topology_file}: {exc}"
        raise JournalTopologyError(msg) from exc


def topology_file_for_log(log_uri: str | None) -> Path | str:
    """Return the journal topology path discovered from a telemetry log URI."""
    if not log_uri:
        msg = "Specify --log-uri or --journal-topology-file."
        raise JournalTopologyError(msg)
    parsed = urlparse(log_uri)
    if parsed.scheme == "s3":
        normalized_uri = log_uri.rstrip("/")
        topology_directory = normalized_uri.rsplit("/", maxsplit=1)[0]
        if normalized_uri.rsplit("/", maxsplit=1)[-1] != "telemetry":
            topology_directory = normalized_uri
        return f"{topology_directory}/{JOURNAL_TOPOLOGY_FILE_NAME}"
    if parsed.scheme not in ("", "file"):
        msg = f"Cannot discover journal topology under {parsed.scheme} URI; copy it and use --journal-topology-file."
        raise JournalTopologyError(msg)
    log_path = Path(unquote(parsed.path)) if parsed.scheme == "file" else Path(log_uri)
    topology_directory = log_path.parent if log_path.name == "telemetry" else log_path
    return topology_directory / JOURNAL_TOPOLOGY_FILE_NAME


def _validate_topology(topology: journal_topology_pb2.JournalTopology) -> None:
    """Validate required topology fields and unique cog paths."""
    if topology.format_version != _FORMAT_VERSION:
        msg = f"Unsupported journal topology format_version {topology.format_version}; expected {_FORMAT_VERSION}."
        raise JournalTopologyError(msg)
    if not topology.system_target_name:
        msg = "Journal topology system_target_name must not be empty."
        raise JournalTopologyError(msg)

    cog_paths: set[str] = set()
    for cog in topology.cogs:
        if not cog.cog_instance_path:
            msg = "Journal topology cog_instance_path must not be empty."
            raise JournalTopologyError(msg)
        if cog.cog_instance_path in cog_paths:
            msg = f"Duplicate journal topology cog_instance_path: {cog.cog_instance_path}"
            raise JournalTopologyError(msg)
        cog_paths.add(cog.cog_instance_path)
        for channel in (*cog.input_channels, *cog.output_channels, *cog.snapshot_channels):
            if not channel.member_name or not channel.channel_name:
                msg = f"Journal topology channels for {cog.cog_instance_path} require member_name and channel_name."
                raise JournalTopologyError(msg)
