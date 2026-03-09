# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""C++ backend for Clockwork schemas.

This module implements a C++ code generation backend for Clockwork
schemas that creates layout structs to define the data layout.  Then
it implements a wrapper class with methods to get/set each field.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import Enum
from itertools import chain
from typing import TYPE_CHECKING, Final, TypeAlias, overload

from clockwork.dsl.cpp.context import (
    CppChunk,
    CppModuleChunks,
    Header,
    Include,
    SystemHeader,
    comment_doc_string,
)
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO

if TYPE_CHECKING:
    from collections.abc import Iterable

GLOBAL_NAMESPACE: Final[str] = ""


class Ref(Enum):
    """Ref qualifier for a type."""

    L = "&"
    R = "&&"
    POINTER = "*"


@dataclass
class CppType:
    """Data to describe a C++ type."""

    includes: list[Include]
    type_name: str
    cpp_namespace: str | None
    const: bool = False
    ref: Ref | None = None

    def render(self, enclosing_namespace: str, with_qualifiers: bool = True) -> str:
        """Convert to a string."""
        const_str = "const " if with_qualifiers and self.const else ""
        ref_str = self.ref.value if with_qualifiers and self.ref else ""
        if enclosing_namespace == self.cpp_namespace or self.cpp_namespace is None:
            return f"{const_str}{self.type_name}{ref_str}"
        return f"{const_str}::{self.cpp_namespace}::{self.type_name}{ref_str}"


@dataclass
class CppValue:
    """Data to describe a C++ value."""

    value_type: CppTypeExpr | None
    value: CppValueExpr | str | list[CppValueExpr | str] | dict[str, CppValueExpr | str]
    """When a list, the values are comma delimited (e.g., initializer
    list).  When a dict, the values are comma delimited as structured
    bindings with the key as the variable name."""

    @property
    def includes(self) -> Iterable[Include]:
        """Includes for the type."""
        if self.value_type is None:
            return []
        yield from self.value_type.includes
        for value in self._value_list():
            if not isinstance(value, str):
                yield from value.includes

    def _value_list(self) -> Iterable[CppValueExpr | str]:
        """Convert the value to an iterable of each value expression ."""
        if isinstance(self.value, list):
            return self.value
        if isinstance(self.value, dict):
            return self.value.values()
        return [self.value]

    def _value_str(self, enclosing_namespace: str) -> str:
        """Convert the value to a str."""
        if isinstance(self.value, dict):
            # When a dict is present, render as structured bindings.
            return ", ".join(
                f".{key} = {render_value_expr(value, enclosing_namespace)}" for key, value in self.value.items()
            )
        return ", ".join(render_value_expr(value, enclosing_namespace) for value in self._value_list())

    def render(self, enclosing_namespace: str) -> str:
        """Convert to a string."""
        if self.value_type is None:
            if isinstance(self.value, list | dict):
                msg = "CppValue does not support multiple value arguments when the value type is None."
                raise ValueError(msg)
            return render_value_expr(self.value, enclosing_namespace)

        return f"{self.value_type.render(enclosing_namespace)}{{{self._value_str(enclosing_namespace)}}}"


@dataclass
class CppFn:
    """A cpp function that can be invoked."""

    headers: list[Include]
    namespace: CppTypeExpr | str | None
    name: str

    def invoke(self, args: Iterable[CppValueExpr]) -> CppFnCall:
        """Invoke the function with arguments."""
        return CppFnCall(fn=self, args=list(args))

    def render(self, enclosing_namespace: str) -> str:
        """Convert to a string."""
        if isinstance(self.namespace, CppTypeExpr):
            return f"{self.namespace.render(enclosing_namespace)}::{self.name}"
        if enclosing_namespace == self.namespace or self.namespace is None:
            return self.name
        return f"::{self.namespace}::{self.name}"

    @property
    def includes(self) -> Iterable[Include]:
        """Includes for the function signature."""
        yield from self.headers
        if isinstance(self.namespace, CppTypeExpr):
            yield from self.namespace.includes


