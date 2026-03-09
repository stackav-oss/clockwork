# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""IR Generic Base Classes."""

from __future__ import annotations

import collections.abc
import dataclasses
import functools
from abc import ABC, abstractmethod
from ast import literal_eval
from dataclasses import dataclass, field
from enum import Enum
from typing import (
    TYPE_CHECKING,
    Any,
    Final,
    Generic,
    Protocol,
    TypeVar,
    Union,  # pyright: ignore[reportDeprecated] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
)

from fltk.fegen.pyrt.terminalsrc import Span
from typing_extensions import override

if TYPE_CHECKING:  # pragma: no cover
    from collections.abc import Container, Iterable, Iterator, Sequence

    from clockwork.dsl.ir.module_id import ModuleID
    from fltk.fegen.pyrt.terminalsrc import TerminalSource


from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir.cst_util import format_line_with_error, get_span, span_for_node

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

    @override
    def get_module(self) -> Module:
        """Access the Module in which this node was defined or created."""
        return self.module

    @override
    def get_cst_nodes(self) -> Iterable[CstNodeTypes]:
        """Access the CST nodes associated with this IR node, if any."""
        return self.cst_nodes


CstNodeType = TypeVar("CstNodeType")


@dataclass
class CstNode(Node[CstNodeType], Generic[CstNodeType]):
    """Base class for IR nodes that correspond to (at most) one CST node."""

    module: Module = field(repr=False)
    cst_node: CstNodeType | None = field(repr=False, compare=False)

    @override
    def get_module(self) -> Module:
        """Access the Module in which this node was defined or created."""
        return self.module

    @override
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
        try:
            return f"In entity {entity.value_key()}:\n{msg}"
        except KeyError:
            # value_key() can raise KeyError if the entity is incomplete
            pass
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
        assert isinstance(self.referent, NamedEntity)
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


class GenerateTarget(Enum):
    """Generate target values."""

    cpp = 0
    proto = 1
    go_proto = 2
    py = 3
    nanobind = 4
    proto_conv = 5
    cpp_cog = 6
    cpp_exe = 7
    py_cog = 8
    py_exe = 9
    cpp_test_cog = 10


class UseResultType(Enum):
    """Use result type values."""

    module_or_entity = 0
    module = 1
    entity = 2


class PyCogWrapperType(Enum):
    """Python cog wrapper types."""

    nanobind = 0
    python = 1


# Generate targets that have no effect in use statements
_UNUSABLE_GENERATE_TARGETS: Final = frozenset(
    {GenerateTarget.cpp_cog, GenerateTarget.cpp_exe, GenerateTarget.py_exe, GenerateTarget.cpp_test_cog}
)


AttributeValueType = TypeVar("AttributeValueType")


def _merge_attribute_value(  # noqa: PLR0913 (Arguments are needed for error handling)
    cst_node: cst.ClkCppAttr | cst.ClkProtoAttr | cst.ClkProtoConvAttr | cst.ClkExeAttr | cst.ClkPyCogAttr | None,
    module: Module,
    attr_name: str,
    current_value: AttributeValueType | None,
    inner_value: AttributeValueType | None,
    default_value: AttributeValueType | None = None,
) -> AttributeValueType | None:
    if current_value is None:
        return inner_value
    if inner_value is not None:
        if current_value == inner_value:
            msg = append_error_line(cst_node, module, f"Value for {attr_name} repeats inner attribute value")
            raise ValueError(msg)
    elif default_value is not None and current_value == default_value:
        msg = append_error_line(cst_node, module, f"Value for {attr_name} repeats default attribute value")
        raise ValueError(msg)
    return current_value


_DEFAULT_CPP_GENERATE_COG_METRICS_VALUE: Final = False


