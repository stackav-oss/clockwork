# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Code gen utilities for converting from protobuf to TAP/Tachyon."""

from __future__ import annotations

import dataclasses
from typing import TYPE_CHECKING, Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.cpp import context, literal, typereg, types
from clockwork.dsl.cpp.context import CppChunk, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    CppMethod,
    CppNamedType,
    CppType,
    CppValue,
    Ref,
)
from clockwork.dsl.ir import clkbuiltins, clkenum, expr, schema, schema_reg, strongtypes, typesys
from clockwork.dsl.ir.conversion_utils import (
    ConversionInfo,
    ConversionParams,
    ConverterRegistry,
    ValidationInfo,
    ValidationParams,
    protobuf_repr_to_cpp_type,
    to_schema_instantiation,
)
from clockwork.dsl.ir.module_id import JEWELS_REPO
from clockwork.dsl.proto import proto_typereg
from clockwork.dsl.serialization import tap
from typing_extensions import override

if TYPE_CHECKING:
    from clockwork.dsl.ir.conversion_utils import ConversionRegistration
    from clockwork.dsl.ir.interface import InterfaceReference
    from clockwork.dsl.ir.representation import RepresentationReference


@final
class ProtoToTapConverterRegistry(Context):
    """Registry of Proto to Tap Conversions."""

    def __init__(self, name: str | None) -> None:
        """Create a new registry."""
        self.name = name
        self.type_conversion_map: dict[str, ConversionInfo] = {}
        self.converter_registry: ConverterRegistry = ConverterRegistry({})

    def register_simple_proto_tap_conversion(self, clk_type: typesys.TypeVal) -> None:
        """Helper to register a simple proto to tap conversion."""
        self.type_conversion_map[clk_type.value_key()] = ConversionInfo(_generate_simple_conversion, [])

    @override
    def import_from(self, other: ProtoToTapConverterRegistry) -> None:
        """Combine this cache with cached converters from another.

        Raises:
            RuntimeError: If a converter already exists with a different definition.
        """
        for key, value in other.converter_registry.registry_dict.items():
            if key in self.converter_registry.registry_dict and self.converter_registry.registry_dict[key] != value:
                msg = f"Converter registry entry {key} has conflicting entry: {self.converter_registry.registry_dict[key]} vs {value}\nWhen merging from {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.converter_registry.registry_dict[key] = value

        for key, value in other.type_conversion_map.items():
            if key in self.type_conversion_map and self.type_conversion_map[key] != value:
                msg = f"Different {key} type conversion:\nOther: {value}\nSelf: {self.type_conversion_map[key]}"
                raise RuntimeError(msg)
            self.type_conversion_map[key] = value


_SMALL_INT_SIZE: Final[int] = 32


