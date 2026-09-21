# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportMissingImports=false

"""Selected Clockwork parser backend."""

from collections.abc import Callable
from typing import Protocol, cast, final

from clockwork.dsl import parser_backend

if not parser_backend.use_rust_parser():
    from clockwork.dsl.clockwork_py_parser import *  # pyrefly: ignore[missing-import] # noqa: F403 Generated targets
else:
    from clockwork_native import parser as _rust_parser  # pyrefly: ignore[missing-module-attribute] Generated targets
    from fltk.fegen.pyrt import terminalsrc as _terminalsrc

    class _RustParserProtocol(Protocol):
        """Python surface exposed by the generated Rust parser."""

        @property
        def rule_names(self) -> list[str]:
            """Generated grammar rule names."""
            ...

        def error_position(self) -> int | None:
            """Return the parse error position."""
            ...

        def error_message(self) -> str:
            """Return the parser error message."""
            ...

    @final
    class _RustErrorTracker:
        """Python-parser-compatible view of the Rust parser's error position."""

        expected_context: list[object]
        """Expected-token context is not exposed by the Rust Python binding."""

        def __init__(self, rust_parser: _RustParserProtocol) -> None:
            """Create an error-tracker view for a Rust parser."""
            self._rust_parser = rust_parser
            self.expected_context = []

        @property
        def longest_parse_len(self) -> int:
            """Return the longest parse position, or -1 if no parse failed."""
            error_position = self._rust_parser.error_position()
            if error_position is None:
                return -1
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            return int(error_position)

    @final
    class Parser:
        """Compatibility wrapper around the generated Rust parser."""

        rule_names: list[str]
        """Generated grammar rule names."""

        error_tracker: _RustErrorTracker
        """Python-parser-compatible error tracker."""

        def __init__(self, terminalsrc: _terminalsrc.TerminalSource) -> None:
            """Create a parser from a Python FLTK terminal source."""
            self._rust_parser = cast("_RustParserProtocol", _rust_parser.Parser(terminalsrc.terminals))
            self.rule_names = list(self._rust_parser.rule_names)
            self.error_tracker = _RustErrorTracker(self._rust_parser)

        def __getattr__(self, name: str) -> Callable[[int], object | None]:
            """Dispatch generated parse methods to the Rust parser."""
            if not name.startswith("apply__parse_"):
                raise AttributeError(name)
            rust_method = cast("Callable[[int], object | None]", getattr(self._rust_parser, name))

            def apply(pos: int = 0) -> object | None:
                return rust_method(pos)

            return apply

        def error_message(self) -> str:
            """Return a Rust-generated parser error message."""
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            return str(self._rust_parser.error_message())

        def error_position(self) -> int | None:
            """Return the Rust parser's error position."""
            error_position = self._rust_parser.error_position()
            if error_position is None:
                return None
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            return int(error_position)
