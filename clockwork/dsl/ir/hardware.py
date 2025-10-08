# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Hardware description IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final

from clockwork.dsl import cst
from clockwork.dsl.compiler_context import CompilerContext, ContextKey
from clockwork.dsl.ir import (
    clkbuiltins,
    cst_util,
    expr,
    node,
    primitive,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override


def _cpu_set_from_cst(cst_node: cst.CpuSet, module: node.Module) -> list[int]:
    """Parse a CPU set from CST.

    Arguments:
        cst_node: CpuSet CST node.
        module: Source module.

    Returns:
        List of CPUs in the cpu set.
    """
    assert module.terminals
    start_cpu = cst_util.int_from_cst(cst_node.child_start_cpu(), module.terminals)
    if maybe_default_end_cpu := cst_node.maybe_end_cpu():
        end_cpu = cst_util.int_from_cst(maybe_default_end_cpu, module.terminals)
        if end_cpu < start_cpu:
            msg = node.append_error_line(cst_node, module, "Invalid CPU range, end cannot be less than start")
            raise ValueError(msg)
    else:
        end_cpu = start_cpu
    return list(range(start_cpu, end_cpu + 1))


@dataclass
class CpuDomain(node.CstNode[cst.CpuDomain], node.DocableEntity, node.NamedEntity, typesys.Value):
    """IR Node representing a cpu_domain declaration."""

    # Additional simplelaunch source labels added to the domain
    simplelaunch_srcs: list[str | expr.Expr]
    # Simplelaunch node name
    simplelaunch_node: str | expr.Expr | None
    # Bridge CPUs
    bridge_cpus: list[int]
    # Default CPUs
    default_cpus: list[int]
    # Logging backup CPU domain
    logging_backup: CpuDomain | expr.Expr | None

    @classmethod
    def from_cst(cls: type[CpuDomain], cst_node: cst.CpuDomain, module: node.Module) -> CpuDomain:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)

        simplelaunch_node: str | expr.Expr | None = None
        simplelaunch_srcs: list[str | expr.Expr] = []
        bridge_cpus: list[int] = []
        default_cpus: list[int] = []
        logging_backup: CpuDomain | expr.Expr | None = None
        if cpu_domain_options := cst_node.maybe_cpu_domain_options():
            if maybe_simplelaunch_node := cpu_domain_options.maybe_simplelaunch_node():
                simplelaunch_node = expr.Expr.from_cst(maybe_simplelaunch_node.child_node(), module)
            simplelaunch_srcs.extend(
                [
                    expr.Expr.from_cst(simplelaunch_src.child_label(), module)
                    for simplelaunch_src in cpu_domain_options.children_simplelaunch_src()
                ]
            )
            if maybe_bridge_cpus := cpu_domain_options.maybe_bridge_cpus():
                bridge_cpus = _cpu_set_from_cst(maybe_bridge_cpus.child_cpu_set(), module)
            if maybe_default_cpus := cpu_domain_options.maybe_default_cpus():
                default_cpus = _cpu_set_from_cst(maybe_default_cpus.child_cpu_set(), module)
            if maybe_logging_backup := cpu_domain_options.maybe_logging_backup():
                logging_backup = expr.Expr.from_cst(maybe_logging_backup.child_cpu_domain(), module)
                typesys.unify(logging_backup.type_info, clkbuiltins.CPU_DOMAIN_TYPE)

        result = cls(
            type_info=clkbuiltins.CPU_DOMAIN_TYPE,
            scope=module.inner_scope,
            name=name,
            doc=doc,
            module=module,
            cst_node=cst_node,
            simplelaunch_node=simplelaunch_node,
            simplelaunch_srcs=simplelaunch_srcs,
            bridge_cpus=bridge_cpus,
            default_cpus=default_cpus,
            logging_backup=logging_backup,
        )
        module.context[_CPU_DOMAIN_GLOBAL_NAMESPACE_KEY].register(name, result)
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if isinstance(self.simplelaunch_node, expr.Expr):
            node_val = self.simplelaunch_node.evaluate()
            if not isinstance(node_val, primitive.StringValue):
                msg = self.simplelaunch_node.append_error_line("Invalid simplelaunch node")
                raise TypeError(msg)
            self.simplelaunch_node = node_val.value
        simplelaunch_srcs: list[str | expr.Expr] = []
        for label_expr in self.simplelaunch_srcs:
            if not isinstance(label_expr, expr.Expr):
                msg = self.append_error_line("Attempt to resolve more than once.")
                raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
            label_val = label_expr.evaluate()
            if not isinstance(label_val, primitive.StringValue):
                msg = label_expr.append_error_line("Invalid simplelaunch label")
                raise TypeError(msg)
            simplelaunch_srcs.append(label_val.value)
        self.simplelaunch_srcs = simplelaunch_srcs
        if isinstance(self.logging_backup, expr.Expr):
            logging_backup = self.logging_backup.evaluate()
            assert isinstance(logging_backup, CpuDomain)
            if logging_backup == self:
                msg = self.logging_backup.append_error_line("Logging backup must be a different CPU domain")
                raise TypeError(msg)
            self.logging_backup = logging_backup

    @override
    def value_key(self) -> str:
        """Generate a comparable, hashable, string representation of this value."""
        return f"CpuDomain({self.name})"

    def simplelaunch_node_name(self) -> str:
        """Generate the name of the simplelaunch node for the CPU domain."""
        if isinstance(self.simplelaunch_node, str):
            return self.simplelaunch_node
        return self.name.lower()


