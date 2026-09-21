# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utilities for working with byte types."""

from typing import TypeAlias

MutableBytes: TypeAlias = bytearray | memoryview
"""Alias for mutable contiguous byte types."""
