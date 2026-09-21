# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Schema related IR for converters between representations."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.ir import (
    clkbuiltins,
    expr,
    node,
    proto_to_tap,
    schema,
    tap_to_proto,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.interface import InterfaceReference
from clockwork.dsl.ir.representation import RepresentationReference

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.cpp import context
    from clockwork.dsl.ir.conversion_utils import ConversionRegistration


@dataclass
class Converter(node.NamedEntity, node.CstNode[cst.Converter]):
    """A class that specifies a conversion between representations and or interfaces."""

    source_reference: RepresentationReference | InterfaceReference | None
    destination_reference: RepresentationReference | InterfaceReference | None
    typespec: typesys.Instantiation | expr.Expr | None
    namespace: str

    @classmethod
    def from_cst(
        cls: type[Converter],
        cst_node: cst.Converter,
        module: node.Module,
        namespace: str,
    ) -> Converter:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        name_cst = cst_node.maybe_name()
        name = get_span(name_cst.child_value(), module.terminals) if name_cst else ""
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(
            module=module,
            scope=module.inner_scope,
            cst_node=cst_node,
            name=name,
            typespec=typespec,
            namespace=namespace,
            source_reference=None,
            destination_reference=None,
        )

    @classmethod
    def from_generate_proto_conv(
        cls: type[Converter],
        module: node.Module,
        namespace: str,
        converter_type: typesys.TypeVal,
        cpp_typespec: typesys.Instantiation,
        proto_typespec: typesys.Instantiation,
    ) -> Converter:
        """Create an IR node for auto generated proto_conv."""
        cpp_reference = InterfaceReference.from_typespec(cpp_typespec)
        assert not isinstance(cpp_reference, str)
        proto_reference = RepresentationReference.from_typespec(proto_typespec)
        assert not isinstance(proto_reference, str)
        if converter_type is clkbuiltins.PROTOBUF_TO_TAP:
            return cls(
                module=module,
                scope=module.inner_scope,
                cst_node=None,
                name="",
                typespec=typesys.Instantiation(
                    instantiates=clkbuiltins.PROTOBUF_TO_TAP,
                    arguments={
                        "source": proto_typespec,
                        "destination": cpp_typespec,
                    },
                    type_info=clkbuiltins.TYPE_TYPE,
                ),
                namespace=namespace,
                source_reference=proto_reference,
                destination_reference=cpp_reference,
            )
        assert converter_type is clkbuiltins.TAP_TO_PROTOBUF
        return cls(
            module=module,
            scope=module.inner_scope,
            cst_node=None,
            name="",
            typespec=typesys.Instantiation(
                instantiates=clkbuiltins.TAP_TO_PROTOBUF,
                arguments={
                    "source": cpp_typespec,
                    "destination": proto_typespec,
                },
                type_info=clkbuiltins.TYPE_TYPE,
            ),
            namespace=namespace,
            source_reference=cpp_reference,
            destination_reference=proto_reference,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.typespec, expr.Expr):
            msg = self.append_error_line(f"Attempt to resolve Converter twice: {self.name}")
            raise TypeError(msg)
        typespec = self.typespec.evaluate()

        num_type_spec_arguments: Final = 2
        if not isinstance(typespec, typesys.Instantiation) or len(typespec.arguments) != num_type_spec_arguments:
            msg = f"Invalid Conversion type: {type(typespec)}"
            raise TypeError(msg)
        self.typespec = typespec
        source_typespec = self.typespec.arguments["source"]
        destination_typespec = self.typespec.arguments["destination"]
        source_reference = _get_reference_from_typespec(source_typespec)
        destination_reference = _get_reference_from_typespec(destination_typespec)

        if isinstance(source_reference, str) or isinstance(destination_reference, str):
            msg = "The source and destination parameters of Converters can only contain representations and interfaces."
            raise TypeError(msg)

        self.source_reference = source_reference
        self.destination_reference = destination_reference

        self._validate_converter()

    def render(self, compiler_context: CompilerContext, namespace: str) -> context.CppModuleChunks:
        """Render the converter into a cpp module."""
        if not isinstance(self.typespec, typesys.Instantiation):
            msg = self.append_error_line(f"Attempted to render a converter before resolving: {self.name}")
            raise TypeError(msg)

        if self.typespec.instantiates is clkbuiltins.PROTOBUF_TO_TAP:
            assert isinstance(self.source_reference, RepresentationReference)
            assert isinstance(self.destination_reference, InterfaceReference)
            return proto_to_tap.render_protobuf_to_tachyon_converter(
                compiler_context, self.source_reference, self.destination_reference, namespace
            )

        if self.typespec.instantiates is clkbuiltins.TAP_TO_PROTOBUF:
            assert isinstance(self.source_reference, InterfaceReference)
            assert isinstance(self.destination_reference, RepresentationReference)
            return tap_to_proto.render_tachyon_to_protobuf_converter(
                compiler_context, self.source_reference, self.destination_reference, namespace
            )

        msg = self.append_error_line("Unsupported Converter. Only TapToProtobuf and ProtobufToTap are supported")
        raise TypeError(msg)

    def _validate_converter(self) -> None:
        """Ensure the conversion being requested in the converter is supported.

        Currently only ProtobufToTap and TapToProtobuf is supported.
        """
        if not isinstance(self.typespec, typesys.Instantiation):
            msg = self.append_error_line(f"Attempted to validate a converter before resolving: {self.name}")
            raise TypeError(msg)

        if self.source_reference is None or self.destination_reference is None:
            msg = f"Attempted to validate a converter before resolving: {self.name}"
            raise ValueError(msg)

        if self.typespec.instantiates is clkbuiltins.PROTOBUF_TO_TAP:  # noqa: SIM102 this is cleaner than aggregating all of the conditions
            if (self.source_reference.typespec.instantiates is not clkbuiltins.PROTOBUF) or (
                self.destination_reference.typespec.instantiates not in (clkbuiltins.TAP, clkbuiltins.TAPPY)
            ):
                msg = self.append_error_line(
                    "Attempted to use a ProtobufToTap Converter with incompatible source or destination type"
                )
                raise TypeError(msg)
        if self.typespec.instantiates is clkbuiltins.TAP_TO_PROTOBUF:  # noqa: SIM102 this is cleaner than aggregating all of the conditions
            if (self.source_reference.typespec.instantiates not in (clkbuiltins.TAP, clkbuiltins.TAPPY)) or (
                self.destination_reference.typespec.instantiates is not clkbuiltins.PROTOBUF
            ):
                msg = self.append_error_line(
                    "Attempted to use a TapToProtobuf Converter with incompatible source or destination type"
                )
                raise TypeError(msg)


