# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Journal request model and validation."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING, Final, final

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import Path


@final
class SnapshotSelection(str, Enum):
    """State snapshot selection mode."""

    BEFORE = "before"
    AFTER = "after"
    CLOSEST = "closest"


_VALID_SNAPSHOT_SELECTIONS: Final = tuple(selection.value for selection in SnapshotSelection)


@final
class RequestValidationError(ValueError):
    """Raised when command-line options do not describe one valid journal request."""


@final
@dataclass(frozen=True, kw_only=True)
class JournalRequest:
    """Validated request for a journal file."""

    log_uri: str
    """Clockwork log path or URI."""

    output: Path
    """Local output path for the serialized journal protobuf."""

    start_time_ns: int
    """Inclusive start sync time for the journal."""

    end_time_ns: int
    """Inclusive end sync time for the journal."""

    cog_instance_paths: tuple[str, ...]
    """Fully resolved runtime cog instance paths."""

    telemetry_log_uri: str | None = None
    """Telemetry log URI used to discover topology when reading an event log."""

    box_instance_paths: tuple[str, ...] = ()
    """Fully resolved runtime box instance paths requested for report-oriented journals."""

    summary_json: Path | None = None
    """Optional local output path for the replay-readiness JSON summary."""

    snapshot_selection: SnapshotSelection = SnapshotSelection.BEFORE
    """State snapshot selection mode."""

    system_clk_file: Path | None = None
    """CLK file containing the system_target used to resolve cog channel maps."""

    system_target: str | None = None
    """Bazel target used to resolve cog channel maps."""

    journal_topology_file: Path | None = None
    """Explicit journal topology protobuf text file."""

    fail_if_not_replayable: bool = False
    """Whether the CLI should exit nonzero when replay readiness is insufficient."""


def create_journal_request(  # noqa: PLR0913 # The arguments intentionally mirror the CLI.
    *,
    log_uri: str,
    output: Path,
    start_time_ns: int,
    end_time_ns: int,
    summary_json: Path | None = None,
    cog_instance_path: str | None = None,
    cog_instance_paths: Sequence[str] | None = None,
    box_instance_path: str | None = None,
    box_instance_paths: Sequence[str] | None = None,
    snapshot_selection: str | SnapshotSelection = SnapshotSelection.BEFORE,
    system_clk_file: Path | None = None,
    system_target: str | None = None,
    journal_topology_file: Path | None = None,
    telemetry_log_uri: str | None = None,
    fail_if_not_replayable: bool = False,
) -> JournalRequest:
    """Validate raw CLI values and return a journal request."""
    _validate_non_empty("log_uri", log_uri)
    if telemetry_log_uri is not None:
        _validate_non_empty("telemetry_log_uri", telemetry_log_uri)
    validated_cog_instance_paths = _validate_cog_instance_paths(
        cog_instance_path=cog_instance_path,
        cog_instance_paths=cog_instance_paths,
    )
    validated_box_instance_paths = _validate_box_instance_paths(
        box_instance_path=box_instance_path,
        box_instance_paths=box_instance_paths,
    )
    _validate_time_range(start_time_ns=start_time_ns, end_time_ns=end_time_ns)
    validated_snapshot_selection = _validate_snapshot_selection(snapshot_selection)
    validated_system_clk_file = _validate_optional_system_clk_file(system_clk_file)
    validated_system_target = _validate_optional_system_target(system_target)
    _validate_system_context_options(system_clk_file=validated_system_clk_file, system_target=validated_system_target)
    _validate_topology_source_options(
        journal_topology_file=journal_topology_file,
        system_clk_file=validated_system_clk_file,
        system_target=validated_system_target,
    )
    _validate_scope_options(
        cog_instance_paths=validated_cog_instance_paths,
        box_instance_paths=validated_box_instance_paths,
        system_clk_file=validated_system_clk_file,
        system_target=validated_system_target,
    )
    _ensure_output_path(output)
    _ensure_optional_output_path("--summary-json", summary_json)

    return JournalRequest(
        log_uri=log_uri,
        output=output,
        summary_json=summary_json,
        start_time_ns=start_time_ns,
        end_time_ns=end_time_ns,
        cog_instance_paths=validated_cog_instance_paths,
        telemetry_log_uri=telemetry_log_uri,
        box_instance_paths=validated_box_instance_paths,
        snapshot_selection=validated_snapshot_selection,
        system_clk_file=validated_system_clk_file,
        system_target=validated_system_target,
        journal_topology_file=journal_topology_file,
        fail_if_not_replayable=fail_if_not_replayable,
    )


def _validate_non_empty(name: str, value: str) -> None:
    """Validate a required string option."""
    if value == "":
        msg = f"{name} must not be empty."
        raise RequestValidationError(msg)