@dataclass
class ClkCppAttribute:
    """Represents the module or entity C++ attribute."""

    cst_node: cst.ClkCppAttr | None = field(default=None, repr=False, compare=False)
    namespace: str | None = None
    type_namespace: str | None = None
    type_header: str | None = None
    type_factory: str | None = None
    generate_cog_metrics: bool | None = None

    # We must disable C901 and PLR0912 here (function complexity, branches) because we
    # inherently have many branches, one for attribute value.  However, they're handled
    # in a uniform way that isn't difficult to understand.
    @classmethod
    def from_cst(  # noqa: C901, PLR0912 (see above)
        cls: type[ClkCppAttribute], module: Module, cst_node: cst.ClkCppAttr
    ) -> ClkCppAttribute:
        """Create an instance from CST.

        Arguments:
            module: Module containing the CST node.
            cst_node: Attribute CST node to merge.

        Returns:
            Attribute merged with the value from CST.
        """
        if module.terminals is None:
            msg = append_error_line(cst_node, module, "Cannot construct IR nodes from CST without a TerminalSource")
            raise ValueError(msg)
        result = ClkCppAttribute(cst_node=cst_node)
        for value in cst_node.children_clk_cpp_value():
            if namespace := value.maybe_clk_cpp_namespace():
                if result.namespace is not None:
                    msg = append_error_line(namespace, module, "Duplicate namespace value")
                    raise ValueError(msg)
                result.namespace = get_span(namespace.child_namespace().span, terminals=module.terminals)
            elif type_namespace := value.maybe_clk_cpp_type_namespace():
                if result.type_namespace is not None:
                    msg = append_error_line(type_namespace, module, "Duplicate type_namespace value")
                    raise ValueError(msg)
                result.type_namespace = get_span(type_namespace.child_namespace().span, terminals=module.terminals)
            elif type_header := value.maybe_clk_cpp_type_header():
                if result.type_header is not None:
                    msg = append_error_line(type_header, module, "Duplicate type_header value")
                    raise ValueError(msg)
                result.type_header = literal_eval(get_span(type_header.child_header().child_value(), module.terminals))
            elif type_factory := value.maybe_clk_cpp_type_factory():
                if result.type_factory is not None:
                    msg = append_error_line(type_factory, module, "Duplicate type_factory value")
                    raise ValueError(msg)
                result.type_factory = get_span(type_factory.child_factory().child_value(), module.terminals)
            elif generate_cog_metrics := value.maybe_clk_cpp_generate_cog_metrics():
                if result.generate_cog_metrics is not None:
                    msg = append_error_line(type_factory, module, "Duplicate generate_cog_metrics value")
                    raise ValueError(msg)
                result.generate_cog_metrics = generate_cog_metrics.child_value().maybe_true() is not None
            else:
                msg = append_error_line(cst_node, module, "Unhandled cpp value")
                raise ValueError(msg)

        return result

    @classmethod
    def merge_with_inner(
        cls: type[ClkCppAttribute], current_attrs: ClkAttributes, inner_attrs: ClkAttributes | None
    ) -> None:
        """Merge with inner attributes.

        Arguments:
            current_attrs: Current attributes.
            inner_attrs: Inner attributes to merge.
        """
        inner_cpp_attr = inner_attrs.cpp_attr if inner_attrs else None
        if current_attrs.cpp_attr is None:
            current_attrs.cpp_attr = inner_cpp_attr or ClkCppAttribute()
            return
        current_attrs.cpp_attr.namespace = _merge_attribute_value(
            current_attrs.cpp_attr.cst_node,
            current_attrs.module,
            "namespace",
            current_attrs.cpp_attr.namespace,
            inner_cpp_attr.namespace if inner_cpp_attr else None,
        )
        current_attrs.cpp_attr.type_namespace = _merge_attribute_value(
            current_attrs.cpp_attr.cst_node,
            current_attrs.module,
            "type_namespace",
            current_attrs.cpp_attr.type_namespace,
            inner_cpp_attr.type_namespace if inner_cpp_attr else None,
        )
        current_attrs.cpp_attr.type_header = _merge_attribute_value(
            current_attrs.cpp_attr.cst_node,
            current_attrs.module,
            "type_header",
            current_attrs.cpp_attr.type_header,
            inner_cpp_attr.type_header if inner_cpp_attr else None,
        )
        current_attrs.cpp_attr.type_factory = _merge_attribute_value(
            current_attrs.cpp_attr.cst_node,
            current_attrs.module,
            "type_factory",
            current_attrs.cpp_attr.type_factory,
            inner_cpp_attr.type_factory if inner_cpp_attr else None,
        )
        current_attrs.cpp_attr.generate_cog_metrics = _merge_attribute_value(
            current_attrs.cpp_attr.cst_node,
            current_attrs.module,
            "generate_cog_metrics",
            current_attrs.cpp_attr.generate_cog_metrics,
            inner_cpp_attr.generate_cog_metrics if inner_cpp_attr else None,
            _DEFAULT_CPP_GENERATE_COG_METRICS_VALUE,
        )


_DEFAULT_PROTO_VALIDATE_VALUE: Final = True
_DEFAULT_PROTO_PREFIX_ENUM_VALUE_NAMES_VALUE: Final = False


