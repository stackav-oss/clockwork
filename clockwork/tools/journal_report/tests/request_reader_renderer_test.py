# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report request validation, reading, rendering, and CLI wiring."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest
from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.reader import JournalReadError, read_journal_file
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.request import RequestValidationError, create_report_request


def _journal_report_binary() -> Path:
    runfiles_dir = os.getenv("RUNFILES_DIR")
    assert runfiles_dir is not None

    matches = sorted(Path(runfiles_dir).glob("*/clockwork/tools/journal_report/journal_report"))
    assert matches
    return matches[0]


def _tiny_journal(*, log_uri: str = "/logs/demo.clog") -> journal_pb2.JournalFile:
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=10,
            end_time_ns=20,
            log_uri=log_uri,
            generator_version="test",
            scope=journal_pb2.JournalScope(cog_instance_paths=["runtime.path.Cog"]),
            replay_readiness=journal_pb2.ReplayReadiness(sufficient_for_replay=True, has_state_snapshot=True),
        )
    )


def test_request_validation_rejects_missing_journal(tmp_path: Path) -> None:
    with pytest.raises(RequestValidationError, match="does not exist"):
        create_report_request(journal=tmp_path / "missing.pb", output=tmp_path / "report.html")


def test_request_validation_creates_output_parent(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    journal_path.write_bytes(_tiny_journal().SerializeToString(deterministic=True))

    request = create_report_request(
        journal=journal_path,
        output=tmp_path / "nested" / "report.html",
        title="  Demo Report  ",
        max_html_bytes=1,
    )

    assert request.output.parent.exists()
    assert request.title == "Demo Report"
    assert request.max_html_bytes == 1


def test_request_validation_rejects_invalid_size_limit(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    journal_path.write_bytes(_tiny_journal().SerializeToString(deterministic=True))

    with pytest.raises(RequestValidationError, match="positive"):
        create_report_request(journal=journal_path, output=tmp_path / "report.html", max_html_bytes=0)


def test_reader_parses_valid_journal(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    expected = _tiny_journal()
    journal_path.write_bytes(expected.SerializeToString(deterministic=True))

    assert read_journal_file(journal_path) == expected


def test_reader_reports_malformed_journal(tmp_path: Path) -> None:
    journal_path = tmp_path / "bad.journal.pb"
    journal_path.write_bytes(b"\xff")

    with pytest.raises(JournalReadError, match="Malformed"):
        read_journal_file(journal_path)


def test_renderer_embeds_escaped_json_without_external_resources() -> None:
    html = render_report(_tiny_journal(log_uri="</script><img src=x>"), title="Demo </script>")

    assert "\\u003c/script\\u003e" in html
    assert '<script id="journal-report-data" type="application/json">' in html
    assert 'id="report-section-tabs"' in html
    assert 'href="#overview"' in html
    assert '<section id="overview" class="report-section report-section-active">' in html
    assert '<section id="execution-timeline" class="report-section" hidden>' in html
    assert '<section id="execution-details" class="report-section" hidden>' in html
    assert "<img" not in html
    assert "<link" not in html
    assert 'src="' not in html
    assert "http://" not in html
    assert "https://" not in html


def test_cli_smoke_generates_html(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    output_path = tmp_path / "demo.html"
    journal_path.write_bytes(_tiny_journal().SerializeToString(deterministic=True))

    subprocess.run(
        [
            _journal_report_binary(),
            "generate",
            "--journal",
            str(journal_path),
            "--output",
            str(output_path),
            "--title",
            "Demo Report",
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    assert output_path.read_text(encoding="utf-8").startswith("<!doctype html>")
    assert "Demo Report" in output_path.read_text(encoding="utf-8")


def test_cli_size_warning_failure_writes_html(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    output_path = tmp_path / "demo.html"
    journal_path.write_bytes(_tiny_journal().SerializeToString(deterministic=True))

    result = subprocess.run(
        [
            _journal_report_binary(),
            "generate",
            "--journal",
            str(journal_path),
            "--output",
            str(output_path),
            "--max-html-bytes",
            "1",
            "--fail-on-size-warning",
        ],
        check=False,
        capture_output=True,
        text=True,
    )

    assert result.returncode != 0
    assert output_path.read_text(encoding="utf-8").startswith("<!doctype html>")
