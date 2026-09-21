# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Top-level rendering functions for Tap interfaces and Tachyon representations.

This module contains the orchestration logic that calls both tap.py and soa.py
to avoid circular dependencies.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from clockwork.dsl.cpp.context import CppModuleChunks
from clockwork.dsl.serialization import soa, tap

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir import typesys


def render(
    compiler_context: CompilerContext, typespec: typesys.Instantiation, enclosing_namespace: str
) -> CppModuleChunks:
    """Render a Tap interface and Tachyon representation.

    This orchestrates the rendering of the interface and optionally adds SoA types
    if the schema has soa_enabled: true.
    """
    cpp_mod = CppModuleChunks()
    cpp_spec = tap.to_cpp_spec(compiler_context, typespec)
    schema_options = cpp_spec.schema_ir.options
    if schema_options and schema_options.provide_constructor:
        cpp_mod.append(tap.render_tap_init_struct(cpp_spec, enclosing_namespace))
    cpp_mod.append(tap.render_representation(compiler_context, cpp_spec, enclosing_namespace))
    cpp_mod.append(tap.render_interface(compiler_context, cpp_spec, enclosing_namespace))

    if soa.should_generate_soa(cpp_spec.schema_ir):
        cpp_mod.append(soa.render_soa_types(compiler_context, cpp_spec.schema_ir, enclosing_namespace))

    return cpp_mod


def render_alias(
    compiler_context: CompilerContext, typespec: typesys.Instantiation, enclosing_namespace: str, alias_to: str
) -> CppModuleChunks:
    """Render the alias for the interface."""
    return tap.render_alias(compiler_context, typespec, enclosing_namespace, alias_to)
