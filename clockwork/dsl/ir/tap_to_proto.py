# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Code gen utilities for converting from TAP/Tachyon to protobuf."""

from __future__ import annotations

import dataclasses
from typing import TYPE_CHECKING, Final

from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.cpp.context import CppChunk, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    CppMethod,
    CppNamedType,
    Ref,
)
from clockwork.dsl.ir import clkbuiltins, clkenum, expr, schema, schema_reg, strongtypes, typesys
from clockwork.dsl.ir.conversion_utils import (
    ConversionInfo,
    ConversionParams,
    ConverterRegistry,
    protobuf_repr_to_cpp_type,
    to_schema_instantiation,
)
from clockwork.dsl.ir.module_id import JEWELS_REPO
from clockwork.dsl.proto import proto_typereg
from clockwork.dsl.serialization import tap

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.conversion_utils import ConversionRegistration
    from clockwork.dsl.ir.interface import InterfaceReference
    from clockwork.dsl.ir.representation import RepresentationReference


_TYPE_CONVERSION_MAP: Final[dict[str, ConversionInfo]] = {}

_CONVERTER_REGISTRY: ConverterRegistry = ConverterRegistry({})


def in_converter_registry(clk_type: typesys.Value) -> bool:
    """Checks whether a schema instantiation is registered in the conversion registry."""
    return _CONVERTER_REGISTRY.in_converter_registry(clk_type)


def get_conversion_registration(clk_type: typesys.Value) -> ConversionRegistration:
    """Obtains the conversion registration for a specific schema type."""
    return _CONVERTER_REGISTRY.get_conversion_registration(clk_type)


def register_schema_conversion(clk_type: typesys.Value, conversion_info: ConversionRegistration) -> None:
    """Register a converter in the registry."""
    _CONVERTER_REGISTRY.register_schema_conversion(clk_type, conversion_info)


def _proto_type_to_cpp_namespace(proto_type_str: str) -> str:
    """Converts a Protobuf type string (using '.') to a C++ namespaced string (using '::').

    Also removes the 'optional ' prefix if present.
    """
    # Remove "optional " prefix if it exists
    proto_type_str = proto_type_str.removeprefix("optional ")
    return proto_type_str.replace(".", "::")


def _generate_simple_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a conversion for types that effectively match in protobuf and C++."""
    get_statement = "value_" if conversion_params.is_optional else "get_"
    return [
        f"{conversion_params.destination_name}."
        + f"set_{conversion_params.proto_field_name}({conversion_params.source_name}.{get_statement}{conversion_params.field_name}());"
    ]


def _generate_enum_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter between the TAP/Tachyon enum and protobuf enum."""
    # Get the Protobuf C++ enum type string and convert to C++ namespace
    proto_pb_enum_type_str = proto_typereg.get_protobuf_type(conversion_params.field_type).render()
    valid_cpp_enum_type = _proto_type_to_cpp_namespace(proto_pb_enum_type_str)
    # Determine the source access method (get_... or value_...
    if conversion_params.is_optional:
        source_access = f"{conversion_params.source_name}.value_{conversion_params.field_name}()"
    else:
        source_access = f"{conversion_params.source_name}.get_{conversion_params.field_name}()"

    return [
        f"{conversion_params.destination_name}"
        + f".set_{conversion_params.proto_field_name}"
        + f"(static_cast<{valid_cpp_enum_type}>"
        + f"({source_access}));"
    ]


def _generate_schema_converter(conversion_params: ConversionParams, namespace: str) -> list[str]:
    """Generates a converter for a schema type. This will be calling another generated tap_to_protobuf function."""
    return [
        f"{namespace}::tap_to_protobuf("
        + f"*{conversion_params.destination_name}.mutable_{conversion_params.proto_field_name}(), "
        + f"{conversion_params.source_name}.get_{conversion_params.field_name}());"
    ]


def _generate_complex_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for types that need to use the helper functions defined in jewels/container/tap/tap_to_protobuf.h."""
    if conversion_params.is_optional:
        source_access = f"{conversion_params.source_name}.value_{conversion_params.field_name}()"
    else:
        source_access = f"{conversion_params.source_name}.get_{conversion_params.field_name}()"
    return [
        "jewels::tap_to_protobuf("
        + f"*{conversion_params.destination_name}.mutable_{conversion_params.proto_field_name}(), "
        + f"{source_access});",
    ]


def _generate_optional_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter for optional types."""
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the optional conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    value_type = conversion_params.field_type.arguments["type"]
    if in_converter_registry(value_type):
        conversion_statement = [
            f"{get_conversion_registration(value_type).namespace}::"
            + f"tap_to_protobuf(*{conversion_params.destination_name}.mutable_{conversion_params.proto_field_name}(), "
            + f"{conversion_params.source_name}.value_{conversion_params.field_name}());",
        ]
    else:
        updated_conversion_params = dataclasses.replace(conversion_params, field_type=value_type, is_optional=True)
        conversion_statement = _generate_conversion_statement(value_type).conversion_generator(
            updated_conversion_params
        )

    return [
        f"if ({conversion_params.source_name}.has_{conversion_params.field_name}())",
        "{",
        *conversion_statement,
        "}",
    ]


