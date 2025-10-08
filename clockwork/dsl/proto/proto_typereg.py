# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Type registry for clockwork to protocol buffer type mappings."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Final, final

from clockwork.dsl.compiler_context import CompilerContext, Context, ContextKey
from clockwork.dsl.ir import clkbuiltins, module_id, typesys
from typing_extensions import override


@dataclass
class ProtobufType:
    """Protocol buffer type representation."""

    type_name: str

    def render(self) -> str:
        """Render the type."""
        return f"{self.type_name}"


@dataclass
class PrimitiveProtobufType(ProtobufType):
    """Represents a primitive protobuf type."""

    @override
    def render(self) -> str:
        """Render as a string."""
        return f"optional {super().render()}"


@dataclass
class DefinedProtobufType(ProtobufType):
    """Represents a non-primitive protobuf type."""

    module_id: module_id.ModuleID | None
    import_location: str | None
    package_name: str
    validate_fields: bool

    @override
    def render(self) -> str:
        """Render as string."""
        if self.package_name:
            return f"{self.package_name}.{super().render()}"
        return f"{super().render()}"


@dataclass
class EnumProtobufType(DefinedProtobufType):
    """Represents a non-primitive protobuf type."""

    @override
    def render(self) -> str:
        """Render as a string."""
        return f"optional {super().render()}"


@dataclass
class ArrayProtobufType(DefinedProtobufType):
    """Special Protobuf type to represent the builtin clockwork VarArray and FixedArray."""

    @override
    def render(self) -> str:
        """Render the var array."""
        return f"repeated {super().render()}"


INDENT = "   "


@dataclass
class OptionalProtobufType(DefinedProtobufType):
    """Special Protobuf type to represent the builtin clockwork Optional container."""

    @override
    def render(self) -> str:
        """Render the optional."""
        return f"optional {super().render()}"


@final
class ProtobufTypeRegistry(Context):
    """Registry for protobuf types."""

    def __init__(self, name: str | None) -> None:
        """Create a new protobuf type registry."""
        self.name = name
        self.protobuf_type_registry: dict[str, ProtobufType] = {}
        self.forbidden_nested_types: set[str] = set()

    @override
    def import_from(self, other: ProtobufTypeRegistry) -> None:
        """Combine this cache with cached modules from another.

        Raises:
            RuntimeError: If a module already exists with a different definition.
        """
        self.forbidden_nested_types = self.forbidden_nested_types & other.forbidden_nested_types

        for key, proto_type in other.protobuf_type_registry.items():
            if key in self.protobuf_type_registry and self.protobuf_type_registry[key] != proto_type:
                msg = f"Module {key} has conflicting cache entry: {self.protobuf_type_registry[key]} vs {proto_type}\nWhen merging from {other.name} into {self.name}"
                raise RuntimeError(msg)
            self.protobuf_type_registry[key] = proto_type


