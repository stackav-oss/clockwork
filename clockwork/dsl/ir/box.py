## Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Box IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal
from enum import Enum
from pathlib import Path
from typing import TYPE_CHECKING, Final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import (
    audio,
    clkbuiltins,
    cog,
    cpp_executable,
    diagnostics,
    expr,
    extern_type,
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
    from collections.abc import Sequence

    from clockwork.dsl.compiler_context import CompilerContext


@dataclass
class BoxTemplate(
    node.CstNode[cst.Box],
    node.DocableEntity,
    typesys.NamedValue,
    typesys.InstantiatableEntity,
    cpp_executable.CasingEntitySource,
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

    @classmethod
    def from_cst(cls: type[BoxTemplate], cst_node: cst.Box, module: node.Module) -> BoxTemplate:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        return cls(
            name=name,
            scope=module.inner_scope,
            type_info=clkbuiltins.TYPE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
        )

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
        )
        # Because we're delaying CST processing, the original name resolution
        # pass won't have done anything.  We need to do a separate name
        # resolution pass manually here, after we've processed the CST.
        node.resolve_names(result, self.scope)
        result.resolve(source_module or module)
        return result

    @override
    def get_module(self) -> node.Module:
        return self.module

    @override
    def produce_casing_entities(self) -> cpp_executable.CasingEntities:
        """Produce a set of casing entities."""
        instance = self.make_instance(
            cst_node=None, module=self.module, scope=self.scope, name="__casing__" + self.name, doc=None
        )
        instance.get_resolved().validate_use_targets()
        return instance.get_resolved().produce_casing_entities()


@dataclass
class ResolvedBox(node.CstNode[cst.Box], node.DocableEntity, typesys.NamedAttribute, typesys.MembershipEntity):
    """IR Node representing a resolved box declaration."""

    source_module: node.Module
    instances: Sequence[node.NamedEntity]
    connections: list[Connection]
    source: Box | None

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
                cpp_cog = cpp_executable.CppCog(cog_ir=instance.cog_class, dial_header=None, cog_header=None)
                result.cogs[instance.cog_class.value_key()] = cpp_cog
                _add_casing_cog_members(result, instance)
                if instance.cog_class.python_options:
                    assert isinstance(instance.cst_node, cst.NewStmt)
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
        if isinstance(member.member, cog.ConfigDef | cog.StateDef):
            iface_info = member.member.get_resolved().message_type
            if isinstance(iface_info, schema_reg.InterfaceInfo):
                entities.interfaces[iface_info] = iface_info
            else:
                entities.externs[iface_info.value_key()] = iface_info
        elif isinstance(member.member, cog.InputDef | cog.OutputDef | cog.MetricsOutputDef | cog.ReportGroupDef):
            iface_info = member.member.get_interface_info()
            entities.interfaces[iface_info] = iface_info
        elif isinstance(
            member.member,
            cog.ConditionDef | diagnostics.DiagnosticsDef | diagnostics.InfraDiagnosticsDef | cog.ResourceDef,
        ):  # pyright: ignore[reportUnnecessaryIsInstance]: Keep this explicit so that if other members are added we can fail the check below.
            # These don't have any casing entities associated with them
            pass
        else:
            msg = f"Unrecognized Cog member type {type(member.member)}"
            raise NotImplementedError(msg)


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
    ) -> Box:
        """Construct an IR node from a CST node."""
        if template.module is None or template.module.terminals is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        template_module = template.module
        inner_scope = lexical_scope.make_dynamic_scope(path_parent=instance_scope, name=instance_name)
        instances = []
        connections = []
        policy_stmts = []
        for element in cst_node.children_box_element():
            if new_stmt_cst := element.maybe_new_stmt():
                new_stmt = statement.NewStmt.from_cst(new_stmt_cst, template_module)
                instances.append(new_stmt)
                inner_scope.define(
                    new_stmt.name,
                    node.NameProxy(new_stmt.name, inner_scope, new_stmt, False),
                    template_module.terminals,
                )
            elif connect_cst := element.maybe_connect_stmt():
                connections.append(Connection.from_cst(connect_cst, template_module))
            elif apply_cst := element.maybe_policy_apply_stmt():
                policy_stmts.append(PolicyApplicationStmt.from_cst(apply_cst, template_module))
            else:
                msg = node.append_error_line(element, template_module, f"Box element type not supported: {element}")
                raise NotImplementedError(msg)
        return cls(
            type_info=clkbuiltins.BOX_TYPE,
            name=instance_name,
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

    # The complexity here is due to handling the various box elements. The branches consist of for loops and if instance checks.
    # The could be broken out into separate functions, but because of the relatively simple nature of what's being done here
    # it wouldn't enhance readability significantly, and would just add indirection.
    def resolve(self, source_module: node.Module) -> None:  # noqa: C901
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
            new_instance = instance.typespec.make_instance(
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
                    )
                )
            if isinstance(instance, cog.CogInstance):
                _register_signal_instances(instance.module.context, instance)
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
        self.resolved = ResolvedBox(
            name=self.name,
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

    def get_resolved(self) -> ResolvedBox:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    def _apply_policy_single(self, policy_apply: PolicyApplication) -> None:
        if not isinstance(
            policy_apply.target,
            cog.CogInstance
            | cog.CogInstanceMember
            | FirstMessageInstance
            | ProcessInstance
            | SerializedDataFileInstance
            | StateInstance
            | SignalInstanceSpec,
        ):
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

    @classmethod
    def from_cst(cls: type[Connection], cst_node: cst.ConnectStmt, module: node.Module) -> Connection:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        source = expr.Expr.from_cst(cst_node.child_source(), module)
        target = expr.Expr.from_cst(cst_node.child_target(), module)
        return cls(doc=doc, module=module, cst_node=cst_node, source=source, target=target)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.source, expr.Expr) or not isinstance(self.target, expr.Expr):
            msg = node.append_error_line(self, self.module, f"Attempt to resolve connection twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        self.source = self.source.evaluate()
        self.target = self.target.evaluate()
        if (
            isinstance(self.source, pubsub.Channel)
            and isinstance(self.target, cog.CogInstanceMember)
            and isinstance(self.target.member, cog.InputDef)
        ):
            if _input_needs_safety_margin(self.target):

                _validate_safety_margin(self.target, self.source, self.module)


            _validate_emergency_margin(self.target, self.source)
            _validate_skip_threshold(self.target, self.source)

        if (
            isinstance(self.source, cog.CogInstanceMember)
            and not self.source.cog_instance.cog_class.is_init()
            and isinstance(self.source.member, cog.OutputDef)
            and isinstance(self.target, pubsub.Channel)
            and self.target.is_published_once
        ):
            msg = node.append_error_line(
                self.source.cog_instance.cog_class,
                self.module,
                f"Channel {self.target.name} is published once but is an output of a non-initialization cog.",
            )
            raise ValueError(msg)


def _input_needs_safety_margin(target: cog.CogInstanceMember[cog.InputDef]) -> bool:
    """Returns true if the given input is associated with a new_msg+max condition."""
    for condition in target.cog_instance.cog_class.conditions.values():
        if (
            isinstance(condition.condition, cog.NewMessagePresent)
            and condition.condition.upper_bound is not None
            and condition.condition.input_name == target.member.name
        ):
            return True

    return False


def _get_safety_margin(target: cog.CogInstanceMember[cog.InputDef], channel: pubsub.Channel) -> int:
    """Return the the safety margin required by a cog input."""
    view_params = target.member.view_params
    assert isinstance(view_params.safety_margin, int)
    assert isinstance(view_params.max_msgs, int)
    if view_params.safety_margin == -1:
        # The user didn't specify a margin. Use the default.
        return max(int(channel.num_slots.value) // 2, view_params.max_msgs)

    return view_params.safety_margin



def _validate_safety_margin(
    target: cog.CogInstanceMember[cog.InputDef], channel: pubsub.Channel, module: node.Module
) -> None:
    """Raises a ValueError if the safety margin requested by a user is unsatisfiable for the given channel."""
    view_params = target.member.view_params
    assert isinstance(view_params.max_msgs, int)
    margin = _get_safety_margin(target, channel)
    if view_params.max_msgs + margin > channel.num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            module,
            f"Channel {channel.name} is not large enough ({channel.num_slots.value} messages) to accommodate safety margin ({margin} messages) for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages).",
        )
        raise ValueError(msg)

    emergency_margin = int(max(2, 0.1 * int(channel.num_slots.value)))
    if margin < emergency_margin:
        msg = node.append_error_line(
            view_params.cst_node,
            module,
            f"Safety margin ({margin} messages) for {target.cog_instance.name}.{target.member.name} is less than the emergency margin for {channel.name} ({emergency_margin} messages).",
        )
        raise ValueError(msg)



def _validate_emergency_margin(target: cog.CogInstanceMember[cog.InputDef], channel: pubsub.Channel) -> None:
    """Raises a ValueError if the emergency margin requested by a user is unsatisfiable for the given channel."""
    if channel.is_published_once:
        return

    view_params = target.member.view_params
    assert isinstance(view_params.max_msgs, int)

    emergency_margin = int(max(2, 0.1 * int(channel.num_slots.value)))
    if view_params.max_msgs + emergency_margin >= channel.num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Channel {channel.name} is not large enough ({channel.num_slots.value} messages) to accommodate emergency margin ({emergency_margin} messages) for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages).",
        )
        raise ValueError(msg)


