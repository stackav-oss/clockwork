# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Provides facilities to convert Clockwork IR types to corresponding C++ types."""

from typing import Final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.cpp import literal, types
from clockwork.dsl.cpp.context import FwdDecl, Header, SystemHeader
from clockwork.dsl.ir import clkbuiltins, primitive, schema, typesys
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from clockwork.dsl.ir.statement import ImmutableBinding
from typing_extensions import override

TYPE_TRAITS_HEADER: Final = SystemHeader("type_traits")

TAGS_HEADER: Final = Header(CLK_REPO, "clockwork/tags.hh")

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

# Module-level instances for C++ template types
_CPP_FIXED_ARRAY_TEMPLATE: Final = types.CppTemplate([SystemHeader("array")], "array", "std")
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
_CPP_UUID_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/uuid/uuid.hh")],
    cpp_namespace="jewels",
    template_name="Uuid",
)
_CPP_OPTIONAL_TEMPLATE: Final = types.CppTemplate(
    includes=[Header(JEWELS_REPO, "jewels/container/tap/optional.hh")],
    cpp_namespace="jewels::tap",
    template_name="Optional",
)


class CppTypeRegistry(Context):
    """Compiler Context for C++ type mappings."""

    def __init__(self) -> None:  # pyright: ignore[reportMissingSuperCall] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
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

        # Register built-in template types
        registry.cpp_template_registry[clkbuiltins.FIXED_ARRAY.value_key()] = _CPP_FIXED_ARRAY_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.VAR_ARRAY.value_key()] = _CPP_VAR_ARRAY_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.VAR_STRING.value_key()] = _CPP_VAR_STRING_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.POD.value_key()] = _CPP_POD_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAP.value_key()] = _CPP_TAP_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TACHYON.value_key()] = _CPP_TACHYON_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAPPY.value_key()] = _CPP_TAPPY_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.TAP_INIT.value_key()] = _CPP_TAP_INIT_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.UUID.value_key()] = _CPP_UUID_TEMPLATE
        registry.cpp_template_registry[clkbuiltins.OPTIONAL.value_key()] = _CPP_OPTIONAL_TEMPLATE

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
    if isinstance(clk_type, schema.InstantiatedSchema):
        clk_type = clk_type.as_instantiation_or_resolved_schema()
    if isinstance(clk_type, typesys.Instantiation):
        return _get_cpp_instantiation(context, clk_type)

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
    base_type = get_cpp_template(context, clk_type.instantiates)

    arguments: list[types.CppTypeExpr | types.CppValueExpr] = []
    parameters = clk_type.instantiates.generic_parameters()
    assert parameters is not None  # noqa: S101  (for mypy)
    for param in parameters:
        try:
            arg = clk_type.arguments[param.name]
        except KeyError:
            msg = f"Attempt to instantiate without fully-bound parameters: {clk_type} missing {param.name}"
            raise TypeError(msg) from None
        if isinstance(arg, typesys.TypeVal):
            type_arg = get_cpp_type(context, arg)
            arguments.append(type_arg)
        elif isinstance(arg, primitive.DecimalValue):
            arguments.append(literal.decimal_value_to_cpp(arg))
        elif isinstance(arg, ImmutableBinding):
            value = arg.value
            if not isinstance(value, primitive.DecimalValue):
                msg = f"Unable to use argument of type {type(value)} as c++ template argument."
                raise NotImplementedError(msg)
            arguments.append(literal.decimal_value_to_cpp(value))
        else:
            msg = f"Cannot construct C++ template argument for {arg}"
            raise NotImplementedError(msg)
    return base_type.instantiate(arguments)
