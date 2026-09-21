# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportMissingImports=false, reportUntypedFunctionDecorator=false
# Cyclopts is locked as a wheel in Clockwork, but Pyright cannot resolve its type metadata from the generated target.

"""Command-line interface for journal file generation and discovery."""

from __future__ import annotations

import sys
from pathlib import Path  # noqa: TC003 - Cyclopts evaluates command annotations at runtime.
from typing import Annotated, Final

import cyclopts
from clockwork.tools.journal_file_generator.generator import build_journal_file_from_log
from clockwork.tools.journal_file_generator.instance_discovery import (
    DiscoveryError,
    DiscoveryValidationError,
    create_instance_discovery_request,
    discover_cog_instances,
    format_discovery_candidates,
)
from clockwork.tools.journal_file_generator.readiness import (
    format_readiness_summary,
    write_readiness_summary_json,
)
from clockwork.tools.journal_file_generator.request import RequestValidationError, create_journal_request
from clockwork.tools.journal_file_generator.topology_file import JournalTopologyError, find_cog_instance_paths
from clockwork.tools.journal_file_generator.writer import write_journal_file
from cyclopts.exceptions import CycloptsError

app: Final = cyclopts.App(version_flags=[])


@app.command(name="generate")
def generate(  # noqa: PLR0913 # CLI options mirror the journal request schema.
    *,
    log_uri: Annotated[str, cyclopts.Parameter(help="Clockwork log path or URI.")],
    telemetry_log_uri: Annotated[
        str | None,
        cyclopts.Parameter(help="Telemetry log URI used to discover topology when --log-uri is an event log."),
    ] = None,
    output: Annotated[Path, cyclopts.Parameter(alias="-o", help="Path to write the journal protobuf.")],
    start_time_ns: Annotated[int, cyclopts.Parameter(help="Inclusive start sync time.")],
    end_time_ns: Annotated[int, cyclopts.Parameter(help="Inclusive end sync time.")],
    summary_json: Annotated[
        Path | None,
        cyclopts.Parameter(help="Optional path to write a replay-readiness JSON summary."),
    ] = None,
    cog_instance_path: Annotated[
        list[str] | None,
        cyclopts.Parameter(help="Fully resolved runtime cog instance path. May be repeated for group journals."),
    ] = None,
    box_instance_path: Annotated[
        list[str] | None,
        cyclopts.Parameter(help="Fully resolved runtime box instance path. May be repeated for box journals."),
    ] = None,
    snapshot_selection: Annotated[
        str,
        cyclopts.Parameter(help="State snapshot selection mode: before, after, or closest."),
    ] = "before",
    system_clk_file: Annotated[
        Path | None,
        cyclopts.Parameter(help="Optional CLK file containing the system_target used to resolve cog channel maps."),
    ] = None,
    system_target: Annotated[
        str | None,
        cyclopts.Parameter(help="Optional Clockwork clk or topology_summary target used to resolve cog channel maps."),
    ] = None,
    journal_topology_file: Annotated[
        Path | None,
        cyclopts.Parameter(help="Explicit journal_topology.pbtxt; otherwise discover it from the telemetry log URI."),
    ] = None,
    fail_if_not_replayable: Annotated[
        bool,
        cyclopts.Parameter(help="Exit nonzero after writing outputs if replay readiness is insufficient."),
    ] = False,
    verbose: Annotated[
        bool,
        cyclopts.Parameter(help="Print extraction progress and a replay-readiness gap summary."),
    ] = False,
) -> None:
    """Generate a schema-valid journal file for the requested scope."""
    try:
        request = create_journal_request(
            log_uri=log_uri,
            telemetry_log_uri=telemetry_log_uri,
            output=output,
            start_time_ns=start_time_ns,
            end_time_ns=end_time_ns,
            summary_json=summary_json,
            cog_instance_paths=cog_instance_path,
            box_instance_paths=box_instance_path,
            snapshot_selection=snapshot_selection,
            system_clk_file=system_clk_file,
            system_target=system_target,
            journal_topology_file=journal_topology_file,
            fail_if_not_replayable=fail_if_not_replayable,
        )
    except RequestValidationError as exc:
        raise CycloptsError(str(exc)) from exc

    if verbose:
        _print_progress("Resolving scope and extracting journal data.")
    try:
        journal = build_journal_file_from_log(request)
    except DiscoveryError as exc:
        raise CycloptsError(str(exc)) from exc
    except (OSError, RuntimeError, ValueError) as exc:
        msg = f"Cannot read log metadata: {exc}"
        raise CycloptsError(msg) from exc

    if verbose:
        _print_progress("Writing journal protobuf.")
    write_journal_file(journal, request.output)
    if request.summary_json is not None:
        try:
            write_readiness_summary_json(journal, request.summary_json)
        except OSError as exc:
            msg = f"Cannot write summary JSON: {exc}"
            raise CycloptsError(msg) from exc

    _print_progress(
        format_readiness_summary(
            journal,
            output_path=request.output,
            summary_json_path=request.summary_json,
            include_gaps=verbose,
        )
    )
    if request.fail_if_not_replayable and not journal.metadata.replay_readiness.sufficient_for_replay:
        msg = "Replay readiness is insufficient."
        raise CycloptsError(msg)


