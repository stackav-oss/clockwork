# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Tests for the Clockwork XXH3 nanobind wrapper."""

from typing import Final

from clockwork.logging.realtime_playback import nb_xxh3_checksum

_EMPTY_INPUT_CHECKSUM: Final[int] = 0x2D06800538D394C2
_ABCDEF_CHECKSUM: Final[int] = 0xDA87BD32D3C47DB6


def test_fixed_xxh3_vectors() -> None:
    """Verify the binding against the fixed vectors used by the C++ wrapper tests."""
    assert nb_xxh3_checksum.compute_xxh3_checksum(memoryview(b"")) == _EMPTY_INPUT_CHECKSUM
    assert nb_xxh3_checksum.compute_xxh3_checksum(memoryview(b"abcdef")) == _ABCDEF_CHECKSUM


def test_incremental_xxh3_matches_contiguous_input() -> None:
    """Verify that streaming updates preserve the contiguous-input checksum."""
    checksum = nb_xxh3_checksum.Xxh3Checksum()
    for data in (b"a", b"bc", b"def"):
        checksum.update(memoryview(data))
    assert checksum.digest() == _ABCDEF_CHECKSUM
    assert checksum.digest() == nb_xxh3_checksum.compute_xxh3_checksum(memoryview(b"abcdef"))