@dataclass
class ClkProtoAttribute:
    """Represents the module or entity protobuf attribute."""

    cst_node: cst.ClkProtoAttr | None = field(default=None, repr=False, compare=False)
    package: str | None = None
    go_package: str | None = None
    validate: bool | None = None
    prefix_enum_value_names: bool | None = None

    # We must disable C901 here (function complexity) because we
    # inherently have many branches, one for attribute value.  However, they're handled
    # in a uniform way that isn't difficult to understand.
    @classmethod
    def from_cst(  # noqa: C901 (see above)
        cls: type[ClkProtoAttribute],
        module: Module,
        cst_node: cst.ClkProtoAttr,
    ) -> ClkProtoAttribute:
        """Create an instance from CST.

        Arguments:
            module: Module containing the CST node.
            cst_node: Attribute CST node to merge.

        Returns:
            Attribute merged with the value from CST.
        """
        if module.terminals is None:
            msg = append_error_line(cst_node, module, "Cannot construct IR nodes from CST without a TerminalSource")
            raise ValueError(msg)
        result = ClkProtoAttribute(cst_node=cst_node)
        for value in cst_node.children_clk_proto_value():
            if package := value.maybe_clk_proto_package():
                if result.package is not None:
                    msg = append_error_line(package, module, "Duplicate package value")
                    raise ValueError(msg)
                result.package = get_span(package.child_package().span, terminals=module.terminals)
            elif go_package := value.maybe_clk_proto_go_package():
                if result.go_package is not None:
                    msg = append_error_line(go_package, module, "Duplicate go_package value")
                    raise ValueError(msg)
                result.go_package = get_span(go_package.child_go_package().span, terminals=module.terminals)
            elif validate := value.maybe_clk_proto_validate():
                if result.validate is not None:
                    msg = append_error_line(go_package, module, "Duplicate validate value")
                    raise ValueError(msg)
                result.validate = validate.child_validate().maybe_true() is not None
            elif prefix_enum_value_names := value.maybe_clk_proto_prefix_enum_value_names():
                if result.prefix_enum_value_names is not None:
                    msg = append_error_line(go_package, module, "Duplicate prefix_enum_value_names value")
                    raise ValueError(msg)
                result.prefix_enum_value_names = prefix_enum_value_names.child_prefix().maybe_true() is not None
            else:
                msg = append_error_line(cst_node, module, "Unhandled proto value")
                raise ValueError(msg)

        return result

    @classmethod
    def merge_with_inner(
        cls: type[ClkProtoAttribute],
        current_attrs: ClkAttributes,
        inner_attrs: ClkAttributes | None,
    ) -> None:
        """Merge with inner attributes.

        Arguments:
            current_attrs: Current attributes
            inner_attrs: Inner attributes to merge
        """
        inner_proto_attr = inner_attrs.proto_attr if inner_attrs else None
        if not current_attrs.proto_attr:
            current_attrs.proto_attr = inner_proto_attr or ClkProtoAttribute()
            return
        current_attrs.proto_attr.package = _merge_attribute_value(
            current_attrs.proto_attr.cst_node,
            current_attrs.module,
            "package",
            current_attrs.proto_attr.package,
            inner_proto_attr.package if inner_proto_attr else None,
        )
        current_attrs.proto_attr.go_package = _merge_attribute_value(
            current_attrs.proto_attr.cst_node,
            current_attrs.module,
            "go_package",
            current_attrs.proto_attr.go_package,
            inner_proto_attr.go_package if inner_proto_attr else None,
        )
        current_attrs.proto_attr.validate = _merge_attribute_value(
            current_attrs.proto_attr.cst_node,
            current_attrs.module,
            "validate",
            current_attrs.proto_attr.validate,
            inner_proto_attr.validate if inner_proto_attr else None,
            _DEFAULT_PROTO_VALIDATE_VALUE,
        )
        current_attrs.proto_attr.prefix_enum_value_names = _merge_attribute_value(
            current_attrs.proto_attr.cst_node,
            current_attrs.module,
            "prefix_enum_value_names",
            current_attrs.proto_attr.prefix_enum_value_names,
            inner_proto_attr.prefix_enum_value_names if inner_proto_attr else None,
            _DEFAULT_PROTO_PREFIX_ENUM_VALUE_NAMES_VALUE,
        )


_DEFAULT_PROTO_CONV_TAP_TO_PROTOBUF_VALUE: Final = True
_DEFAULT_PROTO_CONV_PROTOBUF_TO_TAP_VALUE: Final = True


@dataclass
class ClkProtoConvAttribute:
    """Represents the module or entity proto_conv attribute."""

    cst_node: cst.ClkProtoConvAttr | None = field(default=None, repr=False, compare=False)
    tap_to_protobuf: bool | None = None
    protobuf_to_tap: bool | None = None
    namespace: str | None = None

    @classmethod
    def from_cst(
        cls: type[ClkProtoConvAttribute], module: Module, cst_node: cst.ClkProtoConvAttr
    ) -> ClkProtoConvAttribute:
        """Create an instance from CST.

        Arguments:
            module: Module containing the CST node
            cst_node: Attribute CST node to merge.

        Returns:
            Attribute merged with the value from CST.
        """
        if module.terminals is None:
            msg = append_error_line(cst_node, module, "Cannot construct IR nodes from CST without a TerminalSource")
            raise ValueError(msg)
        result = ClkProtoConvAttribute(cst_node=cst_node)
        for value in cst_node.children_clk_proto_conv_value():
            if namespace := value.maybe_clk_proto_conv_namespace():
                if result.namespace is not None:
                    msg = append_error_line(namespace, module, "Duplicate namespace value")
                    raise ValueError(msg)
                result.namespace = get_span(namespace.child_namespace().span, terminals=module.terminals)
            if protobuf_to_tap := value.maybe_clk_protobuf_to_tap():
                if result.protobuf_to_tap is not None:
                    msg = append_error_line(protobuf_to_tap, module, "Duplicate protobuf_to_tap value")
                    raise ValueError(msg)
                result.protobuf_to_tap = protobuf_to_tap.child_value().maybe_true() is not None
            if tap_to_protobuf := value.maybe_clk_tap_to_protobuf():
                if result.tap_to_protobuf is not None:
                    msg = append_error_line(tap_to_protobuf, module, "Duplicate tap_to_protobuf value")
                    raise ValueError(msg)
                result.tap_to_protobuf = tap_to_protobuf.child_value().maybe_true() is not None

        return result

    @classmethod
    def merge_with_inner(
        cls: type[ClkProtoConvAttribute],
        current_attrs: ClkAttributes,
        inner_attrs: ClkAttributes | None,
    ) -> None:
        """Merge with inner attributes.

        Arguments:
            current_attrs: Current attributes
            inner_attrs: Inner attributes to merge

        Returns:
            Inner attributes merged into the current attributes.
        """
        inner_proto_conv_attr = inner_attrs.proto_conv_attr if inner_attrs else None
        if not current_attrs.proto_conv_attr:
            current_attrs.proto_conv_attr = inner_proto_conv_attr or ClkProtoConvAttribute()
            return
        current_attrs.proto_conv_attr.protobuf_to_tap = _merge_attribute_value(
            current_attrs.proto_conv_attr.cst_node,
            current_attrs.module,
            "protobuf_to_tap",
            current_attrs.proto_conv_attr.protobuf_to_tap,
            inner_proto_conv_attr.protobuf_to_tap if inner_proto_conv_attr else None,
            _DEFAULT_PROTO_CONV_PROTOBUF_TO_TAP_VALUE,
        )
        current_attrs.proto_conv_attr.tap_to_protobuf = _merge_attribute_value(
            current_attrs.proto_conv_attr.cst_node,
            current_attrs.module,
            "tap_to_protobuf",
            current_attrs.proto_conv_attr.tap_to_protobuf,
            inner_proto_conv_attr.tap_to_protobuf if inner_proto_conv_attr else None,
            _DEFAULT_PROTO_CONV_TAP_TO_PROTOBUF_VALUE,
        )
        current_attrs.proto_conv_attr.namespace = _merge_attribute_value(
            current_attrs.proto_conv_attr.cst_node,
            current_attrs.module,
            "namespace",
            current_attrs.proto_conv_attr.namespace,
            inner_proto_conv_attr.namespace if inner_proto_conv_attr else None,
            current_attrs.get_cpp_namespace(),
        )