@app.command(name="list-instances")
def list_instances(  # noqa: PLR0913 # CLI options mirror the discovery request schema.
    *,
    system_clk_file: Annotated[
        Path | None,
        cyclopts.Parameter(help="CLK file containing the system_target that instantiates the deployed system."),
    ] = None,
    system_target: Annotated[
        str | None,
        cyclopts.Parameter(help="Existing compiled system or topology target."),
    ] = None,
    source_clk_file: Annotated[
        Path | None,
        cyclopts.Parameter(help="CLK file containing the source-declared box filter."),
    ] = None,
    box_name: Annotated[
        str | None,
        cyclopts.Parameter(help="Declared box name exactly as it appears in --source-clk-file."),
    ] = None,
    cog_name: Annotated[
        str | None,
        cyclopts.Parameter(help="Optional declared cog instance name inside --box-name."),
    ] = None,
    output_format: Annotated[
        str,
        cyclopts.Parameter(name="--format", help="Discovery output format: table or json."),
    ] = "table",
) -> None:
    """List resolved runtime cog instance paths for source-declared filters."""
    try:
        request = create_instance_discovery_request(
            system_clk_file=system_clk_file,
            system_target=system_target,
            source_clk_file=source_clk_file,
            box_name=box_name,
            cog_name=cog_name,
            output_format=output_format,
        )
        candidates = discover_cog_instances(request)
    except (DiscoveryError, DiscoveryValidationError) as exc:
        raise CycloptsError(str(exc)) from exc

    print(format_discovery_candidates(candidates, request.output_format))


@app.command(name="find-cogs")
def find_cogs(
    *,
    name_contains: Annotated[str, cyclopts.Parameter(help="Case-sensitive literal cog path substring.")],
    log_uri: Annotated[
        str | None,
        cyclopts.Parameter(help="Telemetry log URI; topology is discovered in its timestamp directory."),
    ] = None,
    journal_topology_file: Annotated[
        Path | None,
        cyclopts.Parameter(help="Explicit journal_topology.pbtxt override."),
    ] = None,
) -> None:
    """List runtime cog paths from a logged journal topology."""
    try:
        matches = find_cog_instance_paths(
            name_contains=name_contains,
            log_uri=log_uri,
            journal_topology_file=journal_topology_file,
        )
    except JournalTopologyError as exc:
        raise CycloptsError(str(exc)) from exc
    print("\n".join(matches) if matches else "No matching cog instances.")


def _print_progress(message: str) -> None:
    """Print CLI progress and summaries without using stdout."""
    print(message, file=sys.stderr)


if __name__ == "__main__":
    app()