@dataclass
class CppFnCall:
    """An invocation of a CppFn."""

    fn: CppFn
    args: list[CppValueExpr]

    @property
    def includes(self) -> Iterable[Include]:
        """Includes for the function invocation."""
        yield from self.fn.includes
        for arg in self.args:
            yield from arg.includes

    def render(self, enclosing_namespace: str) -> str:
        """Convert to a string."""
        fn_fqn = self.fn.render(enclosing_namespace)
        args_fqn = (arg.render(enclosing_namespace) for arg in self.args)
        return f"{fn_fqn}({', '.join(args_fqn)})"


@dataclass
class CppScopedExpr:
    """An expression scoped to a namespace or class."""

    scope: str | CppTypeExpr
    header: list[Include]
    name: str

    @property
    def includes(self) -> Iterable[Include]:
        """Includes for the expression."""
        yield from self.header
        if not isinstance(self.scope, str):
            yield from self.scope.includes

    def render(self, enclosing_namespace: str) -> str:
        """Convert to a string."""
        if isinstance(self.scope, str):
            if self.scope == enclosing_namespace:
                return self.name
            scope_str = f"::{self.scope}"
        else:
            scope_str = self.scope.render(enclosing_namespace)
        return f"{scope_str}::{self.name}"


@dataclass
class CppScopedValue(CppScopedExpr):
    """A value scoped to a namespace or class."""


@dataclass
class CppScopedType(CppScopedExpr):
    """A type scoped to a namespace or class."""


@dataclass
class CppTemplateType:
    """Data to describe a C++ template."""

    include: list[Include]
    template_name: str
    cpp_namespace: str
    arguments: list[CppTypeExpr | CppValueExpr] | None = None
    const: bool = False
    ref: Ref | None = None

    @property
    def includes(self) -> Iterable[Include]:
        """Produce a generator of all includes."""
        yield from self.include
        if self.arguments is not None:
            for argument in self.arguments:
                yield from argument.includes

    def render(self, enclosing_namespace: str, with_qualifiers: bool = True) -> str:
        """Convert to c++."""
        if self.arguments is None:
            msg = "Must set template arguments before rendering."
            raise RuntimeError(msg)

        type_name = f"{self.template_name}<{', '.join(arg.render(enclosing_namespace) for arg in self.arguments)}>"
        return CppType(list(self.includes), type_name, self.cpp_namespace, self.const, self.ref).render(
            enclosing_namespace, with_qualifiers=with_qualifiers
        )


@dataclass
class CppTemplate:
    """Represents a C++ template that has not been instantiated."""

    includes: list[Include]
    template_name: str
    cpp_namespace: str

    def instantiate(self, arguments: Iterable[CppTypeExpr | CppValueExpr]) -> CppTemplateType:
        """Instantiate the template with arguments."""
        return CppTemplateType(self.includes, self.template_name, self.cpp_namespace, list(arguments))


@dataclass
class CppNamedType:
    """A named type such as a function parameter."""

    argument_type: CppTypeExpr
    argument_name: str
    qualifiers: list[str] | None = None

    @property
    def includes(self) -> Iterable[Include]:
        """Get includes for argument type."""
        return self.argument_type.includes

    def render(self, enclosing_namespace: str) -> str:
        """Convert to c++.

        Args:
            enclosing_namespace: The namespace in which this type is being used.
        """
        args = (self.argument_type.render(enclosing_namespace), self.argument_name)
        if self.qualifiers:
            return " ".join(chain(self.qualifiers, args))
        return " ".join(args)


@dataclass
class CppTemplateParam:
    """A template parameter with a type and optional initial value."""

    named_type: CppNamedType
    default: CppValueExpr | CppType | CppTemplateType | None = None

    @property
    def includes(self) -> Iterable[Include]:
        """Get includes for parameter type."""
        yield from self.named_type.includes
        if self.default:
            yield from self.default.includes

    def render(self, enclosing_namespace: str) -> str:
        """Convert to c++.

        Args:
            enclosing_namespace: The namespace in which this template parameter is being used.
        """
        if self.default:
            return " ".join(
                (self.named_type.render(enclosing_namespace), "=", self.default.render(enclosing_namespace))
            )
        return self.named_type.render(enclosing_namespace)