def _generate_array_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for variable array types."""
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the array conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    input_name = f"{conversion_params.field_name}_input"
    value_type = conversion_params.field_type.arguments["type"]

    if in_converter_registry(value_type):
        conversion_lines = [
            f"{context.INDENT}auto element_{input_name} = {conversion_params.destination_name}.add_{conversion_params.proto_field_name}();",
            f"{context.INDENT}{get_conversion_registration(value_type).namespace}::tap_to_protobuf(*element_{input_name}, {input_name}[i]);",
        ]
    elif value_type == clkbuiltins.BYTE:
        return [
            "jewels::tap_to_protobuf("
            + f"*{conversion_params.destination_name}.mutable_{conversion_params.proto_field_name}(), "
            + f"{conversion_params.source_name}.get_{conversion_params.field_name}());",
        ]
    elif isinstance(value_type, clkenum.ResolvedEnum):
        # Get the Protobuf C++ enum type string and convert to C++ namespace
        proto_pb_enum_type_str = proto_typereg.get_protobuf_type(value_type).render()
        valid_cpp_enum_type = _proto_type_to_cpp_namespace(proto_pb_enum_type_str)
        conversion_lines = [
            f"{context.INDENT}{conversion_params.destination_name}.add_{conversion_params.proto_field_name}("
            + f"static_cast<{valid_cpp_enum_type}>({input_name}[i]));"
        ]
    elif isinstance(value_type, strongtypes.StrongType):
        underlying_primitive_type = value_type.get_underlying_type()
        underlying_cpp_type_str = typereg.get_cpp_type(
            conversion_params.compiler_context, underlying_primitive_type
        ).render(conversion_params.enclosing_namespace)
        # Get the C++ type string for the strong type itself for the static_assert
        strong_cpp_type_str = typereg.get_cpp_type(conversion_params.compiler_context, value_type).render(
            conversion_params.enclosing_namespace
        )
        conversion_lines = [
            f"{context.INDENT}const auto& strong_element = {input_name}[i];",
            f"{context.INDENT}const auto element_bytes = std::as_bytes(jewels::as_single_item_span(strong_element));",
            f"{context.INDENT}static_assert(sizeof({strong_cpp_type_str}) == sizeof({underlying_cpp_type_str}),"
            + f""" "Size mismatch for array elements in field '{conversion_params.field_name}': sizeof({strong_cpp_type_str}) != sizeof({underlying_cpp_type_str})");""",
            f"{context.INDENT}const auto* element_value_ptr = reinterpret_cast<const {underlying_cpp_type_str}*>(element_bytes.data());",
            f"{context.INDENT}{conversion_params.destination_name}.add_{conversion_params.proto_field_name}(*element_value_ptr);",
        ]
    elif isinstance(value_type, clkbuiltins.PrimitiveType):
        conversion_lines = [
            f"{context.INDENT}{conversion_params.destination_name}.add_{conversion_params.proto_field_name}({input_name}[i]);"
        ]
    else:
        # Assuming complex type handled by jewels::tap_to_protobuf
        conversion_lines = [
            f"{context.INDENT}auto element_{input_name} = {conversion_params.destination_name}.add_{conversion_params.proto_field_name}();",
            f"{context.INDENT}jewels::tap_to_protobuf(*element_{input_name}, {input_name}[i]);",
        ]

    return [
        f"const auto& {input_name} = {conversion_params.source_name}.get_{conversion_params.field_name}();",
        f"{conversion_params.destination_name}.clear_{conversion_params.proto_field_name}();",
        f"for (size_t i = 0; i < {input_name}.size(); ++i)",
        "{",
        *conversion_lines,
        "}",
    ]


def _generate_strong_type_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter for strong types using byte representation."""
    assert isinstance(conversion_params.field_type, strongtypes.StrongType)  # noqa: S101

    underlying_type = conversion_params.field_type.get_underlying_type()

    cpp_type_rendered = typereg.get_cpp_type(conversion_params.compiler_context, underlying_type).render(
        conversion_params.enclosing_namespace
    )

    strong_value_var = f"{conversion_params.field_name}_strong_value"
    byte_span_var = f"{conversion_params.field_name}_bytes"
    get_statement = "value_" if conversion_params.is_optional else "get_"
    value_ptr_var = f"{conversion_params.field_name}_value_ptr"
    return [
        f"const auto& {strong_value_var} = {conversion_params.source_name}.{get_statement}{conversion_params.field_name}();",
        # Use as_single_item_span and as_bytes
        f"const auto {byte_span_var} = std::as_bytes(jewels::as_single_item_span({strong_value_var}));",
        # Size check
        f"static_assert(sizeof(decltype({strong_value_var})) == sizeof({cpp_type_rendered}),"
        + f' "Size mismatch between strong type and proto primitive type for field {conversion_params.field_name}");',
        # Reinterpret cast and set value
        f"const auto* {value_ptr_var} = reinterpret_cast<const {cpp_type_rendered}*>({byte_span_var}.data());",
        f"{conversion_params.destination_name}.set_{conversion_params.proto_field_name}(*{value_ptr_var});",
    ]


