# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""C++ POD struct backend for Clockwork schemas.

This module implements a C++ code generation backend for Clockwork schemas that just creates POD (Plain Old Data)
structs.  It's not a serialization backend per se, as the resulting "wire format" is not guaranteed to be portable
across languages and architectures.  But it is still useful for testing and other purposes.  The caveat is that this
should be used as an in-memory representation only, and generally never written to disk or transmitted over a network.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.cpp.context import CppContext, SystemHeader
from clockwork.dsl.cpp.typereg import get_cpp_type
from clockwork.dsl.ir import expr, schema, typesys

if TYPE_CHECKING:
    from collections.abc import Iterator
    from pathlib import Path


@dataclass
class SchemaPod:
    """Represents a POD struct representation/interface for a Schema.

    Attributes:
        schema_ir: The Clockwork schema
        cpp_namespace: The C++ namespace containing the POD
        class_name: The name of the struct
        header_path: The include path for the POD header, or None if unknown
    """

    schema_ir: schema.Schema
    cpp_namespace: str
    class_name: str
    header_path: Path | None

    def render_cpp_definition(self, cpp_context: CppContext) -> str:
        """Generate C++ source defining the struct."""
        if self.cpp_namespace:
            return f"""
namespace {self.cpp_namespace}
{{
{self._render_struct_definition(cpp_context)}
}}
"""
        return self._render_struct_definition(cpp_context)

    def _render_struct_definition(self, cpp_context: CppContext) -> str:
        cpp_context.add_include(SystemHeader("type_traits"))
        linesep = "\n  "
        return f"""
struct {self.class_name}
{{
  {linesep.join(self._render_fields(cpp_context))}
}};
static_assert(std::is_pod_v<{self.class_name}>);
"""

    def _render_fields(self, cpp_context: CppContext) -> Iterator[str]:
        for fld in self.schema_ir.fields.values():
            if isinstance(fld.type_info, expr.Expr | typesys.InferenceVar):
                msg = f"Encountered unresolved field type expression in {fld}"
                raise TypeError(msg)
            type_info = get_cpp_type(self.schema_ir.module.context, fld.type_info)
            cpp_context.add_includes(type_info.includes)
            yield f"""{type_info.render("")} {fld.cur_name};"""
