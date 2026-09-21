# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Formatter for Clockwork DSL source files."""

import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Annotated, Final, Protocol, TextIO, TypeAlias, TypeGuard, final

import cyclopts
from clockwork.dsl import clockwork_cst as runtime_cst
from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl import clockwork_unparser
from clockwork.dsl.ir import parse
from fltk.fegen.pyrt.span_protocol import SpanProtocol
from fltk.unparse.combinators import Doc
from fltk.unparse.renderer import Renderer, RendererConfig
from fltk.unparse.resolve_specs import resolve_spacing_specs

_CstFingerprint: TypeAlias = tuple[object, ...]
_SourceLineMarker: TypeAlias = tuple[str, bool]
_MultilineBlock: TypeAlias = tuple[str, int | None]
_EXECUTE_WHEN_PREFIX: Final = "execute when: "


class _CstNodeLike(Protocol):
    """Structural type for generated FLTK CST nodes."""

    kind: parse.KindValue
    children: Sequence[tuple[object | None, object]]


@final
class FormatterError(ValueError):
    """Raised when formatting cannot be applied safely."""


def _is_cst_node(value: object) -> TypeGuard[_CstNodeLike]:
    """Return whether a value looks like a generated FLTK CST node."""
    return hasattr(value, "kind") and hasattr(value, "children")


def _label_name(label: object | None) -> str | None:
    """Get a stable representation for a CST child label."""
    if label is None:
        return None
    canonical_name = getattr(label, "_fltk_canonical_name", None)
    if isinstance(canonical_name, str):
        return canonical_name.rsplit(".", maxsplit=1)[-1]
    return str(label).rsplit(".", maxsplit=1)[-1]


def _fingerprint_children(node: _CstNodeLike) -> tuple[tuple[object | None, object], ...]:
    """Get CST children relevant to semantic preservation checks."""
    if node.kind == cst.UseBlock.kind:
        return tuple((label, child) for label, child in node.children if _label_name(label) != "TRAILING_SEPARATOR")
    if node.kind == cst.ArgList.kind:
        return tuple((label, child) for label, child in node.children if _label_name(label) != "TRAILING_COMMA")
    return tuple(node.children)


def _cst_fingerprint(node: object, source: str) -> _CstFingerprint:
    """Build a source-position-independent fingerprint for a CST subtree."""
    if isinstance(node, SpanProtocol):
        return ("span", source[node.start : node.end].strip())
    if _is_cst_node(node):
        return (
            type(node).__module__,
            type(node).__qualname__,
            tuple(
                (_label_name(label), _cst_fingerprint(child, source)) for label, child in _fingerprint_children(node)
            ),
        )
    return ("value", repr(node))


def _validate_cst_preserved(
    *,
    original_cst: cst.Module,
    original_source: str,
    formatted_source: str,
    filename: str | Path,
) -> None:
    """Verify formatting did not alter the parsed source structure."""
    formatted_context = parse.clk_string_to_cst(formatted_source, filename)
    if _cst_fingerprint(original_cst, original_source) != _cst_fingerprint(
        formatted_context.cst,
        formatted_context.terminals.terminals,
    ):
        msg = f"{filename}: formatter changed parsed structure"
        raise FormatterError(msg)


def _source_line_key(line: str) -> str:
    """Build a stable key for matching source lines before and after formatting."""
    return "".join(line.split())


def _is_doc_line(line: str) -> bool:
    """Return whether a rendered line is Clockwork documentation."""
    return line.lstrip().startswith("//")


def _source_line_markers(source: str) -> list[_SourceLineMarker]:
    """Record whether each nonblank source line was preceded by a blank line."""
    markers: list[_SourceLineMarker] = []
    saw_nonblank = False
    blank_before_next = False
    for line in source.splitlines():
        stripped = line.strip()
        if not stripped:
            if saw_nonblank:
                blank_before_next = True
            continue
        markers.append((_source_line_key(stripped), blank_before_next))
        saw_nonblank = True
        blank_before_next = False
    return markers