class ProtoToTapConverterRegistryKey(ContextKey[ProtoToTapConverterRegistry]):
    """Compiler context key for Proto to Tap conversions."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ProtoToTapConverterRegistry:
        """Create a default instance of the registry."""
        registry = ProtoToTapConverterRegistry(compiler_context.name)

        registry.register_simple_proto_tap_conversion(clkbuiltins.BOOL)
        registry.register_simple_proto_tap_conversion(clkbuiltins.FLOAT32)
        registry.register_simple_proto_tap_conversion(clkbuiltins.FLOAT64)
        registry.register_simple_proto_tap_conversion(clkbuiltins.INT32)
        registry.register_simple_proto_tap_conversion(clkbuiltins.INT64)
        registry.register_simple_proto_tap_conversion(clkbuiltins.UINT64)
        registry.register_simple_proto_tap_conversion(clkbuiltins.UINT32)
        registry.type_conversion_map[clkbuiltins.INT8.value_key()] = ConversionInfo(
            _generate_int_converter, [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")]
        )
        registry.type_conversion_map[clkbuiltins.UINT8.value_key()] = ConversionInfo(
            _generate_int_converter, [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")]
        )
        registry.type_conversion_map[clkbuiltins.INT16.value_key()] = ConversionInfo(
            _generate_int_converter, [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")]
        )
        registry.type_conversion_map[clkbuiltins.UINT16.value_key()] = ConversionInfo(
            _generate_int_converter, [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")]
        )
        registry.type_conversion_map[clkbuiltins.VAR_STRING.value_key()] = ConversionInfo(
            _generate_string_conversion,
            [SystemHeader("string"), Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")],
        )
        registry.type_conversion_map[clkbuiltins.UUID.value_key()] = ConversionInfo(_generate_complex_conversion, [])

        registry.type_conversion_map[clkbuiltins.SYNC_TIME.value_key()] = ConversionInfo(
            _generate_complex_conversion, []
        )

        registry.type_conversion_map[clkbuiltins.DURATION.value_key()] = ConversionInfo(
            _generate_complex_conversion, []
        )

        registry.type_conversion_map[clkbuiltins.BYTE.value_key()] = ConversionInfo(_generate_complex_conversion, [])

        registry.type_conversion_map[clkbuiltins.VAR_ARRAY.value_key()] = ConversionInfo(
            _generate_var_array_conversion,
            [Header(JEWELS_REPO, "jewels/std/span.hh"), Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")],
        )

        registry.type_conversion_map[clkbuiltins.FIXED_ARRAY.value_key()] = ConversionInfo(
            _generate_fixed_array_conversion,
            [
                Header(JEWELS_REPO, "jewels/std/span.hh"),
                Header(JEWELS_REPO, "jewels/container/at.hh", iwyu_pragma="// IWYU pragma: export"),
                Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh"),
            ],
        )
        registry.type_conversion_map[clkbuiltins.VAR_SOA.value_key()] = ConversionInfo(
            _generate_var_soa_conversion,
            [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")],
        )
        registry.type_conversion_map[clkbuiltins.FIXED_SOA.value_key()] = ConversionInfo(
            _generate_fixed_soa_conversion,
            [Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")],
        )
        registry.type_conversion_map[clkbuiltins.OPTIONAL.value_key()] = ConversionInfo(
            _generate_optional_conversion, []
        )

        return registry


PROTO_TO_TAP_CONVERTER_REGISTRY_KEY: Final = ProtoToTapConverterRegistryKey("ProtoToTapConverterRegistry")


def in_converter_registry(clk_type: typesys.Value, compiler_context: CompilerContext) -> bool:
    """Checks whether a schema instantiation is registered in the conversion registry."""
    registry = compiler_context[PROTO_TO_TAP_CONVERTER_REGISTRY_KEY]
    return registry.converter_registry.in_converter_registry(clk_type)


def get_conversion_registration(clk_type: typesys.Value, compiler_context: CompilerContext) -> ConversionRegistration:
    """Obtains the conversion registration for a specific schema type."""
    registry = compiler_context[PROTO_TO_TAP_CONVERTER_REGISTRY_KEY]
    return registry.converter_registry.get_conversion_registration(clk_type)


def register_schema_conversion(
    clk_type: typesys.Value, conversion_info: ConversionRegistration, compiler_context: CompilerContext
) -> None:
    """Register a converter in the registry."""
    registry = compiler_context[PROTO_TO_TAP_CONVERTER_REGISTRY_KEY]
    registry.converter_registry.register_schema_conversion(clk_type, conversion_info)


def _generate_validation_statement(
    field_type: typesys.Value, compiler_context: CompilerContext
) -> ValidationInfo | None:
    if in_converter_registry(field_type, compiler_context):
        conversion_registration = get_conversion_registration(field_type, compiler_context)
        return ValidationInfo(
            lambda validation_params: _generate_schema_validator(validation_params, conversion_registration.namespace),
            [conversion_registration.include_location],
        )
    if isinstance(field_type, typesys.Instantiation) and (
        (field_type.instantiates.value_key() == clkbuiltins.VAR_ARRAY.value_key())
        or (field_type.instantiates.value_key() == clkbuiltins.FIXED_ARRAY.value_key())
        or (field_type.instantiates.value_key() == clkbuiltins.VAR_SOA.value_key())
        or (field_type.instantiates.value_key() == clkbuiltins.FIXED_SOA.value_key())
    ):
        value_type = field_type.arguments["type"]
        if in_converter_registry(value_type, compiler_context):
            conversion_registration = get_conversion_registration(value_type, compiler_context)
            return ValidationInfo(
                lambda validation_params: _generate_array_validator(
                    validation_params, conversion_registration.namespace
                ),
                [conversion_registration.include_location],
            )
        return None
    if (
        isinstance(field_type, typesys.Instantiation)
        and field_type.instantiates.value_key() == clkbuiltins.OPTIONAL.value_key()
    ):
        return None

    return ValidationInfo(_generate_standard_validator, [])


def _generate_standard_validator(validation_params: ValidationParams) -> list[str]:
    return [
        f"if (!{validation_params.source_name}.has_{validation_params.proto_field_name}())",
        "{",
        f"{context.INDENT}protobuf_valid = false;",
        *_generate_error_printing(validation_params.proto_field_name, validation_params.source_name),
        "}",
    ]


def _generate_error_printing(proto_field_name: str, source_name: str) -> list[str]:
    return [
        f'{context.INDENT}::jewels::log_cerr_error("{proto_field_name} not set in: {{}}", {source_name}.GetTypeName());'
    ]


def _generate_conversion_statement(field_type: typesys.Value, compiler_context: CompilerContext) -> ConversionInfo:
    """Helper function that analyzes a field type gets the appropriate ConversionInfo object.

    The conversion info object is then used to generate the C++ text for converting the specific field.
    """
    additional_headers: list[Header] = []
    if isinstance(field_type, typesys.Instantiation):
        additional_headers.extend(
            get_conversion_registration(value, compiler_context).include_location
            for value in field_type.arguments.values()
            if in_converter_registry(value, compiler_context)
        )
    registry = compiler_context[PROTO_TO_TAP_CONVERTER_REGISTRY_KEY]
    if in_converter_registry(field_type, compiler_context):
        conversion_registration = get_conversion_registration(field_type, compiler_context)
        conversion_info = ConversionInfo(
            lambda conversion_params: _generate_schema_converter(conversion_params, conversion_registration.namespace),
            [conversion_registration.include_location],
        )
    elif isinstance(field_type, strongtypes.StrongType):
        conversion_info = ConversionInfo(_generate_strong_type_conversion, [])
    elif isinstance(field_type, clkenum.ResolvedEnum):
        conversion_info = ConversionInfo(_generate_enum_conversion, [])
    elif isinstance(field_type, typesys.Instantiation) and (
        field_type.instantiates.value_key() in registry.type_conversion_map
    ):
        conversion_info = registry.type_conversion_map[field_type.instantiates.value_key()]
        for value in field_type.arguments.values():
            if in_converter_registry(value, compiler_context):
                conversion_info.required_includes.append(
                    get_conversion_registration(value, compiler_context).include_location
                )
    elif field_type.value_key() in registry.type_conversion_map:
        conversion_info = registry.type_conversion_map[field_type.value_key()]
    else:
        msg = f"No conversion supported for {field_type.value_key()}"
        raise TypeError(msg)

    conversion_info.required_includes.extend(additional_headers)
    return conversion_info


def _generate_strong_type_conversion(conversion_params: ConversionParams) -> list[str]:
    factory = literal.get_factory_fn(conversion_params.field_type)
    if not factory:
        value = CppValue(None, f"{conversion_params.proto_getter}")
        return [
            f"{conversion_params.destination_name}."  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f"set_{conversion_params.field_name}({value.render(conversion_params.enclosing_namespace)});"
        ]
    args = [CppValue(None, f"{conversion_params.proto_getter}")]
    return [
        f"{conversion_params.destination_name}."  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f"set_{conversion_params.field_name}({factory.fn.invoke(args).render(conversion_params.enclosing_namespace)});"
    ]


def _generate_simple_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a conversion for types that effectively match in protobuf and C++."""
    return [
        f"{conversion_params.destination_name}."  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f"set_{conversion_params.field_name}({conversion_params.source_name}.{conversion_params.proto_field_name}());"
    ]


def _generate_int_converter(conversion_params: ConversionParams) -> list[str]:
    """Generates a converter for a schema type. This will be calling another generated protobuf_to_tap function."""
    get_function = "value_mutable" if conversion_params.is_optional else "get_mutable"

    return [
        # if this is an optional we need to call set first to enable the field. If it isn't the result gets overridden anyway.
        f"{conversion_params.destination_name}.set_{conversion_params.field_name}(0);",
        f"auto {conversion_params.field_name}_status = ::jewels::protobuf_to_tap({conversion_params.destination_name}",
        f".{get_function}_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
        *_generate_expected_check(f"{conversion_params.field_name}_status"),
    ]


def _generate_enum_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter between the protobuf enum and TAP/Tachyon enum.

    The protobuf schema definition ensures that this is just a static cast.
    """
    return [
        f"{conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f".set_{conversion_params.field_name}"
        f"(static_cast<{typereg.get_cpp_type(conversion_params.compiler_context, conversion_params.field_type).render(conversion_params.enclosing_namespace)}>"
        f"({conversion_params.source_name}.{conversion_params.proto_field_name}()));"
    ]


def _generate_schema_converter(conversion_params: ConversionParams, namespace: str) -> list[str]:
    """Generates a converter for a schema type. This will be calling another generated protobuf_to_tap function."""
    return [
        f"auto {conversion_params.field_name}_status = ::{namespace}::protobuf_to_tap({conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f".get_mutable_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
        *_generate_expected_check(f"{conversion_params.field_name}_status"),
    ]


def _generate_schema_validator(validation_params: ValidationParams, namespace: str) -> list[str]:
    return [
        f"if ({validation_params.source_name}.has_{validation_params.proto_field_name}())",
        "{",
        f"{context.INDENT}protobuf_valid = protobuf_valid && ::{namespace}::validate_protobuf({validation_params.source_name}.{validation_params.proto_field_name}());",
        "}",
        "else",
        "{",
        f"{context.INDENT}protobuf_valid = false;",
        *_generate_error_printing(validation_params.proto_field_name, validation_params.source_name),
        "}",
    ]


def _generate_array_validator(validation_params: ValidationParams, namespace: str) -> list[str]:
    return [
        f"for (const auto& {validation_params.field_name}_value: {validation_params.source_name}.{validation_params.proto_field_name}())",
        "{",
        f"{context.INDENT}protobuf_valid = protobuf_valid && ::{namespace}::validate_protobuf({validation_params.field_name}_value);",
        "}",
    ]


def _generate_validation_check(source_name: str, namespace: str) -> list[str]:
    return [
        f"if (!::{namespace}::validate_protobuf({source_name}))",
        "{",
        f"{context.INDENT}return ::jewels::unexpected(::jewels::MonoError());",
        "}",
    ]


def _generate_optional_schema_converter(conversion_params: ConversionParams, namespace: str) -> list[str]:
    """Same as above but for when the schema is contained in a optional."""
    return [
        f"{conversion_params.destination_name}.set_{conversion_params.field_name}(std::remove_reference_t<decltype({conversion_params.destination_name}.value_mutable_{conversion_params.field_name}())>());",
        f"auto {conversion_params.field_name}_status = ::{namespace}::protobuf_to_tap({conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f".value_mutable_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
        *_generate_expected_check(f"{conversion_params.field_name}_status"),
    ]


def _generate_complex_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for types that need to use the helper functions defined in jewels/container/tap/protobuf_to_tap.h."""
    set_optional = f"{conversion_params.destination_name}.set_{conversion_params.field_name}(std::remove_reference_t<decltype({conversion_params.destination_name}.value_mutable_{conversion_params.field_name}())>());"

    get_function = "value_mutable" if conversion_params.is_optional else "get_mutable"
    conversion: list[str] = [
        f"auto {conversion_params.field_name}_status = jewels::protobuf_to_tap({conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f".{get_function}_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
        *_generate_expected_check(f"{conversion_params.field_name}_status"),
    ]
    if conversion_params.is_optional:
        conversion.insert(0, set_optional)
    return conversion


