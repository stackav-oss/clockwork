# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Generic Base Classes."""

from __future__ import annotations

import collections.abc
import dataclasses
import functools
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import (
    TYPE_CHECKING,
    Any,
    Generic,
    Protocol,
    TypeVar,
    Union,  # pyright: ignore[reportDeprecated] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
)

from fltk.fegen.pyrt.terminalsrc import Span

if TYPE_CHECKING:  # pragma: no cover
    from collections.abc import Container, Iterable, Iterator, Sequence

    from clockwork.dsl.ir.module_id import ModuleID
    from fltk.fegen.pyrt.terminalsrc import TerminalSource


from clockwork.dsl import compiler_context
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span, span_for_node

# These generated files must be imported on a separate line from the source file import above due to a pyright limitation:
# https://github.com/microsoft/pyright/issues/3630
from clockwork.dsl import cst  # isort: skip


T = TypeVar("T")
CstNodeTypes = TypeVar("CstNodeTypes")


class Node(ABC, Generic[CstNodeTypes]):
    """Abstract Base Class for IR Nodes."""

    @abstractmethod
    def get_module(self) -> Module:
        """Access the Module in which this node was defined or created."""

    @abstractmethod
    def get_cst_nodes(self) -> Iterable[CstNodeTypes]:
        """Access the CST nodes associated with this IR node, if any."""


@dataclass
class MultiCstNode(Node[CstNodeTypes], Generic[CstNodeTypes]):
    """Base class for IR nodes that can correspond to more than one CST node."""

    module: Module = field(repr=False)
    cst_nodes: Sequence[CstNodeTypes] = field(repr=False)

    def get_module(self) -> Module:
        """Access the Module in which this node was defined or created."""
        return self.module

    def get_cst_nodes(self) -> Iterable[CstNodeTypes]:
        """Access the CST nodes associated with this IR node, if any."""
        return self.cst_nodes


CstNodeType = TypeVar("CstNodeType")


@dataclass
class CstNode(Node[CstNodeType], Generic[CstNodeType]):
    """Base class for IR nodes that correspond to (at most) one CST node."""

    module: Module = field(repr=False)
    cst_node: CstNodeType | None = field(repr=False, compare=False)

    def get_module(self) -> Module:
        """Access the Module in which this node was defined or created."""
        return self.module

    def get_cst_nodes(self) -> Iterable[CstNodeType]:
        """Access the CST node associated with this IR node, if there is one."""
        if self.cst_node is not None:
            yield self.cst_node
        return

    def append_error_line(self, msg: str) -> str:
        """Append source line/col information to an error message, if possible.

        This only works if this node has access to a valid CST node and
        TerminalSrc.  Otherwise, the provided message string is returned
        unchanged.

        Args:
            msg: Error message, without line information.
        """
        return append_error_line(self.cst_node, self.module, msg)


