# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Diagnostics entity."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.cpp import types
from clockwork.dsl.cpp.context import Header, MaybeHeader
from clockwork.dsl.ir import (
    clkbuiltins,
    expr,
    node,
    primitive,
    typesys,
    uuid_reg,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span
from clockwork.dsl.ir.message_type import MessageTypeMixin, resolve_schema_interface
from clockwork.dsl.ir.module_id import CLK_REPO

if TYPE_CHECKING:
    from collections.abc import Iterable

    from clockwork.dsl.ir.cog_components import InputDef, OutputDef


@dataclass
class DiagnosticsSignalDef:
    """Information needed for a infra-defined signal."""

    name: str
    type: str
    detector: str
    fault_id: int


@dataclass
class DiagnosticsDef(
    typesys.NamedAttribute,
    node.DocableEntity,
    node.CstNode[cst.DiagnosticsBlock | cst.DiagnosticsDef],
    MessageTypeMixin,
):
    """A definition of the Cog's diagnostics."""

    group_id: str | expr.Expr
    instance_id: str | expr.Expr | None

    @classmethod
    def from_cst(
        cls: type[DiagnosticsDef],
        cst_node: cst.DiagnosticsBlock | cst.DiagnosticsDef,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> DiagnosticsDef:
        """Construct an DiagnosticsDef IR node from a CST DiagnosticsBlock node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        if isinstance(cst_node, cst.DiagnosticsDef):
            name = get_span(cst_node.child_name().child_value(), terminals=module.terminals)
        else:
            name = "diagnostics"
        if parent_scope.lookup("DiagnosticsReport") is None:
            msg = 'Could not find DiagnosticsReport type, did you "use @clockwork::clockwork::diagnostics::report::Report as DiagnosticsReport"?'
            raise TypeError(msg)
        message_type = expr.Expr.from_str("Tap<Tachyon<DiagnosticsReport>>", module)
        result = cls(
            module=module,
            cst_node=cst_node,
            doc=None,
            scope=parent_scope,
            name=name,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            message_type=message_type,
            group_id="",
            instance_id=None,
        )
        parent_scope.define(name, result, module.terminals)
        seen_params: set[str] = set()
        for param_cst in cst_node.children_diagnostics_param():
            result._handle_param(param_cst, seen_params)
        if not result.group_id:
            msg = "Parameter 'group_id' not specified:\n" + format_line_with_error(
                cst_node.span,
                module.terminals,
                module.module_id,
            )
            raise ValueError(msg)
        return result

    def _handle_param(self, cst_node: cst.DiagnosticsParam, seen_params: set[str]) -> None:
        assert self.module.terminals is not None
        name = get_span(name_span := cst_node.child_param().child_value(), self.module.terminals)
        if name in seen_params:
            msg = f"Parameter '{name}' specified more than once:\n" + format_line_with_error(
                name_span, self.module.terminals, self.module.module_id
            )
            raise ValueError(msg)
        seen_params.add(name)
        value = expr.Expr.from_cst(cst_node.child_value(), self.module)
        if name == "group_id":
            typesys.unify(clkbuiltins.STRING, value.type_info)
            self.group_id = value
            return
        if name == "instance_id":
            typesys.unify(clkbuiltins.STRING, value.type_info)
            self.instance_id = value
            return
        msg = f"Unsupported view parameter '{name}'"
        raise NotImplementedError(msg)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if isinstance(self.message_type, expr.Expr):
            self.message_type = resolve_schema_interface(self.module.context, self.message_type)
        if isinstance(self.group_id, expr.Expr):
            result = self.group_id.evaluate()
            if not isinstance(result, primitive.StringLiteral):
                msg = self.group_id.append_error_line(
                    f"Expected a string for parameter 'group_id', but got {type(result)}",
                )
                raise TypeError(msg)
            self.group_id = result.value
        if isinstance(self.instance_id, expr.Expr):
            result = self.instance_id.evaluate()
            if not isinstance(result, primitive.StringLiteral):
                msg = self.instance_id.append_error_line(
                    f"Expected a string for parameter 'instance_id', but got {type(result)}",
                )
                raise TypeError(msg)
            self.instance_id = result.value


@dataclass
class InfraDiagnosticsDef(typesys.NamedAttribute):
    """Class for infra diagnostics."""

    inputs: Iterable[InputDef]
    outputs: Iterable[OutputDef]
    signals: list[DiagnosticsSignalDef] | None

    @classmethod
    def make(
        cls: type[InfraDiagnosticsDef],
        module: node.Module,
        parent_scope: node.Scope,
        inputs: Iterable[InputDef],
        outputs: Iterable[OutputDef],
    ) -> InfraDiagnosticsDef:
        """Make infra diagnostics."""
        result = cls(
            name=COG_INFRA_DIAGS_GROUP_NAME,
            scope=parent_scope,
            type_info=clkbuiltins.COG_CONFIG_TYPE,
            inputs=list(inputs),
            outputs=list(outputs),
            signals=None,
        )
        uuid_reg.register_entity_with_stable_key(module.context, result)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for i in (i for j in (self.inputs, self.outputs) for i in j):
            i.resolve()
        self.signals = []


@dataclass
class DiagnosticsInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """An instantiation of a diagnostics source."""

    diagnostics: DiagnosticsDef
    inner_scope: node.Scope

    # We have to suppress PLR0913 (too many args) because this is already an extremely simple function
    # that can't be split but still needs all these args. The args are all different types so mypy will
    # catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
    @classmethod
    def make(  # noqa: PLR0913 (see above)
        cls: type[DiagnosticsInstance],
        *,
        diagnostics: DiagnosticsDef,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> DiagnosticsInstance:
        """Factory function for DiagnosticsInstance."""
        inner_scope = scope.make_child_scope(name)
        return cls(
            name=name,
            scope=scope,
            inner_scope=inner_scope,
            type_info=clkbuiltins.DIAGNOSTICS_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            diagnostics=diagnostics,
        )


REPORT_DEFS_HEADER: Final = Header(CLK_REPO, "clockwork/diagnostics/report_definitions.hh")

IMPL_HEADER: Final = Header(CLK_REPO, "clockwork/diagnostics/reporter.hh")

# This redirects to the common shim, since all cog now nominally need this
MANAGER_IMPL_HEADER: Final = Header(CLK_REPO, "clockwork/cog/include_common.hh")

NAMESPACE: Final = "clockwork::diagnostics"

COG_INFRA_DIAGS_GROUP_NAME: Final = "cog_infra_diagnostics"

COG_INFRA_DIAGS_GROUP_DEF_NAME: Final = "CogInfraDiagnosticsSignalDefs"

SIGNAL_GROUP_ID: Final = types.CppType(
    includes=[REPORT_DEFS_HEADER],
    type_name="SignalGroupId",
    cpp_namespace=NAMESPACE,
)

SIGNAL_GROUP_TEMPLATE: Final = types.CppTemplate(
    includes=[REPORT_DEFS_HEADER],
    template_name="SignalGroup",
    cpp_namespace=NAMESPACE,
)


def group_id(group_id_name: str) -> types.CppScopedType:
    """Convert name of group id to c++ group id enum value."""
    return types.CppScopedType(
        scope=SIGNAL_GROUP_ID,
        header=[REPORT_DEFS_HEADER],
        name=group_id_name,
    )


def group_type(group_id_enum: types.CppScopedType) -> types.CppTemplateType:
    """Convert group id enum to c++ group type."""
    return SIGNAL_GROUP_TEMPLATE.instantiate([group_id_enum])


def group_instance_id_type(group_type: types.CppTypeExpr) -> types.CppScopedType:
    """Get nested instance type for signal group type."""
    return types.CppScopedType(
        scope=group_type,
        header=[REPORT_DEFS_HEADER],
        name="InstanceType",
    )


def instance_id(instance_id_type: types.CppTypeExpr, instance_id_name: str) -> types.CppScopedValue:
    """Get instance id enum value."""
    return types.CppScopedValue(
        scope=instance_id_type,
        header=[REPORT_DEFS_HEADER],
        name=instance_id_name,
    )


def to_instance_id(group_id_name: str, instance_id_name: str) -> types.CppScopedValue:
    """Convert group id and instance id names to an instance id enum value."""
    return instance_id(group_instance_id_type(group_type(group_id(group_id_name))), instance_id_name)


def infra_defs_header_from_dial_header(dial_header: types.Header) -> MaybeHeader:
    """Generates a MaybeHeader for the expected infrastructure diagnostic fault thresholds based on the dial_header."""
    return MaybeHeader(
        dial_header.repo, dial_header.path.with_name(dial_header.path.name.replace("_dial.hh", "_diags.hh"))
    )