@dataclass
class CppNamedValue:
    """A named value such as a variable or class member."""

    named_type: CppNamedType
    value: CppValueExpr | None
    doc: str | None
    qualifiers: list[str] | None = None
    render_initializer: bool = True

    @property
    def includes(self) -> Iterable[Include]:
        """Get includes for type."""
        yield from self.named_type.includes
        if self.value:
            yield from self.value.includes

    def render(self, enclosing_namespace: str) -> CppChunk:
        """Convert to c++.

        Args:
            enclosing_namespace: The namespace in which this type is being used.
        """
        value = f"{self.named_type.render(enclosing_namespace)}"
        if self.render_initializer:
            value_str = self.value.render(enclosing_namespace) if self.value else ""
            value += f"{{{value_str}}}"
        value += ";"

        if self.qualifiers:
            value = " ".join(chain(self.qualifiers, (value,)))

        cpp_chunk = CppChunk()
        if self.doc:
            cpp_chunk.append(comment_doc_string(self.doc))
        cpp_chunk.append(value)
        return cpp_chunk


@dataclass
class CppTypeAliasDef:
    """Defines C++ type alias.

    Note: this is only suitable for defining an alias type and not referencing an alias type.
    """

    name: str
    alias_for: CppTypeExpr
    doc: str | None

    @property
    def includes(self) -> Iterable[Include]:
        """Produce a generator of all includes."""
        yield from self.alias_for.includes

    def render(self, enclosing_namespace: str) -> CppChunk:
        """Convert to C++ as a chunk."""
        cpp_chunk = CppChunk()
        if self.doc:
            cpp_chunk.append(comment_doc_string(self.doc))
        cpp_chunk.append(f"using {self.name} = {self.alias_for.render(enclosing_namespace)};")
        return cpp_chunk


@dataclass
class CppMethod:
    """A C++ method.

    Note regarding definition, e.g. if 'body' provided. If leading_qualifiers includes 'inline', then the function
    definition is part of the 'inline_chunk', otherwise the definition is part of the 'implementation_chunk'.
    """

    name: str
    doc: str | None
    return_type: CppTypeExpr
    arguments: list[CppNamedType]
    leading_qualifiers: list[str]
    trailing_qualifiers: list[str]
    body: CppChunk | None
    no_discard: bool
    static: bool = False

    @property
    def includes(self) -> Iterable[Include]:
        """Produce a generator of all includes."""
        yield from self.return_type.includes
        for arg in self.arguments:
            yield from arg.includes

    def render(self, parent_class: CppType | CppTemplateType | None, enclosing_namespace: str) -> CppModuleChunks:
        """Convert to C++ with separate chunks for header / inline.

        Args:
            parent_class: The class in which this method is defined.
            enclosing_namespace: The namespace in which this type is being used.
        """
        cpp_mod = CppModuleChunks()
        parent_fqn = parent_class.render(enclosing_namespace) if parent_class else None
        args = ", ".join(arg.render(enclosing_namespace) for arg in self.arguments)

        return_fqn = self.return_type.render(enclosing_namespace)

        signature = f"{self.name}({args})"

        header_line = ["[[nodiscard]]"] if self.no_discard else []
        if self.static:
            header_line += ["static"]
        header_line += [*self.leading_qualifiers, return_fqn, signature, *self.trailing_qualifiers]

        if self.doc:
            cpp_mod.header_chunk.append(comment_doc_string(self.doc), indent=1 if parent_class else 0)

        cpp_mod.header_chunk.append(f"{' '.join(header_line)};", indent=1 if parent_class else 0)

        if self.body is not None:
            method_name = f"{parent_fqn}::{signature}" if parent_fqn else signature
            # Use the trailing return type syntax for the inline
            # implementation.  Otherwise, because the class is fully
            # qualified, there is parsing ambiguity in something like
            # `uint64_t ::foo::bar()`.  However, using `auto ::foo::bar()
            # -> uint64_t` is not ambiguous.
            signature_line = [
                *self.leading_qualifiers,
                "auto",
                method_name,
                *self.trailing_qualifiers,
                "->",
                return_fqn,
            ]

            # If "inline", then append to inline_chunk, else to impelmentation_chunk
            target_chunk = (
                cpp_mod.inline_chunk
                if "inline" in self.leading_qualifiers or "constexpr" in self.leading_qualifiers
                else cpp_mod.implementation_chunk
            )

            target_chunk.append(
                [
                    " ".join(signature_line),
                    "{",
                ],
            )
            target_chunk.append(self.body, indent=1)
            target_chunk.append("}")

        return cpp_mod


