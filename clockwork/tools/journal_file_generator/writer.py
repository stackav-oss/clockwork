# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Journal serialization helpers."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

    from clockwork.journal import journal_pb2


def write_journal_file(journal: journal_pb2.JournalFile, output: Path) -> None:
    """Write a journal protobuf with deterministic serialization."""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(journal.SerializeToString(deterministic=True))