class ProtobufTypeRegistryKey(ContextKey[ProtobufTypeRegistry]):
    """Compiler context key for the Protobuf Type Registry."""

    @override
    def make_default(self, compiler_context: CompilerContext) -> ProtobufTypeRegistry:
        """Create a default instance of the registry."""
        registry = ProtobufTypeRegistry(compiler_context.name)

        registry.forbidden_nested_types.add(clkbuiltins.OPTIONAL.value_key())
        registry.forbidden_nested_types.add(clkbuiltins.VAR_ARRAY.value_key())
        registry.forbidden_nested_types.add(clkbuiltins.FIXED_ARRAY.value_key())

        registry.protobuf_type_registry[clkbuiltins.BOOL.value_key()] = PrimitiveProtobufType(type_name="bool")
        registry.protobuf_type_registry[clkbuiltins.FLOAT32.value_key()] = PrimitiveProtobufType(type_name="float")
        registry.protobuf_type_registry[clkbuiltins.FLOAT64.value_key()] = PrimitiveProtobufType(type_name="double")
        registry.protobuf_type_registry[clkbuiltins.INT8.value_key()] = PrimitiveProtobufType(type_name="sint32")
        registry.protobuf_type_registry[clkbuiltins.INT16.value_key()] = PrimitiveProtobufType(type_name="sint32")
        registry.protobuf_type_registry[clkbuiltins.INT32.value_key()] = PrimitiveProtobufType(type_name="sint32")
        registry.protobuf_type_registry[clkbuiltins.INT64.value_key()] = PrimitiveProtobufType(type_name="sint64")
        registry.protobuf_type_registry[clkbuiltins.UINT8.value_key()] = PrimitiveProtobufType(type_name="uint32")
        registry.protobuf_type_registry[clkbuiltins.UINT16.value_key()] = PrimitiveProtobufType(type_name="uint32")
        registry.protobuf_type_registry[clkbuiltins.UINT32.value_key()] = PrimitiveProtobufType(type_name="uint32")
        registry.protobuf_type_registry[clkbuiltins.UINT64.value_key()] = PrimitiveProtobufType(type_name="uint64")
        registry.protobuf_type_registry[clkbuiltins.BYTE.value_key()] = PrimitiveProtobufType(type_name="string")
        registry.protobuf_type_registry[clkbuiltins.DURATION.value_key()] = DefinedProtobufType(
            type_name="Duration",
            module_id=module_id.ModuleID(repo="protobuf", name="google::protobuf::duration"),
            import_location="google/protobuf/duration.proto",
            package_name="google.protobuf",
            validate_fields=False,
        )
        registry.protobuf_type_registry[clkbuiltins.SYNC_TIME.value_key()] = DefinedProtobufType(
            type_name="Timestamp",
            module_id=module_id.ModuleID(repo="protobuf", name="google::protobuf::timestamp"),
            import_location="google/protobuf/timestamp.proto",
            package_name="google.protobuf",
            validate_fields=False,
        )

        return registry


PROTOBUF_TYPE_REGISTRY_KEY: Final = ProtobufTypeRegistryKey("ProtobufTypeRegistry")


def register_forbidden_nested_type(clk_type: typesys.Value, compiler_context: CompilerContext) -> None:
    """Register a type as forbidden to be nested in an optional or VarArray."""
    registry = compiler_context[PROTOBUF_TYPE_REGISTRY_KEY]
    registry.forbidden_nested_types.add(clk_type.value_key())


def in_forbidden_nested_types(clk_type: typesys.Value, compiler_context: CompilerContext) -> bool:
    """Check if a type is forbidden to be nested in an optional of VarArray."""
    registry = compiler_context[PROTOBUF_TYPE_REGISTRY_KEY]
    return clk_type.value_key() in registry.forbidden_nested_types


def register_protobuf_type(
    clk_type: typesys.Value, protobuf_type: ProtobufType, compiler_context: CompilerContext
) -> None:
    """Registers the Protobuf type information for a given Clockwork type.

    Args:
        clk_type: The Clockwork type that needs to be mapped to a protobuf type.
        protobuf_type: The corresponding protobuf type descriptor.
        compiler_context: Compiler context that contains the protobuf type registry.

    Raises:
        ValueError: If the Clockwork type is already registered with a different protobuf type.
    """
    registry = compiler_context[PROTOBUF_TYPE_REGISTRY_KEY]
    if not isinstance(clk_type, typesys.TypeVal):
        msg = f"Cannot register Protobuf type corresponding to {clk_type}"
        raise TypeError(msg)
    try:
        existing_type = registry.protobuf_type_registry[clk_type.value_key()]
        msg = f"Type {clk_type} already registered as {existing_type}"
        raise ValueError(msg)
    except KeyError:
        pass
    registry.protobuf_type_registry[clk_type.value_key()] = protobuf_type


