# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Journal protobuf reader for report generation."""

from __future__ import annotations

from typing import TYPE_CHECKING, final

from clockwork.journal import journal_pb2
from google.protobuf.message import DecodeError

if TYPE_CHECKING:
    from pathlib import Path


@final
class JournalReadError(ValueError):
    """Raised when a journal file cannot be read as a JournalFile protobuf."""


def read_journal_file(journal_path: Path) -> journal_pb2.JournalFile:
    """Read a serialized JournalFile protobuf from disk."""
    try:
        journal_bytes = journal_path.read_bytes()
    except OSError as exc:
        msg = f"Cannot read journal file: {journal_path}"
        raise JournalReadError(msg) from exc

    journal = journal_pb2.JournalFile()
    try:
        journal.ParseFromString(journal_bytes)
    except DecodeError as exc:
        msg = f"Malformed JournalFile protobuf: {journal_path}"
        raise JournalReadError(msg) from exc
    return journal