def _preserve_single_blank_lines(*, original_source: str, formatted_source: str) -> str:
    """Preserve one original blank line between matching nonblank lines."""
    source_markers = _source_line_markers(original_source)
    source_index = 0
    output_lines: list[str] = []

    for formatted_line in formatted_source.splitlines():
        stripped = formatted_line.strip()
        if not stripped:
            if output_lines and output_lines[-1]:
                output_lines.append("")
            continue

        had_blank_before = False
        for marker_index in range(source_index, len(source_markers)):
            marker_key, marker_had_blank_before = source_markers[marker_index]
            if marker_key == _source_line_key(stripped):
                source_index = marker_index + 1
                had_blank_before = marker_had_blank_before
                break

        if had_blank_before and output_lines and output_lines[-1] and not _is_doc_line(output_lines[-1]):
            output_lines.append("")
        output_lines.append(formatted_line)

    if formatted_source.endswith("\n"):
        return "\n".join(output_lines) + "\n"
    return "\n".join(output_lines)


def _multiline_block_kind(stripped_line: str) -> str | None:
    """Identify a rendered multiline block that may need a final comma."""
    if (stripped_line.startswith(("use ", "use["))) and stripped_line.endswith("::{"):
        return "use"
    if stripped_line.endswith("("):
        return "paren"
    if stripped_line.endswith("<"):
        return "angle"
    return None


def _is_multiline_block_close(*, kind: str, stripped_line: str) -> bool:
    """Return whether a rendered line closes a tracked multiline block."""
    match kind:
        case "use":
            return stripped_line == "};"
        case "paren":
            return stripped_line in {")", ");", "),"}
        case "angle":
            return stripped_line in {">", ">;", ">,"}
        case _:
            return False


def _with_trailing_comma(line: str) -> str:
    """Add a trailing comma to one rendered item line when needed."""
    stripped_line = line.rstrip()
    if stripped_line.endswith((",", ";", "{", "(", "<")):
        return line
    return f"{stripped_line},"


def _add_multiline_trailing_commas(*, formatted_source: str) -> str:
    """Add final commas to multiline use, call, and generic argument blocks."""
    output_lines: list[str] = []
    block_stack: list[_MultilineBlock] = []

    for line in formatted_source.splitlines():
        stripped = line.strip()
        if block_stack and _is_multiline_block_close(kind=block_stack[-1][0], stripped_line=stripped):
            _, last_item_index = block_stack.pop()
            if last_item_index is not None:
                output_lines[last_item_index] = _with_trailing_comma(output_lines[last_item_index])

        output_lines.append(line)
        if block_kind := _multiline_block_kind(stripped):
            block_stack.append((block_kind, None))
        elif (
            block_stack and stripped and not _is_multiline_block_close(kind=block_stack[-1][0], stripped_line=stripped)
        ):
            block_kind, _ = block_stack[-1]
            block_stack[-1] = (block_kind, len(output_lines) - 1)

    if formatted_source.endswith("\n"):
        return "\n".join(output_lines) + "\n"
    return "\n".join(output_lines)