@dataclass
class CpuDomainGlobalNamespaceContext:
    """Compiler Context for connections between CpuDomains."""

    cpu_domains: dict[str, CpuDomain]

    def import_from(self, other: CpuDomainGlobalNamespaceContext) -> None:
        """Combine this context with items from another."""
        for name, cpu_domain in other.cpu_domains.items():
            self.register(name, cpu_domain)

    def register(self, name: str, cpu_domain: CpuDomain) -> None:
        """Register a new CPU Domain, ensuring globally-unique name."""
        try:
            ours = self.cpu_domains[name]
            if ours is not cpu_domain:
                msg = ours.append_error_line(f"Multiple CPU domains with name {name}")
                msg += f"\n{cpu_domain.append_error_line('Conflicting definition here')}"
                raise ValueError(msg)
        except KeyError:
            pass
        self.cpu_domains[name] = cpu_domain


class CpuDomainGlobalNamespaceContextKey(ContextKey[CpuDomainGlobalNamespaceContext]):
    """Compiler context key."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> CpuDomainGlobalNamespaceContext:
        """Create a default (empty) instance of the context."""
        return CpuDomainGlobalNamespaceContext(cpu_domains={})


_CPU_DOMAIN_GLOBAL_NAMESPACE_KEY: Final = CpuDomainGlobalNamespaceContextKey("CpuDomainGlobalNamespaceContextKey")


@dataclass
class CpuDomainConnectionContext:
    """Compiler Context for connections between CpuDomains."""

    lan_nodes: dict[str, EthernetNode]

    def import_from(self, other: CpuDomainConnectionContext) -> None:
        """Combine this context with items from another."""
        for cpu_domain, lan_node in other.lan_nodes.items():
            try:
                ours = self.lan_nodes[cpu_domain]
                if ours is not lan_node:
                    msg = ours.append_error_line(f"Multiple Ethernet connections for {cpu_domain}")
                    msg += f"\n{lan_node.append_error_line('Conflicting definition here')}"
                    raise ValueError(msg)
            except KeyError:
                pass
            self.lan_nodes[cpu_domain] = lan_node


class CpuDomainConnectionKey(ContextKey[CpuDomainConnectionContext]):
    """Compiler context key."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> CpuDomainConnectionContext:
        """Create a default (empty) instance of the context."""
        return CpuDomainConnectionContext(lan_nodes={})


_CPU_DOMAIN_CONNECTION_KEY: Final = CpuDomainConnectionKey("CpuDomainConnectionKey")


@dataclass
class EthernetLan(node.CstNode[cst.Ethernet], node.DocableEntity, typesys.NamedValue):
    """IR Node representing an ethernet declaration."""

    addresses: dict[str, EthernetNode]

    @classmethod
    def from_cst(cls: type[EthernetLan], cst_node: cst.Ethernet, module: node.Module) -> EthernetLan:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        result = cls(
            type_info=clkbuiltins.CPU_DOMAIN_TYPE,
            scope=module.inner_scope,
            name=name,
            doc=doc,
            module=module,
            cst_node=cst_node,
            addresses={},
        )
        result.addresses = {
            node.address: node
            for node in (
                EthernetNode.from_cst(address, module, result) for address in cst_node.children_ethernet_ipv4()
            )
        }
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for address in self.addresses.values():
            address.resolve()


@dataclass
class EthernetNode(node.CstNode[cst.EthernetIpv4], node.DocableEntity, typesys.ObjectIdentityValue):
    """IR Node representing an ethernet node declaration."""

    lan: EthernetLan
    address: str
    cpu_domain: CpuDomain | expr.Expr
    bridge_ports: tuple[int, int]

    def name_resolution_fields(self) -> list[str]:
        """Override recursion for node.resolve_names.

        This prevents us from recursively resolving back to the EthernetLan
        """
        return ["cpu_domain"]

    @classmethod
    def from_cst(
        cls: type[EthernetNode], cst_node: cst.EthernetIpv4, module: node.Module, lan: EthernetLan
    ) -> EthernetNode:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        address = get_span(cst_node.child_ipv4_address().span, module.terminals)
        cpu_domain = expr.Expr.from_cst(cst_node.child_cpu_domain(), module)
        typesys.unify(cpu_domain.type_info, clkbuiltins.CPU_DOMAIN_TYPE)
        bridge_ports = cst_node.child_bridge_ports()
        port_min = cst_util.int_from_cst(bridge_ports.child_begin(), module.terminals)
        port_max = cst_util.int_from_cst(bridge_ports.child_end(), module.terminals)
        return cls(
            type_info=clkbuiltins.CPU_DOMAIN_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            lan=lan,
            address=address,
            cpu_domain=cpu_domain,
            bridge_ports=(port_min, port_max),
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.cpu_domain, expr.Expr):
            msg = self.append_error_line("Attempt to resolve more than once.")
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        cpu_domain = self.cpu_domain.evaluate()
        assert isinstance(cpu_domain, CpuDomain)
        existing = get_cpu_domain_connection(self.module, cpu_domain)
        if existing is not None:
            msg = self.cpu_domain.append_error_line(f"Multiple LAN connections for {cpu_domain.value_key()}")
            msg += f"\n{node.append_error_line(existing.cst_node, existing.module, 'Previous connection here')}"
            raise ValueError(msg)
        context = self.module.context[_CPU_DOMAIN_CONNECTION_KEY]
        context.lan_nodes[cpu_domain.value_key()] = self
        self.cpu_domain = cpu_domain


def get_cpu_domain_connection(system_module: node.Module, cpu_domain: CpuDomain) -> EthernetNode | None:
    """Look up a CPU Domain's Ethernet connection."""
    context = system_module.context[_CPU_DOMAIN_CONNECTION_KEY]
    try:
        return context.lan_nodes[cpu_domain.value_key()]
    except KeyError:
        return None
