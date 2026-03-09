# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Manage externed Cpp state type."""

from __future__ import annotations

from dataclasses import dataclass, field

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import clkbuiltins, node, typesys
from clockwork.dsl.ir.cst_util import get_span


@dataclass
class ExternType(node.CstNode[cst.ExternType], node.DocRequiredEntity, typesys.TypeDef):
    """Manage generic extern type.

    Note: more information about the type should be specified in a cpp_target.extern definition.
    """

    attributes: node.ClkAttributes | None = field(repr=False)

    @classmethod
    def from_cst(cls: type[ExternType], cst_node: cst.ExternType, module: node.Module) -> ExternType:
        """Construct ExternState IR node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        attributes = module.handle_outer_attrs(cst_node.maybe_clk_outer_attrs())
        if module.generates is not None:
            assert attributes is not None
            if attributes.get_cpp_type_header() is None:
                msg = node.append_error_line(cst_node, module, "cpp type_header attribute is not set")
                raise ValueError(msg)

        return ExternType(
            doc=doc,
            name=name,
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            attributes=attributes,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
