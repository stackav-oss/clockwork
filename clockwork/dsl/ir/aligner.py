# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Aligner IR nodes.

Aligners are DSL entities that declaratively specify how to select and group messages
from multiple input channels based on temporal constraints and optimization objectives.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    cog_components,
    dfl,
    dfl_types,
    expr,
    node,
    primitive,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span
from clockwork.dsl.ir.message_type import MessageTypeMixin, resolve_schema_interface
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir import cog, interface, representation, schema
    from fltk.fegen.pyrt.span_protocol import SpanProtocol


@dataclass
class AlignerInputDef(
    typesys.NamedAttribute,
    typesys.MembershipEntity,
    node.DocableEntity,
    node.CstNode[cst.AlignerInputDef],
    MessageTypeMixin,
):
    """An aligner input definition (unresolved).

    Implements ``MembershipEntity`` to support DFL member access (e.g.,
    ``lidar.observation_time``). Field lookup delegates to the message type's
    ``InstantiatedSchema`` once the interface has been resolved.

    Attributes:
        doc: Optional documentation.
        optional_expr: Expression for optional flag
        timeout_expr: Timeout duration expression
        reuse_expr: Expression for reuse flag
        batch_size_lo_expr: Expression for minimum batch size
        batch_size_hi_expr: Expression for maximum batch size
    """

    view_params: cog_components.ViewParams
    optional_expr: expr.Expr | None = None
    timeout_expr: expr.Expr | None = None
    reuse_expr: expr.Expr | None = None
    batch_size_lo_expr: expr.Expr | None = None
    batch_size_hi_expr: expr.Expr | None = None
    arbitrary_selection_expr: expr.Expr | None = None

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a schema field by name for DFL member access.

        Delegates to the message type's ``InstantiatedSchema``.
        Requires the message type to have been resolved to ``InterfaceInfo``
        (via ``resolve_schema_interface``) before this is called.

        Returns ``None`` if the interface is not yet resolved or the field is not found.
        """
        if not isinstance(self.message_type, schema_reg.InterfaceInfo):
            return None
        return self.get_instantiated_schema().attribute(name)

    @classmethod
    def from_cst(  # noqa: C901, PLR0912, PLR0915 # Inherent complexity from many match arms (one per param type)
        cls: type[AlignerInputDef],
        cst_node: cst.AlignerInputDef,
        module: node.Module,
        scope: node.Scope,
    ) -> AlignerInputDef:
        """Create an AlignerInputDef from a CST node.

        Args:
            cst_node: The CST node for the input definition.
            module: The module containing this input.
            scope: The scope to define the input in.

        Returns:
            The unresolved AlignerInputDef.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        message_type = expr.Expr.from_cst(cst_node.child_input_type(), module)
        typesys.unify(clkbuiltins.TYPE_TYPE, message_type.type_info)

        view_params = cog_components.ViewParams.make_default(module)
        optional_expr: expr.Expr | None = None
        timeout_expr: expr.Expr | None = None
        reuse_expr: expr.Expr | None = None
        batch_size_lo_expr: expr.Expr | None = None
        batch_size_hi_expr: expr.Expr | None = None
        arbitrary_selection_expr: expr.Expr | None = None

        seen_params: set[str] = set()
        if (options_block := cst_node.maybe_block()) is not None:
            for statement_cst in options_block.children_statement():
                param = statement_cst.child_definition()
                # Delegate to ViewParams for shared cog input parameters
                if view_params.try_handle_param(param, seen_params):
                    continue

                param_name = get_span(param.child_name().child_value(), module.terminals)
                if param_name in seen_params:
                    msg = f"Parameter '{param_name}' specified more than once:\n"
                    msg += format_line_with_error(
                        param.child_name().child_value(),
                        module.terminals,
                        module.module_id,
                    )
                    raise ValueError(msg)
                seen_params.add(param_name)

                match param_name:
                    case "optional":
                        optional_expr = expr.Expr.from_cst(param.child_value(), module)
                    case "timeout":
                        timeout_expr = expr.Expr.from_cst(param.child_value(), module)
                    case "reuse":
                        reuse_expr = expr.Expr.from_cst(param.child_value(), module)
                    case "batch_size":
                        batch_size_cst = param.child_value()
                        if (list_cst := batch_size_cst.maybe_list()) is not None:
                            elements = list(list_cst.children_element())
                            expected_element_count = 2
                            if len(elements) != expected_element_count:
                                msg = "batch_size must be a list [lo, hi] with exactly 2 elements"
                                msg += format_line_with_error(param.span, module.terminals, module.module_id)
                                raise ValueError(msg)
                        else:
                            msg = "batch_size must be a list [lo, hi]"
                            msg += format_line_with_error(param.span, module.terminals, module.module_id)
                            raise ValueError(msg)
                        batch_size_lo_expr = expr.Expr.from_cst(elements[0], module)
                        batch_size_hi_expr = expr.Expr.from_cst(elements[1], module)
                    case "arbitrary_selection":
                        arbitrary_selection_expr = expr.Expr.from_cst(param.child_value(), module)
                    case _:
                        msg = f"Unknown aligner input parameter: {param_name}"
                        msg += format_line_with_error(param.span, module.terminals, module.module_id)
                        raise ValueError(msg)

        if isinstance(view_params.manual_cursor, expr.Expr):
            msg = view_params.manual_cursor.append_error_line(
                "Aligner inputs require manual_cursor; setting it explicitly is not allowed."
            )
            raise ValueError(msg) from None  # noqa: TRY004 # ValueError is correct for user error
        view_params.manual_cursor = True
        view_params.expose_seqno = True

        is_batch = batch_size_lo_expr is not None
        input_type_info: typesys.TypeVal = (
            dfl_types.CollectionType(clkbuiltins.ALIGNER_INPUT_TYPE) if is_batch else clkbuiltins.ALIGNER_INPUT_TYPE
        )

        # fmt: off
        return cls(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            doc=doc,
            type_info=input_type_info,
            message_type=message_type,
            view_params=view_params,
            optional_expr=optional_expr,
            timeout_expr=timeout_expr,
            reuse_expr=reuse_expr,
            batch_size_lo_expr=batch_size_lo_expr,
            batch_size_hi_expr=batch_size_hi_expr,
            arbitrary_selection_expr=arbitrary_selection_expr,
            module=module,
            cst_node=cst_node,
        )
        # fmt: on

    def resolve(self) -> ResolvedAlignerInput:
        """Resolve this input definition to a ResolvedAlignerInput.

        Returns:
            The resolved input.

        Raises:
            TypeError: If type checking fails.
            ValueError: If validation fails.
        """
        interface_info = resolve_schema_interface(self.module.context, self.message_type)
        self.message_type = interface_info

        self.view_params.resolve()

        optional = self._resolve_optional()

        timeout = self._resolve_timeout(optional)

        reuse = self._resolve_reuse()

        batch_size = self._resolve_batch_size()

        arbitrary_selection = self._resolve_arbitrary_selection()

        self._validate_max_msgs_explicit_and_batch(batch_size)

        return ResolvedAlignerInput(
            name=self.name,
            doc=self.doc,
            interface_info=interface_info,
            view_params=self.view_params,
            optional=optional,
            timeout=timeout,
            reuse=reuse,
            batch_size=batch_size,
            arbitrary_selection=arbitrary_selection,
        )

    def _resolve_optional(self) -> bool:
        """Resolve the optional flag."""
        if self.optional_expr is None:
            return False
        opt_val = self.optional_expr.evaluate()
        if opt_val is not clkbuiltins.TRUE_VALUE and opt_val is not clkbuiltins.FALSE_VALUE:
            msg = "optional must be a boolean"
            raise TypeError(msg)
        return opt_val is clkbuiltins.TRUE_VALUE

    def _resolve_timeout(self, optional: bool) -> primitive.UnitValue | None:
        """Resolve the timeout value."""
        if self.timeout_expr is not None:
            timeout_val = self.timeout_expr.evaluate()
            if not isinstance(timeout_val, primitive.UnitValue):
                msg = "timeout must be a duration"
                raise TypeError(msg)
            return timeout_val
        if optional:
            msg = f"Optional input '{self.name}' requires a timeout"
            if self.module.terminals is not None and self.cst_node is not None:
                msg += format_line_with_error(
                    self.cst_node.span,
                    self.module.terminals,
                    self.module.module_id,
                )
            raise ValueError(msg)
        return None

    def _resolve_reuse(self) -> bool:
        """Resolve the reuse flag."""
        if self.reuse_expr is None:
            return False
        reuse_val = self.reuse_expr.evaluate()
        if reuse_val is not clkbuiltins.TRUE_VALUE and reuse_val is not clkbuiltins.FALSE_VALUE:
            msg = "reuse must be a boolean"
            raise TypeError(msg)
        return reuse_val is clkbuiltins.TRUE_VALUE

    def _resolve_batch_size(self) -> tuple[int, int] | None:
        """Resolve the batch_size value."""
        if self.batch_size_lo_expr is None or self.batch_size_hi_expr is None:
            return None
        lo_val = self.batch_size_lo_expr.evaluate()
        hi_val = self.batch_size_hi_expr.evaluate()
        if not isinstance(lo_val, primitive.DecimalValue) or not isinstance(hi_val, primitive.DecimalValue):
            msg = "batch_size values must be integers"
            raise TypeError(msg)
        batch_size = (int(lo_val.value), int(hi_val.value))
        if batch_size[0] < 1 or batch_size[1] < batch_size[0]:
            msg = f"batch_size must satisfy 1 <= lo <= hi, got [{batch_size[0]}, {batch_size[1]}]"
            raise ValueError(msg)
        return batch_size

    def _resolve_arbitrary_selection(self) -> bool:
        """Resolve the arbitrary_selection flag."""
        if self.arbitrary_selection_expr is None:
            return False
        val = self.arbitrary_selection_expr.evaluate()
        if val is not clkbuiltins.TRUE_VALUE and val is not clkbuiltins.FALSE_VALUE:
            msg = "arbitrary_selection must be a boolean"
            raise TypeError(msg)
        return val is clkbuiltins.TRUE_VALUE

    def _validate_max_msgs_explicit_and_batch(self, batch_size: tuple[int, int] | None) -> None:
        """Require explicit ``max_msgs`` and ensure the largest batch fits in the view.

        Rationale: a silent default of ``max_msgs = 1`` combined with a
        ``batch_size`` is the original alignment footgun. Forcing users to
        spell out ``max_msgs`` keeps the sizing decision deliberate, and
        rejecting ``max_msgs < max(batch_size)`` prevents the largest batch
        from fitting in the view.
        """
        assert isinstance(self.view_params.max_msgs, int)
        if not self.view_params.is_user_set("max_msgs"):
            msg = (
                f"Aligner input '{self.name}' must specify 'max_msgs' explicitly. "
                "There is no safe default; pick a value large enough for the expected "
                "arrival rate and (if set) 'batch_size'."
            )
            if self.module.terminals is not None and self.cst_node is not None:
                msg += format_line_with_error(
                    self.cst_node.span,
                    self.module.terminals,
                    self.module.module_id,
                )
            raise ValueError(msg)
        if batch_size is not None:
            max_batch = batch_size[1]
            if self.view_params.max_msgs < max_batch:
                msg = (
                    f"Aligner input '{self.name}' has max_msgs={self.view_params.max_msgs} "
                    f"< max(batch_size)={max_batch}. The view must be at least as large as "
                    "the largest batch; either raise 'max_msgs' or shrink 'batch_size'."
                )
                if self.module.terminals is not None and self.cst_node is not None:
                    msg += format_line_with_error(
                        self.cst_node.span,
                        self.module.terminals,
                        self.module.module_id,
                    )
                raise ValueError(msg)