_DEFAULT_EXE_OFFLINE_VALUE: Final = False


@dataclass
class ClkExeAttribute:
    """Represents the module or entity cpp_exe attribute."""

    cst_node: cst.ClkExeAttr | None = field(default=None, repr=False, compare=False)
    offline: bool | None = None

    @classmethod
    def from_cst(cls: type[ClkExeAttribute], module: Module, cst_node: cst.ClkExeAttr) -> ClkExeAttribute:
        """Create an instance from CST.

        Arguments:
            module: Module containing the CST node
            cst_node: Attribute CST node to merge.

        Returns:
            Attribute merged with the value from CST.
        """
        if module.terminals is None:
            msg = append_error_line(cst_node, module, "Cannot construct IR nodes from CST without a TerminalSource")
            raise ValueError(msg)
        result = ClkExeAttribute(cst_node=cst_node)
        if offline := cst_node.maybe_clk_offline():
            if result.offline is not None:
                msg = append_error_line(offline, module, "Duplicate offline value")
                raise ValueError(msg)
            result.offline = offline.child_value().maybe_true() is not None

        return result

    @classmethod
    def merge_with_inner(
        cls: type[ClkExeAttribute],
        current_attrs: ClkAttributes,
        inner_attrs: ClkAttributes | None,
    ) -> None:
        """Merge with inner attributes.

        Arguments:
            current_attrs: Current attributes
            inner_attrs: Inner attributes to merge

        Returns:
            Inner attributes merged into the current attributes.
        """
        inner_exe_attr = inner_attrs.exe_attr if inner_attrs else None
        if not current_attrs.exe_attr:
            current_attrs.exe_attr = inner_exe_attr or ClkExeAttribute()
            return
        current_attrs.exe_attr.offline = _merge_attribute_value(
            current_attrs.exe_attr.cst_node,
            current_attrs.module,
            "offline",
            current_attrs.exe_attr.offline,
            inner_exe_attr.offline if inner_exe_attr else None,
            _DEFAULT_EXE_OFFLINE_VALUE,
        )


_DEFAULT_PY_COG_WRAPPER_TYPE_VALUE: Final = PyCogWrapperType.python


@dataclass
class ClkPyCogAttribute:
    """Represents the module or entity cpp_exe attribute."""

    cst_node: cst.ClkPyCogAttr | None = field(default=None, repr=False, compare=False)
    wrapper_type: PyCogWrapperType | None = None

    @classmethod
    def from_cst(cls: type[ClkPyCogAttribute], module: Module, cst_node: cst.ClkPyCogAttr) -> ClkPyCogAttribute:
        """Create an instance from CST.

        Arguments:
            module: Module containing the CST node
            cst_node: Attribute CST node to merge.

        Returns:
            Attribute merged with the value from CST.
        """
        if module.terminals is None:
            msg = append_error_line(cst_node, module, "Cannot construct IR nodes from CST without a TerminalSource")
            raise ValueError(msg)
        result = ClkPyCogAttribute(cst_node=cst_node)
        if wrapper_type := cst_node.maybe_clk_wrapper_type():
            if result.wrapper_type is not None:
                msg = append_error_line(wrapper_type, module, "Duplicate wrapper_type value")
                raise ValueError(msg)
            result.wrapper_type = (
                PyCogWrapperType.nanobind if wrapper_type.maybe_nanobind() is not None else PyCogWrapperType.python
            )
        return result

    @classmethod
    def merge_with_inner(
        cls: type[ClkPyCogAttribute],
        current_attrs: ClkAttributes,
        inner_attrs: ClkAttributes | None,
    ) -> None:
        """Merge with inner attributes.

        Arguments:
            current_attrs: Current attributes
            inner_attrs: Inner attributes to merge
        """
        inner_py_cog_attr = inner_attrs.py_cog_attr if inner_attrs else None
        if not current_attrs.py_cog_attr:
            current_attrs.py_cog_attr = inner_py_cog_attr or ClkPyCogAttribute()
            return
        current_attrs.py_cog_attr.wrapper_type = _merge_attribute_value(
            current_attrs.py_cog_attr.cst_node,
            current_attrs.module,
            "wrapper_type",
            current_attrs.py_cog_attr.wrapper_type,
            inner_py_cog_attr.wrapper_type if inner_py_cog_attr else None,
            _DEFAULT_PY_COG_WRAPPER_TYPE_VALUE,
        )