def append_error_line(cst_node: CstNodeType | None, module: Module, msg: str) -> str:  # pyright: ignore[reportInvalidTypeVarUse] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    """Append source line/col information to an error message attached to a cst node, if possible.

    This only works if this node has access to a valid CST node and
    TerminalSrc.  Otherwise, the provided message string is returned
    unchanged.

    Args:
        cst_node: CST node with the error.
        module: Module object containing the CST node.
        msg: Error message, without line information.
    """
    if cst_node is None or module.terminals is None or not hasattr(cst_node, "span"):
        return msg
    msg += format_line_with_error(cst_node.span, module.terminals, module.module_id)  # pyright: ignore[reportAttributeAccessIssue] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    return msg


def enrich_error_if_possible(entity: Any, msg: str) -> str:  # noqa: ANN401 (Any is essential given nature of function)
    """Attempt to enrich an error message with information about the entity.

    This introspects the object to see if there's any information about the
    entity useful for locating it in source or otherwise identifying it. It's
    intended for use when you've lost type information at runtime. This should
    not be used if you have type information about the entity that allows you to
    use a more specific function, such as append_error_line.
    """
    if hasattr(entity, "append_error_line"):
        return str(entity.append_error_line(msg))
    if hasattr(entity, "span") and isinstance(entity.span, Span):
        return append_error_line(entity, entity.module, msg)
    if hasattr(entity, "fqn"):
        return f"In entity {entity.fqn}:\n{msg}"
    if hasattr(entity, "value_key"):
        return f"In entity {entity.value_key()}:\n{msg}"
    if hasattr(entity, "module") and isinstance(entity.module, Module):
        return f"In module {entity.module.inner_scope.uniq_path}:\n{msg}"
    return f"Entity of type {type(entity)}:\n{msg}"


@dataclass
class DocableEntity:
    """Base class for things which can optionally have a Doc node."""

    doc: Doc | None


@dataclass
class DocRequiredEntity:
    """Base class for things which must have a Doc node."""

    doc: Doc


@dataclass
class NamedEntity:
    """Base class for things which are bound to names."""

    name: str
    scope: Scope = field(repr=False)

    @functools.cached_property
    def fqn(self) -> str:
        """Return the unique, fully-qualified name of this type.

        Raises ValueError if the FQN cannot be determined.
        """
        return f"{self.scope.uniq_path}.{self.name}"


TemporaryNodeType = TypeVar("TemporaryNodeType")


@dataclass
class NameProxy(NamedEntity, Generic[TemporaryNodeType]):
    """Temporary proxy to a named entity during IR creation.

    This is used to handle name resolution for objects that will be replaced
    with another object following in a stage of IR generation after name
    resolution.  Until that stage is complete, the name will resolve to this
    proxy object.  The proxy will later be replaced with the referent.

    Attributes:
        referent: The node this name refers to
        is_final: If True, then the final referent has been created and the proxy can be replaced with referent
    """

    referent: TemporaryNodeType | NamedEntity
    is_final: bool

    def final_value(self) -> NamedEntity:
        """Return the final referrent, or raise error if not finalized.

        Returns: The final referent

        Raises:
            ValueError if the referent is not final (is_final is False)
        """
        if not self.is_final:
            msg = f"Attempt to retrieve final referrent of non-finalized NameProxy: {self.referent}"
            raise ValueError(msg)
        assert isinstance(self.referent, NamedEntity)  # noqa: S101 (for mypy)
        return self.referent

    def finalize(self, final_referent: NamedEntity, replace_in_scope: bool = True) -> None:
        """Record the final referent for this proxy."""
        if replace_in_scope:
            # This will call back into us with replace_in_scope=False
            self.scope.finalize_proxy(self.name, final_referent)
            return
        if self.is_final:
            msg = f"Attempt to finalize a NameProxy twice: {self.referent}, {final_referent}"
            raise RuntimeError(msg)
        self.referent = final_referent
        self.is_final = True


@dataclass(frozen=True, eq=True, slots=True)
class ImportSpec:
    """Specifies how an import will be performed.

    Attributes:
        module_id: Module ID to be imported.
        entity_name: The single entity name to import from that module, or if None then the module itself is imported.
        import_name: The local name given to the thing being imported (might be an alias).
    """

    module_id: ModuleID
    entity_name: str | None
    import_name: str


class Importer(Protocol):
    """Protocol for classes which provide module loading."""

    def resolve_import(self, enclosing_module: Module, use_result: Module.UseResult) -> ImportSpec:  # pyright: ignore[reportReturnType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Resolve how a UseResult will be interpreted.

        Args:
          enclosing_module: The module in which the using statement was declared.
          use_result: The using statement from the clk file.

        Returns:
            A specification of how to perform the import.
        """

    def execute_import(
        self, spec: ImportSpec, enclosing_module: Module, use_result: Module.UseResult
    ) -> tuple[Module, NamedEntity | None]:  # pyright: ignore[reportReturnType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Execute an import.

        Returns:
            The imported module and, optionally, a specific entity to import from it.
        """

    def try_cached_load(self, module_id: ModuleID) -> Module | None:
        """Load a module from the Module cache.

        Returns:
            The Module if it's already loaded and cached, otherwise None
        """

    def cache_module(self, module_id: ModuleID, module: Module) -> None:
        """Add a compiled module to the Module cache."""


@dataclass
class Module(Node[cst.Module], DocableEntity):
    """Represents a DSL module."""

    module_id: ModuleID
    inner_scope: Scope
    terminals: TerminalSource | None = field(repr=False)
    cst_node: cst.Module | None = field(repr=False)
    unresolved_imports: list[UseResult]
    context: compiler_context.CompilerContext = field(repr=False)

    @classmethod
    def from_cst(
        cls: type[Module],
        *,
        module_id: ModuleID,
        builtins: Scope,
        cst_node: cst.Module,
        terminals: TerminalSource,
    ) -> Module:
        """Create an IR Module from a CST Module."""
        uniq_path = module_id.get_fqn()
        # The externs scope holds names that are imported into the module.  This
        # keeps them separate from the names defined in the module.  When
        # importing names from another module, we only import the names directly
        # defined in the module, not the modules it imports.  You don't get
        # names by transitive inclusion.
        externs_scope = Scope(parent=builtins, uniq_path=uniq_path, module_id_for_errors=module_id)
        # inner_scope is where things defined in this module end up.
        inner_scope = Scope(parent=externs_scope, uniq_path=uniq_path, module_id_for_errors=module_id)
        result = cls(
            module_id=module_id,
            cst_node=cst_node,
            terminals=terminals,
            inner_scope=inner_scope,
            doc=None,
            unresolved_imports=[],
            context=compiler_context.CompilerContext(uniq_path),
        )
        if doc := cst_node.maybe_doc():
            result.doc = Doc.from_cst(doc, module=result)

        for use in cst_node.children_use():
            result.unresolved_imports.extend(result._handle_use(use.child_use_body()))
        return result

    def get_module(self) -> Module:
        """Return self (to implement Node interface)."""
        return self

    def get_cst_nodes(self) -> Iterable[cst.Module]:
        """Access the CST nodes associated with this IR node, if any."""
        if self.cst_node is not None:
            return [self.cst_node]
        return []

    @dataclass(frozen=True, eq=True, slots=True)
    class UseResult:
        """Represents a parse result from a use statement.

        Note: A single use statement in the source can result in multiple names
        being imported.  For every name being imported, there will be a separate
        UseResult created.

        Attributes:
            path: Full path of the entity being imported.  The final element is the import.
            alias: Alias for this entity, if any.
        """

        repo: str | None
        path: tuple[str, ...]
        alias: str | None
        cst_node: cst.UseBody | None = field(default=None, compare=False)

    def _handle_use(self, use: cst.UseBody) -> Iterator[UseResult]:
        assert self.terminals is not None  # noqa: S101  (for mypy)
        if use_repo := use.maybe_use_repo():
            repo_name = get_span(use_repo.child_identifier().child_value(), self.terminals)
        else:
            repo_name = None
        use_path = tuple(
            get_span(identifier.child_value(), self.terminals)
            for identifier in use.child_use_path().children_identifier()
        )

        if block := use.maybe_use_block():
            for sub_use in block.children_use_body():
                for sub_result in self._handle_use(sub_use):
                    yield dataclasses.replace(sub_result, repo=repo_name, path=use_path + sub_result.path)
            return
        alias = (
            get_span(use_alias.child_alias().child_value(), self.terminals)
            if (use_alias := use.maybe_use_alias())
            else None
        )
        yield Module.UseResult(repo_name, use_path, alias, cst_node=use)
        return

    def resolve_imports(self, importer: Importer) -> None:
        """Perform imports for all `use` statements.

        Args:
            importer: A module importer which can load external modules.
        """
        extern_scope = self.inner_scope.parent
        assert extern_scope is not None  # noqa: S101  (for mypy)
        for use_result in self.unresolved_imports:
            import_spec = importer.resolve_import(self, use_result)
            module, entity = importer.execute_import(import_spec, self, use_result)
            self.context.import_from(module.context)
            if entity is None:
                entity = NamespacedModule(
                    name=import_spec.import_name,
                    module=self,
                    cst_node=use_result.cst_node,
                    scope=extern_scope,
                    extern_module=module,
                )
            extern_scope.define(import_spec.import_name, entity, self.terminals)


class NamespaceEntity:
    """Base class for things which support namespace lookup syntax (foo::bar)."""

    def lookup(self, name: str) -> NamedEntity | None:  # noqa: ARG002 (Unused arguments required by parent method signature)
        """Look up a definition in the namespace entity.

        Returns:
            The entity with that name, or None if not found.
        """
        return None


@dataclass
class NamespacedModule(CstNode[cst.UseBody], NamedEntity, NamespaceEntity):
    """Holds a reference to an imported/extern module.

    Attributes:
        extern_module: The referenced module.
    """

    extern_module: Module

    def name_resolution_fields(self) -> Container[str]:
        """Override recursion for node.resolve_names.

        This prevents us from recursively resolving into the referenced module,
        which will already be resolved.
        """
        return []

    def lookup(self, name: str) -> NamedEntity | None:
        """Look up a definition in the extern module.

        Returns:
            The entity with that name, or None if not found.
        """
        return self.extern_module.inner_scope.lookup(name, recursive=False)


class Scope:
    """A lexical scope with a registry of names."""

    def __init__(self, parent: Scope | None, uniq_path: str, module_id_for_errors: ModuleID | None) -> None:  # pyright: ignore[reportMissingSuperCall] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Construct a new Scope."""
        self.parent = parent
        self.uniq_path = uniq_path
        self.module_id_for_errors = module_id_for_errors
        self.names: dict[str, NamedEntity] = {}
        self._anon_id = 0

    def __str__(self) -> str:
        """Return a string representation of the scope."""
        return f"Scope({self.uniq_path})"

    def make_child_scope(self, name: str) -> Scope:
        """Create a new Scope nested within this one.

        Note: This does not define the new scope as a name within the parent.
        """
        return Scope(parent=self, uniq_path=f"{self.uniq_path}::{name}", module_id_for_errors=self.module_id_for_errors)

    def make_anon_child_scope(self, prefix: str) -> Scope:
        """Create a new anonymous scope nested within this one."""
        uniq_path = f"__anon_{prefix}_{self._anon_id}"
        self._anon_id += 1
        return Scope(parent=self, uniq_path=uniq_path, module_id_for_errors=self.module_id_for_errors)

    def make_dynamic_scope(self, path_parent: Scope, name: str) -> Scope:
        """Create a new child scope lexically disconnected from its uniq_path.

        Note: This does not define the new scope as a name within the parent.
        """
        return Scope(
            parent=self, uniq_path=f"{path_parent.uniq_path}.{name}", module_id_for_errors=self.module_id_for_errors
        )

    def __repr__(self) -> str:
        """Return a string representation of the scope."""
        return f"Scope({self.uniq_path})"

    def define(self, name: str, entity: NamedEntity, terminals: TerminalSource | None) -> None:
        """Define a name in this scope.

        Note: name and entity.name may or may not be the same thing, if an alias is being created to an entity defined in a different scope.

        Args:
            name: The name being bound to this entity in this scope.
            entity: The entity being bound.
            terminals: The terminal source containing this name binding (for error reporting)

        Raises:
            ValueError if the name is already defined in this scope or, if shadowing is disallowed, a parent scope.
        """
        # This is a workaround for the fact that there are many schemas
        # called "State" which need to remain so named for various reasons,
        # but we have a Clockwork built-in State.
        # For this case, we allow shadowing the built-in.
        allow_shadowing = name == "State"
        if (previous := self.lookup(name, recursive=(not allow_shadowing))) is not None:
            msg = f'Redefinition of name "{name}"\n'
            # We try to put as much information as possible in the error message, but we store instances of NamedEntity
            # which aren't guaranteed to have links to the CST nodes (and therefore spans into the source file).  So we
            # have to do some runtime checks to see if we can even find the right locations in the source file to
            # include in the error message.

            # First check if the entity we're trying to define right now has source location information:
            if (
                terminals
                and isinstance(entity, Node)
                and (cst_nodes := list(entity.get_cst_nodes()))
                and (span := span_for_node(cst_nodes[0]))
            ):
                msg += format_line_with_error(span, terminals, module_id=self.module_id_for_errors)
            # Now check on the previous definition:
            if terminals and isinstance(previous, Node) and (cst_nodes := list(previous.get_cst_nodes())):
                msg += "Original definition here:\n" + format_line_with_error(
                    cst_nodes[0].span, terminals, module_id=self.module_id_for_errors
                )
            raise ValueError(msg)
        self.names[name] = entity

    def define_proxy(self, name: str, entity: T, terminals: TerminalSource | None) -> NameProxy[T]:
        """Add a temporary NameProxy to this scope.

        The name will resolve temporarily to a new NameProxy referring to entity.

        Args:
            name: The name being bound to this entity in this scope.
            entity: The entity being bound.
            terminals: The terminal source containing this name binding (for error reporting)

        Returns: The new NameProxy

        Raises:
            ValueError if the name is already defined in this scope or a parent scope.
        """
        proxy = NameProxy(name, self, entity, is_final=False)
        self.define(name, proxy, terminals)
        return proxy

    def finalize_proxy(self, name: str, entity: NamedEntity) -> None:
        """Finalize a NameProxy in this scope.

        The NameProxy will be removed from the scope and replaced with the final entity.
        """
        proxy = self.lookup(name, recursive=False)
        if proxy is None or not isinstance(proxy, NameProxy):
            msg = f"Expected to find a NameProxy at {self.uniq_path}::{name} but got {proxy}"
            raise KeyError(msg)
        proxy.finalize(entity, replace_in_scope=False)
        self.names[name] = entity

    def lookup(self, name: str, recursive: bool = True) -> NamedEntity | None:
        """Look up a name in this scope.

        Note that on failure we return None rather than raising an exception because whether this is an error or not is
        context-dependent; some callers may simply want to know if a name is defined or not.

        Args:
            name: The identifier to look up
            recursive: If True, search enclosing scopes, otherwise only this scope

        Returns:
            The entity if found, else None.
        """
        try:
            return self.names[name]
        except KeyError:
            if not recursive or self.parent is None:
                return None
        return self.parent.lookup(name)


