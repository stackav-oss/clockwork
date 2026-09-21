## Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Box IR nodes."""

from __future__ import annotations

from dataclasses import dataclass, field
from decimal import Decimal
from enum import Enum
from pathlib import Path
from typing import TYPE_CHECKING, Final, TypeAlias

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import (
    aligner,
    audio,
    clkbuiltins,
    cog,
    cog_components,
    cpp_executable,
    dfl,
    dfl_analysis,
    dfl_types,
    diagnostics,
    expr,
    extern_type,
    fmt_string,
    node,
    policy,
    primitive,
    pubsub,
    schema,
    schema_reg,
    signal_registry,
    statement,
    typesys,
    udp,
)
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.signal import SignalInstanceSpec
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterator, Mapping, Sequence


CogInputMember: TypeAlias = (
    cog.CogInstanceMember[cog.InputDef] | cog.CogInstanceMember[cog_components.CogAlignedInputDef]
)


def _parse_parameters_block_for_template(
    parameters_block: cst.BoxParametersBlock | None, module: node.Module
) -> dict[str, typesys.TypeVal | expr.TypeExpression] | None:
    if module.terminals is None:
        msg = "Cannot construct IR nodes from CST without a TerminalSource"
        raise ValueError(msg)

    if not parameters_block:
        return None

    parameters = {}

    for cst_param in parameters_block.children_box_parameter():
        param_name = get_span(cst_param.child_name().child_value(), module.terminals)
        if param_name in parameters:
            msg = node.append_error_line(cst_param, module, f"Duplicate parameter name '{param_name}'")
            raise ValueError(msg)
        param_type = expr.TypeExpression.make(expr.Expr.from_cst(cst_param.child_typespec(), module))
        parameters[param_name] = param_type

    if not parameters:
        msg = node.append_error_line(parameters_block, module, "A box parameter block cannot be empty.")
        raise ValueError(msg)

    # pyrefly: ignore[bad-return] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    return parameters