def _get_reference_from_typespec(typespec: typesys.Value) -> RepresentationReference | InterfaceReference | str:
    maybe_rep_ref = RepresentationReference.from_typespec(typespec)
    if isinstance(maybe_rep_ref, RepresentationReference):
        return maybe_rep_ref

    return InterfaceReference.from_typespec(typespec)


def register_schema_conversion(
    converter: Converter, conversion_info: ConversionRegistration, compiler_context: CompilerContext
) -> None:
    """Register a converter in the registry.

    This allows other converters to know that a conversion exists for a specific schema and to obtain its namespace.
    """
    if not isinstance(converter.typespec, typesys.Instantiation):
        msg = "Attempted to register a converter that hasn't been resolved"
        raise TypeError(msg)

    if converter.typespec.instantiates is clkbuiltins.PROTOBUF_TO_TAP:
        assert isinstance(converter.source_reference, RepresentationReference)
        assert isinstance(converter.destination_reference, InterfaceReference)

        if converter.source_reference.schema_ir.schema.parameters:
            schema_arg = converter.source_reference.typespec.arguments["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
            proto_to_tap.register_schema_conversion(schema_ir, conversion_info, compiler_context)
        else:
            proto_to_tap.register_schema_conversion(
                converter.source_reference.schema_ir, conversion_info, compiler_context
            )
    elif converter.typespec.instantiates is clkbuiltins.TAP_TO_PROTOBUF:
        assert isinstance(converter.source_reference, InterfaceReference)
        assert isinstance(converter.destination_reference, RepresentationReference)

        if (
            converter.destination_reference.schema_ir.schema.parameters
            and "schema" in converter.source_reference.typespec.arguments
        ):
            schema_arg = converter.source_reference.typespec.arguments["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
            proto_to_tap.register_schema_conversion(schema_ir, conversion_info, compiler_context)
        else:
            tap_to_proto.register_schema_conversion(
                converter.destination_reference.schema_ir, conversion_info, compiler_context
            )