@dataclass
class Doc(CstNode[cst.Doc]):
    """A documentation comment."""

    value: str

    @classmethod
    def from_cst(cls: type[Doc], cst_doc: cst.Doc, module: Module) -> Doc:
        """Construct an IR Doc from a CST Doc."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        return cls(
            module=module,
            cst_node=cst_doc,
            value="\n".join(
                get_span(line_text, module.terminals) if (line_text := line.maybe_text()) else ""
                for line in cst_doc.children_line()
            ),
        )

    @classmethod
    def maybe_from_cst(cls: type[Doc], cst_doc: cst.Doc | None, module: Module) -> Doc | None:
        """Construct an optional IR Doc from an optional CST Doc.

        This is just a convenience wrapper around from_cst where the cst_doc
        argument can be None, resulting in None being returned.  This is usually
        used in construction of IR nodes with an optional Doc component.
        """
        if cst_doc is None:
            return None
        return cls.from_cst(cst_doc, module)

    def render(self, prefix: str) -> str:
        """Render the docstring with each line prefixed with the given prefix."""
        return "\n".join([f"{prefix} {line}" for line in self.value.split("\n")])


ExpectedType = TypeVar("ExpectedType")


@dataclass
class DeferredLookup(Generic[ExpectedType]):
    """Proxy node for deferring lookup of a node by identifier.

    This will be replaced by the resolved entity during the name resolution phase.
    """

    identifier: str
    expected_type: type[ExpectedType]
    cst_identifier: cst.Identifier | None
    terminals: TerminalSource | None

    @classmethod
    def make(
        cls: type[DeferredLookup[ExpectedType]],
        expected_type: type[ExpectedType],
        cst_identifier: cst.Identifier | None = None,
        identifier: str | None = None,
        terminals: TerminalSource | None = None,
    ) -> DeferredLookup[ExpectedType]:
        """Construct a DeferredLookup instance from a CST Identifier or string.

        Note that if you don't supply the identifier as a string, you must supply it as a CST node, along with the
        terminal source for the CST node.

        Args:
            expected_type: The Python type (not IR TypeVal) you expect for the result
            cst_identifier: The CST node of the identifier, or None if constructing from a string
            identifier: The identifier as a string; takes priority if both this and cst_identifier are provided
            terminals: The terminal source; must not be None if constructing from CST rather than a string
        """
        if identifier is None:
            if terminals is None or cst_identifier is None:
                msg = "Must supply either the string identifier or the CST identifier and a terminal source."
                raise ValueError(msg)
            identifier = get_span(cst_identifier.child_value(), terminals)
        return cls(
            identifier=identifier,
            expected_type=expected_type,
            cst_identifier=cst_identifier,
            terminals=terminals,
        )

    def resolve(self, scope: Scope) -> ExpectedType:
        """Resolve the identifier in the given scope."""
        result = scope.lookup(self.identifier)
        if result is None:
            msg = f"Undefined identifier {self.identifier}"
            if self.cst_identifier and self.terminals:
                msg += "\n" + format_line_with_error(
                    self.cst_identifier.child_value(),
                    self.terminals,
                    module_id=scope.module_id_for_errors,
                )
            raise ValueError(msg)
        if not isinstance(result, self.expected_type):
            msg = f"For identifier {self.identifier}: Expected entity of type {self.expected_type}, got {type(result)}"
            if self.cst_identifier and self.terminals:
                msg += "\n" + format_line_with_error(
                    self.cst_identifier.child_value(),
                    self.terminals,
                    module_id=scope.module_id_for_errors,
                )
            raise TypeError(msg)
        return result


# It seems mypy requires Union here rather than |, probably a bug:
Deferrable = Union[ExpectedType, DeferredLookup[ExpectedType]]  # pyright: ignore[reportDeprecated] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip # noqa: UP007 (see above)


def resolve_names(parent: Any, scope: Scope) -> Any:  # noqa: ANN401 (Any is essential given nature of function)
    """Recursively resolve temporary DeferredLookup nodes into their referents.

    Note: This currently relies on all nodes being dataclasses, because it uses dataclasses.fields(...) to find the
    child nodes.  (The type annotation for parent is Any only because there's not a standard way to annotate an instance
    of any datatype, but runtime type checking is inside the function.)  If we have to change the IR later to include
    non-dataclass nodes, we can certainly switch to having a common base class which provides the ability to visit child
    nodes.

    Returns:
        The resolved version of parent.
    """
    if isinstance(parent, str):
        return parent
    if hasattr(parent, "inner_scope"):
        inner_scope = parent.inner_scope
        if inner_scope.parent is not scope:
            # This is just a sanity check; it should never happen, and if it does it's a bug in the IR generation.
            msg = (
                f"While resolving names encountered improperly nested scope:\n"
                f"Node:\n{parent}\nhas inner scope\n{inner_scope}\nwith parent\n{inner_scope.parent}\n"
                f"but is a child of node with different scope\n{scope}"
            )
            raise ValueError(msg)
    else:
        inner_scope = scope
    if isinstance(parent, DeferredLookup):
        return parent.resolve(inner_scope)
        # We don't recurse into the thing just resolved, because it must "live" at a different part of the IR (its
        # point of definition, not its point of reference), and the resolver will recurse into it at that point. We
        # don't want to do it twice.
    if isinstance(parent, collections.abc.Mapping):
        return {key: resolve_names(value, inner_scope) for key, value in parent.items()}
    if isinstance(parent, collections.abc.Iterable):
        return type(parent)(resolve_names(item, inner_scope) for item in parent)  # type: ignore[call-arg]
    if not dataclasses.is_dataclass(parent):
        return parent
    return _resolve_recurse_dataclass(parent, inner_scope)


def _resolve_recurse_dataclass(parent: Any, scope: Scope) -> Any:  # noqa: ANN401 (Any is essential given nature of function)
    if hasattr(parent, "name_resolution_fields"):
        fields: Iterable[str] = parent.name_resolution_fields()
    else:
        fields = [fld.name for fld in dataclasses.fields(parent)]
    for fld in fields:
        child = getattr(parent, fld)
        if child is parent:
            continue
        if isinstance(child, DeferredLookup | collections.abc.Iterable) or (
            _is_ir_node(fld, child) and fld != "module"
        ):
            # We skip the "module" field because this is a reference back "up" the scope stack
            result = resolve_names(child, scope)
            if result is not child:
                setattr(parent, fld, result)
    return parent


def _is_ir_node(name: str | None, node: Any) -> bool:  # noqa: ANN401 (Any is essential given nature of function)
    if name and name.startswith("cst_"):
        return False
    return bool(dataclasses.is_dataclass(node))
