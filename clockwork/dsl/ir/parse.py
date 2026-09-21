# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helpers for parsing Clockwork source."""

from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Generic, Protocol, TypeVar, cast

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl import clockwork_parser as parser
from fltk.fegen.pyrt import errors, memo, terminalsrc

if TYPE_CHECKING:
    from collections.abc import Callable

CstNodeType = TypeVar("CstNodeType")


class KindValue(Protocol):
    """Protocol for enum-like backend-stable kind constants."""

    _fltk_canonical_name: str


KindType = TypeVar("KindType", bound=KindValue)


class KindedNode(Protocol[KindType]):
    """Protocol for CST nodes exposing a discriminating kind field."""

    kind: KindType


class KindedNodeClass(Protocol[KindType]):
    """Protocol for CST node classes exposing a class-level kind constant."""

    kind: KindType


def _kind_of(value: KindedNode[KindType] | KindedNodeClass[KindType]) -> KindType:
    """Return the backend-stable kind value for a node instance or node class."""
    return value.kind


@dataclass(frozen=True)
class CstParseContext(Generic[CstNodeType]):
    """First-stage parsing output: A syntax tree and the source code it came from."""

    cst: CstNodeType
    terminals: terminalsrc.TerminalSource


def clk_source_to_cst(source_file_name: str) -> CstParseContext[cst.Module]:
    """Parse a Clockwork DSL source file to CST."""
    with Path(source_file_name).open() as source_file:
        terminals = terminalsrc.TerminalSource(source_file.read())
    return terminals_to_cst(terminals, len(terminals.terminals), "module", cst.Module, source_file_name)


def clk_string_to_cst(source: str, filename: str | Path = "<unknown>") -> CstParseContext[cst.Module]:
    """Parse a string containing Clockwork source to CST."""
    return terminals_to_cst(terminalsrc.TerminalSource(source), len(source), "module", cst.Module, filename)


def clk_string_to_any_cst(
    source: str, length: int, rule: str, cst_type: type[CstNodeType], filename: str | Path = "<unknown>"
) -> CstParseContext[CstNodeType]:
    """Parse a string containing Clockwork source to CST."""
    return terminals_to_cst(terminalsrc.TerminalSource(source), length, rule, cst_type, filename)


if TYPE_CHECKING:
    from typing import overload

    @overload
    def parse_rule(clk_parser: parser.Parser, rule: str) -> memo.ApplyResult[int, object] | None: ...

    @overload
    def parse_rule(
        clk_parser: parser.Parser,
        rule: str,
        expected: type[CstNodeType],
    ) -> memo.ApplyResult[int, CstNodeType] | None: ...

    @overload
    def parse_rule(
        clk_parser: parser.Parser,
        rule: str,
        expected: KindType,
    ) -> memo.ApplyResult[int, KindedNode[KindType]] | None: ...


def parse_rule(
    clk_parser: parser.Parser,
    rule: str,
    expected: type[CstNodeType] | KindType | None = None,
) -> (
    memo.ApplyResult[int, CstNodeType]
    | memo.ApplyResult[int, KindedNode[KindType]]
    | memo.ApplyResult[int, object]
    | None
):
    """Dispatch a generated parse rule and optionally verify the result kind."""
    parse_method = cast(
        "Callable[[int], memo.ApplyResult[int, object] | None]",
        getattr(clk_parser, f"apply__parse_{rule}"),
    )
    result = parse_method(0)
    if result is None or expected is None:
        return result

    expected_kind = _kind_of(cast("KindedNodeClass[KindValue]", expected)) if isinstance(expected, type) else expected
    actual_kind = _kind_of(cast("KindedNode[KindValue]", result.result))
    if actual_kind != expected_kind:
        msg = f"parse expected {expected_kind} but returned {actual_kind}"
        raise TypeError(msg)
    return result


def format_parse_error(clk_parser: parser.Parser, terminals: terminalsrc.TerminalSource) -> str:
    """Format a parse error for either parser backend."""
    error_message = cast("Callable[[], str] | None", getattr(clk_parser, "error_message", None))
    if error_message is not None:
        return error_message()
    # fmt: off
    return errors.format_error_message(
        # pyrefly: ignore[invalid-cast] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        cast("errors.ErrorTracker[int]", clk_parser.error_tracker),
        terminals,
        lambda rule_id: clk_parser.rule_names[rule_id],
    )
    # fmt: on


def terminals_to_cst(
    terminals: terminalsrc.TerminalSource, length: int, rule: str, cst_type: type[CstNodeType], filename: str | Path
) -> CstParseContext[CstNodeType]:
    """Parse terminals to CST.

    Arguments:
        terminals: the source
        length: the length of source expected to be parsed, usually len(terminals) but may be less if that was padded
        rule: name of the CST type to parse
        cst_type: type of the CST type to parse
        filename: name to use in the error string
    """
    clk_parser = parser.Parser(terminalsrc=terminals)
    result = parse_rule(clk_parser, rule, cst_type)
    if not result or result.pos != length:
        error_linecol = terminals.pos_to_line_col(clk_parser.error_tracker.longest_parse_len)
        msg = f"In {filename}:{error_linecol.line + 1}:{error_linecol.col + 1}:\n{format_parse_error(clk_parser, terminals)}"
        raise SyntaxError(msg)
    return CstParseContext(cst=result.result, terminals=terminals)