@dataclass
class BoxTemplate(
    typesys.TypeDef,
    node.CstNode[cst.Box],
    node.DocableEntity,
    typesys.NamedValue,
    typesys.InstantiatableEntity,
    cpp_executable.CasingEntitySource,
    statement.InstantiatableEntityFactory,
    statement.InstantiationFactory,
):
    """IR Node representing a box declaration.

    Boxes can be separately declared and instantiated, like Cogs, but we treat
    them a bit differently.  This class is for the box definition, akin to the
    Cog class, and Box is for the instance, akin to CogInstance.  For Cogs,
    during Cog class construction we fully resolve everything inside the Cog;
    all of its endpoints, conditions, etc.  But for boxes this is trickier,
    because arbitrary box entities can refer to arbitrary other box entities in
    complicated ways.  If we did a full resolution of the box definition,
    including name resolution, we'd have to resolve all those names to
    something.  But that something doesn't actually exist until the box is
    instantiated.

    We could resolve the box definition into a graph of proxy objects, and then
    instantiating the box means walking that entire graph, which may have
    cycles, and creating a new graph with the same structure but where all the
    proxy objects are replaced with concrete instances.

    Instead of doing that, which is a lot of complexity, we simply leave the box
    template as a parsed but unresolved syntax tree (CST).  To instantiate the
    box, we walk that syntax tree and generate a resolved graph of instances
    from it.  It's a bit like macro expansion, except the macro is fully
    syntax-checked already.

    So this class really just holds an unresolved syntax tree plus a little bit
    of contextual information (like a scope and module) that are used to create
    the box instances later.
    """

    parameters: dict[str, typesys.TypeVal | expr.TypeExpression] | None

    @classmethod
    def from_cst(cls: type[BoxTemplate], cst_node: cst.Box, module: node.Module) -> BoxTemplate:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        parameters = _parse_parameters_block_for_template(cst_node.maybe_box_parameters_block(), module)
        return cls(
            name=name,
            scope=module.inner_scope,
            type_info=clkbuiltins.TYPE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            parameters=parameters,
        )

    def create_box_in_module(  # noqa: PLR0913 (Using kwargs to address number of argumets)
        self,
        *,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
        arguments: Mapping[str, typesys.Value] | None,
    ) -> Box:
        """Create and resolve a Box instance without registering signal instances.

        Signal registration is the responsibility of the caller when needed.
        """
        if self.cst_node is None:
            msg = self.append_error_line("Attempt to instantiate BoxTemplate without CST.")
            raise RuntimeError(msg)

        result = Box.from_cst(
            cst_node=self.cst_node,
            instance_module=module,
            template=self,
            lexical_scope=self.scope,
            instance_scope=scope,
            instance_name=name,
            doc=(doc or self.doc),
            arguments=arguments,
        )
        # Because we're delaying CST processing, the original name resolution
        # pass won't have done anything.  We need to do a separate name
        # resolution pass manually here, after we've processed the CST.
        node.resolve_names(result, self.scope)
        result.resolve(source_module or module)
        return result

    def is_generic(self) -> bool:
        """Return True if this box has parameters (is generic)."""
        return bool(self.parameters)

    @override
    def generic_parameters(self) -> Sequence[typesys.Parameter] | None:
        """Get the generic parameters for the box.

        Returns:
            The generic parameters, or None if the box is not generic.
        """
        if not self.parameters:
            return None
        params = []
        for param_name, param_type in sorted(self.parameters.items()):
            type_info = param_type.evaluate() if isinstance(param_type, expr.TypeExpression) else param_type
            assert isinstance(type_info, typesys.TypeVal)
            optional = isinstance(type_info, typesys.Instantiation) and type_info.instantiates is clkbuiltins.OPTIONAL
            params.append(
                typesys.Parameter(
                    name=param_name,
                    type_bound=type_info,
                    default=None,
                    is_optional=optional,
                )
            )
        return params

    # We have to suppress PLR0913 (too many args) because these args are needed to create objects.
    # We have made the args kwonly to minimize the risk of mixups.
    def make_system_target_instance(  # noqa: PLR0913 (see above)
        self,
        *,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
        system_target: node.CstNode[cst.SystemTarget],
    ) -> Box:
        """Create an instance of the entity for the top level system target box, registering all signal instances."""
        assert isinstance(system_target, typesys.NamedValue)
        arguments = {node.CURRENT_SYSTEM_TARGET_SCOPE_KEY: system_target}
        result = self.create_box_in_module(
            module=module, source_module=source_module, scope=scope, name=name, doc=doc, arguments=arguments
        )
        _register_box_signal_instances(result.module.context, result.get_resolved())
        return result

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
    ) -> Box:
        """Create an instance of the entity, registering all signal instances."""
        result = self.make_instance_for_resolution(
            cst_node=cst_node, module=module, source_module=source_module, scope=scope, name=name, doc=doc
        )
        _register_box_signal_instances(result.module.context, result.get_resolved())
        return result

    @override
    def make_instance_for_resolution(
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> Box:
        """Create a Box instance for use during outer box resolution.

        Unlike make_instance, this does not register signal instances.
        Registration is deferred to the outermost make_instance call, which
        walks the full resolved tree via _register_box_signal_instances.
        """
        if self.parameters:
            msg = node.append_error_line(
                self.cst_node,
                self.module,
                f"Cannot make an instance of a generic box missing {list(self.parameters.keys())}",
            )
            raise ValueError(msg)
        return self.create_box_in_module(
            module=module, source_module=source_module, scope=scope, name=name, doc=doc, arguments=None
        )

    @override
    def get_module(self) -> node.Module:
        return self.module

    @override
    def produce_casing_entities(self) -> cpp_executable.CasingEntities:
        """Produce a set of casing entities."""
        if self.parameters:
            msg = node.append_error_line(self.cst_node, self.module, "Cannot produce casing entities for a generic box")
            raise ValueError(msg)
        instance = self.create_box_in_module(
            module=self.module, scope=self.scope, name="__casing__" + self.name, doc=None, arguments=None
        )
        instance.get_resolved().validate_use_targets()
        return instance.get_resolved().produce_casing_entities()

    @override
    def make_from_new_stmt(
        self,
        *,
        new_stmt: statement.NewStmt,
    ) -> typesys.InstantiatableEntity:
        """Make an InstantiatabeEntity from a NewStmt."""
        if not self.parameters:
            msg = new_stmt.append_error_line("Cannot instantiate a generic box")
            raise ValueError(msg)
        return InstantiatedBoxFactory(
            cst_node=new_stmt.cst_node,
            module=new_stmt.module,
            new_stmt=new_stmt,
        )

    @override
    def make_from_instantiate_stmt(self, *, instantiate_stmt: statement.InstantiateStmt) -> InstantiatedBox:
        """Instantiate an entity from an InstantiateStmt."""
        return InstantiatedBox.from_instantiate_stmt(instantiate_stmt)


@dataclass
class ResolvedBox(node.CstNode[cst.Box], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity):
    """IR Node representing a resolved box declaration."""

    source_module: node.Module
    instances: Sequence[node.NamedEntity]
    connections: list[Connection]
    source: Box | None

    @override
    def append_error_line(self, msg: str) -> str:
        """Append source line/col information using the template's source module."""
        # ResolvedBox inherits cst_node from the box template definition, whose
        # spans are relative to source_module.  The base-class module is the
        # *instantiation* module, which may be a different .clk file with
        # different terminals, so we must use source_module here.
        return node.append_error_line(self.cst_node, self.source_module, msg)

    # We must disable C901/PLR0912 here (function complexity, # branches) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    def produce_casing_entities(self) -> cpp_executable.CasingEntities:  # noqa: C901, PLR0912 # see above
        """Produce a set of casing entities."""
        result = cpp_executable.CasingEntities()
        for instance in self.instances:
            if isinstance(instance, Box):
                result = result.merged_with(instance.get_resolved().produce_casing_entities())
            elif isinstance(instance, cog.CogInstance):
                if isinstance(instance.cog_class, cog.Cog):
                    cpp_cog = cpp_executable.CppCog(cog_ir=instance.cog_class, dial_header=None, cog_header=None)
                else:
                    cpp_cog = cpp_executable.CppInstantiatedCog(
                        instantiation=instance.cog_class, dial_header=None, cog_header=None
                    )
                result.cogs[instance.cog_class.value_key()] = cpp_cog
                _add_casing_cog_members(result, instance)
                if instance.cog_class.python_options:
                    assert instance.cst_node is not None
                    assert instance.cst_node.kind == cst.NewStmt.kind
                    assert isinstance(cpp_cog, cpp_executable.CppCog)
                    result.python_cogs[instance.cog_class.value_key()] = cpp_executable.CppPythonCog(
                        cst_node=instance.cst_node, module=instance.module, cpp_cog=cpp_cog
                    )
            elif isinstance(instance, SerializedDataFileInstance | StateInstance):
                if isinstance(instance.repr_typespec, typesys.Instantiation):
                    result.add_representation(self.module.context, instance.repr_typespec, self)
                else:
                    result.externs[instance.repr_typespec.value_key()] = instance.repr_typespec
            elif isinstance(instance, FirstMessageInstance):
                assert instance.channel.message_repr is not None
                result.add_representation(self.module.context, instance.channel.message_repr.typespec, self)
            elif isinstance(instance, udp.UdpSocketInstance):
                cpp_udp_socket = cpp_executable.CppUdpSocket(udp_socket_ir=instance.socket, options=None)
                if instance.socket.options:
                    cpp_udp_socket.options = cpp_executable.CppSocketOptions(instance.socket.options.options)
                result.udp_sockets[instance.socket.value_key()] = cpp_udp_socket
                assert isinstance(instance.socket.message_type, typesys.Instantiation)
                result.add_representation(self.module.context, instance.socket.message_type, self)
            elif isinstance(instance, audio.AudioSourceInstance):
                cpp_audio_source = cpp_executable.CppAudioSource(audio_source_ir=instance.source)
                result.audio_sources[instance.source.value_key()] = cpp_audio_source
                assert isinstance(instance.source.message_type, typesys.Instantiation)
                result.add_representation(self.module.context, instance.source.message_type, self)
            elif isinstance(instance, MemoryResourceInstance | ProcessInstance):
                # These do not require instantiation in a casing
                pass
            else:
                msg = f"Unrecognized box entity type {type(instance)}"
                raise NotImplementedError(msg)
        return result

    # We must disable C901/PLR0912 here (function complexity, # branches) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    def validate_use_targets(self) -> None:  # noqa: C901, PLR0912 # see above
        """Validate that the instances declared in the box have valid use targets."""
        if self.module.generates is None:
            return
        for instance in self.instances:
            if isinstance(instance, Box):
                self.source_module.validate_use_targets(
                    self, instance.name, instance.get_resolved().source_module.module_id, {node.GenerateTarget.cpp}
                )
                instance.get_resolved().validate_use_targets()
            elif isinstance(instance, cog.CogInstance):
                if instance.cog_class.python_options:
                    self.source_module.validate_use_targets(
                        self,
                        instance.cog_class.name,
                        instance.cog_class.module.module_id,
                        {node.GenerateTarget.cpp, node.GenerateTarget.py_cog},
                    )
                else:
                    self.source_module.validate_use_targets(
                        self, instance.name, instance.cog_class.module.module_id, {node.GenerateTarget.cpp}
                    )
            elif isinstance(instance, SerializedDataFileInstance | StateInstance):
                if isinstance(instance.repr_typespec, typesys.Instantiation):
                    schema_ir = instance.repr_typespec.arguments["schema"]
                    if isinstance(schema_ir, typesys.Instantiation):
                        schema_ir = schema_ir.instantiates
                    assert isinstance(schema_ir, schema.Schema)
                    typedef = instance.repr_typespec.instantiates
                    assert typedef is clkbuiltins.PROTOBUF or typedef is clkbuiltins.TACHYON
                    if typedef is clkbuiltins.PROTOBUF:
                        self.source_module.validate_use_targets(
                            self,
                            schema_ir.name,
                            schema_ir.module.module_id,
                            {node.GenerateTarget.cpp, node.GenerateTarget.proto, node.GenerateTarget.proto_conv},
                        )
                    else:
                        self.source_module.validate_use_targets(
                            self, schema_ir.name, schema_ir.module.module_id, {node.GenerateTarget.cpp}
                        )
                else:
                    self.source_module.validate_use_targets(
                        self,
                        instance.repr_typespec.name,
                        instance.repr_typespec.module.module_id,
                        {node.GenerateTarget.cpp},
                    )
            elif isinstance(instance, FirstMessageInstance):
                assert instance.channel.message_repr
                self.source_module.validate_use_targets(
                    self,
                    instance.channel.message_repr.schema_ir.schema_name,
                    instance.channel.message_repr.module.module_id,
                    {node.GenerateTarget.cpp},
                )
            elif isinstance(instance, udp.UdpSocketInstance):
                self.source_module.validate_use_targets(
                    self, instance.socket.name, instance.socket.module.module_id, {node.GenerateTarget.cpp}
                )
            elif isinstance(instance, audio.AudioSourceInstance):
                self.source_module.validate_use_targets(
                    self, instance.source.name, instance.source.module.module_id, {node.GenerateTarget.cpp}
                )
            elif isinstance(instance, MemoryResourceInstance | ProcessInstance):
                # These do not require any use targets
                pass
            else:
                msg = f"Unrecognized box entity type {type(instance)}"
                raise NotImplementedError(msg)


def _add_casing_cog_members(entities: cpp_executable.CasingEntities, instance: cog.CogInstance) -> None:
    for member in instance.members:
        match member.member:
            case cog.ConfigDef() | cog.StateDef():
                iface_info = member.member.get_resolved().message_type
                assert isinstance(iface_info, extern_type.ExternType | schema_reg.InterfaceInfo), (
                    f"Unexpected type for cog config/state member: {iface_info}"
                )
                if isinstance(iface_info, schema_reg.InterfaceInfo):
                    entities.interfaces[iface_info] = iface_info
                else:
                    entities.externs[iface_info.value_key()] = iface_info
            case cog.InputDef() | cog.OutputDef() | cog.MetricsOutputDef() | cog.ReportGroupDef():
                iface_info = member.member.get_interface_info()
                entities.interfaces[iface_info] = iface_info
            case cog_components.CogAlignedInputDef():
                for iface_info in aligner.get_aligned_input_interfaces(member.member):
                    entities.interfaces[iface_info] = iface_info
            case (
                cog.ConditionDef()
                | diagnostics.DiagnosticsDef()
                | diagnostics.InfraDiagnosticsDef()
                | cog.ResourceDef()
            ):
                # These don't have any casing entities associated with them
                pass


def _gather_box_elements(
    statements: Iterator[cst.Statement], ctx: dfl.Context, module: node.Module, traits: dfl_types.TraitRegistry
) -> list[cst.BoxElement]:
    elements = []
    for stmt in statements:
        expr = dfl.statement_from_cst(stmt, ctx, module)
        result = dfl.const_fold(expr, traits)
        if isinstance(result, dfl.Block):
            result = dfl_analysis.flatten_blocks(result)

        match result:
            case dfl.Block(statements=block_statements):
                for block_stmt in block_statements:
                    match block_stmt:
                        case dfl.CstPassthrough(cst_node=block_stmt_cst):
                            if box_element := block_stmt_cst.maybe_box_element():
                                elements.append(box_element)
                            else:
                                msg = f"Got unexpected statement of type {type(block_stmt_cst)} in box context."
                                raise TypeError(ctx.format_error(block_stmt_cst.span, msg))
                        case _:
                            msg = f"Block statement evaluated to {block_stmt}, but expected a Statement."
                            raise TypeError(ctx.format_error(dfl.get_expr_span(block_stmt), msg))
            case dfl.CstPassthrough(cst_node=block_stmt_cst):
                if box_element := block_stmt_cst.maybe_box_element():
                    elements.append(box_element)
                else:
                    msg = f"Got unexpected statement of type {type(block_stmt_cst)} in box context."
                    raise TypeError(ctx.format_error(block_stmt_cst.span, msg))
            case _:
                msg = f"Conditional statement evaluated to {result}, but expected a Block."
                raise TypeError(ctx.format_error(stmt.span, msg))

    return elements


@dataclass
class Box(node.CstNode[cst.Box], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity):
    """IR Node representing a box declaration."""

    template: BoxTemplate | None
    inner_scope: node.Scope
    instances: Sequence[statement.NewStmt] | Sequence[node.NamedEntity]
    connections: list[Connection]
    policy_stmts: list[PolicyApplicationStmt]
    resolved: ResolvedBox | None

    # We have to suppress PLR0913 (too many args) because this is already an extremely simple function that can't be split but still needs all these args. The args are all different types so mypy will catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
    @classmethod
    def from_cst(  # noqa: PLR0913 (see above)
        cls: type[Box],
        cst_node: cst.Box,
        instance_module: node.Module,
        template: BoxTemplate,
        lexical_scope: node.Scope,
        instance_scope: node.Scope,
        instance_name: str,
        doc: node.Doc | None,
        arguments: Mapping[str, typesys.Value] | None,
    ) -> Box:
        """Construct an IR node from a CST node."""
        if template.module is None or template.module.terminals is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        template_module = template.module
        inner_scope = lexical_scope.make_dynamic_scope(path_parent=instance_scope, name=instance_name)
        ctx = dfl.Context(scope=inner_scope, terminals=template.module.terminals, module_id=template_module.module_id)
        traits = dfl_types.get_trait_registry(template_module)
        current_system_target = instance_scope.lookup(node.CURRENT_SYSTEM_TARGET_SCOPE_KEY)
        if current_system_target:
            inner_scope.define(node.CURRENT_SYSTEM_TARGET_SCOPE_KEY, current_system_target, template_module.terminals)
        if arguments is not None:
            for arg_name, arg_value in arguments.items():
                if isinstance(arg_value, node.NamedEntity):
                    inner_scope.define(arg_name, arg_value, template.module.terminals)
                else:
                    inner_scope.define(
                        arg_name,
                        node.NamedBindingRef(
                            name=arg_name, value=arg_value, scope=inner_scope.make_child_scope(arg_name)
                        ),
                        template.module.terminals,
                    )
        instances = []
        connections = []
        policy_stmts = []
        for element in _gather_box_elements(cst_node.children_statement(), ctx, template_module, traits):
            if new_stmt_cst := element.maybe_new_stmt():
                new_stmt = statement.NewStmt.from_cst(new_stmt_cst, template_module)
                instances.append(new_stmt)
                inner_scope.define(
                    new_stmt.name,
                    node.NameProxy(new_stmt.name, inner_scope, new_stmt, False),
                    template_module.terminals,
                )
            elif connect_cst := element.maybe_connect_stmt():
                connections.append(Connection.from_cst(connect_cst, template_module, inner_scope))
            elif apply_cst := element.maybe_policy_apply_stmt():
                policy_stmts.append(PolicyApplicationStmt.from_cst(apply_cst, template_module))
            else:
                msg = node.append_error_line(element, template_module, f"Box element type not supported: {element}")
                raise NotImplementedError(msg)
        # fmt: off
        return cls(
            type_info=clkbuiltins.BOX_TYPE,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=instance_name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=instance_scope,
            doc=doc,
            module=instance_module,
            cst_node=cst_node,
            template=template,
            inner_scope=inner_scope,
            instances=instances,
            connections=connections,
            policy_stmts=policy_stmts,
            resolved=None,
        )
        # fmt: on

    # The complexity here is due to handling the various box elements. The branches consist of for loops and if instance checks.
    # The could be broken out into separate functions, but because of the relatively simple nature of what's being done here
    # it wouldn't enhance readability significantly, and would just add indirection.
    def resolve(self, source_module: node.Module) -> None:
        """Perform finalization of the IR."""
        if self.resolved:
            msg = "Attempt to resolve already-resolved object."
            raise RuntimeError(msg)
        instances = []
        for instance in self.instances:
            if not isinstance(instance, statement.NewStmt):
                msg = self.append_error_line(f"Attempt to resolve Box twice: {self}")
                raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
            instance.resolve()
            assert isinstance(instance.typespec, typesys.InstantiatableEntity)
            new_instance = instance.typespec.make_instance_for_resolution(
                cst_node=instance.cst_node,
                module=self.module,
                source_module=instance.typespec.get_module(),
                scope=self.inner_scope,
                name=instance.name,
                doc=instance.doc,
            )
            instances.append(new_instance)
            self.inner_scope.finalize_proxy(instance.name, new_instance)
        self.instances = instances
        for connection in self.connections:
            connection.resolve()
        for instance in self.instances:
            if isinstance(instance, StateInstance) and instance.init_cog_endpoint:
                self.connections.append(
                    Connection(
                        doc=node.Doc(
                            module=self.module,
                            cst_node=None,
                            value=f"Auto-generated implicit connection for state init of {instance.value_key()}",
                        ),
                        module=self.module,
                        cst_node=None,
                        source=instance,
                        target=instance.init_cog_endpoint,
                        instantiation_scope=None,
                    )
                )
        for policy_stmt in self.policy_stmts:
            policy_apply = policy_stmt.resolve()
            if policy_apply.is_recursive:
                if not isinstance(policy_apply.target, Box):
                    msg = policy_apply.append_error_line(
                        'In-box recursive ("in") policy applications can only apply to boxes, not {type(policy_apply.target)}'
                    )
                    raise TypeError(msg)
                policy_apply.target._apply_policy_recursive(self.module, policy_apply.policy_data)  # noqa: SLF001 (target is also a Box instance)
            else:
                self._apply_policy_single(policy_apply)
        # fmt: off
        self.resolved = ResolvedBox(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=self.name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=self.scope,
            type_info=self.type_info,
            doc=self.doc,
            module=self.module,
            cst_node=self.cst_node,
            source_module=source_module,
            instances=self.instances,
            connections=self.connections,
            source=self,
        )
        # fmt: on

    def get_resolved(self) -> ResolvedBox:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    def _apply_policy_single(self, policy_apply: PolicyApplication) -> None:
        # fmt: off
        if not isinstance(
            policy_apply.target,
            cog.CogInstance
            # pyrefly: ignore[implicit-any-type-argument] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            | cog.CogInstanceMember
            | FirstMessageInstance
            | ProcessInstance
            | SerializedDataFileInstance
            | StateInstance
            | SignalInstanceSpec,
        ):
        # fmt: on
            msg = policy_apply.append_error_line(
                f"In-box policy applications can only apply to instance types, not {type(policy_apply.target)}"
            )
            raise TypeError(msg)
        policy.bind_policy_data(self.module, policy_apply.policy_data, policy_apply.target)

    def _apply_policy_recursive(self, system_module: node.Module, policy_data: policy.UnboundPolicyData) -> None:
        for instance in self.instances:
            if isinstance(instance, Box):
                instance._apply_policy_recursive(system_module, policy_data)  # noqa: SLF001 (target is also a Box instance)
            else:
                assert isinstance(instance, typesys.Value)
                policy.try_bind_policy_data(system_module, policy_data, instance)

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        """Look up a definition in the membership entity.

        Returns:
            The entity with that name, or None if not found.
        """
        member = self.inner_scope.lookup(name)
        if member is not None:
            assert isinstance(member, typesys.Value)
            return member
        return None


def _register_box_signal_instances(
    context: CompilerContext,
    resolved_box: ResolvedBox,
) -> None:
    """Recursively register signal instances for all cog instances in a resolved box tree."""
    for instance in resolved_box.instances:
        if isinstance(instance, cog.CogInstance):
            _register_signal_instances(context, instance)
        elif isinstance(instance, Box):
            _register_box_signal_instances(context, instance.get_resolved())


def _register_signal_instances(
    context: CompilerContext,
    cog_instance: cog.CogInstance,
) -> None:
    """Register signal instances for all signal members of a cog instance.

    Note: Signal definitions (both module-level and cog-private) are registered
    during cog resolution in compiler.py. This function only registers the instances.
    """
    for rg_instance in cog_instance.report_group_instances:
        for entry in rg_instance.entries.values():
            signal_registry.register_signal_instance(
                context,
                signal_ir=entry.signal,
                instance_name=entry.instance_name,
                cog_instance=cog_instance,
            )


@dataclass
class Connection(node.CstNode[cst.ConnectStmt], node.DocableEntity):
    """IR Node representing a connection to or from an endpoint."""

    source: expr.Expr | typesys.Value
    target: expr.Expr | typesys.Value
    instantiation_scope: node.Scope | None

    @classmethod
    def from_cst(
        cls: type[Connection], cst_node: cst.ConnectStmt, module: node.Module, instantiation_scope: node.Scope | None
    ) -> Connection:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        source = expr.Expr.from_cst(cst_node.child_source(), module)
        target = expr.Expr.from_cst(cst_node.child_target(), module)
        return cls(
            doc=doc,
            module=module,
            cst_node=cst_node,
            source=source,
            target=target,
            instantiation_scope=instantiation_scope,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.source, expr.Expr) or not isinstance(self.target, expr.Expr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve connection twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        evaluated_source = self.source.evaluate()
        if isinstance(evaluated_source, typesys.Instantiation) and self.instantiation_scope:
            evaluated_source = fmt_string.evaluate_instantiation_strings(evaluated_source, self.instantiation_scope)
        self.source = _resolve_connection_endpoint(evaluated_source, self.module.context)
        evaluated_target = self.target.evaluate()
        if isinstance(evaluated_target, typesys.Instantiation) and self.instantiation_scope:
            evaluated_target = fmt_string.evaluate_instantiation_strings(evaluated_target, self.instantiation_scope)
        self.target = _resolve_connection_endpoint(evaluated_target, self.module.context)

        sources_to_check = self.source.elements if isinstance(self.source, typesys.Values) else [self.source]
        for source in sources_to_check:
            if (
                isinstance(source, pubsub.Channel | pubsub.InstantiatedChannel)
                and isinstance(self.target, cog.CogInstanceMember)
                and isinstance(self.target.member, cog.InputDef | cog_components.CogAlignedInputDef)
            ):
                if _input_needs_safety_margin(self.target):

                    _validate_safety_margin(self.target, source)


                _validate_emergency_margin(self.target, source)
                _validate_skip_threshold(self.target, source)

            if (
                isinstance(source, cog.CogInstanceMember)
                and not source.cog_instance.cog_class.is_init()
                and isinstance(source.member, cog.OutputDef)
                and isinstance(self.target, pubsub.Channel | pubsub.InstantiatedChannel)
                and self.target.is_published_once
            ):
                msg = source.cog_instance.append_error_line(
                    f"Channel {self.target.value_key()} is published once but is an output of a non-initialization cog.",
                )
                raise ValueError(msg)


def _resolve_connection_endpoint(value: typesys.Value, calling_context: CompilerContext | None = None) -> typesys.Value:
    """A connection endpoint could be an instantion.  Resolve it if it is.

    Args:
        value: Any value that can be connected or needs to be resolved to be connected.
        calling_context: Optional compiler context from the module where the connection
            is defined. Passed through to channel instantiation for representation
            lookups when the message type references schemas from an external module.

    Returns:
        A connectable endpoint.
    """
    if isinstance(value, statement.ImmutableBinding):
        value = value.value
    if isinstance(value, typesys.Values):
        return typesys.Values(
            type_info=value.type_info,
            elements=[_resolve_connection_endpoint(element, calling_context) for element in value.elements],
        )
    if (
        isinstance(value, typesys.Instantiation)
        and isinstance(value.instantiates, pubsub.Channel)
        and value.instantiates.is_generic()
    ):
        return pubsub.InstantiatedChannel.from_instantiation(value, calling_context)
    return value


def _input_needs_safety_margin(target: CogInputMember) -> bool:
    """Returns true if the given input is associated with a new_msg+max condition."""
    for condition in target.cog_instance.cog_class.conditions.values():
        if (
            isinstance(condition.condition, cog.NewMessagePresent)
            and condition.condition.upper_bound is not None
            and condition.condition.input_name == target.member.name
        ):
            return True

    return False


def _get_safety_margin(target: CogInputMember, channel: pubsub.Channel | pubsub.InstantiatedChannel) -> int:
    """Return the the safety margin required by a cog input."""
    view_params = target.member.view_params
    assert isinstance(view_params.safety_margin, int)
    assert isinstance(view_params.max_msgs, int)
    if view_params.safety_margin == -1:
        # The user didn't specify a margin. Use the default.
        num_slots = channel.num_slots
        return max(int(num_slots.value) // 2, view_params.max_msgs)

    return view_params.safety_margin


def _get_channel_display_name(channel: pubsub.Channel | pubsub.InstantiatedChannel) -> str:
    """Get a human-readable name for a channel for use in error messages."""
    if isinstance(channel, pubsub.InstantiatedChannel):
        return channel.channel_name.value
    # For Channel, channel_name could be Expr or StringValue
    if isinstance(channel.channel_name, primitive.StringValue):
        return channel.channel_name.value
    return channel.name



def _validate_safety_margin(
    target: CogInputMember,
    channel: pubsub.Channel | pubsub.InstantiatedChannel,
) -> None:
    """Raises a ValueError if the safety margin requested by a user is unsatisfiable for the given channel."""
    view_params = target.member.view_params
    assert isinstance(view_params.max_msgs, int)
    margin = _get_safety_margin(target, channel)
    num_slots = channel.num_slots
    channel_display_name = _get_channel_display_name(channel)
    if view_params.max_msgs + margin > num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Channel {channel_display_name} is not large enough ({num_slots.value} messages) to accommodate safety margin ({margin} messages) for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages).",
        )
        raise ValueError(msg)

    emergency_margin = int(max(2, 0.1 * int(num_slots.value)))
    if margin < emergency_margin:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Safety margin ({margin} messages) for {target.cog_instance.name}.{target.member.name} is less than the emergency margin for {channel_display_name} ({emergency_margin} messages).",
        )
        raise ValueError(msg)