@dataclass
class CppConstructor:
    """A C++ constructor."""

    doc: str | None
    arguments: list[CppNamedType]
    leading_qualifiers: list[str]
    trailing_qualifiers: list[str]
    member_init_list: list[tuple[str, str]] | None
    body: CppChunk | None
    implicit: bool = False

    @property
    def includes(self) -> Iterable[Include]:
        """Produce a generator of all includes."""
        for arg in self.arguments:
            yield from arg.includes

    def render(
        self,
        parent_class: CppType | CppTemplateType,
        enclosing_namespace: str,
        *,
        constructor_name: str | None = None,
    ) -> CppModuleChunks:
        """Convert to C++ with separate chunks for header / inline.

        Args:
            parent_class: The class in which this method is defined.
            enclosing_namespace: The namespace in which this type is being used.
            constructor_name: Optional override for the constructor name. If None, extracted from parent_class.
        """
        if constructor_name is None:
            name = parent_class.type_name if isinstance(parent_class, CppType) else parent_class.template_name
        else:
            name = constructor_name
        parent_fqn = parent_class.render(enclosing_namespace)
        args = ", ".join(arg.render(enclosing_namespace) for arg in self.arguments)

        signature = f"{name}({args})"

        nolint = []
        header_line = [*self.leading_qualifiers]
        if len(self.arguments) == 1:
            if self.implicit:
                nolint += ["google-explicit-constructor"]
            else:
                header_line += ["explicit"]
        header_line += [signature, *self.trailing_qualifiers]
        nolint_str = f" // NOLINT({','.join(nolint)})" if nolint else ""

        cpp_mod = CppModuleChunks()
        if self.doc:
            cpp_mod.header_chunk.append(comment_doc_string(self.doc), indent=1)

        cpp_mod.header_chunk.append(f"{' '.join(header_line)};{nolint_str}", indent=1)

        if self.body or self.member_init_list:
            signature_line = [
                *self.leading_qualifiers,
                f"{parent_fqn}::{signature}",
                *self.trailing_qualifiers,
            ]

            # If "inline", then append to inline_chunk, else to impelmentation_chunk
            target_chunk = cpp_mod.inline_chunk if "inline" in self.leading_qualifiers else cpp_mod.implementation_chunk

            target_chunk.append(" ".join(signature_line))

            if self.member_init_list:
                for index, (init_member, init_value) in enumerate(self.member_init_list):
                    prefix = ":" if index == 0 else ","
                    target_chunk.append(f"{prefix} {init_member}{{{init_value}}}", indent=1)

            target_chunk.append("{")
            if self.body:
                target_chunk.append(self.body, indent=1)
            target_chunk.append("}")

        return cpp_mod


@dataclass
class CppField:
    """Generated includes, set/get methods, and a data member for a schema field."""

    doc: str
    member: CppNamedValue
    methods: list[CppMethod]

    @property
    def includes(self) -> Iterable[Include]:
        """Produce a generator of all includes."""
        yield from self.member.named_type.includes
        for method in self.methods:
            yield from method.includes


class MemberAccess(Enum):
    """Access qualifier for members."""

    public = "public"
    protected = "protected"
    private = "private"


CppMember: TypeAlias = "CppNamedValue | CppMethod | CppConstructor | CppTypeAliasDef | CppStruct"

CppMemberDict: TypeAlias = "dict[MemberAccess, list[CppMember]]"


@dataclass
class CppTypeArg(CppNamedType):
    """A template arg for a class type."""

    def __init__(self, name: str) -> None:
        """Construct the type arg."""
        super().__init__(CppType([], "class", None), name)


