# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Mixin type for entities with a message type."""

from __future__ import annotations

from dataclasses import dataclass
from textwrap import dedent
from typing import TYPE_CHECKING

from clockwork.dsl.ir import (
    clkbuiltins,
    cog_parameters,
    expr,
    interface,
    representation,
    schema,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir.interface import InterfaceReference

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


@dataclass
class MessageTypeMixin:
    """Mixin class for things that have a message type."""

    message_type: schema_reg.InterfaceInfo | cog_parameters.CogParameterRef | typesys.Instantiation | expr.Expr

    def get_interface(self) -> interface.InterfaceInstantiation:
        """Retrieve the underlying interface.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        iface_info = self.get_interface_info()
        if not isinstance((interface_inst := iface_info.interface_ir), interface.InterfaceInstantiation):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = f"Attempt to get interface for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return interface_inst

    def get_interface_info(self) -> schema_reg.InterfaceInfo:
        """Retrieve the underlying interface info.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        if not isinstance(self.message_type, schema_reg.InterfaceInfo):
            msg = f"Attempt to get interface for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return self.message_type

    def get_representation_typespec(self) -> typesys.Instantiation:
        """Retrieve the underlying representation type.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        interface_inst = self.get_interface()
        if not isinstance(interface_inst.representation, representation.RepresentationReference) or not isinstance(  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            typespec := interface_inst.representation.typespec, typesys.Instantiation
        ):
            msg = f"Attempt to get representation for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return typespec

    def get_representation_reference(self) -> representation.RepresentationReference:
        """Retrieve the underlying representation reference.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        interface_inst = self.get_interface()
        if not isinstance(interface_inst.representation, representation.RepresentationReference):
            msg = f"Attempt to get representation for unresolved entity: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Accessing unresolved entity is a runtime error)
        return interface_inst.representation

    def get_instantiated_schema(self) -> schema.InstantiatedSchema:
        """Retrieve the instantiated schema for this entity's message type.

        Navigates the chain: interface → representation → schema_ir.

        Raises:
            RuntimeError if self is not yet resolved.
        """
        return self.get_representation_reference().schema_ir


def resolve_schema_interface(
    compiler_context: CompilerContext,
    message_type: schema_reg.InterfaceInfo | cog_parameters.CogParameterRef | typesys.Instantiation | expr.Expr,
) -> schema_reg.InterfaceInfo:
    """Private helper function for resolving schema interfaces."""
    if isinstance(message_type, cog_parameters.CogParameterRef | typesys.Instantiation):
        msg = f"Cannot resolve parameterized schema interface {message_type}"
        raise TypeError(msg)
    if isinstance(message_type, schema_reg.InterfaceInfo):
        return message_type
    typespec = message_type.evaluate()
    interface_ref = InterfaceReference.from_typespec(typespec)
    if isinstance(interface_ref, str):
        msg = message_type.append_error_line(
            f"Expected a schema interface: {interface_ref}",
        )
        raise TypeError(msg)
    interface = schema_reg.lookup_interface(compiler_context, interface_ref)
    if interface is None:
        msg = message_type.append_error_line(
            dedent(
                """
                Interface not instantiated, or instantiation not visible.
                Make sure this is instantiated in a cpp_target block in a module you've imported.
                """,
            ),
        )
        raise TypeError(msg)
    return interface


def resolve_parameterized_schema_interface(
    compiler_context: CompilerContext,
    message_type: schema_reg.InterfaceInfo | cog_parameters.CogParameterRef | typesys.Instantiation | expr.Expr,
) -> schema_reg.InterfaceInfo | cog_parameters.CogParameterRef | typesys.Instantiation:
    """Validate that the typespec will be a valid reference after cog parameter substitution."""
    if not isinstance(message_type, expr.Expr):
        return message_type
    typespec = message_type.evaluate()
    if isinstance(typespec, cog_parameters.CogParameterRef):
        return typespec
    if not isinstance(typespec, typesys.Instantiation) or len(typespec.arguments) != 1:
        msg = message_type.append_error_line(f"Invalid Interface type: {type(message_type)}")
        raise ValueError(msg)
    if typespec.instantiates not in (clkbuiltins.TAP, clkbuiltins.TAPPY):
        msg = message_type.append_error_line(f"Only Tap interface supported so far: {typespec}")
        raise ValueError(msg)
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
        msg = message_type.append_error_line(f"Interface must be instantiated for a schema: {message_type}")
        raise TypeError(msg)
    schema_ir = representation.arguments.get("schema")
    if isinstance(schema_ir, cog_parameters.CogParameterRef):
        return typespec
    if not isinstance(schema_ir, typesys.Instantiation):
        return resolve_schema_interface(compiler_context, message_type)
    if not isinstance(schema_ir.instantiates, schema.Schema):
        msg = message_type.append_error_line("Interface must be a schema")
        raise TypeError(msg)
    return typespec