def _validate_emergency_margin(target: CogInputMember, channel: pubsub.Channel | pubsub.InstantiatedChannel) -> None:
    """Raises a ValueError if the emergency margin requested by a user is unsatisfiable for the given channel."""
    if channel.is_published_once:
        return

    view_params = target.member.view_params
    assert isinstance(view_params.max_msgs, int)

    num_slots = channel.num_slots
    channel_display_name = _get_channel_display_name(channel)
    emergency_margin = int(max(2, 0.1 * int(num_slots.value)))
    if view_params.max_msgs + emergency_margin >= num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Channel {channel_display_name} is not large enough ({num_slots.value} messages) to accommodate emergency margin ({emergency_margin} messages) for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages).",
        )
        raise ValueError(msg)


def _validate_skip_threshold(target: CogInputMember, channel: pubsub.Channel | pubsub.InstantiatedChannel) -> None:
    """Raises a ValueError if the preemptive skip threshold requested by a user is too large for the given channel."""
    view_params = target.member.view_params
    if not isinstance(view_params.skip_threshold, int) or channel.is_published_once:
        return

    assert isinstance(view_params.max_msgs, int)
    num_slots = channel.num_slots
    channel_display_name = _get_channel_display_name(channel)
    if view_params.max_msgs + view_params.skip_threshold > num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Skip treshold ({view_params.skip_threshold} messages) and view for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages) sum to more than the capacity of {channel_display_name} ({num_slots.value} messages).",
        )
        raise ValueError(msg)