def _condition_expr_segments(expr: str) -> list[str]:
    """Split a condition expression at top-level boolean operators."""
    segments: list[str] = []
    segment_chars: list[str] = []
    pending_operator: str | None = None
    depth = 0
    index = 0
    while index < len(expr):
        char = expr[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1

        operator: str | None = None
        chars_to_skip = 0
        if depth == 0 and expr.startswith(" and ", index):
            operator = "and"
            chars_to_skip = len(" and ")
        elif depth == 0 and expr.startswith(" or ", index):
            operator = "or"
            chars_to_skip = len(" or ")

        if operator is not None:
            segment_text = "".join(segment_chars).strip()
            if segment_text:
                segments.append(segment_text if pending_operator is None else f"{pending_operator} {segment_text}")
            pending_operator = operator
            segment_chars = []
            index += chars_to_skip
            continue

        segment_chars.append(char)
        index += 1

    segment_text = "".join(segment_chars).strip()
    if segment_text:
        segments.append(segment_text if pending_operator is None else f"{pending_operator} {segment_text}")
    return segments


def _format_execute_when_condition(*, indent: str, expr: str, max_width: int) -> list[str]:
    """Format one execute condition line, wrapping top-level boolean chains when needed."""
    one_line = f"{indent}{_EXECUTE_WHEN_PREFIX}{expr};"
    if len(one_line) <= max_width:
        return [one_line]

    segments = _condition_expr_segments(expr)
    if len(segments) <= 1:
        return [one_line]

    continuation_indent = f"{indent}  "
    output_lines = [f"{indent}{_EXECUTE_WHEN_PREFIX}{segments[0]}"]
    output_lines.extend(f"{continuation_indent}{segment}" for segment in segments[1:-1])
    output_lines.append(f"{continuation_indent}{segments[-1]};")
    return output_lines


def _wrap_execute_when_conditions(*, formatted_source: str, options: "FormatterOptions") -> str:
    """Normalize multiline execute conditions after rendering."""
    lines = formatted_source.splitlines()
    output_lines: list[str] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.lstrip()
        if not stripped.startswith(_EXECUTE_WHEN_PREFIX):
            output_lines.append(line)
            index += 1
            continue

        indent = line[: len(line) - len(stripped)]
        expr_parts = [stripped.removeprefix(_EXECUTE_WHEN_PREFIX).strip()]
        while not expr_parts[-1].endswith(";") and index + 1 < len(lines):
            index += 1
            expr_parts.append(lines[index].strip())

        expr = " ".join(expr_parts)
        if expr.endswith(";"):
            expr = expr[:-1].strip()
        output_lines.extend(_format_execute_when_condition(indent=indent, expr=expr, max_width=options.max_width))
        index += 1

    if formatted_source.endswith("\n"):
        return "\n".join(output_lines) + "\n"
    return "\n".join(output_lines)


def _remove_space_before_closing_delimiters(*, formatted_source: str) -> str:
    """Remove rendered whitespace before closing delimiters outside strings."""
    output_lines: list[str] = []
    for line in formatted_source.splitlines():
        if line.lstrip().startswith("//"):
            output_lines.append(line)
            continue

        output_chars: list[str] = []
        in_string = False
        escaped = False
        for char in line:
            if char == '"' and not escaped:
                in_string = not in_string
            if char in {")", "]"} and not in_string and any(not output_char.isspace() for output_char in output_chars):
                while output_chars and output_chars[-1] == " ":
                    output_chars.pop()
            output_chars.append(char)
            escaped = char == "\\" and not escaped
            if char != "\\":
                escaped = False
        output_lines.append("".join(output_chars))

    if formatted_source.endswith("\n"):
        return "\n".join(output_lines) + "\n"
    return "\n".join(output_lines)


@final
@dataclass(frozen=True)
class FormatterOptions:
    """Options controlling rendered Clockwork source layout."""

    max_width: int = 120
    """Maximum rendered line width."""


_DEFAULT_OPTIONS: Final = FormatterOptions()


def _unparse_doc(cst_node: cst.Module, source: str) -> Doc:
    """Unparse a Clockwork module CST to an FLTK document tree."""
    assert isinstance(cst_node, runtime_cst.Module)
    result = clockwork_unparser.Unparser(source).unparse_module(cst_node)
    if result is None:
        msg = "Unparsing failed"
        raise FormatterError(msg)
    return resolve_spacing_specs(result.accumulator.doc)


def format_source(
    source: str,
    *,
    filename: str | Path = "<unknown>",
    options: FormatterOptions | None = None,
) -> str:
    """Format Clockwork source text.

    Args:
        source: Source text to format.
        filename: Name to include in parser diagnostics.
        options: Rendering options.

    Returns:
        Formatted Clockwork source text.
    """
    options = options or _DEFAULT_OPTIONS
    parse_context = parse.clk_string_to_cst(source, filename)
    source_text = parse_context.terminals.terminals
    formatted = format_cst(parse_context.cst, source_text, options=options)
    formatted = _preserve_single_blank_lines(original_source=source_text, formatted_source=formatted)
    formatted = _add_multiline_trailing_commas(formatted_source=formatted)
    formatted = _wrap_execute_when_conditions(formatted_source=formatted, options=options)
    formatted = _remove_space_before_closing_delimiters(formatted_source=formatted)
    _validate_cst_preserved(
        original_cst=parse_context.cst,
        original_source=source_text,
        formatted_source=formatted,
        filename=filename,
    )
    return formatted


def format_cst(
    cst_node: cst.Module,
    source: str,
    *,
    options: FormatterOptions | None = None,
) -> str:
    """Format a parsed Clockwork module CST.

    Args:
        cst_node: Parsed Clockwork module CST.
        source: Original source text used to produce the CST.
        options: Rendering options.

    Returns:
        Formatted Clockwork source text.
    """
    options = options or _DEFAULT_OPTIONS
    doc = _unparse_doc(cst_node, source)
    rendered = Renderer(RendererConfig(max_width=options.max_width, indent_width=2)).render(doc)
    return rendered.strip("\n") + "\n"


def format_file(path: Path, *, options: FormatterOptions | None = None) -> str:
    """Format one Clockwork source file.

    Args:
        path: Source file path.
        options: Rendering options.

    Returns:
        Formatted Clockwork source text.
    """
    return format_source(path.read_text(encoding="utf-8"), filename=path, options=options)


def format_inputs(  # noqa: PLR0913 Parameterized I/O for testing
    input_files: tuple[Path, ...],
    *,
    check: bool,
    options: FormatterOptions,
    stdin: TextIO,
    stdout: TextIO,
    stderr: TextIO,
) -> list[Path]:
    """Format command inputs.

    Args:
        input_files: Files to format. Empty input means read source from stdin.
        check: Report files that are not formatted without rewriting them.
        options: Rendering options.
        stdin: Stream used when no input files are provided.
        stdout: Stream for formatted source output.
        stderr: Stream for diagnostics.

    Returns:
        Files that would be reformatted when ``check`` is enabled.

    Raises:
        ValueError: If the requested formatting mode is invalid.
    """
    if options.max_width <= 0:
        msg = "--width must be positive"
        raise ValueError(msg)
    if not input_files:
        if check:
            msg = "stdin input cannot be used with --check"
            raise ValueError(msg)
        stdout.write(format_source(stdin.read(), filename="<stdin>", options=options))
        return []

    formatted_inputs: list[tuple[Path, str, str]] = []
    for input_file in input_files:
        source = input_file.read_text(encoding="utf-8")
        formatted = format_source(source, filename=input_file, options=options)
        formatted_inputs.append((input_file, source, formatted))

    unformatted_files: list[Path] = []
    for input_file, source, formatted in formatted_inputs:
        if check:
            if formatted != source:
                unformatted_files.append(input_file)
                print(f"{input_file} would be reformatted", file=stderr)
        elif formatted != source:
            input_file.write_text(formatted, encoding="utf-8")
    return unformatted_files


def main(
    *input_files: Annotated[Path, cyclopts.Parameter(help="Clockwork .clk files to format.")],
    check: Annotated[
        bool,
        cyclopts.Parameter(negative=[], help="Return a non-zero exit code if any input file is not formatted."),
    ] = False,
    width: Annotated[int, cyclopts.Parameter(alias="-w", help="Maximum rendered line width.")] = 120,
) -> None:
    """Format Clockwork source files."""
    options = FormatterOptions(max_width=width)
    try:
        unformatted_files = format_inputs(
            input_files,
            check=check,
            options=options,
            stdin=sys.stdin,
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
    except ValueError as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(2) from exc
    if unformatted_files:
        print(f"{len(unformatted_files)} file(s) would be reformatted", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    cyclopts.run(main)