def _validate_cog_instance_paths(
    *,
    cog_instance_path: str | None,
    cog_instance_paths: Sequence[str] | None,
) -> tuple[str, ...]:
    """Validate and return sorted resolved cog instance paths."""
    if cog_instance_path is not None and cog_instance_paths is not None:
        msg = "Specify --cog-instance-path values through one request field."
        raise RequestValidationError(msg)

    raw_paths = tuple(cog_instance_paths) if cog_instance_paths is not None else ()
    if cog_instance_path is not None:
        raw_paths = (cog_instance_path,)

    for path in raw_paths:
        _validate_non_empty("cog_instance_path", path)
    return tuple(sorted(set(raw_paths)))


def _validate_box_instance_paths(
    *,
    box_instance_path: str | None,
    box_instance_paths: Sequence[str] | None,
) -> tuple[str, ...]:
    """Validate and return sorted resolved box instance paths."""
    if box_instance_path is not None and box_instance_paths is not None:
        msg = "Specify --box-instance-path values through one request field."
        raise RequestValidationError(msg)

    raw_paths = tuple(box_instance_paths) if box_instance_paths is not None else ()
    if box_instance_path is not None:
        raw_paths = (box_instance_path,)

    for path in raw_paths:
        _validate_non_empty("box_instance_path", path)
    return tuple(sorted(set(raw_paths)))


def _validate_scope_options(
    *,
    cog_instance_paths: tuple[str, ...],
    box_instance_paths: tuple[str, ...],
    system_clk_file: Path | None,
    system_target: str | None,
) -> None:
    """Validate the selected generation scope mode."""
    if cog_instance_paths and box_instance_paths:
        msg = "Specify either --cog-instance-path or --box-instance-path, not both."
        raise RequestValidationError(msg)
    if not cog_instance_paths and not box_instance_paths:
        msg = "Specify --cog-instance-path or --box-instance-path."
        raise RequestValidationError(msg)
    if box_instance_paths and system_clk_file is None and system_target is None:
        msg = "--box-instance-path requires --system-clk-file or --system-target."
        raise RequestValidationError(msg)


def _validate_time_range(*, start_time_ns: int, end_time_ns: int) -> None:
    """Validate the inclusive sync-time range."""
    if start_time_ns > end_time_ns:
        msg = "--start-time-ns must be less than or equal to --end-time-ns."
        raise RequestValidationError(msg)


def _validate_snapshot_selection(snapshot_selection: str | SnapshotSelection) -> SnapshotSelection:
    """Validate and narrow the snapshot selection mode."""
    if isinstance(snapshot_selection, SnapshotSelection):
        return snapshot_selection
    try:
        return SnapshotSelection(snapshot_selection)
    except ValueError as exc:
        expected = ", ".join(_VALID_SNAPSHOT_SELECTIONS)
        msg = f"--snapshot-selection must be one of: {expected}."
        raise RequestValidationError(msg) from exc


def _validate_optional_system_clk_file(system_clk_file: Path | None) -> Path | None:
    """Validate an optional system CLK file."""
    if system_clk_file is None:
        return None
    if system_clk_file.suffix != ".clk":
        msg = "--system-clk-file must refer to a .clk file."
        raise RequestValidationError(msg)
    return system_clk_file


def _validate_optional_system_target(system_target: str | None) -> str | None:
    """Validate an optional system target."""
    if system_target is None:
        return None
    _validate_non_empty("system_target", system_target)
    return system_target


def _validate_system_context_options(*, system_clk_file: Path | None, system_target: str | None) -> None:
    """Validate optional system context selection."""
    if system_clk_file is not None and system_target is not None:
        msg = "Specify at most one of --system-clk-file or --system-target."
        raise RequestValidationError(msg)


def _validate_topology_source_options(
    *,
    journal_topology_file: Path | None,
    system_clk_file: Path | None,
    system_target: str | None,
) -> None:
    """Keep explicit topology and legacy CLK sources mutually exclusive."""
    if journal_topology_file is not None and (system_clk_file is not None or system_target is not None):
        msg = "Specify --journal-topology-file or a legacy system CLK source, not both."
        raise RequestValidationError(msg)


def _ensure_output_path(output: Path) -> None:
    """Ensure the output path can be written by creating its parent directory."""
    if output.exists() and output.is_dir():
        msg = f"Output path is a directory: {output}"
        raise RequestValidationError(msg)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        msg = f"Output parent directory cannot be created: {output.parent}"
        raise RequestValidationError(msg) from exc


def _ensure_optional_output_path(option_name: str, output: Path | None) -> None:
    """Ensure an optional output path can be written by creating its parent directory."""
    if output is None:
        return
    if output.exists() and output.is_dir():
        msg = f"{option_name} path is a directory: {output}"
        raise RequestValidationError(msg)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        msg = f"{option_name} parent directory cannot be created: {output.parent}"
        raise RequestValidationError(msg) from exc
