# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helpers for generating python code."""

from dataclasses import dataclass

from typing_extensions import Self


@dataclass
class PythonChunks:
    """Generated python code representation."""

    system_imports: set[str]
    imports: set[str]
    impl: list[str]

    def __init__(self) -> None:  # pyright: ignore[reportMissingSuperCall] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Create PythonChunks."""
        self.system_imports = set()
        self.imports = set()
        self.impl = []

    def append(self, chunks: Self) -> None:
        """Append a python chunk to this chunk.

        Arguments:
            chunks: Chunks to append.
        """
        self.system_imports |= chunks.system_imports
        self.imports |= chunks.imports
        if self.impl:
            self.impl.append("")
        self.impl.extend(chunks.impl)

    def render_to_str(self) -> str:
        """Render the python chunks to a string.

        Return:
            String rendering of python chunks.
        """
        lines = [
            *sorted(self.system_imports),
            "",
            *sorted(self.imports),
            "",
            *(self.impl),
            "",
        ]
        return "\n".join(lines)