@dataclass
class AlignerLetBinding(node.NamedEntity):
    """A let binding in the aligner body.

    Attributes:
        value: The DFL expression to bind.
        span: Source location.
        type_info: Inferred type after type checking (None until resolved).
    """

    value: dfl.Expr
    span: SpanProtocol
    type_info: typesys.TypeVal | typesys.InferenceVar | None = None


@dataclass
class AlignerSpecStmt:
    """A specification expression statement in the aligner body.

    Yields a Spec value (require/minimize/maximize or composed via 'and').

    Attributes:
        expr: The DFL expression (unexpanded).
        expanded_expr: The fully expanded expression, populated by type checking.
    """

    expr: dfl.Expr
    expanded_expr: dfl.Expr | None = None


AlignerBodyStmt = AlignerLetBinding | dfl.FnDef | AlignerSpecStmt


@dataclass(frozen=True, slots=True)
class ResolvedAlignerInput:
    """A resolved aligner input with evaluated types.

    Attributes:
        name: Input name.
        doc: Optional documentation.
        interface_info: Resolved message interface information.
        view_params: Resolved view parameters shared with cog inputs.
        optional: Whether this input is optional.
        timeout: Resolved timeout duration (if optional).
        reuse: Whether messages can be reused.
        batch_size: Tuple of (min, max) batch size.
        arbitrary_selection: Whether the user has opted into arbitrary
            solver selection for this input (suppresses validation errors
            from the objective-coverage pass).
    """

    name: str
    doc: node.Doc | None
    interface_info: schema_reg.InterfaceInfo
    view_params: cog_components.ViewParams
    optional: bool
    timeout: primitive.UnitValue | None
    reuse: bool
    batch_size: tuple[int, int] | None
    arbitrary_selection: bool = False