def get_protobuf_type(clk_type: typesys.Value, compiler_context: CompilerContext) -> ProtobufType:
    """Gets the Protobuf type information for a given Clockwork type value.

    Args:
        clk_type: The Clockwork type for which the Protobuf type info is required.
        compiler_context: Compiler context that contains the protobuf type registry.

    Returns:
        The corresponding Protobuf type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a Protobuf type.
    """
    if isinstance(clk_type, typesys.Instantiation) and (
        (clk_type.instantiates == clkbuiltins.VAR_ARRAY) | (clk_type.instantiates == clkbuiltins.FIXED_ARRAY)
    ):
        if clk_type.arguments["type"] == clkbuiltins.BYTE:
            return PrimitiveProtobufType(type_name="string")

        nested_type = clk_type.arguments["type"]
        value_type = get_protobuf_type(nested_type, compiler_context)
        if isinstance(nested_type, typesys.Instantiation) and in_forbidden_nested_types(
            nested_type.instantiates, compiler_context
        ):
            msg = f"Cannot resolve the contained type in a VarArray. Note that protobuf generation does not support nested Arrays. Contained type was: {clk_type.arguments['type']}"
            raise TypeError(msg)
        module_id = value_type.module_id if isinstance(value_type, DefinedProtobufType) else None
        import_location = value_type.import_location if isinstance(value_type, DefinedProtobufType) else None
        package_name = value_type.package_name if isinstance(value_type, DefinedProtobufType) else ""
        return ArrayProtobufType(
            type_name=value_type.type_name,
            module_id=module_id,
            import_location=import_location,
            package_name=package_name,
            validate_fields=False,
        )
    if isinstance(clk_type, typesys.Instantiation) and clk_type.instantiates == clkbuiltins.OPTIONAL:
        nested_type = clk_type.arguments["type"]
        value_type = get_protobuf_type(nested_type, compiler_context)
        if isinstance(nested_type, typesys.Instantiation) and in_forbidden_nested_types(
            nested_type.instantiates, compiler_context
        ):
            msg = f"Cannot resolve the contained type in an Optional note that protobuf generation does not support optional VarArrays. Contained type was: {clk_type.arguments['type']}"
            raise TypeError(msg)
        module_id = value_type.module_id if isinstance(value_type, DefinedProtobufType) else None
        import_location = value_type.import_location if isinstance(value_type, DefinedProtobufType) else None
        package_name = value_type.package_name if isinstance(value_type, DefinedProtobufType) else ""
        return OptionalProtobufType(
            type_name=value_type.type_name,
            module_id=module_id,
            import_location=import_location,
            package_name=package_name,
            validate_fields=False,
        )

    if isinstance(clk_type, typesys.Instantiation) and (
        (clk_type.instantiates == clkbuiltins.UUID) | (clk_type.instantiates == clkbuiltins.VAR_STRING)
    ):
        return PrimitiveProtobufType(type_name="string")
    return get_protobuf_type_from_registry(clk_type, compiler_context)


def get_protobuf_type_from_registry(clk_type: typesys.Value, compiler_context: CompilerContext) -> ProtobufType:
    """Gets the Protobuf type information for a given Clockwork type value.

    Args:
        clk_type: The Clockwork type for which the Protobuf type info is required.
        compiler_context: Compiler context that contains the protobuf type registry.

    Returns:
        The corresponding Protobuf type descriptor.

    Raises:
        TypeError: If the Clockwork type cannot be mapped to a Protobuf type.
    """
    registry = compiler_context[PROTOBUF_TYPE_REGISTRY_KEY]
    if not isinstance(clk_type, typesys.TypeVal):
        msg = f"Cannot construct Protobuf type corresponding to {clk_type}"
        raise TypeError(msg)
    try:
        return registry.protobuf_type_registry[clk_type.value_key()]
    except KeyError:
        msg = (
            f"No Protobuf type registered for Clockwork type {clk_type}.\n"
            "If this is a user defined type, it might be missing from a `proto_target`."
        )
        raise TypeError(msg) from None