def _validate_skip_threshold(target: cog.CogInstanceMember[cog.InputDef], channel: pubsub.Channel) -> None:
    """Raises a ValueError if the preemptive skip threshold requested by a user is too large for the given channel."""
    view_params = target.member.view_params
    if not isinstance(view_params.skip_threshold, int) or channel.is_published_once:
        return

    assert isinstance(view_params.max_msgs, int)
    if view_params.max_msgs + view_params.skip_threshold > channel.num_slots.value:
        msg = node.append_error_line(
            view_params.cst_node,
            view_params.module,
            f"Skip treshold ({view_params.skip_threshold} messages) and view for {target.cog_instance.name}.{target.member.name} ({view_params.max_msgs} messages) sum to more than the capacity of {channel.name} ({channel.num_slots.value} messages).",
        )
        raise ValueError(msg)


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
        typesys.unify(result.policy_data.type_info, policy.POLICY_DATA_TYPE)
        if is_recursive:
            typesys.unify(result.target.type_info, clkbuiltins.BOX_TYPE)
        return result

    def resolve(self) -> PolicyApplication:
        """Perform final IR resolution."""
        if self.resolved_value:
            return self.resolved_value
        policy_data = self.policy_data.evaluate()
        assert isinstance(policy_data, policy.UnboundPolicyData)
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
    file_path: Path

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
        return SerializedDataFileInstance(
            name=name,
            scope=scope,
            type_info=clkbuiltins.CONFIG_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            repr_typespec=self.repr_typespec,
            file_path=self.file_path,
        )

    @override
    def get_module(self) -> node.Module:
        return self.module


