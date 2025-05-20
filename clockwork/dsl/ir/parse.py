# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Helpers for parsing Clockwork source."""

from dataclasses import dataclass
from pathlib import Path
from typing import Generic, TypeVar

from clockwork.dsl import cst, parser
from fltk.fegen.pyrt import errors, terminalsrc

CstNodeType = TypeVar("CstNodeType")


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
    result = getattr(clk_parser, f"apply__parse_{rule}")(0)
    if not result or result.pos != length:
        error_linecol = terminals.pos_to_line_col(clk_parser.error_tracker.longest_parse_len)
        msg = f"In {filename}:{error_linecol.line + 1}:{error_linecol.col + 1}:\n"
        msg += errors.format_error_message(
            clk_parser.error_tracker,
            terminals,
            lambda rule_id: clk_parser.rule_names[rule_id],
        )
        raise SyntaxError(msg)
    if not isinstance(result.result, cst_type):
        msg = f"parse expected {cst_type} but returned {type(result.result)}"
        raise TypeError(msg)
    return CstParseContext(cst=result.result, terminals=terminals)