@dataclass
class InstantiatedBoxFactory(node.CstNode[cst.NewStmt], typesys.InstantiatableEntity):
    """Factory to instantiate BoxInstance entities from a NewStmt."""

    new_stmt: statement.NewStmt

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
    ) -> Box:
        """Create an instance of the entity."""
        result = self.make_instance_for_resolution(
            cst_node=cst_node, module=module, source_module=source_module, scope=scope, name=name, doc=doc
        )
        _register_box_signal_instances(result.module.context, result.get_resolved())
        return result

    @override
    def make_instance_for_resolution(
        self,
        *,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        source_module: node.Module | None = None,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> Box:
        """Create an instance of the entity during box resolution."""
        assert self.new_stmt.instantiation
        evaluated_instantiation = fmt_string.evaluate_instantiation_strings(self.new_stmt.instantiation, scope)
        box_ir = evaluated_instantiation.instantiates
        assert isinstance(box_ir, BoxTemplate)
        result = box_ir.create_box_in_module(
            module=module,
            source_module=source_module,
            scope=scope,
            name=name,
            doc=doc,
            arguments=evaluated_instantiation.arguments,
        )
        # In low boilerplate the casing for a C++ executable automatically contains everything
        # defined in the source file that generates the executable. This means that parameterized
        # boxes either have to be included in a box or explicitly instantiated so the compiler
        # knows to add them to the casing.
        if (
            box_ir.module.generates is not None
            and node.GenerateTarget.cpp_exe in box_ir.module.generates
            and lookup_instantiated_box(box_ir.module.context, evaluated_instantiation) is None
        ):
            if self.new_stmt.module.module_id == box_ir.module.module_id:
                # The instantiations for boxes that are elements of other boxes in the
                # same module are implicit. This box is pulled into the cpp_executable
                # along with the elements of the containing box.
                register_instantiated_box(
                    box_ir.module.context,
                    InstantiatedBox(
                        name=name,
                        scope=box_ir.module.inner_scope,
                        type_info=clkbuiltins.INSTANTIATED_BOX,
                        module=box_ir.module,
                        instantiation=evaluated_instantiation,
                        instance=None,
                    ),
                )
            else:
                msg = self.new_stmt.append_error_line(
                    "Box has not been instantiated: ({evaluated_instantiation.value_key()})"
                )
                raise ValueError(msg)
        return result


@dataclass
class InstantiatedBox(typesys.Value, node.NamedEntity, cpp_executable.CasingEntitySource):
    """Casing entity source for a box instantiated in module that generates cpp_exe."""

    module: node.Module
    instantiation: typesys.Instantiation
    instance: Box | None

    @classmethod
    def from_instantiate_stmt(
        cls: type[InstantiatedBox], instantiate_stmt: statement.InstantiateStmt
    ) -> InstantiatedBox:
        """Make an instantiated box."""
        instantiation = instantiate_stmt.typespec
        if not isinstance(instantiation, typesys.Instantiation):
            msg = instantiate_stmt.append_error_line("Attempt to instantiate unresolved statement")
            raise TypeError(msg)
        instantiation = fmt_string.evaluate_instantiation_strings(instantiation, instantiate_stmt.module.inner_scope)
        box_ir = instantiation.instantiates
        if not isinstance(box_ir, BoxTemplate):
            msg = instantiate_stmt.append_error_line(
                f"Expected box instantiation, got {type(instantiation.instantiates)}"
            )
            raise TypeError(msg)
        if not box_ir.parameters:
            msg = instantiate_stmt.append_error_line("Cannot instantiate a non-generic box")
            raise ValueError(msg)
        result = cls(
            type_info=clkbuiltins.INSTANTIATED_BOX,
            scope=box_ir.module.inner_scope,
            name=box_ir.name
            + "_"
            + "_".join(f"{param}_{instantiation.arguments[param].value_key()}" for param in box_ir.parameters),
            module=box_ir.module,
            instantiation=instantiation,
            instance=None,
        )
        register_instantiated_box(box_ir.module.context, result)
        return result

    def get_instance(self) -> Box:
        """Get instantiated box instance."""
        if not self.instance:
            assert isinstance(self.instantiation.instantiates, BoxTemplate)
            self.instance = self.instantiation.instantiates.create_box_in_module(
                module=self.module,
                scope=self.scope,
                name="__casing__" + self.instantiation.instantiates.name,
                doc=None,
                arguments=self.instantiation.arguments,
            )
        return self.instance

    @override
    def produce_casing_entities(self) -> cpp_executable.CasingEntities:
        """Produce a set of casing entities."""
        resolved_instance = self.get_instance().get_resolved()
        resolved_instance.validate_use_targets()
        return resolved_instance.produce_casing_entities()

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return self.instantiation.value_key()


@dataclass
class PolicyApplication(node.CstNode[cst.PolicyApplyStmt], node.DocableEntity):
    """Represents a resolved, unapplied policy application statement."""

    policy_data: policy.UnboundPolicyData
    target: typesys.Value
    is_recursive: bool
    source: PolicyApplicationStmt | None


@dataclass
class PolicyApplicationStmt(node.CstNode[cst.PolicyApplyStmt], node.DocableEntity):
    """Represents an unapplied policy application statement."""

    policy_data: expr.Expr
    target: expr.Expr
    is_recursive: bool
    resolved_value: PolicyApplication | None

    @classmethod
    def from_cst(
        cls: type[PolicyApplicationStmt], cst_node: cst.PolicyApplyStmt, module: node.Module
    ) -> PolicyApplicationStmt:
        """Construct an IR node from a CST node."""
        is_recursive = cst_node.maybe_in() is not None
        result = cls(
            doc=node.Doc.maybe_from_cst(cst_node.maybe_doc(), module),
            module=module,
            cst_node=cst_node,
            policy_data=expr.Expr.from_cst(cst_node.child_policy_data(), module),
            target=expr.Expr.from_cst(cst_node.child_target(), module),
            is_recursive=is_recursive,
            resolved_value=None,
        )
        if is_recursive:
            typesys.unify(result.target.type_info, clkbuiltins.BOX_TYPE)
        return result

    def resolve(self) -> PolicyApplication:
        """Perform final IR resolution."""
        if self.resolved_value:
            return self.resolved_value
        policy_data = self.policy_data.evaluate()
        if isinstance(policy_data, ProcessInstance):
            policy_data = HOST_PROCESS_POLICY.evaluate_call(
                ir_node=self.cst_node,  # pyright: ignore[reportArgumentType] (this class should be typed with covariant arguments)
                module=self.module,
                args=[("process", policy_data)],
            )
        if not isinstance(policy_data, policy.UnboundPolicyData):
            msg = self.policy_data.append_error_line(
                f"Expected a policy data expression but got {type(policy_data).__name__}"
            )
            raise TypeError(msg)
        target = self.target.evaluate()
        self.resolved_value = PolicyApplication(
            doc=self.doc,
            module=self.module,
            cst_node=self.cst_node,
            policy_data=policy_data,
            target=target,
            is_recursive=self.is_recursive,
            source=self,
        )
        return self.resolved_value


@dataclass
class SerializedDataFile(typesys.InstantiatableEntity, typesys.ObjectIdentityValue):
    """An instantiatable reference to a specific data file."""

    module: node.Module
    repr_typespec: typesys.Instantiation
    file_path: Path | fmt_string.UnevaluatedFmtString

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
        """Create an instance of the entity."""
        file_path = self.file_path
        if isinstance(file_path, fmt_string.UnevaluatedFmtString):
            file_path = Path(file_path.evaluate_from_scope(scope).value)
        # fmt: off
        return SerializedDataFileInstance(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=clkbuiltins.CONFIG_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            repr_typespec=self.repr_typespec,
            file_path=file_path,
        )
        # fmt: on

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class SerializedDataFileInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """An instance of a textproto file."""

    repr_typespec: typesys.Instantiation
    file_path: Path | fmt_string.UnevaluatedFmtString


@dataclass
class SerializedDataFileFactory(node.NamedEntity, typesys.CallableEntity, typesys.ObjectIdentityValue):
    """A factory supporting DSL Call syntax for creating textproto file references."""

    @override
    def evaluate_call(
        self,
        *,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> SerializedDataFile:
        """Apply the Call operation."""
        expected = ("representation", "path")
        try:
            kw_args = typesys.extract_kwargs(expected, args)
        except ValueError as e:
            msg = node.enrich_error_if_possible(ir_node, str(e))
            raise ValueError(msg) from e
        for param in expected:
            if param not in kw_args:
                msg = node.enrich_error_if_possible(ir_node, f'Missing parameter "{param}"')
                raise ValueError(msg)
        repr_typespec = kw_args["representation"]
        if not isinstance(repr_typespec, typesys.Instantiation) or repr_typespec.instantiates not in (
            clkbuiltins.PROTOBUF,
            clkbuiltins.TACHYON,
        ):
            msg = node.enrich_error_if_possible(
                ir_node, f"Representation must be a Protobuf or Tachyon instantiation, not {repr_typespec}"
            )
            raise TypeError(msg)
        pathname = kw_args["path"]
        if isinstance(pathname, fmt_string.UnevaluatedFmtString):
            file_path = pathname
        else:
            if not isinstance(pathname, primitive.StringValue):
                msg = node.enrich_error_if_possible(
                    ir_node, f"path must be a string value or fmt! call, not {pathname}"
                )
                raise TypeError(msg)
            file_path = Path(pathname.value)
        return SerializedDataFile(
            type_info=clkbuiltins.TYPE_TYPE, module=module, repr_typespec=repr_typespec, file_path=file_path
        )


clkbuiltins.BUILTINS_SCOPE.define(
    "SerializedDataFile",
    SerializedDataFileFactory(type_info=clkbuiltins.TYPE_TYPE, name="TextprotoFile", scope=clkbuiltins.BUILTINS_SCOPE),
    None,
)


@dataclass
class FallbackEndpoint(typesys.ObjectIdentityValue):
    """Represents the `.fallback` endpoint on a data source."""

    parent_data_source: FirstMessageInstance


FALLBACK_ENDPOINT_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="FallbackEndpoint", type_info=clkbuiltins.TYPE_TYPE
)
DATA_SOURCE_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="DataSource", type_info=clkbuiltins.TYPE_TYPE
)