@dataclass
class SerializedDataFileInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """An instance of a textproto file."""

    repr_typespec: typesys.Instantiation
    file_path: Path


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
        if not isinstance(pathname, primitive.StringValue):
            msg = node.enrich_error_if_possible(ir_node, f"path must be a string value, not {pathname}")
            raise TypeError(msg)
        return SerializedDataFile(
            type_info=clkbuiltins.TYPE_TYPE, module=module, repr_typespec=repr_typespec, file_path=Path(pathname.value)
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
        return FirstMessageInstance(
            name=name,
            scope=scope,
            type_info=DATA_SOURCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            channel=self.channel,
            allow_default=self.allow_default,
        )

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
        return StateInstance(
            name=name,
            scope=scope,
            type_info=clkbuiltins.STATE_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            repr_typespec=self.repr_typespec,
            init_cog_endpoint=self.init_cog_endpoint,
            memory_resource=self.memory_resource,
        )

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
    def evaluate_call(  # pyright: ignore[reportIncompatibleMethodOverride] # TODO(DX-2384): Fix incompatible override errors
        self,
        ir_node: node.CstNode[cst.Expr] | None,
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
        for param in ("representation",):
            if param not in kw_args:
                msg = node.append_error_line(ir_node, module, f"Missing parameter {kw_args}")
                raise ValueError(msg)
        repr_typespec = kw_args["representation"]
        if not (
            (isinstance(repr_typespec, typesys.Instantiation) and repr_typespec.instantiates is clkbuiltins.TACHYON)
            or isinstance(repr_typespec, extern_type.ExternType)
        ):
            msg = node.append_error_line(
                ir_node, module, f"Representation must be a Tachyon instantiation or extern type, not {repr_typespec}"
            )
            raise TypeError(msg)
        if init_cog := kw_args.get("init"):
            if (
                not isinstance(init_cog, cog.CogInstanceMember)
                or not init_cog.cog_instance.cog_class.is_init()
                or not isinstance(init_cog.member, cog.StateDef)
                or not init_cog.member.params.mutable
            ):
                msg = node.append_error_line(
                    ir_node,
                    module,
                    "State init parameter must be a mutable state endpoint of an init cog",
                )
                raise TypeError(msg)
            init_cog_endpoint = init_cog
        else:
            init_cog_endpoint = None
        if isinstance(repr_typespec, extern_type.ExternType):
            try:
                memres = kw_args["memory_resource"]
            except KeyError:
                msg = node.append_error_line(ir_node, module, "Extern type state must have a memory_resource parameter")
                raise ValueError(msg) from None
            if not isinstance(memres, MemoryResourceInstance):
                msg = node.append_error_line(
                    ir_node, module, f"memory_resource must be a memory resource instance, not {type(memres)}"
                )
                raise TypeError(msg)
        else:
            memres = None
        assert memres is None or isinstance(memres, MemoryResourceInstance)
        assert isinstance(repr_typespec, typesys.Instantiation | extern_type.ExternType)
        return State(
            type_info=clkbuiltins.TYPE_TYPE,
            module=module,
            repr_typespec=repr_typespec,
            init_cog_endpoint=init_cog_endpoint,
            memory_resource=memres,
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
        return MemoryResourceInstance(
            name=name,
            scope=scope,
            type_info=clkbuiltins.MEMORY_RESOURCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            resource_type=self.resource_type,
            max_size=self.max_size,
        )

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
        return ProcessInstance(
            name=name,
            scope=scope,
            type_info=clkbuiltins.PROCESS_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            executable=self.executable,
        )

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
    target_bound=(
        clkbuiltins.COG_INSTANCE_TYPE,
        clkbuiltins.CONFIG_INSTANCE_TYPE,
        clkbuiltins.MEMORY_RESOURCE_TYPE,
        clkbuiltins.STATE_INSTANCE_TYPE,
        clkbuiltins.UDP_SOCKET_INSTANCE_TYPE,
        clkbuiltins.AUDIO_SOURCE_INSTANCE_TYPE,
        DATA_SOURCE_TYPE,
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
    target_bound=(clkbuiltins.PROCESS_TYPE,),
    schema=HOST_CPU_DOMAIN_POLICY_SCHEMA,
    source=None,
)

clkbuiltins.BUILTINS_SCOPE.define(HOST_CPU_DOMAIN_POLICY.name, HOST_CPU_DOMAIN_POLICY, None)
