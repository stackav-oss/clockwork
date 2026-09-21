# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal report fixtures."""

from __future__ import annotations

from typing import TYPE_CHECKING

import pytest
from clockwork.tools.journal_report.renderer import render_report
from clockwork.tools.journal_report.tests.support.journal_fixtures import (
    alignment_journal,
    group_journal,
    missing_data_journal,
    single_cog_journal,
)

if TYPE_CHECKING:
    from collections.abc import Callable

    from clockwork.journal import journal_pb2


@pytest.mark.parametrize(
    "journal_factory",
    [
        single_cog_journal,
        group_journal,
        missing_data_journal,
        alignment_journal,
    ],
)
def test_fixture_reports_render_static_html(journal_factory: Callable[[], journal_pb2.JournalFile]) -> None:
    journal = journal_factory()

    html = render_report(journal, title="Fixture")

    assert html.startswith("<!doctype html>")
    assert "Execution Timeline" in html
    assert "Channel Flow" in html
    assert "Message Sequences" in html


def test_group_fixture_scope_matches_cogs() -> None:
    journal = group_journal()

    assert tuple(journal.metadata.scope.cog_instance_paths) == tuple(
        cog.cog_instance_path for cog in journal.cog_journals
    )