@dataclass
class CppStruct:
    """A Cpp struct.

    Any template parameters need to be manually managed.  The type
    does not know where they should be used or if this is a
    specialization of a template.

    """

    name: CppType | CppTemplateType
    doc: str
    attributes: list[str] = field(default_factory=list)
    static_data_members: list[CppNamedValue] = field(default_factory=list)
    members: CppMemberDict = field(default_factory=dict)
    template_param: list[CppTemplateParam] = field(default_factory=list)
    no_lints: list[str] | None = None
    leading_header_chunk: CppChunk | None = None
    parent: CppStruct | None = None
    base_classes: list[tuple[CppTypeExpr, MemberAccess]] = field(default_factory=list)

    @property
    def public(self) -> list[CppMember]:
        """Access the public members."""
        return self.members.setdefault(MemberAccess.public, [])

    @property
    def protected(self) -> list[CppMember]:
        """Access the protected members."""
        return self.members.setdefault(MemberAccess.protected, [])

    @property
    def private(self) -> list[CppMember]:
        """Access the private members."""
        return self.members.setdefault(MemberAccess.private, [])

    def _render_template_params(self, namespace: str) -> str:
        """Render template parameters as a string."""
        return ", ".join(param.render(namespace) for param in self.template_param)

    def _get_qualified_name(self, namespace: str) -> str:
        """Get the fully-qualified name including parent struct names.

        Args:
            namespace: The enclosing namespace for rendering.

        Returns:
            The fully-qualified struct name chain (e.g., "Outer::Inner"), without namespace prefix.
        """
        if self.parent is None:
            return self.name.type_name if isinstance(self.name, CppType) else self.name.template_name
        parent_fqn = self.parent._get_qualified_name(namespace)  # noqa: SLF001 - recursive call to same method
        struct_name = self.name.type_name if isinstance(self.name, CppType) else self.name.template_name
        return f"{parent_fqn}::{struct_name}"

    def _render_struct_declaration(self, namespace: str, *, is_nested: bool = False) -> list[str]:
        """Render the struct declaration line.

        Args:
            namespace: The enclosing namespace.
            is_nested: If True, renders only the simple struct name without namespace qualification.
        """
        if is_nested or self.parent is not None:
            struct_name = self.name.type_name if isinstance(self.name, CppType) else self.name.template_name
        else:
            struct_name = self.name.render(namespace)
        decl = ["struct", *self.attributes, struct_name]

        if self.base_classes:
            base_list = []
            for base_type, access in self.base_classes:
                base_list.append(f"{access.value} {base_type.render(namespace)}")
            decl.append(f": {', '.join(base_list)}")

        if self.no_lints:
            decl.append(f" // NOLINT({', '.join(self.no_lints)})")
        return [" ".join(decl), "{"]

    def _render_static_data_members(self, cpp_mod: CppModuleChunks, namespace: str) -> None:
        """Render static data members into the cpp module."""
        if not self.static_data_members:
            return
        cpp_mod.header_chunk.append("public:")
        for member in self.static_data_members:
            cpp_mod.header_chunk.context.add_includes(member.includes)
            cpp_mod.header_chunk.append(member.render(namespace), indent=1)

    def _render_method_member(self, member: CppMethod, cpp_mod: CppModuleChunks, namespace: str) -> None:
        """Render a method member."""
        member_chunks = member.render(self.name, namespace)

        if self.parent is not None:
            fqn_str = self._get_qualified_name(namespace)
            if isinstance(self.name, CppType):
                fqn_type = CppType(includes=self.name.includes, type_name=fqn_str, cpp_namespace=None)
            else:
                fqn_type = replace(self.name, template_name=fqn_str, cpp_namespace=None)
            fqn_chunks = member.render(fqn_type, namespace)
            member_chunks.inline_chunk = fqn_chunks.inline_chunk
            member_chunks.implementation_chunk = fqn_chunks.implementation_chunk

        if self.template_param and member_chunks.inline_chunk.produce:
            params = self._render_template_params(namespace)
            member_chunks.inline_chunk.lines.insert(0, f"template <{params}>")
        cpp_mod.append(member_chunks)

    def _render_constructor_member(self, member: CppConstructor, cpp_mod: CppModuleChunks, namespace: str) -> None:
        """Render a constructor member."""
        if self.parent is None:
            member_chunks = member.render(self.name, namespace)
        else:
            fqn_str = self._get_qualified_name(namespace)
            simple_name = self.name.type_name if isinstance(self.name, CppType) else self.name.template_name
            if isinstance(self.name, CppType):
                fqn_type = CppType(includes=self.name.includes, type_name=fqn_str, cpp_namespace=None)
            else:
                fqn_type = replace(self.name, template_name=fqn_str, cpp_namespace=None)
            member_chunks = member.render(fqn_type, namespace, constructor_name=simple_name)

        if self.template_param and member_chunks.inline_chunk.produce:
            params = self._render_template_params(namespace)
            member_chunks.inline_chunk.lines.insert(0, f"template <{params}>")
        cpp_mod.append(member_chunks)

    def _render_member(self, member: CppMember, cpp_mod: CppModuleChunks, namespace: str) -> None:
        """Render a single member into the cpp module."""
        cpp_mod.header_chunk.context.add_includes(member.includes)
        if isinstance(member, CppMethod):
            self._render_method_member(member, cpp_mod, namespace)
        elif isinstance(member, CppConstructor):
            self._render_constructor_member(member, cpp_mod, namespace)
        elif isinstance(member, CppStruct):
            if member.parent is None:
                member.parent = self
            nested_chunks = member.render(namespace)
            cpp_mod.header_chunk.append(nested_chunks.header_chunk, indent=1)
            cpp_mod.inline_chunk.append(nested_chunks.inline_chunk)
            cpp_mod.implementation_chunk.append(nested_chunks.implementation_chunk)
        else:
            cpp_mod.header_chunk.append(member.render(namespace), indent=1)

    def _render_members_by_access(self, cpp_mod: CppModuleChunks, namespace: str) -> None:
        """Render members grouped by access level."""
        for access in MemberAccess:
            members = self.members.get(access)
            if members is None:
                continue
            cpp_mod.header_chunk.append(f"{access.value}:")
            for member in members:
                self._render_member(member, cpp_mod, namespace)

    def render(self, namespace: str) -> CppModuleChunks:
        """Render as Cpp code."""
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.context.add_includes(self.name.includes)
        cpp_mod.header_chunk.append(comment_doc_string(self.doc))

        if isinstance(self.name, CppTemplateType) or self.template_param:
            params = self._render_template_params(namespace)
            cpp_mod.header_chunk.append(f"template <{params}>")

        cpp_mod.header_chunk.append(self._render_struct_declaration(namespace))

        if self.leading_header_chunk:
            cpp_mod.header_chunk.append(self.leading_header_chunk)

        self._render_static_data_members(cpp_mod, namespace)
        self._render_members_by_access(cpp_mod, namespace)

        cpp_mod.header_chunk.append("};")

        return cpp_mod

    @property
    def includes(self) -> Iterable[Include]:
        """Get includes required for the struct."""
        yield from self.name.includes

        for param in self.template_param:
            yield from param.includes

        for base_type, _ in self.base_classes:
            yield from base_type.includes

        for member in self.static_data_members:
            yield from member.includes

        for member_list in self.members.values():
            for member in member_list:
                yield from member.includes


