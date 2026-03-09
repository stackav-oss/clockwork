# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CppTarget-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import box, clkbuiltins, expr, node, signal_policy_validation, typesys
from clockwork.dsl.ir.cst_util import get_span


@dataclass
class SystemTarget(node.CstNode[cst.SystemTarget], node.DocableEntity, typesys.NamedValue):
    """IR Node representing a system_target declaration."""

    box_instance: box.ResolvedBox
    source: UnresolvedSystemTarget | None
    require_logging_policies: bool


@dataclass
class UnresolvedSystemTarget(node.CstNode[cst.SystemTarget], node.DocableEntity, typesys.NamedValue):
    """IR Node representing a system_target declaration."""

    box_expr: expr.Expr
    resolved: SystemTarget | None
    require_logging_policies: bool

    @classmethod
    def from_cst(
        cls: type[UnresolvedSystemTarget], cst_node: cst.SystemTarget, module: node.Module
    ) -> UnresolvedSystemTarget:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        box_expr = expr.Expr.from_cst(cst_node.child_system_box().child_typespec(), module)
        require_logging_policies: bool = False
        if system_options_block_cst := cst_node.maybe_system_options_block():
            for system_option_cst in system_options_block_cst.children_system_option():
                if maybe_require_logging_policies_cst := system_option_cst.maybe_require_logging_policies():
                    require_logging_policies_value = maybe_require_logging_policies_cst.child_boolean()
                    require_logging_policies = require_logging_policies_value.maybe_true() is not None
        typesys.unify(box_expr.type_info, clkbuiltins.TYPE_TYPE)
        return cls(
            name=name,
            scope=module.inner_scope,
            type_info=clkbuiltins.SYSTEM_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            box_expr=box_expr,
            resolved=None,
            require_logging_policies=require_logging_policies,
        )

    def resolve(self) -> SystemTarget:
        """Perform finalization of the IR."""
        if self.resolved:
            msg = f"Attempt to resolve twice: {self}"
            raise RuntimeError(msg)
        box_template = self.box_expr.evaluate()
        if not isinstance(box_template, box.BoxTemplate):
            msg = self.box_expr.append_error_line(f"Expected a BoxTemplate type but received {type(box_template)}")
            raise TypeError(msg)
        box_instance = box_template.make_instance(
            cst_node=None, module=self.module, scope=self.scope, name=self.name, doc=self.doc
        )

        signal_policy_validation.validate_signal_policies(self.module)
        self.resolved = SystemTarget(
            name=self.name,
            scope=self.scope,
            type_info=self.type_info,
            doc=self.doc,
            module=self.module,
            cst_node=self.cst_node,
            box_instance=box_instance.get_resolved(),
            source=self,
            require_logging_policies=self.require_logging_policies,
        )
        return self.resolved

    def get_resolved(self) -> SystemTarget:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved
