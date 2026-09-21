# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Provides facilities to convert Clockwork IR types to corresponding C++ types."""

from dataclasses import replace
from typing import Final, cast

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.cpp import literal, types
from clockwork.dsl.cpp.context import FwdDecl, Header, SystemHeader
from clockwork.dsl.ir import clkbuiltins, clkenum, cog_parameters, primitive, schema, tensor_builtins, typesys
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from clockwork.dsl.ir.statement import ImmutableBinding, InstantiateStmt
from typing_extensions import override

TYPE_TRAITS_HEADER: Final = SystemHeader("type_traits")

TAGS_HEADER: Final = Header(CLK_REPO, "clockwork/tags.hh")

STRING_PARAM_HEADER: Final = Header(CLK_REPO, "jewels/utility/string_param.hh")

META_CONCEPTS_HEADER: Final = Header(JEWELS_REPO, "jewels/meta/concepts.hh")

REPR_IFACE_HEADER: Final = Header(CLK_REPO, "clockwork/repr_iface.hh")

TACHYON_FWD_DECL: Final = FwdDecl("clockwork", "template <class> struct Tachyon")

CLOCKWORK_NAMESPACE: Final = "clockwork"

# Module-level instances for C++ primitive types
_CPP_BOOL_TYPE: Final = types.CppType([], "bool", None)
_CPP_INT64_TYPE: Final = types.CppType([SystemHeader("cstdint")], "int64_t", None)
_CPP_INT32_TYPE: Final = types.CppType([SystemHeader("cstdint")], "int32_t", None)
_CPP_INT16_TYPE: Final = types.CppType([SystemHeader("cstdint")], "int16_t", None)
_CPP_INT8_TYPE: Final = types.CppType([SystemHeader("cstdint")], "int8_t", None)
_CPP_UINT64_TYPE: Final = types.CppType([SystemHeader("cstdint")], "uint64_t", None)
_CPP_UINT32_TYPE: Final = types.CppType([SystemHeader("cstdint")], "uint32_t", None)
_CPP_UINT16_TYPE: Final = types.CppType([SystemHeader("cstdint")], "uint16_t", None)
_CPP_UINT8_TYPE: Final = types.CppType([SystemHeader("cstdint")], "uint8_t", None)
_CPP_BYTE_TYPE: Final = types.CppType([SystemHeader("cstddef")], "byte", "std")
_CPP_FLOAT32_TYPE: Final = types.CppType([], "float", None)
_CPP_FLOAT64_TYPE: Final = types.CppType([], "double", None)
_CPP_DURATION_TYPE: Final = types.CppType([SystemHeader("chrono")], "nanoseconds", "std::chrono")
_CPP_SYNC_TIME_TYPE: Final = types.CppType(
    [Header(JEWELS_REPO, "jewels/time/sync_time.hh")], "SyncTime", "jewels::time"
)
_CPP_SCHEMA_TAG_TYPE: Final = types.CppType(includes=[TAGS_HEADER], type_name="SchemaTag", cpp_namespace="clockwork")
_CPP_REPRESENTATION_TAG_TYPE: Final = types.CppType(
    includes=[TAGS_HEADER], type_name="RepresentationTag", cpp_namespace="clockwork"
)
_CPP_STRING_PARAM_TYPE: Final = types.CppType(
    includes=[STRING_PARAM_HEADER], type_name="StringParam", cpp_namespace="jewels"
)

# Module-level instances for C++ template types
_CPP_TENSOR_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/tensor.hh")],
    template_name="Tensor",
    cpp_namespace="jewels::tap",
)
_CPP_BITSET_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/bitset.hh")],
    template_name="Bitset",
    cpp_namespace="jewels::tap",
)
_CPP_VAR_ARRAY_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/var_array.hh")],
    template_name="VarArray",
    cpp_namespace="jewels::tap",
)
_CPP_VAR_STRING_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/var_string.hh")],
    template_name="VarString",
    cpp_namespace="jewels::tap",
)
_CPP_POD_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(CLK_REPO, "clockwork/serialization/pod.hh")],
    template_name="Pod",
    cpp_namespace="clockwork",
)
_CPP_TAP_TEMPLATE: Final = types.CppTemplate(
    includes=[REPR_IFACE_HEADER],
    template_name="Tap",
    cpp_namespace="clockwork",
)
_CPP_TACHYON_TEMPLATE: Final = types.CppTemplate(
    includes=[REPR_IFACE_HEADER, TACHYON_FWD_DECL],
    template_name="Tachyon",
    cpp_namespace="clockwork",
)
_CPP_TAPPY_TEMPLATE: Final = types.CppTemplate(
    includes=[REPR_IFACE_HEADER],
    template_name="Tappy",
    cpp_namespace="clockwork",
)
_CPP_TAP_INIT_TEMPLATE: Final = types.CppTemplate(
    includes=[REPR_IFACE_HEADER],
    template_name="TapInit",
    cpp_namespace="clockwork",
)
_CPP_OPTIONAL_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/optional.hh")],
    cpp_namespace="jewels::tap",
    template_name="Optional",
)
_CPP_FIXED_SOA_TEMPLATE: Final = types.CppTemplate(
    includes=[],  # Will be added dynamically during lookup
    template_name="FixedSoa",
    cpp_namespace="clockwork",
)
_CPP_VAR_SOA_TEMPLATE: Final = types.CppTemplate(
    includes=[],  # Will be added dynamically during lookup
    template_name="VarSoa",
    cpp_namespace="clockwork",
)


class CppTypeRegistry(Context):
    """Compiler Context for C++ type mappings."""

    def __init__(self) -> None:
        """Create a new, empty type registry."""
        self.cpp_type_registry: dict[str, types.CppType] = {}
        self.cpp_template_registry: dict[str, types.CppTemplate] = {}

    @override
    def import_from(self, other: "CppTypeRegistry") -> None:
        """Combine this context with items from another.

        Raises:
            ValueError: If a type already exists with a different object identity.
        """
        for key, cpp_type in other.cpp_type_registry.items():
            if key in self.cpp_type_registry and self.cpp_type_registry[key] is not cpp_type:
                msg = f"Type {key} has conflicting registrations"
                raise ValueError(msg)
            self.cpp_type_registry[key] = cpp_type

        for key, cpp_template_type in other.cpp_template_registry.items():
            if key in self.cpp_template_registry and self.cpp_template_registry[key] is not cpp_template_type:
                msg = f"Template {key} has conflicting registrations"
                raise ValueError(msg)
            self.cpp_template_registry[key] = cpp_template_type


class CppTypeRegistryKey(ContextKey[CppTypeRegistry]):
    """Compiler context key for C++ type registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> CppTypeRegistry:
        """Create a default instance of the context with built-in types."""
        registry = CppTypeRegistry()

        # Register built-in primitive types
        registry.cpp_type_registry[clkbuiltins.BOOL.value_key()] = _CPP_BOOL_TYPE
        registry.cpp_type_registry[clkbuiltins.INT64.value_key()] = _CPP_INT64_TYPE
        registry.cpp_type_registry[clkbuiltins.INT32.value_key()] = _CPP_INT32_TYPE
        registry.cpp_type_registry[clkbuiltins.INT16.value_key()] = _CPP_INT16_TYPE
        registry.cpp_type_registry[clkbuiltins.INT8.value_key()] = _CPP_INT8_TYPE
        registry.cpp_type_registry[clkbuiltins.UINT64.value_key()] = _CPP_UINT64_TYPE
        registry.cpp_type_registry[clkbuiltins.UINT32.value_key()] = _CPP_UINT32_TYPE
        registry.cpp_type_registry[clkbuiltins.UINT16.value_key()] = _CPP_UINT16_TYPE
        registry.cpp_type_registry[clkbuiltins.UINT8.value_key()] = _CPP_UINT8_TYPE
        registry.cpp_type_registry[clkbuiltins.BYTE.value_key()] = _CPP_BYTE_TYPE
        registry.cpp_type_registry[clkbuiltins.FLOAT32.value_key()] = _CPP_FLOAT32_TYPE
        registry.cpp_type_registry[clkbuiltins.FLOAT64.value_key()] = _CPP_FLOAT64_TYPE
        registry.cpp_type_registry[clkbuiltins.DURATION.value_key()] = _CPP_DURATION_TYPE
        registry.cpp_type_registry[clkbuiltins.SYNC_TIME.value_key()] = _CPP_SYNC_TIME_TYPE
        registry.cpp_type_registry[clkbuiltins.SCHEMA_TAG_TYPE.value_key()] = _CPP_SCHEMA_TAG_TYPE
        registry.cpp_type_registry[clkbuiltins.REPRESENTATION_TAG_TYPE.value_key()] = _CPP_REPRESENTATION_TAG_TYPE
        registry.cpp_type_registry[clkbuiltins.STRING.value_key()] = _CPP_STRING_PARAM_TYPE

        # Register built-in template types
        registry.cpp_template_registry[clkbuiltins.BITSET.value_key()] = _CPP_BITSET_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.FIXED_ARRAY.value_key()] = types.ARRAY
        registry.cpp_template_registry[tensor_builtins.TENSOR.value_key()] = _CPP_TENSOR_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.VAR_ARRAY.value_key()] = _CPP_VAR_ARRAY_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.VAR_STRING.value_key()] = _CPP_VAR_STRING_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.POD.value_key()] = _CPP_POD_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAP.value_key()] = _CPP_TAP_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TACHYON.value_key()] = _CPP_TACHYON_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAPPY.value_key()] = _CPP_TAPPY_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAP_INIT.value_key()] = _CPP_TAP_INIT_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.UUID.value_key()] = types.UUID
        registry.cpp_template_registry[clkbuiltins.OPTIONAL.value_key()] = _CPP_OPTIONAL_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.FIXED_SOA.value_key()] = _CPP_FIXED_SOA_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.VAR_SOA.value_key()] = _CPP_VAR_SOA_TEMPLATE

        return registry


CPP_TYPE_REGISTRY_KEY: Final = CppTypeRegistryKey("CppTypeRegistry")


def register_cpp_type(
    context: CompilerContext, clk_type: typesys.TypeVal, cpp_type: types.CppType, overwrite: bool = False
) -> None:
    """Registers the C++ type information for a given Clockwork type.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The Clockwork type that needs to be mapped to a C++ type.
        cpp_type: The corresponding C++ type descriptor.
        overwrite: If true, replace any value that already was registered.

    Raises:
        ValueError: If the Clockwork type is already registered with a different C++ type.
    """
    registry = context[CPP_TYPE_REGISTRY_KEY]
    if not overwrite:
        try:
            existing_type = registry.cpp_type_registry[clk_type.value_key()]
            if existing_type is not cpp_type:
                msg = f"Type {clk_type} already registered as {existing_type}"
                raise ValueError(msg)
        except KeyError:
            pass
    registry.cpp_type_registry[clk_type.value_key()] = cpp_type


def register_cpp_template(
    context: CompilerContext, clk_generic: typesys.TypeVal, cpp_template: types.CppTemplate
) -> None:
    """Registers the C++ template information for a given Clockwork generic.

    Args:
        context: Compiler context containing the type registry.
        clk_generic: The Clockwork generic that needs to be mapped to a C++ template.
        cpp_template: The corresponding C++ template descriptor.

    Raises:
        ValueError: If the Clockwork generic is already registered with a different C++ template.
    """
    if not clk_generic.generic_parameters():
        msg = "Type being registered as a template does not have generic parameters."
        raise ValueError(msg)

    registry = context[CPP_TYPE_REGISTRY_KEY]
    try:
        existing_template = registry.cpp_template_registry[clk_generic.value_key()]
        if existing_template is not cpp_template:
            msg = f"Type {clk_generic} already registered as {existing_template}"
            raise ValueError(msg)
    except KeyError:
        pass
    registry.cpp_template_registry[clk_generic.value_key()] = cpp_template


def get_cpp_template(context: CompilerContext, clk_type: typesys.TypeVal) -> types.CppTemplate:
    """Gets the C++ template information for a given Clockwork type value.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The Clockwork type for which the C++ type info is required.

    Returns:
        The corresponding C++ type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a C++ type.
    """
    if not clk_type.generic_parameters():
        msg = "Cannot get a Cpp template for a type that is not generic."
        raise ValueError(msg)

    registry = context[CPP_TYPE_REGISTRY_KEY]
    try:
        return registry.cpp_template_registry[clk_type.value_key()]
    except KeyError:
        msg = f"No C++ template registered for Clockwork type {clk_type}"
        raise TypeError(msg) from None


def get_cpp_type(context: CompilerContext, clk_type: typesys.Value) -> types.CppType | types.CppTemplateType:
    """Gets the C++ type information for a given Clockwork type value.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The Clockwork type for which the C++ type info is required.

    Returns:
        The corresponding C++ type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a C++ type.
    """
    if isinstance(clk_type, InstantiateStmt):
        assert isinstance(clk_type.typespec, typesys.Instantiation)
        assert isinstance(clk_type.typespec.instantiates, schema.Schema | schema.ResolvedSchema)
        clk_type = schema.InstantiatedSchema.from_typespec(clk_type.typespec)
    if isinstance(clk_type, schema.InstantiatedSchema):
        clk_type = clk_type.as_instantiation_or_resolved_schema()
    if isinstance(clk_type, typesys.Instantiation):
        return _get_cpp_instantiation(context, clk_type)
    if isinstance(clk_type, cog_parameters.CogParameterRef):
        return types.CppType([], clk_type.parameter_def.param_name, None)

    if not isinstance(clk_type, typesys.TypeVal):
        msg = f"Cannot construct C++ type corresponding to {clk_type}"
        raise TypeError(msg)

    registry = context[CPP_TYPE_REGISTRY_KEY]
    try:
        return registry.cpp_type_registry[clk_type.value_key()]
    except KeyError:
        msg = (
            f"No C++ type registered for Clockwork type {clk_type.value_key()} ({type(clk_type)}).\n"
            "If this is a user defined type, it might be missing from a `cpp_target`."
        )
        raise TypeError(msg) from None


def _unwrap_tap_tachyon_schema(type_arg: typesys.Value) -> schema.InstantiatedSchema:
    """Unwrap Tap<Tachyon<Schema>> to extract the underlying schema.

    Args:
        type_arg: The type argument, expected to be Tap<Tachyon<Schema>>.

    Returns:
        The unwrapped InstantiatedSchema.

    Raises:
        TypeError: If the structure is not Tap<Tachyon<Schema>>.
    """
    # Validate it's an Instantiation
    if not isinstance(type_arg, typesys.Instantiation) or not isinstance(type_arg.instantiates, typesys.TypeDef):
        msg = f"SoA type argument must be Tap<Tachyon<Schema>>, got {type(type_arg)}"
        raise TypeError(msg)

    # Check it's Tap
    if type_arg.instantiates.fqn != ".Tap":
        msg = f"SoA type argument must be Tap<...>, got {type_arg.instantiates.fqn}"
        raise TypeError(msg)

    # Extract Tachyon<Schema> from Tap
    repr_arg = type_arg.arguments.get("representation")
    if not isinstance(repr_arg, typesys.Instantiation) or not isinstance(repr_arg.instantiates, typesys.TypeDef):
        msg = f"Expected Tap<Tachyon<Schema>>, but representation is {type(repr_arg)}"
        raise TypeError(msg)

    # Check it's Tachyon
    if repr_arg.instantiates.fqn != ".Tachyon":
        fqn = repr_arg.instantiates.fqn
        msg = f"Expected Tap<Tachyon<...>>, got Tap<{fqn}>"
        raise TypeError(msg)

    # Extract the schema from Tachyon
    schema_arg = repr_arg.arguments.get("schema")
    if not isinstance(schema_arg, schema.Schema | schema.ResolvedSchema | typesys.Instantiation):
        msg = f"Expected Tachyon<Schema>, but schema is {type(schema_arg)}"
        raise TypeError(msg)

    # Convert to InstantiatedSchema
    return schema.InstantiatedSchema.from_typespec(schema_arg)


def _get_soa_cpp_instantiation(context: CompilerContext, clk_type: typesys.Instantiation) -> types.CppTemplateType:
    """Handle SoA (FixedSoa/VarSoa) instantiation with dynamic header resolution.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The SoA instantiation (FixedSoa<T, N> or VarSoa<T, N>).

    Returns:
        The corresponding C++ template type with the correct header.

    Raises:
        TypeError: If the schema doesn't have soa_enabled or has no header.
    """
    # Extract and unwrap the schema argument from Tap<Tachyon<Schema>>
    type_arg = clk_type.arguments["type"]
    schema_ir = _unwrap_tap_tachyon_schema(type_arg)

    # Validate that the schema has soa_enabled
    if not schema_ir.options or not schema_ir.options.soa_enabled:
        type_name = "FixedSoa" if clk_type.instantiates is clkbuiltins.FIXED_SOA else "VarSoa"
        msg = (
            f"Cannot use {type_name} with schema '{schema_ir.schema_name}': schema must have 'soa_enabled: true' option"
        )
        raise TypeError(msg)

    # Get the header where this schema's SoA was generated
    # (SoA is generated in the same header as the schema's Tap/Tachyon)
    schema_cpp_type = get_cpp_type(context, schema_ir.as_instantiation_or_resolved_schema())
    if not schema_cpp_type.includes:
        msg = f"Schema {schema_ir.schema_name} has no header - cannot determine SoA header location"
        raise TypeError(msg)

    # Get the base SoA template and add the schema's header
    base_template = get_cpp_template(context, clk_type.instantiates)
    base_template_with_header = replace(base_template, includes=list(schema_cpp_type.includes))

    # Build the template arguments: SoA<Schema, size>
    # We use the unwrapped schema type (not Tap<Tachyon<Schema>>)
    size_param_name = "size" if clk_type.instantiates is clkbuiltins.FIXED_SOA else "max_size"
    size_arg = clk_type.arguments[size_param_name]

    if isinstance(size_arg, primitive.DecimalValue):
        size_cpp_arg = literal.decimal_value_to_cpp(size_arg)
    elif isinstance(size_arg, ImmutableBinding):
        if not isinstance(size_arg.value, primitive.DecimalValue):
            msg = f"Unable to use argument of type {type(size_arg.value)} as size parameter."
            raise NotImplementedError(msg)
        size_cpp_arg = literal.decimal_value_to_cpp(size_arg.value)
    else:
        msg = f"Cannot construct size argument for {size_arg}"
        raise NotImplementedError(msg)

    arguments = [schema_cpp_type, size_cpp_arg]
    return base_template_with_header.instantiate(arguments)


def _make_size_sequence(values: typesys.Values) -> types.CppTemplateType:
    # We only support integer sequences for now.
    if not all(isinstance(element, primitive.DecimalValue) for element in values.elements):
        msg = f"Value lists must be integer sequences, but got: {values.elements}."
        raise NotImplementedError(msg)
    sizes = [literal.decimal_value_to_cpp(cast("primitive.DecimalValue", element)) for element in values.elements]
    return types.TENSOR_SIZES.instantiate(sizes)


def _substitute_cpp_argument(
    context: CompilerContext, clk_type: typesys.Instantiation, arg: typesys.Value
) -> types.CppTypeExpr | types.CppValueExpr:
    """Convert a Clockwork generic argument to a C++ template argument.

    Args:
        context: Compiler context used to resolve Clockwork type arguments.
        clk_type: Enclosing generic instantiation, used to derive tensor strides.
        arg: Generic argument to convert.

    Returns:
        The corresponding C++ type or value expression.

    Raises:
        NotImplementedError: If an argument type is not supported.
    """
    if isinstance(arg, typesys.TypeVal | cog_parameters.CogParameterRef):
        return get_cpp_type(context, arg)

    if isinstance(arg, primitive.DecimalValue):
        return literal.decimal_value_to_cpp(arg)

    if isinstance(arg, ImmutableBinding):
        value = arg.value
        if not isinstance(value, primitive.DecimalValue):
            msg = f"Unable to use argument of type {type(value)} as c++ template argument."
            raise NotImplementedError(msg)
        return literal.decimal_value_to_cpp(value)

    if isinstance(arg, typesys.Values):
        return _make_size_sequence(arg)

    if isinstance(arg, clkenum.ValueRef) and arg.value_def.enum is tensor_builtins.TENSOR_LAYOUT_ENUM:
        strides = [
            literal.int_to_cpp(stride, clkbuiltins.UINT64) for stride in tensor_builtins.get_tensor_strides(clk_type)
        ]
        return types.TENSOR_SIZES.instantiate(strides)

    if isinstance(arg, primitive.StringLiteral):
        return literal.string_literal_to_cpp(arg)

    msg = f"Cannot construct C++ template argument for {arg}"
    raise NotImplementedError(msg)


def _get_cpp_instantiation(context: CompilerContext, clk_type: typesys.Instantiation) -> types.CppTemplateType:
    """Recursively resolve an instantiation to a CppTemplateType.

    Args:
        context: Compiler context containing the type registry.
        clk_type: The type instantiation to resolve.

    Returns:
        The corresponding C++ template type.

    Raises:
        ValueError: If the instantiation is not instantiating a generic type.
        TypeError: If the instantiation has unbound parameters or arguments of unsupported types.
        NotImplementedError: If an argument type is not supported.
    """
    if not clk_type.instantiates.generic_parameters():
        msg = f"Instantiation is not instantiating a generic type: {type(clk_type.instantiates)} {clk_type.value_key()}"
        raise ValueError(msg)

    # Special handling for SoA types
    if clk_type.instantiates in (clkbuiltins.FIXED_SOA, clkbuiltins.VAR_SOA):
        return _get_soa_cpp_instantiation(context, clk_type)

    # Regular template instantiation logic
    base_type = get_cpp_template(context, clk_type.instantiates)

    arguments: list[types.CppTypeExpr | types.CppValueExpr] = []
    parameters = clk_type.instantiates.generic_parameters()
    assert parameters is not None
    for param in parameters:
        try:
            arg = clk_type.arguments[param.name]
        except KeyError:
            msg = f"Attempt to instantiate without fully-bound parameters: {clk_type} missing {param.name}"
            raise TypeError(msg) from None

        arguments.append(_substitute_cpp_argument(context, clk_type, arg))

    return base_type.instantiate(arguments)