def _generate_conversion_statement(field_type: typesys.Value) -> ConversionInfo:
    """Helper function that analyzes a field type gets the appropriate ConversionInfo object.

    The conversion info object is then used to generate the C++ text for converting the specific field.
    """
    additional_headers: list[Header] = []
    # If this is a var_array, fixed array or optional this takes care of getting the header for the nested type.
    if isinstance(field_type, typesys.Instantiation) and "type" in field_type.arguments:
        value_type = field_type.arguments["type"]
        if in_converter_registry(value_type):
            additional_headers.append(get_conversion_registration(value_type).include_location)

    if in_converter_registry(field_type):
        conversion_registration = get_conversion_registration(field_type)
        conversion_info = ConversionInfo(
            lambda conversion_params: _generate_schema_converter(conversion_params, conversion_registration.namespace),
            [conversion_registration.include_location],
        )
    elif isinstance(field_type, strongtypes.StrongType):
        required_headers = [
            Header(JEWELS_REPO, "jewels/std/span.hh"),
        ]
        conversion_info = ConversionInfo(_generate_strong_type_conversion, required_headers)
    elif isinstance(field_type, clkenum.ResolvedEnum):
        conversion_info = ConversionInfo(_generate_enum_conversion, [])
    elif isinstance(field_type, typesys.Instantiation) and (
        field_type.instantiates.value_key() in _TYPE_CONVERSION_MAP
    ):
        conversion_info = _TYPE_CONVERSION_MAP[field_type.instantiates.value_key()]
    elif field_type.value_key() in _TYPE_CONVERSION_MAP:
        conversion_info = _TYPE_CONVERSION_MAP[field_type.value_key()]
    else:
        msg = f"No conversion supported for {field_type.value_key()}"
        raise TypeError(msg)

    conversion_info.required_includes.extend(additional_headers)
    return conversion_info


# Register simple conversions for primitive types
def _register_simple_tap_proto_conversion(clk_type: typesys.TypeVal) -> None:
    """Register a converter for conversions that don't require any changes to the underlying types."""
    _TYPE_CONVERSION_MAP[clk_type.value_key()] = ConversionInfo(_generate_simple_conversion, [])


_register_simple_tap_proto_conversion(clkbuiltins.BOOL)
_register_simple_tap_proto_conversion(clkbuiltins.FLOAT32)
_register_simple_tap_proto_conversion(clkbuiltins.FLOAT64)
_register_simple_tap_proto_conversion(clkbuiltins.INT32)
_register_simple_tap_proto_conversion(clkbuiltins.INT64)
_register_simple_tap_proto_conversion(clkbuiltins.UINT64)
_register_simple_tap_proto_conversion(clkbuiltins.UINT32)
_register_simple_tap_proto_conversion(clkbuiltins.INT8)
_register_simple_tap_proto_conversion(clkbuiltins.UINT8)
_register_simple_tap_proto_conversion(clkbuiltins.INT16)
_register_simple_tap_proto_conversion(clkbuiltins.UINT16)

_TYPE_CONVERSION_MAP[clkbuiltins.VAR_STRING.value_key()] = ConversionInfo(_generate_complex_conversion, [])

_TYPE_CONVERSION_MAP[clkbuiltins.UUID.value_key()] = ConversionInfo(_generate_complex_conversion, [])

_TYPE_CONVERSION_MAP[clkbuiltins.SYNC_TIME.value_key()] = ConversionInfo(_generate_complex_conversion, [])

_TYPE_CONVERSION_MAP[clkbuiltins.DURATION.value_key()] = ConversionInfo(_generate_complex_conversion, [])

_TYPE_CONVERSION_MAP[clkbuiltins.BYTE.value_key()] = ConversionInfo(_generate_complex_conversion, [])