@dataclass
class FirstMessage(typesys.InstantiatableEntity, typesys.ObjectIdentityValue):
    """An instantiatable reference to a first message from a channel."""

    module: node.Module
    channel: pubsub.Channel
    allow_default: bool = False

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
        """Create an instance of the entity."""
        # fmt: off
        return FirstMessageInstance(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=DATA_SOURCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            channel=self.channel,
            allow_default=self.allow_default,
        )
        # fmt: on

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class FirstMessageInstance(
    node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity
):
    """An instance of a first message data source."""

    channel: pubsub.Channel
    allow_default: bool = False
    fallback_endpoint: FallbackEndpoint | None = None

    @override
    def attribute(self, name: str) -> typesys.Value | None:
        if name == "fallback":
            if self.fallback_endpoint is None:
                self.fallback_endpoint = FallbackEndpoint(type_info=FALLBACK_ENDPOINT_TYPE, parent_data_source=self)
            return self.fallback_endpoint
        return None


@dataclass
class FirstMessageFactory(node.NamedEntity, typesys.CallableEntity, typesys.ObjectIdentityValue):
    """A factory supporting DSL Call syntax for creating first message references."""

    @override
    def evaluate_call(  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors
        self,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> FirstMessage:
        """Apply the Call operation."""
        expected = ("channel", "allow_default")
        try:
            kw_args = typesys.extract_kwargs(expected, args)
        except ValueError as e:
            msg = node.enrich_error_if_possible(ir_node, str(e))
            raise ValueError(msg) from e
        channel = kw_args.get("channel")
        if channel is None:
            msg = node.enrich_error_if_possible(ir_node, 'Missing parameter "channel"')
            raise ValueError(msg)
        if not isinstance(channel, pubsub.Channel):
            msg = node.enrich_error_if_possible(ir_node, f"channel must be a Channel, not {type(channel)}")
            raise TypeError(msg)
        allow_default_value = kw_args.get("allow_default")
        allow_default = primitive.value_to_bool(allow_default_value) if allow_default_value is not None else False
        return FirstMessage(
            type_info=clkbuiltins.TYPE_TYPE, module=module, channel=channel, allow_default=allow_default
        )


clkbuiltins.BUILTINS_SCOPE.define(
    "FirstMessage",
    FirstMessageFactory(type_info=clkbuiltins.TYPE_TYPE, name="FirstMessage", scope=clkbuiltins.BUILTINS_SCOPE),
    None,
)


@dataclass
class State(typesys.InstantiatableEntity, typesys.ObjectIdentityValue):
    """An instantiatable reference to a specific state instance."""

    module: node.Module
    repr_typespec: typesys.Instantiation | extern_type.ExternType
    init_cog_endpoint: cog.CogInstanceMember[cog.StateDef] | None
    memory_resource: MemoryResourceInstance | None

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
        """Create an instance of the entity."""
        # fmt: off
        return StateInstance(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=clkbuiltins.STATE_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            repr_typespec=self.repr_typespec,
            init_cog_endpoint=self.init_cog_endpoint,
            memory_resource=self.memory_resource,
        )
        # fmt: on

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class StateInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """An instance of state."""

    repr_typespec: typesys.Instantiation | extern_type.ExternType
    init_cog_endpoint: cog.CogInstanceMember[cog.StateDef] | None
    memory_resource: MemoryResourceInstance | None


@dataclass
class StateFactory(node.NamedEntity, typesys.CallableEntity, typesys.ObjectIdentityValue):
    """A factory supporting DSL Call syntax for creating state instances."""

    @override
    def evaluate_call(
        self,
        *,
        ir_node: node.CstNode[cst.Expr],
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> State:
        """Apply the Call operation."""
        expected = ("representation", "init", "memory_resource")
        try:
            kw_args = typesys.extract_kwargs(expected, args)
        except ValueError as e:
            msg = node.append_error_line(ir_node, module, str(e))
            raise ValueError(msg) from e

        if repr_typespec := kw_args.get("representation"):
            if not (
                (isinstance(repr_typespec, typesys.Instantiation) and repr_typespec.instantiates is clkbuiltins.TACHYON)
                or isinstance(repr_typespec, extern_type.ExternType)
            ):
                msg = ir_node.append_error_line(
                    f"Representation must be a Tachyon instantiation or extern type, not {repr_typespec}"
                )
                raise TypeError(msg)
        else:
            msg = node.append_error_line(ir_node, module, "Missing parameter 'representation'")
            raise ValueError(msg)

        if init_cog := kw_args.get("init"):
            if (
                not isinstance(init_cog, cog.CogInstanceMember)
                or not init_cog.cog_instance.cog_class.is_init()
                or not isinstance(init_cog.member, cog.StateDef)
                or not init_cog.member.params.mutable
            ):
                msg = ir_node.append_error_line(
                    "State init parameter must be a mutable state endpoint of an init cog",
                )
                raise TypeError(msg)
            init_cog_endpoint = init_cog
        else:
            init_cog_endpoint = None

        if memory_resource := kw_args.get("memory_resource"):
            if not isinstance(memory_resource, MemoryResourceInstance):
                msg = ir_node.append_error_line(
                    f"memory_resource must be a memory resource instance, not {type(memory_resource)}"
                )
                raise TypeError(msg)
            if isinstance(repr_typespec, typesys.Instantiation):
                msg = ir_node.append_error_line("States with Tachyon representations can not specify a memory_resource")
                raise ValueError(msg)
        elif isinstance(repr_typespec, extern_type.ExternType):
            msg = ir_node.append_error_line("Extern type state must have a memory_resource parameter")
            raise ValueError(msg) from None

        assert memory_resource is None or isinstance(memory_resource, MemoryResourceInstance)
        assert isinstance(repr_typespec, typesys.Instantiation | extern_type.ExternType)
        return State(
            type_info=clkbuiltins.TYPE_TYPE,
            module=module,
            repr_typespec=repr_typespec,
            init_cog_endpoint=init_cog_endpoint,
            memory_resource=memory_resource,
        )


clkbuiltins.BUILTINS_SCOPE.define(
    "State",
    StateFactory(type_info=clkbuiltins.TYPE_TYPE, name="State", scope=clkbuiltins.BUILTINS_SCOPE),
    None,
)


class MemResourceType(Enum):
    """Memory resource type."""

    HEAP = 0


@dataclass
class MemoryResource(typesys.InstantiatableEntity, typesys.ObjectIdentityValue):
    """An instantiatable reference to a specific memory resource."""

    module: node.Module
    resource_type: MemResourceType
    max_size: int

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
        """Create an instance of the entity."""
        # fmt: off
        return MemoryResourceInstance(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=clkbuiltins.MEMORY_RESOURCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            resource_type=self.resource_type,
            max_size=self.max_size,
        )
        # fmt: on

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class MemoryResourceInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """A memory resource."""

    resource_type: MemResourceType
    max_size: int


@dataclass
class MemoryResourceFactory(node.NamedEntity, typesys.CallableEntity, typesys.ObjectIdentityValue):
    """A factory supporting DSL Call syntax for creating state instances."""

    resource_type: MemResourceType

    @override
    def evaluate_call(  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors
        self,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> MemoryResource:
        """Apply the Call operation."""
        expected = ("max_size",)
        try:
            kw_args = typesys.extract_kwargs(expected, args)
        except ValueError as e:
            msg = node.append_error_line(ir_node, module, str(e))
            raise ValueError(msg) from e
        for param in expected:
            if param not in kw_args:
                msg = node.append_error_line(ir_node, module, f"Missing parameter {kw_args}")
                raise ValueError(msg)
        max_size = kw_args["max_size"]
        if not isinstance(max_size, primitive.DecimalValue):
            msg = node.append_error_line(ir_node, module, "Max size must be an integer")
            raise TypeError(msg)
        max_size_int = int(max_size.value)
        if max_size_int != max_size.value:
            msg = node.append_error_line(ir_node, module, "Max size must be an integer, not {max_size.value}")
            raise ValueError(msg)
        return MemoryResource(
            type_info=clkbuiltins.TYPE_TYPE, module=module, resource_type=self.resource_type, max_size=max_size_int
        )


clkbuiltins.BUILTINS_SCOPE.define(
    "HeapMemory",
    MemoryResourceFactory(
        type_info=clkbuiltins.TYPE_TYPE,
        name="State",
        scope=clkbuiltins.BUILTINS_SCOPE,
        resource_type=MemResourceType.HEAP,
    ),
    None,
)


@dataclass
class Process(typesys.InstantiatableEntity, typesys.ObjectIdentityValue):
    """An instantiatable reference to a specific process."""

    module: node.Module
    executable: cpp_executable.CppExecutable

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
        """Create an instance of the entity."""
        # fmt: off
        return ProcessInstance(
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            name=name,
            # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            scope=scope,
            type_info=clkbuiltins.PROCESS_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            executable=self.executable,
        )
        # fmt: on

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class ProcessInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """A process instance."""

    executable: cpp_executable.CppExecutable


@dataclass
class ProcessFactory(typesys.CallableEntity, typesys.TypeDef):
    """A factory supporting DSL Call syntax for creating process instances."""

    @override
    def evaluate_call(  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors
        self,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> Process:
        """Apply the Call operation."""
        expected = ("executable",)
        try:
            kw_args = typesys.extract_kwargs(expected, args)
        except ValueError as e:
            msg = node.append_error_line(ir_node, module, str(e))
            raise ValueError(msg) from e
        for param in expected:
            if param not in kw_args:
                msg = node.append_error_line(ir_node, module, f"Missing parameter {kw_args}")
                raise ValueError(msg)
        executable = kw_args["executable"]
        if not isinstance(executable, cpp_executable.CppExecutable):
            msg = node.append_error_line(
                ir_node, module, f"executable must be a cpp_executable, not {type(executable)}"
            )
            raise TypeError(msg)
        return Process(type_info=clkbuiltins.TYPE_TYPE, module=module, executable=executable)


clkbuiltins.BUILTINS_SCOPE.define(
    "Process",
    ProcessFactory(
        type_info=clkbuiltins.TYPE_TYPE,
        name="Process",
        scope=clkbuiltins.BUILTINS_SCOPE,
    ),
    None,
)

HOST_PROCESS_POLICY_SCHEMA: Final = schema.make_schema_class(
    name="HostCpuDomainSchema",
    module=clkbuiltins.BUILTINS_MODULE,
    fields=(
        schema.make_field(
            module=clkbuiltins.BUILTINS_MODULE,
            num=0,
            name="process",
            typespec=clkbuiltins.PROCESS_TYPE,
            doc="The process instance to host entities with this policy.",
        ),
    ),
    doc="Schema for the HostProcess policy.",
)

HOST_PROCESS_POLICY: Final = policy.PolicyClass(
    name="HostProcess",
    scope=clkbuiltins.BUILTINS_SCOPE,
    type_info=clkbuiltins.POLICY_TYPE,
    doc=node.Doc(
        module=clkbuiltins.BUILTINS_MODULE,
        cst_node=None,
        value="Built-in policy for determining which process to host instantiated entities in.",
    ),
    target_bound=typesys.TypeUnion.make(
        types=(
            clkbuiltins.COG_INSTANCE_TYPE,
            clkbuiltins.CONFIG_INSTANCE_TYPE,
            clkbuiltins.MEMORY_RESOURCE_TYPE,
            clkbuiltins.STATE_INSTANCE_TYPE,
            clkbuiltins.UDP_SOCKET_INSTANCE_TYPE,
            clkbuiltins.AUDIO_SOURCE_INSTANCE_TYPE,
            DATA_SOURCE_TYPE,
        )
    ),
    schema=HOST_PROCESS_POLICY_SCHEMA,
    source=None,
)

clkbuiltins.BUILTINS_SCOPE.define(HOST_PROCESS_POLICY.name, HOST_PROCESS_POLICY, None)


HOST_CPU_DOMAIN_POLICY_SCHEMA: Final = schema.make_schema_class(
    name="HostCpuDomainSchema",
    module=clkbuiltins.BUILTINS_MODULE,
    fields=(
        schema.make_field(
            module=clkbuiltins.BUILTINS_MODULE,
            num=0,
            name="cpu_domain",
            typespec=clkbuiltins.CPU_DOMAIN_TYPE,
            doc="The CPU domain to host processes with this policy.",
        ),
    ),
    doc="Schema for the HostCpuDomain policy.",
)

HOST_CPU_DOMAIN_POLICY: Final = policy.PolicyClass(
    name="HostCpuDomain",
    scope=clkbuiltins.BUILTINS_SCOPE,
    type_info=clkbuiltins.POLICY_TYPE,
    doc=node.Doc(
        module=clkbuiltins.BUILTINS_MODULE,
        cst_node=None,
        value="Built-in policy for determining which CPU domain to run processes in.",
    ),
    target_bound=clkbuiltins.PROCESS_TYPE,
    schema=HOST_CPU_DOMAIN_POLICY_SCHEMA,
    source=None,
)

clkbuiltins.BUILTINS_SCOPE.define(HOST_CPU_DOMAIN_POLICY.name, HOST_CPU_DOMAIN_POLICY, None)


INSTANTIATED_BOX_KEY_TYPE: TypeAlias = str


@dataclass(frozen=True, eq=True, slots=True)
class InstantiatedBoxInfo:
    """Represents an instantiated box in the registry.

    Attributes:
        instantiated_box: The instantiated box
        type_key: The computed unique key for this instantiated box
    """

    instantiated_box: InstantiatedBox = field(compare=False)
    type_key: INSTANTIATED_BOX_KEY_TYPE

    @classmethod
    def make(
        cls: type[InstantiatedBoxInfo],
        instantiated_box: InstantiatedBox,
    ) -> InstantiatedBoxInfo:
        """Construct a InstantiatedBoxInfo with computed key.

        Args:
            instantiated_box: The instantiated box to be registered

        Returns:
            An object for referencing this instantiated box uniquely in the registry.
        """
        type_key = cls.key_for(instantiated_box)
        return cls(instantiated_box=instantiated_box, type_key=type_key)

    @staticmethod
    def key_for(
        instantiated_box: InstantiatedBox | typesys.Instantiation,
    ) -> INSTANTIATED_BOX_KEY_TYPE:
        """Compute the registry key for the given instantiated box."""
        return instantiated_box.value_key()


class InstantiatedBoxRegistry(Context):
    """Registry for instantiated boxs."""

    def __init__(self, name: str | None) -> None:
        """Create a new, empty instantiated box registry."""
        self.name = name
        self.registry: dict[INSTANTIATED_BOX_KEY_TYPE, InstantiatedBoxInfo] = {}

    @override
    def import_from(self, other: InstantiatedBoxRegistry) -> None:
        """Combine this registry with items from another.

        Arguments:
            other: Registry to combine.

        Raises:
            RuntimeError: If a type already exists with different info.
        """
        for key, item_info in other.registry.items():
            if key in self.registry and self.registry[key] != item_info:
                msg = f"Type {key} has conflicting item info"
                raise RuntimeError(msg)
            self.registry[key] = item_info


def register_instantiated_box(compiler_context: CompilerContext, instantiated_box: InstantiatedBox) -> None:
    """Registers an instantiated box in the context.

    Args:
        compiler_context: Compiler context containing the registry.
        instantiated_box: Instantiated box casing entity source to register.

    Raises:
        ValueError: If the instantiated box is already registered.
    """
    item_info = InstantiatedBoxInfo.make(instantiated_box)
    registry = compiler_context[INSTANTIATED_BOX_REGISTRY_KEY]
    try:
        existing_type = registry.registry[item_info.type_key]
        msg = f"Type {item_info} already registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    registry.registry[item_info.type_key] = item_info


def lookup_instantiated_box(
    compiler_context: CompilerContext, instantiation: typesys.Instantiation
) -> InstantiatedBox | None:
    """Look up an instantiated box in the registry.

    Args:
        compiler_context: Compiler context containing the registry.
        instantiation: An instantiation for a generic Box.

    Returns:
        The registry entry if found, else None.
    """
    registry = compiler_context[INSTANTIATED_BOX_REGISTRY_KEY]
    try:
        return registry.registry[InstantiatedBoxInfo.key_for(instantiation)].instantiated_box
    except KeyError:
        return None


class InstantiatedBoxRegistryKey(ContextKey[InstantiatedBoxRegistry]):
    """Compiler context key for instantiated box registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> InstantiatedBoxRegistry:
        """Create a default instance of the registry."""
        return InstantiatedBoxRegistry(compiler_context.name)


INSTANTIATED_BOX_REGISTRY_KEY: Final = InstantiatedBoxRegistryKey("InstantiatedBoxRegistry")
