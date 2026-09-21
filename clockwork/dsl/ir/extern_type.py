# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Manage externed Cpp state type."""

from __future__ import annotations

from dataclasses import dataclass, field

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import clkbuiltins, expr, node, typesys
from clockwork.dsl.ir.cst_util import get_span


@dataclass
class ExternType(node.CstNode[cst.ExternType], node.DocRequiredEntity, typesys.TypeDef):
    """Manage generic extern type.

    Note: more information about the type should be specified in a cpp_target.extern definition.
    """

    attributes: node.ClkAttributes | None = field(repr=False)
    serialized_form: typesys.Instantiation | expr.Expr | None = field(default=None, repr=False)

    @classmethod
    def from_cst(cls: type[ExternType], cst_node: cst.ExternType, module: node.Module) -> ExternType:
        """Construct ExternState IR node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        attributes = module.handle_outer_attrs(cst_node.maybe_clk_outer_attrs())
        serialized_form = None
        if (extern_type_body := cst_node.maybe_extern_type_body()) and (
            serialized_form_cst := extern_type_body.maybe_serialized_form()
        ):
            serialized_form_representation = serialized_form_cst.child_serialized_form_representation()
            serialized_form = expr.Expr.from_cst(serialized_form_representation.child_typespec(), module)
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
            serialized_form=serialized_form,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        if self.serialized_form is None:
            return
        if not isinstance(self.serialized_form, expr.Expr):
            msg = f"Attempt to resolve serialized form twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        serialized_form = self.serialized_form.evaluate()
        if (
            not isinstance(serialized_form, typesys.Instantiation)
            or serialized_form.instantiates is not clkbuiltins.TACHYON
        ):
            msg = self.serialized_form.append_error_line("Serialized form must be a concrete Tachyon<Schema> type")
            raise TypeError(msg)
        schema_arg = serialized_form.arguments.get("schema")
        if isinstance(schema_arg, typesys.Instantiation):
            schema_arg = schema_arg.instantiates
        if not isinstance(schema_arg, typesys.SchemaType):
            msg = self.serialized_form.append_error_line("Serialized form must be a concrete Tachyon<Schema> type")
            raise TypeError(msg)
        self.serialized_form = serialized_form