def _generate_expected_check(name_to_check: str, indent: int = 0) -> list[str]:
    """Generate the error checking for conversion types that need it."""
    indent_string = context.INDENT * indent
    return [
        f"{indent_string}if (!{name_to_check})",
        indent_string + "{",
        f"{context.INDENT + indent_string} return {name_to_check};",
        indent_string + "}",
    ]


def _generate_string_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter for string types."""
    return [
        f"auto {conversion_params.field_name}_status"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
        f" = jewels::protobuf_to_tap({conversion_params.destination_name}.get_underlying_{conversion_params.field_name}(),"
        f"  {conversion_params.source_name}.{conversion_params.proto_field_name}());",
        *_generate_expected_check(f"{conversion_params.field_name}_status"),
    ]


def _generate_optional_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate converter for optional types."""
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the optional conversion, got {conversion_params.field_type}"
        raise TypeError(msg)
    value_type = conversion_params.field_type.arguments["type"]
    if in_converter_registry(value_type, conversion_params.compiler_context):
        conversion_statement = _generate_optional_schema_converter(
            conversion_params, get_conversion_registration(value_type, conversion_params.compiler_context).namespace
        )
    else:
        updated_conversion_params = dataclasses.replace(
            conversion_params,
            # Update the field_type to the inner type for the optional conversion.
            field_type=value_type,
            is_optional=True,
        )
        conversion_statement = _generate_conversion_statement(
            value_type, conversion_params.compiler_context
        ).conversion_generator(updated_conversion_params)
    return [
        f"if ({conversion_params.source_name}.has_{conversion_params.proto_field_name}())",
        "{",
        *conversion_statement,
        "}",
    ]