@dataclass(frozen=True, slots=True)
class ResolvedAligner:
    """A resolved aligner definition.

    Attributes:
        name: Aligner name.
        doc: Documentation.
        inputs: Ordered dict of resolved inputs.
        body_stmts: Body statements
        body_scope: Scope for the body.
        source: The unresolved Aligner this was resolved from.
    """

    name: str
    doc: node.Doc
    inputs: dict[str, ResolvedAlignerInput]
    body_stmts: tuple[AlignerBodyStmt, ...]
    body_scope: node.Scope
    source: Aligner


@dataclass
class Aligner(typesys.TypeDef, node.DocRequiredEntity, node.CstNode[cst.Aligner], typesys.InstantiatableEntity):
    """IR Node representing an aligner definition (unresolved).

    Attributes:
        inner_scope: Scope for input names (used in body).
        inputs: Ordered dict of input definitions.
        body_stmts: Sequence of body statements (let bindings, inline fns, specs).
        body_scope: Scope for the body (inherits inner_scope, adds let bindings and fns).
        attributes: Optional outer attributes.
        resolved: Resolved version, or None if not yet resolved.
    """

    inner_scope: node.Scope
    inputs: dict[str, AlignerInputDef]
    body_stmts: tuple[AlignerBodyStmt, ...]
    body_scope: node.Scope
    attributes: node.ClkAttributes | None = None
    resolved: ResolvedAligner | None = field(default=None, repr=False)
    synthetic_cog: cog.Cog | None = field(default=None, repr=False)

    # Generated alignment output artifacts, set by make_aligner_cog().
    # Stored here for injection into CppTarget during compilation.
    alignment_schema: schema.InstantiatedSchema | None = field(default=None, repr=False)
    alignment_repr: representation.ResolvedReprInstantiation | None = field(default=None, repr=False)
    alignment_iface: interface.InterfaceInstantiation | None = field(default=None, repr=False)

    def name_resolution_fields(self) -> tuple[str, ...]:
        """Return fields for standard IR name resolution.

        Excludes body_stmts because DFL expressions use frozen Ref nodes with
        deferred lookup rather than in-place mutation during name resolution.
        """
        return ("inputs",)

    @override
    def concrete_type_info(self) -> typesys.TypeVal | typesys.InferenceVar:
        """Return ALIGNER_TYPE for policy binding purposes."""
        return clkbuiltins.ALIGNER_TYPE

    @classmethod
    def from_cst(
        cls: type[Aligner],
        cst_aligner: cst.Aligner,
        module: node.Module,
        parent_scope: node.Scope,
    ) -> Aligner:
        """Create unresolved Aligner from CST.

        Args:
            cst_aligner: The CST node for the aligner definition.
            module: The module containing this aligner.
            parent_scope: The scope to define the aligner in.

        Returns:
            The unresolved Aligner.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        name = get_span(cst_aligner.child_identifier().child_value(), module.terminals)
        doc = node.Doc.from_cst(cst_aligner.child_doc(), module)
        attributes = module.handle_outer_attrs(cst_aligner.maybe_clk_outer_attrs())

        inner_scope = parent_scope.make_child_scope(name)

        inputs: dict[str, AlignerInputDef] = {}
        inputs_block = cst_aligner.child_aligner_inputs_block()
        for input_cst in inputs_block.children_aligner_input_def():
            input_def = AlignerInputDef.from_cst(input_cst, module, inner_scope)
            inner_scope.define(input_def.name, input_def, module.terminals)
            inputs[input_def.name] = input_def

        body_scope = node.Scope(
            parent=inner_scope,
            uniq_path=f"{inner_scope.uniq_path}.body",
            module_id_for_errors=module.module_id,
        )

        dfl_ctx = dfl.Context(scope=body_scope, terminals=module.terminals, module_id=module.module_id)

        body_stmts: list[AlignerBodyStmt] = []
        body_cst = cst_aligner.child_aligner_body()
        for stmt_cst in body_cst.children_aligner_body_stmt():
            if (let_cst := stmt_cst.maybe_aligner_let_stmt()) is not None:
                let_name = get_span(let_cst.child_identifier().child_value(), module.terminals)
                let_value = dfl.expr_from_cst(let_cst.child_dfl_expr(), dfl_ctx, module)
                let_binding = AlignerLetBinding(
                    name=let_name,
                    scope=body_scope,
                    value=let_value,
                    span=let_cst.span,
                )
                body_scope.define(let_name, let_binding, module.terminals)
                body_stmts.append(let_binding)
            elif (fn_cst := stmt_cst.maybe_dfl_fn_def()) is not None:
                fn_def = dfl.FnDef.from_cst(fn_cst, module, body_scope)
                body_scope.define(fn_def.name, fn_def, module.terminals)
                body_stmts.append(fn_def)
            elif (spec_cst := stmt_cst.maybe_aligner_spec_stmt()) is not None:
                spec_expr = dfl.expr_from_cst(spec_cst.child_dfl_expr(), dfl_ctx, module)
                spec_stmt = AlignerSpecStmt(expr=spec_expr)
                body_stmts.append(spec_stmt)
            else:
                msg = node.append_error_line(stmt_cst, module, "Unknown aligner body statement type")
                raise ValueError(msg)

        return cls(
            name=name,
            scope=parent_scope,
            doc=doc,
            type_info=clkbuiltins.TYPE_TYPE,
            inner_scope=inner_scope,
            inputs=inputs,
            body_stmts=tuple(body_stmts),
            body_scope=body_scope,
            attributes=attributes,
            module=module,
            cst_node=cst_aligner,
        )

    def resolve(self) -> ResolvedAligner:
        """Resolve input type expressions."""
        if self.resolved is not None:
            return self.resolved

        resolved_inputs: dict[str, ResolvedAlignerInput] = {}
        for input_def in self.inputs.values():
            resolved_inputs[input_def.name] = input_def.resolve()

        self.resolved = ResolvedAligner(
            name=self.name,
            doc=self.doc,
            inputs=resolved_inputs,
            body_stmts=self.body_stmts,
            body_scope=self.body_scope,
            source=self,
        )

        return self.resolved

    def get_resolved(self) -> ResolvedAligner:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    @override
    def get_module(self) -> node.Module:
        """Return the module containing this aligner."""
        return self.module

    @override
    def make_instance(
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> node.NamedEntity:
        """Create an instance by delegating to the synthetic cog.

        The aligner compiler creates a synthetic ``cog.Cog`` that represents
        the aligner's runtime behavior. Box definitions instantiate the
        aligner by name (``new aligner: MyAligner;``), and this method
        delegates to the synthetic cog's ``make_instance``.
        """
        if self.synthetic_cog is None:
            msg = self.append_error_line(
                f"Aligner '{self.name}' has no synthetic cog. "
                + "Ensure the aligner module has a cpp_target or #![generate(cpp)] attribute."
            )
            raise RuntimeError(msg)
        return self.synthetic_cog.make_instance(
            cst_node=cst_node,
            module=module,
            source_module=source_module,
            scope=scope,
            name=name,
            doc=doc,
        )


def get_aligned_input_interfaces(
    aligned_input: cog_components.CogAlignedInputDef,
) -> list[schema_reg.InterfaceInfo]:
    """Expand a CogAlignedInputDef into the N+1 interface infos it implies.

    Returns one interface for the alignment message, plus one for each of
    the aligner's upstream input schemas.

    Args:
        aligned_input: A resolved CogAlignedInputDef.

    Returns:
        List of InterfaceInfo: [alignment_msg, upstream_1, ..., upstream_N].
    """
    aligner_type = aligned_input.aligned_type
    if not isinstance(aligner_type, Aligner):
        msg = f"Expected Aligner, got {type(aligner_type).__name__}"
        raise TypeError(msg)

    assert aligner_type.alignment_iface is not None, (
        f"Aligner '{aligner_type.name}' has no alignment interface. "
        "Ensure the aligner module is compiled with cpp generation enabled."
    )

    interfaces: list[schema_reg.InterfaceInfo] = [schema_reg.InterfaceInfo.make(aligner_type.alignment_iface)]

    interfaces.extend(aligner_input.get_interface_info() for aligner_input in aligner_type.inputs.values())

    return interfaces
