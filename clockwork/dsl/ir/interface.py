# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Interface-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.ir import clkbuiltins, expr, node, schema, typesys
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.representation import RepresentationReference
from clockwork.dsl.serialization import tap_render

if TYPE_CHECKING:
    from clockwork.dsl.cpp.context import CppModuleChunks


@dataclass(frozen=True, eq=True, slots=True)
class InterfaceReference:
    """A reference to an interface.

    Attributes:
        representation_ir: Representation supported by this interface
        typespec: Instantiation of interface
    """

    representation: RepresentationReference
    typespec: typesys.Instantiation

    @classmethod
    def from_typespec(cls: type[InterfaceReference], typespec: typesys.Value) -> InterfaceReference | str:
        """Create an InterfaceReference from a type specification.

        Args:
            typespec: A Value expected to resolve to a interface.

        Returns:
            A reference if the typespec has the form of a interface, otherwise an error message.
        """
        if not isinstance(typespec, typesys.Instantiation) or len(typespec.arguments) != 1:
            return f"Invalid Interface type: {type(typespec)}"
        if typespec.instantiates not in (clkbuiltins.POD, clkbuiltins.TAP, clkbuiltins.TAPPY):
            return f"Only Pod and Tap interface supported so far: {typespec}"
        if typespec.instantiates is clkbuiltins.TAPPY:
            # Some sleight of hand to convert from the TAPPY short-hand notation to Tap<Tachyon<>>;
            typespec = typesys.Instantiation(
                type_info=clkbuiltins.TYPE_TYPE,
                instantiates=clkbuiltins.TAP,
                arguments={
                    "representation": typesys.Instantiation(
                        type_info=clkbuiltins.TYPE_TYPE,
                        instantiates=clkbuiltins.TACHYON,
                        arguments={"schema": typespec.arguments["schema"]},
                    )
                },
            )
        representation = typespec.arguments["representation"]
        if not isinstance(representation, typesys.Instantiation):
            return f"Interface must be instantiated for a schema: {schema}"

        schema_ir = schema.retrieve_schema(representation.arguments.get("schema"))
        if isinstance(schema_ir, str):
            return schema_ir

        representation_ref = RepresentationReference.from_typespec(representation)
        if isinstance(representation_ref, str):
            return representation_ref
        return cls(representation=representation_ref, typespec=typespec)


@dataclass(slots=True)
class InterfaceAlias(node.NamedEntity):
    """An alias (name) for an InterfaceInstantion.

    This indirection is needed to prevent having multiple references to named
    instantiations (at both module scope and inside the
    cpp_target/py_target/proto_target), which would lead to incorrect name
    resolution.  Specifically, name resolution would attempt to recurse into the
    instantiation twice, once at module scope and again inside the target.
    """

    interface: InterfaceInstantiation

    @classmethod
    def from_instantiation(cls: type[InterfaceAlias], instantiation: InterfaceInstantiation) -> InterfaceAlias:
        """Factory to construct an InterfaceAlias."""
        return InterfaceAlias(name=instantiation.name, scope=instantiation.module.inner_scope, interface=instantiation)


@dataclass
class InterfaceInstantiation(node.NamedEntity, node.CstNode[cst.CppInterface]):
    """An instantation of an interface for a schema representation."""

    representation: RepresentationReference | None
    is_generic: bool
    typespec: typesys.TypeVal | expr.Expr = field(repr=False)

    @classmethod
    def from_cst(
        cls: type[InterfaceInstantiation],
        cst_node: cst.CppInterface,
        module: node.Module,
    ) -> InterfaceInstantiation:
        """Create an IR SchemaInterface from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        is_generic = cst_node.maybe_generic() is not None
        name_cst = cst_node.maybe_name()
        name = get_span(name_cst.child_value(), module.terminals) if name_cst else ""
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            representation=None,
            is_generic=is_generic,
            name=name,
            typespec=typespec,
        )

    @classmethod
    def from_schema(
        cls: type[InterfaceInstantiation],
        schema_ir: schema.Schema | typesys.Instantiation,
        module: node.Module,
        name: str | None = None,
    ) -> InterfaceInstantiation:
        """Create a resolved interface instantiation from a resolved schema or schema instantiation.

        Arguments:
            schema_ir: Resolved schema or schema instantiation
            module: Module containing the schema.
            name: Instantiation name

        Returns:
            Resolved interface instantion for the schema.
        """
        repr_typespec = typesys.Instantiation(
            instantiates=clkbuiltins.TACHYON,
            arguments={"schema": schema_ir},
            type_info=clkbuiltins.TYPE_TYPE,
        )
        return cls(
            module=module,
            cst_node=None,
            name=name or "",
            scope=module.inner_scope,
            representation=RepresentationReference(
                schema_ir=schema.InstantiatedSchema.from_typespec(schema_ir),
                typespec=repr_typespec,
            ),
            is_generic=False,
            typespec=typesys.Instantiation(
                instantiates=clkbuiltins.TAP,
                arguments={"representation": repr_typespec},
                type_info=clkbuiltins.TYPE_TYPE,
            ),
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.typespec, expr.Expr):
            msg = self.append_error_line(f"Attempt to resolve Interface twice: {self}")
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.typespec.evaluate()
        if not isinstance(typespec, typesys.Instantiation) or len(typespec.arguments) != 1:
            msg = self.append_error_line(f"Invalid Interface type: {typespec}")
            raise TypeError(msg)
        ref = InterfaceReference.from_typespec(typespec)
        if isinstance(ref, str):
            msg = self.typespec.append_error_line(ref)
            raise TypeError(msg)
        parameters = ref.representation.schema_ir.schema.generic_parameters()
        if self.is_generic:
            if parameters is None:
                msg = self.append_error_line("Generic interface requested for non-generic schema")
                raise ValueError(msg)
            if isinstance(ref.typespec, typesys.Instantiation):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
                msg = self.typespec.append_error_line("Generic interface requested for instantiated schema")
                raise ValueError(msg)
        elif parameters is not None and not isinstance(
            ref.representation.typespec.arguments["schema"],
            typesys.Instantiation,
        ):
            msg = self.append_error_line(
                'Instantiating interface for generic schema requires parameters or "generic" keyword',
            )
            raise ValueError(msg)
        self.typespec = ref.typespec
        self.representation = ref.representation

    def _render_pre_check(self) -> typesys.Instantiation:
        """Validates the interface is renderable."""
        if not isinstance(self.typespec, typesys.Instantiation):
            msg = "Attempted to render before resolving."
            raise TypeError(msg)
        if self.typespec.instantiates is not clkbuiltins.TAP:
            msg = "Only Tap interfaces are supported."
            raise TypeError(msg)
        if self.representation is None:
            msg = "Resolved instantiation is missing the representation."
            raise TypeError(msg)
        if self.representation.schema_ir is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = "Resolved instantiation is missing the schema."
            raise TypeError(msg)

        return self.typespec

    def render(self, enclosing_namespace: str) -> CppModuleChunks:
        """Render to C++ code."""
        typespec = self._render_pre_check()
        return tap_render.render(
            compiler_context=self.module.context, typespec=typespec, enclosing_namespace=enclosing_namespace
        )

    def render_alias(self, enclosing_namespace: str) -> CppModuleChunks:
        """Render alias to C++ code."""
        typespec = self._render_pre_check()
        return tap_render.render_alias(self.module.context, typespec, enclosing_namespace, self.name)
