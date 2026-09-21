# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Shared codegen helper utilities used by cppcog and cppdial."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from clockwork.dsl.cpp.context import CppChunk, Header
from clockwork.dsl.cpp.typereg import CLOCKWORK_NAMESPACE, get_cpp_type
from clockwork.dsl.cpp.types import (
    CppNamedType,
    CppTemplate,
    CppTemplateParam,
    CppTemplateType,
    CppType,
    CppTypeArg,
    CppTypeExpr,
    CppValue,
    CppValueExpr,
)
from clockwork.dsl.ir import clkbuiltins, cog, primitive, typesys
from clockwork.dsl.ir.cog_parameters import CogParameterRef
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO

if TYPE_CHECKING:
    from collections.abc import Iterable
    from uuid import UUID

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.cog_parameters import CogParameter

_UUID_TEMPLATE = CppTemplate([Header(JEWELS_REPO, "jewels/uuid/uuid/hh")], "Uuid", "jewels")
_COG_CLASS_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
    "CogClassId",
    CLOCKWORK_NAMESPACE + "::common",
)
_ENDPOINT_TYPE = CppType(
    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
    "EndpointClassId",
    CLOCKWORK_NAMESPACE + "::common",
)
COG_CLASS_UUID_TYPE = _UUID_TEMPLATE.instantiate([_COG_CLASS_TYPE])
ENDPOINT_UUID_TYPE = _UUID_TEMPLATE.instantiate([_ENDPOINT_TYPE])

UUID_TEMPLATE = _UUID_TEMPLATE


def to_camel(snake: str) -> str:
    """Convert from snake to camel case, replacing '__' with '_'."""
    toks = snake.split("__")
    return "_".join(_to_camel_impl(tok) for tok in toks)


def _to_camel_impl(snake: str) -> str:
    """Convert from snake to camel case."""
    toks = snake.split("_")
    return "".join(tok.title() for tok in toks)


def formatted_struct(name: str, declarations: str | list[str]) -> CppChunk:
    """Helper function to format a struct."""
    chunk = CppChunk()
    chunk.append(
        [
            f"struct {name}",
            "{",
        ],
    )
    # Body
    chunk.append(
        declarations,
        indent=1,
    )
    # Closing
    chunk.append("};")
    return chunk


def gen_const_str(terms: str | Iterable[str] | CogParameterRef, name: str = "name") -> str:
    """Generate a static constexpr string_view declaration."""
    if isinstance(terms, CogParameterRef):
        return f"static constexpr ::std::string_view {name} = {terms.parameter_def.param_name};"
    full_str = terms if isinstance(terms, str) else ".".join(terms)
    init = f' = "{full_str}"' if full_str else "{}"
    return f"static constexpr ::std::string_view {name}{init};"


@dataclass(frozen=True)
class UuidHandler:
    """Manage UUID."""

    uuid_cpp_type: CppTemplateType
    uuid: UUID

    def render_from_string_func(self) -> str:
        """Render Uuid::from_string(...).value() ."""
        return f'{self.uuid_cpp_type.render("")}::from_string("{self.uuid}").value()'


def get_template_params(context: CompilerContext, params: dict[str, CogParameter]) -> list[CppTemplateParam]:
    """Get template parameters for the cog parameters."""
    if params:
        return [
            CppTemplateParam(CppTypeArg(param.param_name), None)
            if param.get_typeval() is clkbuiltins.TYPE_TYPE
            else CppTemplateParam(CppNamedType(get_cpp_type(context, param.get_typeval()), param.param_name))
            for _, param in params.items()
        ]
    return []


def get_template_args(params: dict[str, CogParameter]) -> list[CppTypeExpr | CppValueExpr]:
    """Get template arguments for the cog parameters."""
    if params:
        return [CppValue(None, param.param_name) for _, param in params.items()]
    return []


def get_cog_instantiation_args(instantiated_cog: cog.InstantiatedCog) -> list[CppType | CppValue]:
    """Get a string with the instantiation arguments for an instantiated cog."""
    assert instantiated_cog.cog_ir.is_generic()
    args = []
    for param_name in instantiated_cog.cog_ir.parameters:
        arg = instantiated_cog.instantiation.arguments[param_name]
        match arg:
            case primitive.StringValue():
                args.append(CppValue(value_type=None, value=f'"{arg.value}"'))
            case primitive.DecimalValue():
                args.append(CppValue(value_type=None, value=str(arg.value)))
            case typesys.TypeVal():
                # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                args.append(get_cpp_type(instantiated_cog.module.context, arg))
            case _:
                msg = f"Unsupported cog parameter type for argument {param_name}: {type(arg)}"
                raise TypeError(msg)
    # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return args


def get_rendered_cog_instantiation_args(instantiated_cog: cog.InstantiatedCog, cpp_namespace: str) -> str:
    """Get a string with the instantiation arguments for an instantiated cog."""
    args = [arg.render(cpp_namespace) for arg in get_cog_instantiation_args(instantiated_cog)]
    return ", ".join(args)
