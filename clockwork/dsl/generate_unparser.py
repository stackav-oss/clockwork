# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate the Clockwork FLTK unparser source file."""

import ast
from pathlib import Path

import cyclopts
from fltk import plumbing
from fltk.fegen import gsm
from fltk.iir.context import create_default_context
from fltk.iir.py import compiler
from fltk.unparse import gsm2unparser


def main(grammar: Path, format_spec: Path, cst_module: str, output: Path) -> None:
    """Generate the Clockwork FLTK unparser implementation.

    Args:
        grammar: Path to the Clockwork .fltkg grammar.
        format_spec: Path to the Clockwork .fltkfmt config.
        cst_module: Import path for the generated Clockwork CST module.
        output: Path to write the generated unparser Python source.
    """
    grammar_model = plumbing.parse_grammar_file(grammar)
    formatter_config = plumbing.parse_format_config_file(format_spec)
    context = create_default_context(capture_trivia=True)
    grammar_with_trivia = gsm.classify_trivia_rules(gsm.add_trivia_rule_to_grammar(grammar_model, context))
    unparser_class, imports = gsm2unparser.generate_unparser(
        grammar_with_trivia,
        context,
        cst_module,
        formatter_config=formatter_config,
    )
    unparser_ast = compiler.compile_class(unparser_class, context)
    module = ast.fix_missing_locations(ast.Module(body=[*imports, unparser_ast], type_ignores=[]))
    output.write_text(ast.unparse(module) + "\n", encoding="utf-8")


if __name__ == "__main__":
    cyclopts.run(main)