@dataclass
class ClkAttributes:
    """Represents the module or entity attribute."""

    module: Module = field(repr=False, compare=False)
    cpp_attr: ClkCppAttribute | None = None
    proto_attr: ClkProtoAttribute | None = None
    proto_conv_attr: ClkProtoConvAttribute | None = None
    exe_attr: ClkExeAttribute | None = None
    py_cog_attr: ClkPyCogAttribute | None = None

    # We must disable C901, PLR0912, PLR0915 here (complexity, branches, statements)
    # because we inherently have many branches, one for attribute.  However, they're
    # handled in a uniform way that isn't difficult to understand.
    @classmethod
    def from_cst(  # noqa: C901, PLR0912, PLR0915 (see above)
        cls: type[ClkAttributes],
        module: Module,
        maybe_cst_node: cst.ClkInnerAttrs | cst.ClkOuterAttrs | None,
        inner_attrs: ClkAttributes | None = None,
    ) -> ClkAttributes:
        """Create an instance from CST, optionally merging with inner attributes.

        Arguments:
            module: Module containing the CST node
            maybe_cst_node: Attribute CST node to merge.
            inner_attrs: Optional inner attributes to merge with result
        Returns:
            Attribute merged with the value from CST.
        """
        result = ClkAttributes(module=module)

        if maybe_cst_node is not None:
            if isinstance(maybe_cst_node, cst.ClkInnerAttrs):
                clk_attrs = maybe_cst_node.children_clk_inner_attr()
            else:
                clk_attrs = maybe_cst_node.children_clk_outer_attr()
            for clk_attr in clk_attrs:
                attr = clk_attr.child_clk_attr()
                for cpp_attr in attr.children_clk_cpp_attr():
                    if module.generates is None or GenerateTarget.cpp not in module.generates:
                        msg = append_error_line(cpp_attr, module, "Module must generate cpp to have a cpp attribute")
                        raise ValueError(msg)
                    if result.cpp_attr is not None:
                        msg = append_error_line(cpp_attr, module, "Duplicate cpp attribute")
                        raise ValueError(msg)
                    result.cpp_attr = ClkCppAttribute.from_cst(module, cpp_attr)
                    if isinstance(maybe_cst_node, cst.ClkOuterAttrs):
                        if result.cpp_attr.namespace is not None:
                            msg = append_error_line(
                                cpp_attr, module, "cpp namespace is only allowed in inner attributes"
                            )
                            raise ValueError(msg)
                        if result.cpp_attr.generate_cog_metrics is not None:
                            msg = append_error_line(
                                cpp_attr, module, "cpp generate_cog_metrics is only allowed in inner attributes"
                            )
                            raise ValueError(msg)
                for proto_attr in attr.children_clk_proto_attr():
                    if module.generates is None or GenerateTarget.proto not in module.generates:
                        msg = append_error_line(
                            proto_attr, module, "Module must generate proto to have a proto attribute"
                        )
                        raise ValueError(msg)
                    if result.proto_attr is not None:
                        msg = append_error_line(proto_attr, module, "Duplicate proto attribute")
                        raise ValueError(msg)
                    result.proto_attr = ClkProtoAttribute.from_cst(module, proto_attr)
                    if isinstance(maybe_cst_node, cst.ClkOuterAttrs):
                        if result.proto_attr.package is not None:
                            msg = append_error_line(
                                proto_attr, module, "proto package is only allowed in inner attributes"
                            )
                            raise ValueError(msg)
                        if result.proto_attr.go_package is not None:
                            msg = append_error_line(
                                proto_attr, module, "proto go_package is only allowed in inner attributes"
                            )
                            raise ValueError(msg)
                        if result.proto_attr.validate is not None:
                            msg = append_error_line(
                                proto_attr, module, "proto validate is only allowed in inner attributes"
                            )
                            raise ValueError(msg)
                for proto_conv_attr in attr.children_clk_proto_conv_attr():
                    if module.generates is None or GenerateTarget.proto_conv not in module.generates:
                        msg = append_error_line(
                            proto_conv_attr, module, "Module must generate proto_conv to have a proto_conv attribute"
                        )
                        raise ValueError(msg)
                    if result.proto_conv_attr is not None:
                        msg = append_error_line(proto_conv_attr, module, "Duplicate proto_conv attribute")
                        raise ValueError(msg)
                    result.proto_conv_attr = ClkProtoConvAttribute.from_cst(module, proto_conv_attr)
                for exe_attr in attr.children_clk_exe_attr():
                    if module.generates is None or (
                        GenerateTarget.cpp_exe not in module.generates and GenerateTarget.py_exe not in module.generates
                    ):
                        msg = append_error_line(
                            exe_attr, module, "Module must generate cpp_exe or py_exe to have an exe attribute"
                        )
                        raise ValueError(msg)
                    if result.exe_attr is not None:
                        msg = append_error_line(exe_attr, module, "Duplicate exe attribute")
                        raise ValueError(msg)
                    if isinstance(maybe_cst_node, cst.ClkOuterAttrs):
                        msg = append_error_line(exe_attr, module, "exe attribute is only allowed in inner attributes")
                        raise TypeError(msg)
                    result.exe_attr = ClkExeAttribute.from_cst(module, exe_attr)
                for py_cog_attr in attr.children_clk_py_cog_attr():
                    if module.generates is None or GenerateTarget.py_cog not in module.generates:
                        msg = append_error_line(
                            py_cog_attr, module, "Module must generate py_cog to have a py_cog attribute"
                        )
                        raise ValueError(msg)
                    if result.py_cog_attr is not None:
                        msg = append_error_line(py_cog_attr, module, "Duplicate py_cog attribute")
                        raise ValueError(msg)
                    if isinstance(maybe_cst_node, cst.ClkOuterAttrs):
                        msg = append_error_line(
                            py_cog_attr, module, "py_cog attribute is only allowed in inner attributes"
                        )
                        raise TypeError(msg)
                    result.py_cog_attr = ClkPyCogAttribute.from_cst(module, py_cog_attr)

        result.merge_with_inner(inner_attrs)
        return result

    def merge_with_inner(self, inner_attrs: ClkAttributes | None) -> None:
        """Merge values from inner attributes to fill in unset values.

        Arguments:
            inner_attrs: Inner attributes to merge.
        """
        ClkCppAttribute.merge_with_inner(self, inner_attrs)
        ClkProtoAttribute.merge_with_inner(self, inner_attrs)
        ClkProtoConvAttribute.merge_with_inner(self, inner_attrs)
        ClkExeAttribute.merge_with_inner(self, inner_attrs)
        ClkPyCogAttribute.merge_with_inner(self, inner_attrs)

    def get_cpp_namespace(self) -> str | None:
        """Get the cpp namespace value."""
        return self.cpp_attr.namespace if self.cpp_attr is not None else None

    def get_cpp_type_namespace(self) -> str | None:
        """Get the cpp type_namespace value."""
        return self.cpp_attr.type_namespace if self.cpp_attr is not None else None

    def get_cpp_type_header(self) -> str | None:
        """Get the cpp type_header value."""
        return self.cpp_attr.type_header if self.cpp_attr is not None else None

    def get_cpp_type_factory(self) -> str | None:
        """Get the cpp type_factory value."""
        return self.cpp_attr.type_factory if self.cpp_attr is not None else None

    def get_cpp_generate_cog_metrics(self) -> bool:
        """Get the cpp generate_cog_metrics value."""
        result = self.cpp_attr.generate_cog_metrics if self.cpp_attr is not None else None
        return result if result is not None else _DEFAULT_CPP_GENERATE_COG_METRICS_VALUE

    def get_proto_package(self) -> str | None:
        """Get the proto package value."""
        return self.proto_attr.package if self.proto_attr is not None else None

    def get_proto_go_package(self) -> str | None:
        """Get the proto go_package value."""
        return self.proto_attr.go_package if self.proto_attr is not None else None

    def get_proto_validate(self) -> bool:
        """Get the proto validate value."""
        result = self.proto_attr.validate if self.proto_attr is not None else None
        return result if result is not None else _DEFAULT_PROTO_VALIDATE_VALUE

    def get_proto_prefix_enum_value_names(self) -> bool:
        """Get the proto prefix_enum_value_names value."""
        result = self.proto_attr.prefix_enum_value_names if self.proto_attr is not None else None
        return result if result is not None else _DEFAULT_PROTO_PREFIX_ENUM_VALUE_NAMES_VALUE

    def get_proto_conv_tap_to_protobuf(self) -> bool:
        """Get the proto_conv tap_to_protobuf value."""
        result = self.proto_conv_attr.tap_to_protobuf if self.proto_conv_attr is not None else None
        return result if result is not None else _DEFAULT_PROTO_CONV_TAP_TO_PROTOBUF_VALUE

    def get_proto_conv_protobuf_to_tap(self) -> bool:
        """Get the proto_conv protobuf_to_tap value."""
        result = self.proto_conv_attr.protobuf_to_tap if self.proto_conv_attr is not None else None
        return result if result is not None else _DEFAULT_PROTO_CONV_PROTOBUF_TO_TAP_VALUE

    def get_proto_conv_namespace(self) -> str | None:
        """Get the proto_conv namespace value."""
        result = self.proto_conv_attr.namespace if self.proto_conv_attr is not None else None
        return result if result is not None else self.get_cpp_namespace()

    def get_exe_offline(self) -> bool:
        """Get the exe offline value."""
        result = self.exe_attr.offline if self.exe_attr is not None else None
        return result if result is not None else _DEFAULT_EXE_OFFLINE_VALUE

    def get_py_cog_wrapper_type(self) -> PyCogWrapperType:
        """Get the python cog wrapper type value."""
        result = self.py_cog_attr.wrapper_type if self.py_cog_attr is not None else None
        return result if result is not None else _DEFAULT_PY_COG_WRAPPER_TYPE_VALUE