_TYPE_CONVERSION_MAP[clkbuiltins.VAR_ARRAY.value_key()] = ConversionInfo(
    _generate_array_conversion,
    [Header(JEWELS_REPO, "jewels/std/span.hh"), Header(JEWELS_REPO, "jewels/container/tap/tap_to_protobuf.hh")],
)

_TYPE_CONVERSION_MAP[clkbuiltins.FIXED_ARRAY.value_key()] = ConversionInfo(
    _generate_array_conversion,
    [Header(JEWELS_REPO, "jewels/std/span.hh"), Header(JEWELS_REPO, "jewels/container/tap/tap_to_protobuf.hh")],
)
_TYPE_CONVERSION_MAP[clkbuiltins.OPTIONAL.value_key()] = ConversionInfo(_generate_optional_conversion, [])


def tachyon_tap_to_cpp_type(tap_interface: schema_reg.InterfaceInfo) -> types.CppType | types.CppTemplateType:
    """Creates a CppType that corresponds to the provided TAP interface."""
    tap_ir = tap_interface.interface_ir
    if isinstance(tap_ir.typespec, expr.Expr):
        msg = "Attempted to render an unresolved converter"
        raise TypeError(msg)
    if not isinstance(tap_ir.typespec, typesys.Instantiation):
        msg = f"Expected an instantiation, got {type(tap_ir.typespec)}"
        raise TypeError(msg)
    return tap.get_tap_tachyon_cpp_type(tap_interface.interface_ir.module.context, tap_ir.typespec)


def render_tachyon_to_protobuf_converter(
    compiler_context: CompilerContext,
    tap_interface_ref: InterfaceReference,
    proto_representation_ref: RepresentationReference,
    namespace: str,
) -> context.CppModuleChunks:
    """Render the converter from TAP/Tachyon to Protobuf."""
    proto_rep = schema_reg.lookup_representation(compiler_context, proto_representation_ref)
    tap_interface = schema_reg.lookup_interface(compiler_context, tap_interface_ref)

    if proto_rep is None or tap_interface is None:
        msg = "Converter was unable to look up one of the parameters specified."
        "Make sure that representations and interfaces are instantiated in either a proto_target or cpp_target."
        raise TypeError(msg)

    proto_ir = proto_rep.representation_ir
    tap_ir = tap_interface.interface_ir
    if proto_ir.schema_ir is None:  # pyright: ignore[reportUnnecessaryComparison] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        raise ValueError

    if isinstance(tap_ir.typespec, expr.Expr) or isinstance(proto_ir.typespec, expr.Expr):
        msg = "Attempted to render an unresolved converter"
        raise TypeError(msg)

    # Get the C++ type for the TAP interface (source)
    tap_cpp_type = tachyon_tap_to_cpp_type(tap_interface)
    tap_cpp_type.const = True
    tap_cpp_type.ref = Ref.L

    # Get the C++ type for the proto representation (destination)
    proto_cpp_type = protobuf_repr_to_cpp_type(proto_rep)
    proto_cpp_type.const = False
    proto_cpp_type.ref = Ref.L

    source_name = "input"
    destination_name = "output"
    conversion_method = CppMethod(
        name="tap_to_protobuf",
        doc=None,
        return_type=types.VOID,
        arguments=[
            CppNamedType(proto_cpp_type, destination_name),
            CppNamedType(argument_type=tap_cpp_type, argument_name=source_name),
        ],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=CppChunk(),
        no_discard=False,  # Changed from True
    )

    needed_includes = {
        Header(JEWELS_REPO, "jewels/container/tap/tap_to_protobuf.hh"),
        SystemHeader("string_view"),
        SystemHeader("type_traits"),
        *tap_cpp_type.includes,
        *proto_cpp_type.includes,
    }

    if not conversion_method.body:
        msg = "Conversion method body not properly initialized"
        raise TypeError(msg)

    schema_typespec = to_schema_instantiation(proto_ir.typespec)
    instantiated_schema = schema.InstantiatedSchema.from_typespec(schema_typespec)

    # Generate code for each field
    for field in instantiated_schema.fields.values():
        conversion_params = ConversionParams(
            compiler_context=instantiated_schema.schema.module.context,
            source_name=source_name,
            destination_name=destination_name,
            field_type=field.type_info,
            field_name=field.cur_name,
            enclosing_namespace=namespace,
        )
        conversion_info = _generate_conversion_statement(field.type_info)
        conversion_method.body.append(conversion_info.conversion_generator(conversion_params))
        needed_includes.update(conversion_info.required_includes)

    module_chunks = conversion_method.render(None, namespace)
    module_chunks.header_chunk.context.add_includes(needed_includes)
    module_chunks.implementation_chunk.context.add_includes(needed_includes)
    return module_chunks
