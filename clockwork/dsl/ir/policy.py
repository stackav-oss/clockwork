# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Policy IR nodes."""

from __future__ import annotations

from collections import defaultdict
from copy import copy
from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.compiler_context import CompilerContext, ContextKey
from clockwork.dsl.ir import clkbuiltins, expr, node, schema, statement, typesys
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterable, Sequence

POLICY_DATA_TYPE: Final = typesys.TypeDef(
    scope=clkbuiltins.BUILTINS_SCOPE, name="PolicyData", type_info=clkbuiltins.TYPE_TYPE
)


@dataclass(slots=True)
class PolicyClass(node.DocableEntity, typesys.NamedValue, typesys.CallableEntity):
    """IR Node representing a resolved policy class."""

    target_bound: Sequence[typesys.TypeVal]
    schema: schema.InstantiatedSchema
    source: PolicyDef | None

    @override
    def evaluate_call(
        self,
        *,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> UnboundPolicyData:
        """Evaluate the call operation."""
        kw_args = []
        for name, value in args:
            if name is None:
                msg = node.append_error_line(ir_node, module, "Only named arguments supported for policies.")
                raise ValueError(msg)
            kw_args.append((name, value))
        return UnboundPolicyData(
            type_info=POLICY_DATA_TYPE,
            policy_class=self,
            data=schema.SchemaInstance.from_args(
                schema_ir=self.schema, args=kw_args, error_report_node=ir_node, error_report_module=module
            ),
            source=ir_node,
        )


@dataclass
class PolicyDef(node.CstNode[cst.PolicyDef], node.DocableEntity, typesys.NamedValue, typesys.CallableEntity):
    """IR Node representing a policy class definition ("def policy")."""

    target_bound: Sequence[typesys.TypeVal] | expr.Expr
    schema: schema.InstantiatedSchema | expr.Expr
    resolved: PolicyClass | None

    @classmethod
    def from_cst(cls: type[PolicyDef], cst_node: cst.PolicyDef, module: node.Module) -> PolicyDef:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        target_bound = expr.Expr.from_cst(cst_node.child_policy_def_binds().child_typespec(), module)
        typesys.unify(target_bound.type_info, clkbuiltins.TYPE_TYPE)
        schema_expr = expr.Expr.from_cst(cst_node.child_policy_def_schema().child_typespec(), module)
        typesys.unify(schema_expr.type_info, clkbuiltins.TYPE_TYPE)
        return cls(
            name=name,
            scope=module.inner_scope,
            type_info=clkbuiltins.POLICY_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            target_bound=target_bound,
            schema=schema_expr,
            resolved=None,
        )

    def resolve(self) -> None:
        """Perform IR finalization."""
        if self.resolved or not isinstance(self.target_bound, expr.Expr) or not isinstance(self.schema, expr.Expr):
            msg = self.append_error_line("Attempt to finalize PolicyDef twice")
            raise RuntimeError(msg)
        target_bound = self.target_bound.evaluate()
        if not isinstance(target_bound, typesys.TypeVal):
            msg = self.target_bound.append_error_line(f"Expected a type but got {target_bound}")
            raise TypeError(msg)
        typespec = self.schema.evaluate()
        if isinstance(typespec, typesys.Instantiation):
            if not isinstance(typespec.instantiates, schema.Schema):
                msg = self.schema.append_error_line("Only schema instantiations allowed here")
                raise TypeError(msg)
        elif not isinstance(typespec, schema.Schema):
            msg = self.schema.append_error_line(f"Expected schema type, got {typespec}")
            raise TypeError(msg)
        assert isinstance(typespec, schema.Schema | typesys.Instantiation)
        self.schema = schema.InstantiatedSchema.from_typespec(typespec)
        self.target_bound = (target_bound,)
        self.resolved = PolicyClass(
            name=self.name,
            scope=self.scope,
            type_info=self.type_info,
            doc=self.doc,
            target_bound=self.target_bound,
            schema=self.schema,
            source=self,
        )

    def get_resolved(self) -> PolicyClass:
        """Get a resolved policy class."""
        if not self.resolved:
            msg = self.append_error_line("Attempt to access unresolved PolicyDef")
            raise RuntimeError(msg)
        return self.resolved

    @override
    def evaluate_call(
        self,
        *,
        ir_node: node.CstNode[cst.Expr] | None,
        module: node.Module,
        args: Sequence[tuple[str | None, typesys.Value]],
    ) -> UnboundPolicyData:
        """Evaluate the call operation."""
        return self.get_resolved().evaluate_call(ir_node=ir_node, module=module, args=args)


@dataclass(slots=True)
class UnboundPolicyData(typesys.ObjectIdentityValue):
    """Policy data not associated with a specific entity."""

    policy_class: PolicyClass
    data: schema.SchemaInstance
    source: node.CstNode[cst.Expr] | None


@dataclass(slots=True, frozen=True)
class PolicyData:
    """Policy data associated with a specific entity."""

    policy_class: PolicyClass
    data: schema.SchemaInstance
    target: typesys.Value
    source: PolicyInstance | node.CstNode[cst.Expr] | None


@dataclass
class PolicyInstance(node.CstNode[cst.Policy], node.DocableEntity, typesys.Value):
    """IR Node representing a policy instance definition ("policy X for Y")."""

    inner_scope: node.Scope
    policy_class: PolicyClass | expr.Expr
    target: typesys.Value | expr.Expr
    bindings: list[statement.ImmutableBinding]
    data: schema.SchemaInstance | None
    resolved: PolicyData | None

    @classmethod
    def from_cst(cls: type[PolicyInstance], cst_node: cst.Policy, module: node.Module) -> PolicyInstance:
        """Construct an IR node from a CST node."""
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        policy_class = expr.Expr.from_cst(cst_node.child_policy_class(), module)
        target = expr.Expr.from_cst(cst_node.child_target(), module)
        inner_scope = module.inner_scope.make_anon_child_scope("policy")
        bindings = [
            statement.ImmutableBinding.from_cst(binding_cst, module, inner_scope)
            for binding_cst in cst_node.children_assignment_stmt()
        ]
        return cls(
            type_info=clkbuiltins.POLICY_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            inner_scope=inner_scope,
            policy_class=policy_class,
            target=target,
            bindings=bindings,
            data=None,
            resolved=None,
        )

    @override
    def value_key(self) -> str:
        """Generate a unique, comparable, hashable type key for this type."""
        if isinstance(self.target, expr.Expr) or isinstance(self.policy_class, expr.Expr):
            msg = self.append_error_line("Attempt to compute value key for unresolved PolicyInstance")
            raise RuntimeError(msg)  # noqa: TRY004 (Accesing unresolved instance is a runtime error)
        return f"Policy({self.policy_class.value_key()}, {self.target.value_key()}"

    def resolve(self) -> None:
        """Finalize the IR."""
        if not isinstance(self.policy_class, expr.Expr) or not isinstance(self.target, expr.Expr):
            msg = self.append_error_line("Attempt to resolve policy twice")
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        policy = self.policy_class.evaluate()
        if isinstance(policy, PolicyDef):
            policy_class = policy.get_resolved()
        elif isinstance(policy, PolicyClass):
            policy_class = policy
        else:
            msg = self.policy_class.append_error_line(f"Expected a policy class, but got {policy}")
            raise TypeError(msg)
        target = self.target.evaluate()
        if target.type_info not in policy_class.target_bound:
            expected_type = (
                f"{policy_class.target_bound[0]}"
                if len(policy_class.target_bound) == 1
                else f"one of {policy_class.target_bound}"
            )
            msg = self.target.append_error_line(f"Expected {expected_type} but got {target.type_info}")
            raise TypeError(msg)
        schema_instance = schema.SchemaInstance.from_unresolved_bindings(
            policy_class.schema,
            self.bindings,
            self,
            self.module,
        )
        self.policy_class = policy_class
        self.target = target
        self.data = schema_instance

    def get_resolved(self) -> PolicyData:
        """Access resolved policy data."""
        if self.resolved:
            return self.resolved
        if not isinstance(self.policy_class, PolicyClass) or isinstance(self.target, expr.Expr) or self.data is None:
            msg = self.append_error_line("Attempt to access unresolved policy")
            raise RuntimeError(msg)
        self.resolved = PolicyData(policy_class=self.policy_class, data=self.data, target=self.target, source=self)
        return self.resolved


@dataclass(slots=True)
class PolicyContext:
    """Compiler Context for policy instances."""

    # This is a registry of registries; each PolicyClass has its own registry
    # The key in the outer dict is the value_key of the PolicyClass.
    # The key in the inner dicts is the value_key of the target to which the policy is attached
    registry: dict[str, dict[str, PolicyData]]

    def import_from(self, other: PolicyContext) -> None:
        """Combine this context with items from another."""
        for policy_class_id, policy_reg in other.registry.items():
            if policy_class_id in self.registry:
                self._merge_registry(policy_reg)
            else:
                self.registry[policy_class_id] = copy(policy_reg)

    def register_policy(self, policy: PolicyData, merging: bool = False) -> None:
        """Register a PolicyData."""
        policy_reg = self.registry[policy.policy_class.value_key()]
        key = policy.target.value_key()
        if key in policy_reg:
            if merging and policy_reg[key] is policy:
                # This is already in our context via some other import path
                return
            msg = f"Policy {policy.policy_class.value_key()} already specified for {policy.target.value_key()}"
            if policy.source:
                msg = policy.source.append_error_line(msg)
            existing = policy_reg[key]
            if existing.source:
                msg += existing.source.append_error_line(
                    f"Original definition in {existing.source.module.inner_scope.uniq_path}"
                )
            raise ValueError(msg)
        if policy.target.type_info not in policy.policy_class.target_bound:
            expected_type = (
                f"{policy.policy_class.target_bound[0]}"
                if len(policy.policy_class.target_bound) == 1
                else f"one of {policy.policy_class.target_bound}"
            )
            msg = f"Expected {expected_type} but got {policy.target.type_info}"
            if policy.source:
                msg = policy.source.append_error_line(msg)
            raise TypeError(msg)
        policy_reg[key] = policy

    def _merge_registry(self, other_reg: dict[str, PolicyData]) -> None:
        for policy_instance in other_reg.values():
            self.register_policy(policy_instance, merging=True)


class PolicyKey(ContextKey[PolicyContext]):
    """Compiler context key."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> PolicyContext:
        """Create a default (empty) instance of the context."""
        return PolicyContext(registry=defaultdict(dict))


_POLICY_KEY: Final = PolicyKey("PolicyKey")


def register_policy(module: node.Module, policy: PolicyInstance) -> None:
    """Register a PolicyInstance."""
    context = module.context[_POLICY_KEY]
    context.register_policy(policy.get_resolved())


def bind_policy_data(module: node.Module, policy_data: UnboundPolicyData, target: typesys.Value) -> None:
    """Bind policy data to a target."""
    context = module.context[_POLICY_KEY]
    bound = PolicyData(
        policy_class=policy_data.policy_class, data=policy_data.data, target=target, source=policy_data.source
    )
    context.register_policy(bound)


def try_bind_policy_data(module: node.Module, policy_data: UnboundPolicyData, target: typesys.Value) -> bool:
    """Bind policy data to a target if the target is of the right type."""
    context = module.context[_POLICY_KEY]
    if target.type_info not in policy_data.policy_class.target_bound:
        return False
    bound = PolicyData(
        policy_class=policy_data.policy_class, data=policy_data.data, target=target, source=policy_data.source
    )
    context.register_policy(bound)
    return True


def lookup_policy(module: node.Module, policy_class: PolicyClass, target: typesys.Value) -> PolicyData | None:
    """Look up a policy for a target."""
    context = module.context[_POLICY_KEY]
    try:
        return context.registry[policy_class.value_key()][target.value_key()]
    except KeyError:
        return None


def lookup_all_policies(module: node.Module, policy_class: PolicyClass) -> Iterable[PolicyData]:
    """Look up all policies for a specific policy class."""
    context = module.context[_POLICY_KEY]
    return context.registry[policy_class.value_key()].values()


def iterate_all_policy_data(module: node.Module) -> Iterable[PolicyData]:
    """Iterate over all registered policy data in the module.

    Yields:
        All PolicyData instances registered in the module.
    """
    context = module.context[_POLICY_KEY]
    for policy_reg in context.registry.values():
        yield from policy_reg.values()