def ref_qualify(cpp_type: CppTypeExpr, ref: Ref) -> CppType | CppTemplateType:
    """Apply ref qualification."""
    if isinstance(cpp_type, CppScopedType):
        msg = "Const qualify is not yet implemented for scoped types."
        raise NotImplementedError(msg)
    new_qual = replace(cpp_type)
    new_qual.ref = ref
    return new_qual


def const_qualify(cpp_type: CppTypeExpr, const: bool) -> CppTypeExpr:
    """Apply const qualification."""
    if isinstance(cpp_type, CppScopedType):
        msg = "Const qualify is not yet implemented for scoped types."
        raise NotImplementedError(msg)
    new_qual = replace(cpp_type)
    new_qual.const = const
    return new_qual


@overload
def cref_qualify(cpp_type: CppType, const: bool, ref: Ref) -> CppType:
    pass


@overload
def cref_qualify(cpp_type: CppTemplateType, const: bool, ref: Ref) -> CppTemplateType:
    pass


def cref_qualify(cpp_type: CppType | CppTemplateType, const: bool, ref: Ref) -> CppType | CppTemplateType:
    """Apply ref qualification."""
    return replace(cpp_type, ref=ref, const=const)


CppValueExpr: TypeAlias = CppValue | CppFnCall | CppScopedValue
CppTypeExpr: TypeAlias = CppType | CppTemplateType | CppScopedType


