# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR nodes for Cog parameters and parameter references."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import (
    expr,
    node,
    typesys,
)
from typing_extensions import override


@dataclass
class CogParameter(node.CstNode[cst.CogParameter], node.DocableEntity):
    """IR Node representing a cog parameter."""

    param_name: str
    type_info: typesys.TypeVal | expr.TypeExpression

    def get_typeval(self) -> typesys.TypeVal:
        """Get the referenced parameter type."""
        type_info = self.type_info
        if isinstance(type_info, expr.TypeExpression):
            type_info = type_info.evaluate()
        assert isinstance(type_info, typesys.TypeVal)
        return type_info


@dataclass
class CogParameterRef(node.NamedEntity, typesys.DeferrableType):
    """A reference to a generic cog parameter."""

    parameter_def: CogParameter

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        msg = f"Attempt to generate a value key for an unsubstituted cog parameter: {self}"
        raise RuntimeError(msg)
