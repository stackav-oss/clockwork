# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Mixin type for entities with a message type."""

from __future__ import annotations

from dataclasses import dataclass
from textwrap import dedent
from typing import TYPE_CHECKING

from clockwork.dsl.ir import (
    expr,
    interface,
    representation,
    schema_reg,
    typesys,
)
from clockwork.dsl.ir.interface import InterfaceReference

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext


@dataclass
class MessageTypeMixin:
    """Mixin class for things that have a message type."""

    message_type: schema_reg.InterfaceInfo | expr.Expr

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


def resolve_schema_interface(
    compiler_context: CompilerContext, message_type: schema_reg.InterfaceInfo | expr.Expr
) -> schema_reg.InterfaceInfo:
    """Private helper function for resolving schema interfaces."""
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
