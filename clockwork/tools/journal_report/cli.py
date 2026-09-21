# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportMissingImports=false, reportUntypedFunctionDecorator=false
# Cyclopts is locked as a wheel in Clockwork, but Pyright cannot resolve its type metadata from the generated target.

"""Command-line interface for journal report generation."""

from __future__ import annotations

import sys
from pathlib import Path  # noqa: TC003 - Cyclopts evaluates command annotations at runtime.
from typing import Annotated, Final

import cyclopts
from clockwork.tools.journal_report.reader import JournalReadError, read_journal_file
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.request import (
    DEFAULT_MAX_HTML_BYTES,
    DEFAULT_SERVER_HOST,
    DEFAULT_SERVER_PORT,
    RequestValidationError,
    create_report_request,
    create_serve_request,
)
from clockwork.tools.journal_report.server import make_message_lookup, make_report_server
from cyclopts.exceptions import CycloptsError

app: Final = cyclopts.App(version_flags=[])


@app.command(name="generate")
def generate(
    *,
    journal: Annotated[Path, cyclopts.Parameter(help="Path to the serialized JournalFile protobuf.")],
    output: Annotated[Path, cyclopts.Parameter(alias="-o", help="Path to write the static HTML report.")],
    title: Annotated[str | None, cyclopts.Parameter(help="Optional report title override.")] = None,
    max_html_bytes: Annotated[
        int,
        cyclopts.Parameter(help="Warn when the generated HTML exceeds this many bytes."),
    ] = DEFAULT_MAX_HTML_BYTES,
    fail_on_size_warning: Annotated[
        bool,
        cyclopts.Parameter(help="Exit nonzero when the generated HTML exceeds --max-html-bytes."),
    ] = False,
) -> None:
    """Generate a self-contained static HTML report."""
    try:
        request = create_report_request(
            journal=journal,
            output=output,
            title=title,
            max_html_bytes=max_html_bytes,
            fail_on_size_warning=fail_on_size_warning,
        )
        journal_file = read_journal_file(request.journal)
    except (JournalReadError, RequestValidationError) as exc:
        raise CycloptsError(str(exc)) from exc

    html = render_report(journal_file, title=request.title)
    html_size = len(html.encode("utf-8"))
    request.output.write_text(html, encoding="utf-8")
    if html_size > request.max_html_bytes:
        _print_progress(f"Report size warning: generated HTML is {html_size} bytes.")
        if request.fail_on_size_warning:
            msg = "Generated report exceeds --max-html-bytes."
            raise CycloptsError(msg)

    _print_progress(f"Report written: {request.output}")


@app.command(name="serve")
def serve(
    *,
    journal: Annotated[Path, cyclopts.Parameter(help="Path to the serialized JournalFile protobuf.")],
    host: Annotated[
        str,
        cyclopts.Parameter(help="Bind host; use 0.0.0.0 for SVW port forwarding."),
    ] = DEFAULT_SERVER_HOST,
    port: Annotated[
        int,
        cyclopts.Parameter(help="Local server port; zero selects an ephemeral port."),
    ] = DEFAULT_SERVER_PORT,
    log_uri: Annotated[
        str | None,
        cyclopts.Parameter(help="Optional source-log URI override."),
    ] = None,
    title: Annotated[str | None, cyclopts.Parameter(help="Optional report title override.")] = None,
) -> None:
    """Serve the report and allowlisted Tachyon message JSON over HTTP."""
    try:
        request = create_serve_request(journal=journal, host=host, port=port, log_uri=log_uri, title=title)
        journal_file = read_journal_file(request.journal)
        source_log_uri = _resolve_log_uri(request.log_uri, journal_file.metadata.log_uri)
        message_lookup = make_message_lookup(journal_file, log_uri=source_log_uri)
        server = make_report_server(
            journal_file,
            host=request.host,
            port=request.port,
            message_lookup=message_lookup,
            title=request.title,
        )
    except (JournalReadError, OSError, RequestValidationError, RuntimeError, ValueError) as exc:
        raise CycloptsError(str(exc)) from exc

    with server:
        _print_progress(f"Journal report server listening on http://{request.host}:{server.server_port}")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            _print_progress("Journal report server stopped.")


def _print_progress(message: str) -> None:
    """Print CLI progress and summaries without using stdout."""
    print(message, file=sys.stderr)


def _resolve_log_uri(override: str | None, journal_log_uri: str) -> str:
    """Resolve a non-empty source-log URI for message lookup."""
    source_log_uri = override or journal_log_uri
    if source_log_uri == "":
        msg = "Journal metadata has no source log URI; specify --log-uri."
        raise RequestValidationError(msg)
    return source_log_uri


if __name__ == "__main__":
    app()