def _generate_var_array_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for array types."""
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the array conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    output = conversion_params.field_name
    value_type = conversion_params.field_type.arguments["type"]
    if in_converter_registry(value_type, conversion_params.compiler_context):
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = "  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f"::{get_conversion_registration(value_type, conversion_params.compiler_context).namespace}::protobuf_to_tap({output}.emplace_back(),"
            f" {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    elif value_type is clkbuiltins.BYTE:
        return [
            f"auto {conversion_params.field_name}_status = jewels::protobuf_to_tap({conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f".get_underlying_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
            *_generate_expected_check(f"{conversion_params.field_name}_status"),
        ]
    elif isinstance(value_type, clkenum.ResolvedEnum):
        conversion_lines = [
            f"{context.INDENT}{output}.emplace_back"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f"(static_cast<{typereg.get_cpp_type(value_type.module.context, value_type).render(conversion_params.enclosing_namespace)}>({conversion_params.field_name}_value));"
        ]
    elif isinstance(value_type, strongtypes.StrongType):
        factory = literal.get_factory_fn(value_type)
        if not factory:
            msg = f"No factory registered for strong type {value_type}"
            raise ValueError(msg)
        args = [CppValue(None, f"{conversion_params.field_name}_value")]
        conversion_lines = [
            f"{context.INDENT}{output}.emplace_back({factory.fn.invoke(args).render(conversion_params.enclosing_namespace)});"
        ]
    # If the value type is smaller than 32 bits, we need to use jewels::protobuf_to_tap to convert it.
    elif (
        isinstance(value_type, clkbuiltins.IntegerPrimitiveBuiltinSerializable)
        and value_type.bit_width < _SMALL_INT_SIZE
    ):
        # We need the conversion to be done using the jewels::protobuf_to_tap function
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = ::jewels::protobuf_to_tap({output}.emplace_back(), {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    elif isinstance(value_type, clkbuiltins.PrimitiveType):
        conversion_lines = [f"{context.INDENT}{output}.emplace_back({conversion_params.field_name}_value);"]
    else:
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = ::jewels::protobuf_to_tap({output}.emplace_back(), {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    return [
        f"auto& {output} = {conversion_params.destination_name}.get_underlying_{conversion_params.field_name}();",
        f"{output}.clear();",
        f"{output}.reserve(static_cast<size_t>({conversion_params.source_name}.{conversion_params.proto_field_name}().size()));",
        f"for (const auto& {conversion_params.field_name}_value: {conversion_params.source_name}.{conversion_params.proto_field_name}())",
        "{",
        *conversion_lines,
        "}",
    ]


def _generate_fixed_array_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for array types."""
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the array conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    output = conversion_params.field_name
    value_type = conversion_params.field_type.arguments["type"]
    if in_converter_registry(value_type, conversion_params.compiler_context):
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = "  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f"::{get_conversion_registration(value_type, conversion_params.compiler_context).namespace}::protobuf_to_tap(::jewels::at({output}, i),"
            f" {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    elif value_type is clkbuiltins.BYTE:
        return [
            f"auto {conversion_params.field_name}_status = ::jewels::protobuf_to_tap({conversion_params.destination_name}"  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f".get_mutable_{conversion_params.field_name}(), {conversion_params.source_name}.{conversion_params.proto_field_name}());",
            *_generate_expected_check(f"{conversion_params.field_name}_status"),
        ]
    elif isinstance(value_type, clkenum.ResolvedEnum):
        conversion_lines = [
            f"{context.INDENT}::jewels::at({output}, static_cast<int64_t>(i)) = "  # pyright: ignore[reportImplicitStringConcatenation] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
            f"(static_cast<{typereg.get_cpp_type(value_type.module.context, value_type).render(conversion_params.enclosing_namespace)}>({conversion_params.field_name}_value));"
        ]
    elif isinstance(value_type, strongtypes.StrongType):
        factory = literal.get_factory_fn(value_type)
        if not factory:
            msg = f"No factory registered for strong type {value_type}"
            raise ValueError(msg)
        args = [CppValue(None, f"{conversion_params.field_name}_value")]
        conversion_lines = [
            f"{context.INDENT}::jewels::at({output}, static_cast<int64_t>(i)) = {factory.fn.invoke(args).render(conversion_params.enclosing_namespace)};"
        ]
        # If the value type is smaller than 32 bits, we need to use jewels::protobuf_to_tap to convert it.
    elif (
        isinstance(value_type, clkbuiltins.IntegerPrimitiveBuiltinSerializable)
        and value_type.bit_width < _SMALL_INT_SIZE
    ):
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = ::jewels::protobuf_to_tap(::jewels::at({output}, static_cast<int64_t>(i)), {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    elif isinstance(value_type, clkbuiltins.PrimitiveType):
        conversion_lines = [
            f"{context.INDENT}::jewels::at({output}, static_cast<int64_t>(i)) = {conversion_params.field_name}_value;"
        ]
    else:
        conversion_lines = [
            f"{context.INDENT}auto conversion_status = ::jewels::protobuf_to_tap(::jewels::at({output}, static_cast<int64_t>(i)), {conversion_params.field_name}_value);",
            *_generate_expected_check("conversion_status", indent=1),
        ]
    return [
        f"auto {output} = {conversion_params.destination_name}.get_mutable_{conversion_params.field_name}();",
        f"for (int i = 0; i < {conversion_params.source_name}.{conversion_params.proto_field_name}_size(); ++i)",
        "{",
        f"const auto &{conversion_params.field_name}_value = {conversion_params.source_name}.{conversion_params.proto_field_name}(i);",
        *conversion_lines,
        "}",
    ]


def _generate_var_soa_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for VarSoa (variable-size Struct-of-Arrays) container types.

    SoA containers are deserialized from protobuf the same way as arrays: from repeated messages.
    Each protobuf element is converted via the element schema's protobuf_to_tap converter,
    producing a Tap<Tachyon<T>> which is then assigned to the SoA element proxy.
    """
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the VarSoa conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    output = conversion_params.field_name
    value_type = conversion_params.field_type.arguments["type"]

    if not in_converter_registry(value_type, conversion_params.compiler_context):
        msg = f"SoA element type {value_type} must have a registered converter"
        raise TypeError(msg)

    conversion_registration = get_conversion_registration(value_type, conversion_params.compiler_context)
    tap_cpp_type = typereg.get_cpp_type(conversion_params.compiler_context, value_type)
    tap_tachyon_type = (
        f"::clockwork::Tap<::clockwork::Tachyon<{tap_cpp_type.render(conversion_params.enclosing_namespace)}>>"
    )

    return [
        f"auto& {output} = {conversion_params.destination_name}.get_mutable_{conversion_params.field_name}();",
        f"{output}.clear();",
        f"{output}.resize(static_cast<size_t>({conversion_params.source_name}.{conversion_params.proto_field_name}_size()));",
        f"for (int i = 0; i < {conversion_params.source_name}.{conversion_params.proto_field_name}_size(); ++i)",
        "{",
        f"{context.INDENT}{tap_tachyon_type} tap_element;",
        (
            f"{context.INDENT}auto conversion_status = ::{conversion_registration.namespace}::protobuf_to_tap(tap_element,"
            f" {conversion_params.source_name}.{conversion_params.proto_field_name}(i));"
        ),
        *_generate_expected_check("conversion_status", indent=1),
        f"{context.INDENT}{output}[static_cast<size_t>(i)] = tap_element;",
        "}",
    ]


def _generate_fixed_soa_conversion(conversion_params: ConversionParams) -> list[str]:
    """Generate a converter for FixedSoa (fixed-size Struct-of-Arrays) container types.

    Each protobuf element is converted via the element schema's protobuf_to_tap converter,
    producing a Tap<Tachyon<T>> which is then assigned to the SoA element proxy.
    """
    if not isinstance(conversion_params.field_type, typesys.Instantiation):
        msg = f"expected an instantiation type for the FixedSoa conversion, got {conversion_params.field_type}"
        raise TypeError(msg)

    output = conversion_params.field_name
    value_type = conversion_params.field_type.arguments["type"]

    if not in_converter_registry(value_type, conversion_params.compiler_context):
        msg = f"SoA element type {value_type} must have a registered converter"
        raise TypeError(msg)

    conversion_registration = get_conversion_registration(value_type, conversion_params.compiler_context)
    tap_cpp_type = typereg.get_cpp_type(conversion_params.compiler_context, value_type)
    tap_tachyon_type = (
        f"::clockwork::Tap<::clockwork::Tachyon<{tap_cpp_type.render(conversion_params.enclosing_namespace)}>>"
    )

    return [
        f"auto& {output} = {conversion_params.destination_name}.get_mutable_{conversion_params.field_name}();",
        f"for (int i = 0; i < {conversion_params.source_name}.{conversion_params.proto_field_name}_size(); ++i)",
        "{",
        f"{context.INDENT}{tap_tachyon_type} tap_element;",
        (
            f"{context.INDENT}auto conversion_status = ::{conversion_registration.namespace}::protobuf_to_tap(tap_element,"
            f" {conversion_params.source_name}.{conversion_params.proto_field_name}(i));"
        ),
        *_generate_expected_check("conversion_status", indent=1),
        f"{context.INDENT}{output}[static_cast<size_t>(i)] = tap_element;",
        "}",
    ]


def render_protobuf_to_tachyon_converter(
    compiler_context: CompilerContext,
    proto_representation_ref: RepresentationReference,
    tap_interface_ref: InterfaceReference,
    namespace: str,
) -> context.CppModuleChunks:
    """Render the converter."""
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

    proto_cpp_type = protobuf_repr_to_cpp_type(proto_rep, compiler_context)
    proto_cpp_type.const = True
    proto_cpp_type.ref = Ref.L
    if not isinstance(tap_ir.typespec, typesys.Instantiation):
        msg = f"Attempted to render an unvalidated converter: {tap_ir.name}"
        raise TypeError(msg)
    tap_cpp_type = tap.get_tap_tachyon_cpp_type(compiler_context, tap_ir.typespec)
    tap_cpp_type.const = False
    tap_cpp_type.ref = Ref.L

    kits_proto_to_tap = context.Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh")
    expected_tap_output = CppType(
        includes=[kits_proto_to_tap],
        type_name="ConversionStatusExpected",
        cpp_namespace="jewels",
    )

    source_name = "input"
    destination_name = "output"
    conversion_method = CppMethod(
        name="protobuf_to_tap",
        doc=None,
        return_type=expected_tap_output,
        arguments=[
            CppNamedType(tap_cpp_type, destination_name),
            CppNamedType(argument_type=proto_cpp_type, argument_name=source_name),
        ],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=CppChunk(),
        no_discard=True,
    )

    needed_includes = {
        Header(JEWELS_REPO, "jewels/container/tap/protobuf_to_tap.hh"),
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

    validation_method = _generate_validation_method(source_name, proto_cpp_type, namespace, instantiated_schema)
    protobuf_type = proto_typereg.get_protobuf_type(proto_ir.schema_ir, compiler_context)
    if isinstance(protobuf_type, proto_typereg.DefinedProtobufType) and protobuf_type.validate_fields:
        conversion_method.body.append(_generate_validation_check(source_name, namespace))

    for field in instantiated_schema.fields.values():
        conversion_params = ConversionParams(
            compiler_context=compiler_context,
            source_name=source_name,
            destination_name=destination_name,
            field_type=field.type_info,
            field_name=field.cur_name,
            enclosing_namespace=namespace,
        )
        conversion_info = _generate_conversion_statement(field.type_info, conversion_params.compiler_context)
        conversion_method.body.append(conversion_info.conversion_generator(conversion_params))
        needed_includes.update(conversion_info.required_includes)

    conversion_method.body.append("return ::jewels::ConversionStatusExpected();")
    module_chunks = conversion_method.render(None, namespace)
    module_chunks.append(validation_method.render(None, namespace))
    module_chunks.header_chunk.context.add_includes(needed_includes)
    module_chunks.implementation_chunk.context.add_includes(needed_includes)
    return module_chunks


def _generate_validation_method(
    source_name: str, source_cpp_type: CppType, namespace: str, instantiated_schema: schema.InstantiatedSchema
) -> CppMethod:
    validation_method = CppMethod(
        name="validate_protobuf",
        doc=None,
        return_type=types.BOOLEAN,
        arguments=[
            CppNamedType(qualifiers=["[[maybe_unused]]"], argument_type=source_cpp_type, argument_name=source_name),
        ],
        leading_qualifiers=[],
        trailing_qualifiers=[],
        body=CppChunk(),
        no_discard=True,
    )

    if not validation_method.body:
        msg = "Validation method body not properly initialized"
        raise TypeError(msg)

    validation_method.body.append("bool protobuf_valid = true;")
    for field in instantiated_schema.fields.values():
        validation_params = ValidationParams(
            source_name=source_name,
            field_type=field.type_info,
            field_name=field.cur_name,
            enclosing_namespace=namespace,
        )
        validation_info = _generate_validation_statement(field.type_info, instantiated_schema.schema.module.context)
        if validation_info:
            validation_method.body.append(validation_info.conversion_generator(validation_params))
    validation_method.body.append("return protobuf_valid;")
    return validation_method
