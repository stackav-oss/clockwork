# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Protobuf types used for describing and rendering protobuf messages."""

from __future__ import annotations

import itertools
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from clockwork.dsl.ir import clkbuiltins, module_id, schema, typesys
from clockwork.dsl.proto import proto_typereg
from clockwork.dsl.proto.proto_typereg import (
    DefinedProtobufType,
    ProtobufType,
)

if TYPE_CHECKING:
    from collections.abc import Iterable

    from clockwork.dsl.compiler_context import CompilerContext


INDENT = "   "


@dataclass
class ProtobufField:
    """Represents a field in a protobuf message."""

    type_info: ProtobufType
    var_name: str

    def render(self) -> str:
        """Render as string."""
        return f"{self.type_info.render()} {self.var_name}"


@dataclass(frozen=True)
class ProtobufDep:
    """Represents a dependency."""

    module_id: module_id.ModuleID
    path: Path


@dataclass
class ProtobufMsgLayout:
    """Represents a single protobuf message."""

    fields: list[ProtobufField]
    type_name: str

    @property
    def deps(self) -> Iterable[ProtobufDep]:
        """Get all dependencies of this message."""
        for field in self.fields:
            if (
                isinstance(field.type_info, DefinedProtobufType)
                and field.type_info.module_id
                and field.type_info.import_location
            ):
                yield ProtobufDep(
                    module_id=field.type_info.module_id,
                    path=Path(field.type_info.import_location),
                )

    @property
    def includes(self) -> Iterable[str]:
        """Get all dependencies of this message."""
        for field in self.fields:
            if isinstance(field.type_info, DefinedProtobufType) and field.type_info.import_location:
                yield field.type_info.import_location

    def render(self) -> str:
        """Render the message."""
        preamble = [f"message {self.type_name} " + "{"]

        lines = [f"{INDENT} {field.render()} = {num + 1!s};" for num, field in enumerate(self.fields)]
        postamble = ["}"]
        return "\n".join(line for line in itertools.chain(preamble, lines, postamble))


def to_protobuf_layout(
    alias: str, typespec: schema.Schema | typesys.Instantiation, compiler_context: CompilerContext
) -> ProtobufMsgLayout:
    """Convert from IR to a ProtobufMsgLayout."""
    schema_ir = schema.InstantiatedSchema.from_typespec(typespec)
    message_fields = [
        ProtobufField(proto_typereg.get_protobuf_type(field.type_info, compiler_context), field.cur_name)
        for field in schema_ir.fields.values()
    ]
    name = alias if (isinstance(typespec, typesys.Instantiation) or alias) else typespec.name
    return ProtobufMsgLayout(fields=message_fields, type_name=name)


def to_schema_instantiation(typespec: typesys.Instantiation) -> schema.Schema | typesys.Instantiation:
    """Extract the instantiated schema type from a Protobuf instantiation."""
    if typespec.instantiates is clkbuiltins.PROTOBUF:
        schema_ir = typespec.arguments["schema"]
        if isinstance(schema_ir, schema.Schema | typesys.Instantiation):
            return schema_ir
        msg = f"Unexpected type for schema: {type(schema_ir)}"
        raise TypeError(msg)
    msg = f"Expected an instantiation of Protobuf Received: {type(typespec)}"
    raise TypeError(msg)


def render(alias: str, typespec: typesys.Instantiation, compiler_context: CompilerContext) -> ProtobufMsgLayout:
    """Render a protobuf interface and instantiation."""
    return to_protobuf_layout(alias, to_schema_instantiation(typespec), compiler_context)