@dataclass
class Module(Node[cst.Module], DocableEntity):
    """Represents a DSL module."""

    module_id: ModuleID
    inner_scope: Scope
    terminals: TerminalSource | None = field(repr=False)
    cst_node: cst.Module | None = field(repr=False)
    unresolved_imports: list[UseResult]
    context: CompilerContext = field(repr=False)
    generates: frozenset[GenerateTarget] | None
    inner_attrs: ClkAttributes | None
    import_use_targets: dict[ModuleID, frozenset[GenerateTarget] | None] = field(repr=False, default_factory=dict)

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
            context=CompilerContext(uniq_path),
            generates=None,
            inner_attrs=None,
        )
        if doc := cst_node.maybe_doc():
            result.doc = Doc.from_cst(doc, module=result)

        if clk_generate := cst_node.maybe_clk_generate():
            result.generates = result._handle_generate(clk_generate)
            default_use_targets = {target for target in result.generates if target not in _UNUSABLE_GENERATE_TARGETS}
            if GenerateTarget.cpp_exe in result.generates or GenerateTarget.py_exe in result.generates:
                default_use_targets.add(GenerateTarget.cpp)
            default_use_targets = frozenset(default_use_targets)
            result.import_use_targets[result.module_id] = default_use_targets
            result.inner_attrs = ClkAttributes.from_cst(result, cst_node.maybe_clk_inner_attrs())
            for clk_use in cst_node.children_clk_use():
                if clk_use_target := clk_use.maybe_clk_use_target():
                    frozen_use_targets = result._handle_use_targets(clk_use_target, default_use_targets)
                else:
                    frozen_use_targets = default_use_targets
                result.unresolved_imports.extend(result._handle_use(clk_use.child_use_body(), frozen_use_targets))
        else:
            for use in cst_node.children_use():
                result.unresolved_imports.extend(result._handle_use(use.child_use_body(), None))
        return result

    @override
    def get_module(self) -> Module:
        """Return self (to implement Node interface)."""
        return self

    @override
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
            use_targets: Generated targets imported from the source
        """

        repo: str | None
        path: tuple[str, ...]
        alias: str | None
        use_targets: frozenset[GenerateTarget] | None
        use_type: UseResultType
        cst_node: cst.UseBody | None = field(default=None, compare=False, repr=False)

    def _handle_use(self, use: cst.UseBody, use_targets: frozenset[GenerateTarget] | None) -> Iterator[UseResult]:
        assert self.terminals is not None
        if use_repo := use.maybe_use_repo():
            repo_name = get_span(use_repo.child_identifier().child_value(), self.terminals)
        else:
            repo_name = None
        use_path = tuple(
            get_span(identifier.child_value(), self.terminals)
            for identifier in use.child_use_path().children_identifier()
        )

        if block := use.maybe_use_block():
            for use_type in block.children_use_type():
                typename = get_span(use_type.child_typename().child_value(), self.terminals)
                alias = (
                    get_span(use_alias.child_alias().child_value(), self.terminals)
                    if (use_alias := use_type.maybe_use_alias())
                    else None
                )
                yield Module.UseResult(
                    repo_name,
                    (*use_path, typename),
                    alias,
                    use_targets=use_targets,
                    cst_node=use,
                    use_type=UseResultType.entity,
                )
            return
        alias = (
            get_span(use_alias.child_alias().child_value(), self.terminals)
            if (use_alias := use.maybe_use_alias())
            else None
        )
        yield Module.UseResult(
            repo_name, use_path, alias, use_targets=use_targets, cst_node=use, use_type=UseResultType.module
        )

    def _handle_clk_generate_target(self, target: cst.ClkGenerateTarget) -> GenerateTarget:  # noqa: C901 One condition per target value
        target_value: GenerateTarget | None = None
        if target.maybe_cpp_cog() is not None:
            target_value = GenerateTarget.cpp_cog
        elif target.maybe_cpp_exe() is not None:
            target_value = GenerateTarget.cpp_exe
        elif target.maybe_cpp_test_cog() is not None:
            target_value = GenerateTarget.cpp_test_cog
        elif target.maybe_cpp() is not None:
            target_value = GenerateTarget.cpp
        elif target.maybe_proto() is not None and target.maybe_conv() is not None:
            target_value = GenerateTarget.proto_conv
        elif target.maybe_proto() is not None:
            target_value = GenerateTarget.proto
        elif target.maybe_go_proto() is not None:
            target_value = GenerateTarget.go_proto
        elif target.maybe_py_cog() is not None:
            target_value = GenerateTarget.py_cog
        elif target.maybe_py_exe() is not None:
            target_value = GenerateTarget.py_exe
        elif target.maybe_py() is not None:
            target_value = GenerateTarget.py
        elif target.maybe_nanobind() is not None:
            target_value = GenerateTarget.nanobind
        if target_value is None:
            msg = append_error_line(target, self, "Invalid generate target value")
            raise ValueError(msg)
        return target_value

    def resolve_imports(self, importer: Importer) -> None:
        """Perform imports for all `use` statements.

        Args:
            importer: A module importer which can load external modules.
        """
        extern_scope = self.inner_scope.parent
        assert extern_scope is not None
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
            self.import_use_targets[module.module_id] = use_result.use_targets

    def _handle_generate(self, generate_cst: cst.ClkGenerate) -> frozenset[GenerateTarget]:
        """Handle converting the generate target CST to IR."""
        generates = set()
        for target in generate_cst.children_clk_generate_target():
            generates.add(self._handle_clk_generate_target(target))
        if GenerateTarget.nanobind in generates and GenerateTarget.cpp not in generates:
            msg = append_error_line(generate_cst, self, "Generating nanobind depends on generating cpp")
            raise ValueError(msg)
        if GenerateTarget.go_proto in generates and GenerateTarget.proto not in generates:
            msg = append_error_line(generate_cst, self, "Generating go_proto depends on generating proto")
            raise ValueError(msg)
        if GenerateTarget.proto_conv in generates and (
            GenerateTarget.proto not in generates or GenerateTarget.cpp not in generates
        ):
            msg = append_error_line(generate_cst, self, "Generating proto_conv depends on generating proto and cpp")
            raise ValueError(msg)
        if GenerateTarget.cpp_cog in generates and GenerateTarget.py_cog in generates:
            msg = append_error_line(
                generate_cst, self, "Generating cpp_cog and py_cog in the same module is not supported"
            )
            raise ValueError(msg)
        if GenerateTarget.cpp_exe in generates and GenerateTarget.py_exe in generates:
            msg = append_error_line(
                generate_cst, self, "Generating cpp_exe and py_exe in the same module is not supported"
            )
            raise ValueError(msg)
        if GenerateTarget.cpp_cog in generates and GenerateTarget.cpp not in generates:
            msg = append_error_line(generate_cst, self, "Generating cpp_cog depends on generating cpp")
            raise ValueError(msg)
        if GenerateTarget.py_cog in generates and GenerateTarget.cpp not in generates:
            msg = append_error_line(generate_cst, self, "Generating py_cog depends on generating cpp")
            raise ValueError(msg)
        if (
            GenerateTarget.cpp_test_cog in generates
            and GenerateTarget.cpp_cog not in generates
            and GenerateTarget.py_cog not in generates
        ):
            msg = append_error_line(
                generate_cst, self, "Generating cpp_test_cog depends on generating cpp_cog or py_cog"
            )
            raise ValueError(msg)
        return frozenset(generates)

    def _handle_use_targets(
        self, use_target_cst: cst.ClkUseTarget, default_use_targets: frozenset[GenerateTarget]
    ) -> frozenset[GenerateTarget]:
        """Handle converting a clk_use target CST to IR."""
        assert self.generates is not None
        use_targets = set()
        for target in use_target_cst.children_clk_generate_target():
            target_value = self._handle_clk_generate_target(target)
            if target_value in _UNUSABLE_GENERATE_TARGETS:
                msg = append_error_line(target, self, f"Specifying {target_value.name} in use statement has no effect")
                raise ValueError(msg)
            use_targets.add(target_value)
        use_targets = frozenset(use_targets)
        if use_targets == default_use_targets:
            msg = append_error_line(use_target_cst, self, "Explit use targets cannot duplicate the defaults")
            raise ValueError(msg)
        if GenerateTarget.go_proto in use_targets and GenerateTarget.proto not in use_targets:
            msg = append_error_line(use_target_cst, self, "Cannot use go_proto without using proto")
            raise ValueError(msg)
        return use_targets

    def handle_outer_attrs(self, attrs_cst: cst.ClkOuterAttrs | None) -> ClkAttributes | None:
        """Handle common processing for outer attributes.

        Arguments:
            attrs_cst: Outer attributes CST node or None
        Returns
            Outer attributes of None if attrs_cst was None
        """
        if self.generates is None and attrs_cst is not None:
            msg = append_error_line(
                attrs_cst, self, "Outer attributes are not permitted without a generate inner attribute"
            )
            raise ValueError(msg)
        attributes: ClkAttributes | None = None
        if self.generates is not None:
            attributes = ClkAttributes.from_cst(self, attrs_cst, self.inner_attrs)
            assert attributes is not None
            if GenerateTarget.cpp in self.generates and attributes.get_cpp_namespace() is None:
                msg = append_error_line(self.cst_node, self, "cpp namespace attribute is not set")
                raise ValueError(msg)
            if GenerateTarget.go_proto in self.generates and attributes.get_proto_go_package() is None:
                msg = append_error_line(self.cst_node, self, "proto go_package attribute is not set")
                raise ValueError(msg)
        return attributes

    def validate_use_targets(
        self, cst_node: CstNode[Any], name: str, module_id: ModuleID, required_targets: set[GenerateTarget]
    ) -> None:
        """Validate that the use targets available for the module with module_id contain the required targets.

        Args:
            cst_node: CstNode for error messages.
            name: Name for error messages.
            module_id: Module ID of the imported module.
            required_targets: Use targets required from the imported module.
        """
        if self.generates is None:
            return
        if module_id not in self.import_use_targets:
            req = [tgt.name for tgt in required_targets]
            req_str = ", ".join(sorted(req))
            msg = append_error_line(
                cst_node,
                self,
                f"Missing use statement for {name}, require [{req_str}]. DON'T FORGET TO RUN GAZELLE AFTER FIXING.",
            )
            raise ValueError(msg)
        avail_targets = self.import_use_targets[module_id]
        assert avail_targets is not None
        if required_targets - avail_targets:
            req = [tgt.name for tgt in required_targets]
            req_str = ", ".join(sorted(req))
            avail = [tgt.name for tgt in avail_targets]
            avail_str = ", ".join(sorted(avail))
            msg = append_error_line(
                cst_node,
                self,
                f"Missing use targets for {name}, require [{req_str}], have [{avail_str}]. DON'T FORGET TO RUN GAZELLE AFTER FIXING.",
            )
            raise ValueError(msg)


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

    @override
    def lookup(self, name: str) -> NamedEntity | None:
        """Look up a definition in the extern module.

        Returns:
            The entity with that name, or None if not found.
        """
        return self.extern_module.inner_scope.lookup(name, recursive=False)


class Scope:
    """A lexical scope with a registry of names."""

    def __init__(self, parent: Scope | None, uniq_path: str, module_id_for_errors: ModuleID | None) -> None:
        """Construct a new Scope."""
        self.parent = parent
        self.uniq_path = uniq_path
        self.module_id_for_errors = module_id_for_errors
        self.names: dict[str, NamedEntity] = {}
        self._anon_id = 0

    @override
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

    @override
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
        return type(parent)(resolve_names(item, inner_scope) for item in parent)  # pyright: ignore[reportCallIssue] False positive
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
