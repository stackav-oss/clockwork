# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Journal report request model and validation."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final, final

if TYPE_CHECKING:
    from pathlib import Path

DEFAULT_MAX_HTML_BYTES: Final = 5 * 1024 * 1024
DEFAULT_SERVER_HOST: Final = "127.0.0.1"
DEFAULT_SERVER_PORT: Final = 0
_MAX_SERVER_PORT: Final = 65535


@final
class RequestValidationError(ValueError):
    """Raised when command-line options do not describe one valid report request."""


@final
@dataclass(frozen=True, kw_only=True)
class ReportRequest:
    """Validated request for static report generation."""

    journal: Path
    """Local path to the serialized JournalFile protobuf."""

    output: Path
    """Local path for the generated HTML report."""

    title: str | None
    """Optional report title override."""

    max_html_bytes: int
    """Warning threshold for generated HTML size."""

    fail_on_size_warning: bool
    """Whether size warnings should make the CLI exit nonzero."""


@final
@dataclass(frozen=True, kw_only=True)
class ServeRequest:
    """Validated request for serving a journal report and indexed message data."""

    journal: Path
    """Local path to the serialized JournalFile protobuf."""

    host: str
    """Host interface used by the HTTP server."""

    port: int
    """TCP port, with zero selecting an ephemeral port."""

    log_uri: str | None
    """Optional source-log URI override."""

    title: str | None
    """Optional report title override."""


def create_report_request(
    *,
    journal: Path,
    output: Path,
    title: str | None = None,
    max_html_bytes: int = DEFAULT_MAX_HTML_BYTES,
    fail_on_size_warning: bool = False,
) -> ReportRequest:
    """Validate raw CLI values and return a report request."""
    _ensure_journal_path(journal)
    _ensure_output_path(output)
    if max_html_bytes <= 0:
        msg = "--max-html-bytes must be positive."
        raise RequestValidationError(msg)
    return ReportRequest(
        journal=journal,
        output=output,
        title=_validate_title(title),
        max_html_bytes=max_html_bytes,
        fail_on_size_warning=fail_on_size_warning,
    )


def create_serve_request(
    *,
    journal: Path,
    host: str = DEFAULT_SERVER_HOST,
    port: int = DEFAULT_SERVER_PORT,
    log_uri: str | None = None,
    title: str | None = None,
) -> ServeRequest:
    """Validate raw CLI values for local report serving."""
    _ensure_journal_path(journal)
    stripped_host = host.strip()
    if stripped_host == "":
        msg = "--host must not be empty."
        raise RequestValidationError(msg)
    if not 0 <= port <= _MAX_SERVER_PORT:
        msg = "--port must be between 0 and 65535."
        raise RequestValidationError(msg)
    return ServeRequest(
        journal=journal,
        host=stripped_host,
        port=port,
        log_uri=_validate_optional_non_empty("--log-uri", log_uri),
        title=_validate_title(title),
    )


def _ensure_journal_path(journal: Path) -> None:
    """Ensure the journal path is readable."""
    if not journal.exists():
        msg = f"Journal path does not exist: {journal}"
        raise RequestValidationError(msg)
    if journal.is_dir():
        msg = f"Journal path is a directory: {journal}"
        raise RequestValidationError(msg)


def _ensure_output_path(output: Path) -> None:
    """Ensure the report output path can be written."""
    if output.exists() and output.is_dir():
        msg = f"Output path is a directory: {output}"
        raise RequestValidationError(msg)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        msg = f"Output parent directory cannot be created: {output.parent}"
        raise RequestValidationError(msg) from exc


def _validate_title(title: str | None) -> str | None:
    """Validate an optional report title."""
    if title is None:
        return None
    stripped = title.strip()
    if stripped == "":
        msg = "--title must not be empty."
        raise RequestValidationError(msg)
    return stripped


def _validate_optional_non_empty(option_name: str, value: str | None) -> str | None:
    """Strip and validate an optional string option."""
    if value is None:
        return None
    stripped = value.strip()
    if stripped == "":
        msg = f"{option_name} must not be empty."
        raise RequestValidationError(msg)
    return stripped