def render_value_expr(value: CppValueExpr | str, enclosing_namespace: str) -> str:
    """Render a value expression or a string.

    Pyright produces a partially unknown type error if we use
    CppValueExpr | str, so here we explicitly list each of the value
    types.

    """
    if isinstance(value, str):
        return value
    return value.render(enclosing_namespace)


ALGORITHM_HEADER: Final = SystemHeader("algorithm")

ITERATOR_HEADER: Final = SystemHeader("iterator")

VOID: Final = CppType([], "void", None)

STRING_VIEW: Final = CppType([SystemHeader("string_view")], "string_view", "std")

STRING: Final = CppType([SystemHeader("string")], "string", "std")
PMR_STRING: Final = CppType([SystemHeader("memory_resource"), SystemHeader("string")], "string", "std::pmr")

MOVE: Final = CppFn([SystemHeader("utility")], "std", "move")

MEMORY_RESOURCE: Final = CppType(
    [Header(JEWELS_REPO, "jewels/memory/memory_resource.hh")], "MemoryResource", "jewels::memory"
)

BINARY_OUTCOME: Final = CppType([Header(JEWELS_REPO, "jewels/callsig/outcome.hh")], "BinaryOutcome", "jewels")

CHAR: Final = CppType([], "char", None)

SPAN: Final = CppTemplate(
    includes=[SystemHeader("span")],
    template_name="span",
    cpp_namespace="std",
)

AUTO: Final = CppType([], "auto", None)

CASING_PROTO_SCHEMA = CppTemplate(
    includes=[Header(CLK_REPO, "clockwork/scaffolding/casing.hh")],
    template_name="ProtoSchema",
    cpp_namespace="clockwork::scaffolding",
)

CXX_SCHEMA_SCHEMA = CppTemplate(
    includes=[Header(CLK_REPO, "clockwork/scaffolding/casing.hh")],
    template_name="CxxSchema",
    cpp_namespace="clockwork::scaffolding",
)

BOOLEAN: Final = CppType([], "bool", None)

NB_BYTES: Final = CppType([SystemHeader("nanobind/nanobind.h")], "bytes", "nanobind")

ARRAY: Final = CppTemplate(
    includes=[SystemHeader("array")],
    template_name="array",
    cpp_namespace="std",
)

UINT8: Final = CppType([SystemHeader("cstdint")], "uint8_t", None)
UUID: Final = CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/uuid/uuid.hh")],
    cpp_namespace="jewels",
    template_name="Uuid",
)

SYNC_TIME: Final = CppType(
    includes=[Header(JEWELS_REPO, "jewels/time/sync_time.hh")],
    type_name="SyncTime",
    cpp_namespace="jewels::time",
)

NANOSECONDS = CppType(
    includes=[SystemHeader("chrono")],
    type_name="nanoseconds",
    cpp_namespace="std::chrono",
)

MICROSECONDS = CppType(
    includes=[SystemHeader("chrono")],
    type_name="microseconds",
    cpp_namespace="std::chrono",
)

MILLISECONDS = CppType(
    includes=[SystemHeader("chrono")],
    type_name="milliseconds",
    cpp_namespace="std::chrono",
)

SECONDS = CppType(
    includes=[SystemHeader("chrono")],
    type_name="seconds",
    cpp_namespace="std::chrono",
)
